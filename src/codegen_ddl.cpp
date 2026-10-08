#include "codegen_ddl.h"
#include "codegen_context.h"
#include "codegen_ddl_internal.h"
#include "codegen_utils.h"
#include "ddl_serialization_scope.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

#include <map>

namespace sqlite2orm {

    /**
     *  Why a BLOB literal has no place in a clause sqlite_orm writes into the schema, as the
     *  first half of a warning: every site appends what its own clause becomes without it.
     *  Measured on the pinned revision — `field_printer<std::vector<char>>` streams the bytes
     *  of the blob themselves and `quote_blob_literal` wraps that in `x'…'`, so `x'4142'`
     *  reaches SQLite as `x'AB'`, the single byte 0xAB, and `x'0102'` as a token SQLite does
     *  not recognize at all.
     */
    std::string ddlBlobLiteralReason(std::string_view literal) {
        return std::string(literal) +
               ", a BLOB literal in a clause sqlite_orm writes into the schema as text: it prints a blob as the "
               "bytes themselves inside x'…' instead of as their hex digits, which SQLite reads as a different "
               "value where every byte of the blob is a hex digit and refuses as an unrecognized token where "
               "one is not";
    }

    /**
     *  The warning about `literal`, an infinity `subject` is written with. sqlite_orm prints a
     *  double into DDL through its serializer, and an infinity comes out of it as `inf`, which
     *  SQLite reads as a column name and not as a number, so `consequence` is what the schema
     *  built from the generated code does. The value itself is one C++ spells
     *  (`std::numeric_limits<double>::infinity()`) and one SQLite keeps in the schema it was
     *  read from, so the clause is generated all the same and only warned about.
     */
    CodegenWarning
    ddlInfinityWarning(const DdlInfinityLiteral& literal, std::string_view subject, std::string_view consequence) {
        return sourceSpanWarning(std::string(subject) + " uses " + literal.literal +
                                     ", an infinity: sqlite_orm writes an infinity into DDL as `inf`, which "
                                     "SQLite reads as a column name rather than as a number, so " +
                                     std::string(consequence),
                                 literal.span);
    }

    DdlCodeGenerator::DdlCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context) :
        coordinator(coordinator), context(context) {}

    CodeGenResult DdlCodeGenerator::generateCreateTable(const CreateTableNode& createTable) {
        CreateTableParts parts = this->createTableParts(createTable);
        if (parts.makeTableExpression.empty()) {
            return CodeGenResult{"/* CREATE TABLE " + createTable.tableName + " — not supported for sqlite_orm */",
                                 std::move(parts.decisionPoints),
                                 std::move(parts.warnings),
                                 {},
                                 std::move(parts.comments)};
        }
        std::string code =
            parts.structDeclaration + "\nauto storage = make_storage(\"\",\n    " + parts.makeTableExpression + ");";
        return CodeGenResult{std::move(code),
                             std::move(parts.decisionPoints),
                             std::move(parts.warnings),
                             {},
                             std::move(parts.comments)};
    }

    CodeGenResult DdlCodeGenerator::generateCreateTrigger(const CreateTriggerNode& createTrigger) {
        // A trigger is created from the text sqlite_orm serializes it into, WHEN clause and body
        // statements alike, so a value written in it reaches SQLite as that text rather than bound.
        const DdlSerializationScope ddlScope{this->context};
        std::vector<CodegenWarning> warnings;
        if (createTrigger.ifNotExists) {
            warnings.push_back(
                "CREATE TRIGGER IF NOT EXISTS is not represented in sqlite_orm make_trigger(); generated code "
                "omits IF NOT EXISTS");
        }
        if (createTrigger.temporary) {
            warnings.push_back(
                "TEMP/TEMPORARY TRIGGER is not represented in sqlite_orm make_trigger(); generated code does not "
                "mark the trigger as temporary");
        }
        if (createTrigger.triggerSchemaName) {
            warnings.push_back(
                "schema-qualified trigger name is not represented in sqlite_orm; generated code uses unqualified "
                "trigger name only");
        }
        if (createTrigger.tableSchemaName) {
            warnings.push_back("schema-qualified ON table in TRIGGER is not represented in sqlite_orm mapping");
        }

        std::string subject = this->context.structNameForTable(createTrigger.tableName);
        std::string savedStruct = this->context.structName;
        this->context.structName = subject;

        std::string timingHead;
        switch (createTrigger.timing) {
            case TriggerTiming::before:
                timingHead = "before()";
                break;
            case TriggerTiming::after:
                timingHead = "after()";
                break;
            case TriggerTiming::insteadOf:
                timingHead = "instead_of()";
                break;
        }

        std::string typeChain = timingHead;
        switch (createTrigger.eventKind) {
            case TriggerEventKind::delete_:
                typeChain += ".delete_()";
                break;
            case TriggerEventKind::insert_:
                typeChain += ".insert()";
                break;
            case TriggerEventKind::update_:
                typeChain += ".update()";
                break;
            case TriggerEventKind::updateOf:
                typeChain += ".update_of(";
                for (size_t columnIndex = 0; columnIndex < createTrigger.updateOfColumns.size(); ++columnIndex) {
                    if (columnIndex > 0) {
                        typeChain += ", ";
                    }
                    typeChain += "&" + subject + "::" +
                                 this->context.structColumnMember(subject, createTrigger.updateOfColumns[columnIndex]);
                }
                typeChain += ")";
                break;
        }

        std::string base = typeChain + ".on<" + subject + ">()";
        if (createTrigger.forEachRow) {
            base += ".for_each_row()";
        }
        std::vector<DecisionPoint> decisionPoints;
        std::vector<CodegenWarning> whenClauseWarnings;
        // Like a view body, a trigger body is stored and compiled only when the trigger fires, so
        // SQLite accepts a hex literal in it that it refuses in a query of its own.
        this->context.storedHexLiteralsTooBig.clear();
        // Everything a trigger is made of — the WHEN clause and every statement of the body —
        // reaches SQLite as the text of the CREATE TRIGGER, so the whole of it is serialized.
        this->context.ddlBlobLiterals.clear();
        const bool triggerWasStoredExpression = this->context.storedExpression;
        this->context.storedExpression = true;
        if (createTrigger.whenClause) {
            // `trigger_base_t::when()` keeps the expression in an `optional_container`, whose field
            // is default-constructed before the expression is assigned to it, so the WHEN clause
            // only compiles while every sqlite_orm type in it has a default constructor. The
            // comparisons, AND and OR do; the predicates, the arithmetic and bit operators and the
            // functions do not, and the emitters record what they put out.
            this->context.formsWithoutDefaultConstructor.clear();
            auto whenResult = this->coordinator.generateNode(*createTrigger.whenClause);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(whenResult.decisionPoints.begin()),
                                  std::make_move_iterator(whenResult.decisionPoints.end()));
            warnings.insert(warnings.end(),
                            std::make_move_iterator(whenResult.warnings.begin()),
                            std::make_move_iterator(whenResult.warnings.end()));
            for (const std::string& form: this->context.formsWithoutDefaultConstructor) {
                // Held back until the trigger is known to generate: a trigger that is left out
                // altogether has no generated code to fail to compile.
                whenClauseWarnings.push_back(
                    "CREATE TRIGGER " + stripIdentifierQuotes(createTrigger.triggerName) + " uses " + form +
                    " in its WHEN clause, a form sqlite_orm gives no default constructor: make_trigger() keeps "
                    "a trigger's WHEN expression in an optional_container, which default-constructs the "
                    "expression before assigning it, so the generated trigger does not compile");
            }
            this->context.formsWithoutDefaultConstructor.clear();
            base += ".when(" + whenResult.code + ")";
        }

        std::string stepsJoined;
        for (const auto& step: createTrigger.bodyStatements) {
            auto stepResult = this->coordinator.generateTriggerStep(*step, subject);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(stepResult.decisionPoints.begin()),
                                  std::make_move_iterator(stepResult.decisionPoints.end()));
            warnings.insert(warnings.end(),
                            std::make_move_iterator(stepResult.warnings.begin()),
                            std::make_move_iterator(stepResult.warnings.end()));
            if (!stepsJoined.empty()) {
                stepsJoined += ", ";
            }
            stepsJoined += stepResult.code;
        }

        this->context.storedExpression = triggerWasStoredExpression;
        this->context.structName = savedStruct;
        if (!this->context.storedHexLiteralsTooBig.empty()) {
            for (const std::string& literal: this->context.storedHexLiteralsTooBig) {
                warnings.push_back("CREATE TRIGGER " + stripIdentifierQuotes(createTrigger.triggerName) + " uses " +
                                   literal +
                                   ", too big for a signed 64-bit integer: SQLite stores the trigger but refuses "
                                   "every statement that fires it, and C++ has no literal for it, so the trigger "
                                   "is not generated");
            }
            return CodeGenResult{{}, std::move(decisionPoints), std::move(warnings)};
        }
        if (!this->context.ddlBlobLiterals.empty()) {
            for (const std::string& literal: this->context.ddlBlobLiterals) {
                warnings.push_back("CREATE TRIGGER " + stripIdentifierQuotes(createTrigger.triggerName) + " uses " +
                                   ddlBlobLiteralReason(literal) + ", so the trigger is not generated");
            }
            return CodeGenResult{{}, std::move(decisionPoints), std::move(warnings)};
        }

        // SQLite stores a trigger without compiling it, so the CREATE TRIGGER carrying an `inf`
        // goes through and the statement that fires the trigger is the one refused. Checked
        // against sqlite3 3.51.0 and the libsqlite3 3.45.1 the tests link.
        for (const DdlInfinityLiteral& literal: this->context.ddlInfinityLiterals) {
            warnings.push_back(
                ddlInfinityWarning(literal,
                                   "trigger " + stripIdentifierQuotes(createTrigger.triggerName),
                                   "sync_schema() creates a trigger that refuses every statement firing it "
                                   "(\"no such column: inf\")"));
        }
        warnings.insert(warnings.end(),
                        std::make_move_iterator(whenClauseWarnings.begin()),
                        std::make_move_iterator(whenClauseWarnings.end()));

        std::string triggerLiteral = identifierToCppStringLiteral(createTrigger.triggerName);
        std::string code = "make_trigger(" + triggerLiteral + ", " + base + ".begin(" + stepsJoined + "));";
        return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
    }

    CodeGenResult DdlCodeGenerator::generateCreateIndex(const CreateIndexNode& createIndex) {
        // An index is created from the text sqlite_orm serializes it into, its indexed columns and
        // its partial WHERE alike, so a value written in it reaches SQLite as that text.
        const DdlSerializationScope ddlScope{this->context};
        std::vector<CodegenWarning> warnings;
        if (createIndex.indexSchemaName) {
            warnings.push_back(
                "schema-qualified INDEX name is not represented in sqlite_orm; generated code uses unqualified "
                "index name");
        }
        if (createIndex.tableSchemaName) {
            warnings.push_back("schema-qualified table in CREATE INDEX is not represented in sqlite_orm mapping");
        }
        if (!createIndex.ifNotExists) {
            warnings.push_back(
                "sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs "
                "from serialized output");
        }

        std::string tableStruct = this->context.structNameForTable(createIndex.tableName);
        std::string savedStruct = this->context.structName;
        this->context.structName = tableStruct;

        // An index carries no bound parameter: both its indexed columns and the WHERE of a partial
        // index reach SQLite as the text sqlite_orm serializes the index into.
        this->context.ddlBlobLiterals.clear();

        std::string functionName = createIndex.unique ? "make_unique_index" : "make_index";
        std::string indexLiteral = identifierToCppStringLiteral(createIndex.indexName);
        std::vector<DecisionPoint> decisionPoints;
        std::string columnParts;
        bool firstColumnNamesTable = false;
        bool atFirstColumn = true;
        for (const auto& indexedColumn: createIndex.indexedColumns) {
            if (!columnParts.empty()) {
                columnParts += ", ";
            }
            this->context.emittedTableTypedColumnRef = false;
            auto expressionResult = this->coordinator.generateNode(*indexedColumn.expression);
            if (atFirstColumn) {
                // `make_index` deduces the table an index is made for from its first argument alone,
                // and only a column reference generated as a pointer into the struct carries one.
                // COLLATE and a unary plus emit their operand and nothing else, so the form that came
                // out is the one of the node under them.
                firstColumnNamesTable =
                    this->context.emittedTableTypedColumnRef &&
                    dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(*indexedColumn.expression)) != nullptr;
                atFirstColumn = false;
            }
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(expressionResult.decisionPoints.begin()),
                                  std::make_move_iterator(expressionResult.decisionPoints.end()));
            warnings.insert(warnings.end(),
                            std::make_move_iterator(expressionResult.warnings.begin()),
                            std::make_move_iterator(expressionResult.warnings.end()));
            std::string part = "indexed_column(" + expressionResult.code + ")";
            if (!indexedColumn.collation.empty()) {
                if (trailingCollateBindsWholeExpression(*indexedColumn.expression)) {
                    std::string collationLower = toLowerAscii(indexedColumn.collation);
                    if (collationLower == "nocase") {
                        part += ".collate(\"nocase\")";
                    } else if (collationLower == "binary") {
                        part += ".collate(\"binary\")";
                    } else if (collationLower == "rtrim") {
                        part += ".collate(\"rtrim\")";
                    } else {
                        warnings.push_back("COLLATE " + indexedColumn.collation +
                                           " is not a built-in collation; generated .collate(...) uses literal "
                                           "name as in SQL");
                        part += ".collate(" + identifierToCppStringLiteral(indexedColumn.collation) + ")";
                    }
                } else {
                    // `.collate(…)` is serialized as a bare COLLATE after the expression, with no
                    // parentheses around it, and SQLite binds COLLATE tighter than the operators an
                    // expression is built of. Generating it would leave the collation on part of the
                    // expression, which indexes the key the way no collation at all does and stores
                    // an index the schema it was generated from does not describe.
                    warnings.push_back(sourceSpanWarning(
                        "COLLATE " + indexedColumn.collation + " over an expression in index " +
                            stripIdentifierQuotes(createIndex.indexName) +
                            " is not generated: sqlite_orm serializes an indexed column's collation as a bare "
                            "COLLATE after the expression, and SQLite binds COLLATE tighter than the operators "
                            "in it, so the collation would apply to part of the expression rather than to the "
                            "indexed value",
                        *indexedColumn.expression));
                }
            }
            if (indexedColumn.sortDirection == SortDirection::asc) {
                part += ".asc()";
            } else if (indexedColumn.sortDirection == SortDirection::desc) {
                part += ".desc()";
            }
            columnParts += part;
        }

        // The WHERE of a partial index is generated before it is decided whether the index has a form
        // at all, so an index that is left out still reports what its clause decides and warns about,
        // the way an indexed column of the same index does.
        std::string whereArgument;
        if (createIndex.whereClause) {
            auto whereResult = this->coordinator.generateNode(*createIndex.whereClause);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(whereResult.decisionPoints.begin()),
                                  std::make_move_iterator(whereResult.decisionPoints.end()));
            warnings.insert(warnings.end(),
                            std::make_move_iterator(whereResult.warnings.begin()),
                            std::make_move_iterator(whereResult.warnings.end()));
            whereArgument = ", where(" + whereResult.code + ")";
        }

        if (!firstColumnNamesTable && createIndex.unique) {
            // `make_unique_index` takes the table as a defaulted template parameter behind its
            // argument pack, which leaves no way to spell it out, so there is no form of this index.
            warnings.push_back("UNIQUE index " + stripIdentifierQuotes(createIndex.indexName) +
                               " starts with an expression: sqlite_orm deduces the table an index is made "
                               "for from its first indexed column, and make_unique_index has no form that "
                               "spells that table out, so the index is not generated");
            this->context.structName = savedStruct;
            return CodeGenResult{{}, std::move(decisionPoints), std::move(warnings)};
        }

        if (!this->context.ddlBlobLiterals.empty()) {
            for (const std::string& literal: this->context.ddlBlobLiterals) {
                warnings.push_back("index " + stripIdentifierQuotes(createIndex.indexName) + " uses " +
                                   ddlBlobLiteralReason(literal) + ", so the index is not generated");
            }
            this->context.structName = savedStruct;
            return CodeGenResult{{}, std::move(decisionPoints), std::move(warnings)};
        }

        // SQLite compiles an index when it creates it, so the CREATE INDEX carrying an `inf` is the
        // statement refused. Warned about only here, where the index is known to be generated at
        // all: one that is left out has no CREATE INDEX for sync_schema to run. Checked against
        // sqlite3 3.51.0 and the libsqlite3 3.45.1 the tests link.
        for (const DdlInfinityLiteral& literal: this->context.ddlInfinityLiterals) {
            warnings.push_back(ddlInfinityWarning(literal,
                                                  "index " + stripIdentifierQuotes(createIndex.indexName),
                                                  "sync_schema() throws instead of creating the index (\"no such "
                                                  "column: inf\")"));
        }

        // An index that does not start with a column names the table it indexes itself.
        std::string tableTypeArgument = firstColumnNamesTable ? "" : "<" + tableStruct + ">";
        std::string code =
            functionName + tableTypeArgument + "(" + indexLiteral + ", " + columnParts + whereArgument + ");";

        this->context.structName = savedStruct;
        return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
    }

    CodeGenResult DdlCodeGenerator::generateTransactionControl(const TransactionControlNode& node) {
        if (node.kind == TransactionControlNode::Kind::begin) {
            switch (node.beginMode) {
                case BeginTransactionMode::deferred:
                    return CodeGenResult{"storage.begin_deferred_transaction();", {}, {}};
                case BeginTransactionMode::immediate:
                    return CodeGenResult{"storage.begin_immediate_transaction();", {}, {}};
                case BeginTransactionMode::exclusive:
                    return CodeGenResult{"storage.begin_exclusive_transaction();", {}, {}};
                case BeginTransactionMode::plain:
                default:
                    return CodeGenResult{"storage.begin_transaction();", {}, {}};
            }
        }
        if (node.kind == TransactionControlNode::Kind::commit) {
            return CodeGenResult{"storage.commit();", {}, {}};
        }
        if (node.rollbackToSavepoint) {
            if (policyEquals(this->context.codeGenPolicy, "savepoint_style", "guard")) {
                return CodeGenResult{savepointGuardVariableName(*node.rollbackToSavepoint) + ".rollback_to();", {}, {}};
            }
            return CodeGenResult{"storage.rollback_to_savepoint(" +
                                     identifierToCppStringLiteral(*node.rollbackToSavepoint) + ");",
                                 {},
                                 {}};
        }
        return CodeGenResult{"storage.rollback();", {}, {}};
    }

    namespace {

        std::string chosenSavepointStyle(const CodeGenPolicy* policy) {
            for (std::string_view style: {"guard", "functional"}) {
                if (policyEquals(policy, "savepoint_style", style)) {
                    return std::string(style);
                }
            }
            return "manual";
        }

        std::string savepointManualCode(const std::string& name) {
            return "storage.savepoint(" + identifierToCppStringLiteral(name) + ");";
        }

        std::string savepointGuardCode(const std::string& name) {
            return "auto " + savepointGuardVariableName(name) + " = storage.savepoint_guard(" +
                   identifierToCppStringLiteral(name) + ");";
        }

        std::string savepointFunctionalCode(const std::string& name) {
            return "storage.savepoint(" + identifierToCppStringLiteral(name) + ", [&] {\n    return true;\n});";
        }

    }  // namespace

    CodeGenResult DdlCodeGenerator::generateSavepoint(const SavepointNode& node) {
        const std::string style = chosenSavepointStyle(this->context.codeGenPolicy);
        std::string code = style == "guard"        ? savepointGuardCode(node.name)
                           : style == "functional" ? savepointFunctionalCode(node.name)
                                                   : savepointManualCode(node.name);

        // options lists every variant (the chosen one included); the consumer decides how to
        // present the selection.
        std::vector<Option> options = {
            Option{"manual",
                   savepointManualCode(node.name),
                   "direct calls mirroring the SQL statements 1:1 (savepoint / release_savepoint / "
                   "rollback_to_savepoint)"},
            Option{"guard",
                   savepointGuardCode(node.name),
                   "RAII savepoint_t: rolls back and releases in its destructor unless released; RELEASE and "
                   "ROLLBACK TO for this name become " +
                       savepointGuardVariableName(node.name) + ".release() / .rollback_to()"},
            Option{"functional",
                   savepointFunctionalCode(node.name),
                   "storage.savepoint(name, lambda): the statements up to the matching RELEASE move into the "
                   "lambda; returning true releases the savepoint, false rolls it back"},
        };

        CodeGenResult result{std::move(code), {}, {}};
        result.decisionPoints.push_back(DecisionPoint{this->context.nextDecisionPointId++,
                                                      "savepoint_style",
                                                      style,
                                                      result.code,
                                                      std::move(options)});
        return result;
    }

    CodeGenResult DdlCodeGenerator::generateRelease(const ReleaseNode& node) {
        if (chosenSavepointStyle(this->context.codeGenPolicy) == "guard") {
            return CodeGenResult{savepointGuardVariableName(node.name) + ".release();", {}, {}};
        }
        return CodeGenResult{"storage.release_savepoint(" + identifierToCppStringLiteral(node.name) + ");", {}, {}};
    }

    CodeGenResult DdlCodeGenerator::generateVacuum(const VacuumStatementNode& node) {
        if (node.schemaName) {
            return CodeGenResult{
                "storage.vacuum();",
                {},
                {"VACUUM with explicit schema is not modeled separately in sqlite_orm::storage_base::vacuum()"}};
        }
        return CodeGenResult{"storage.vacuum();", {}, {}};
    }

    CodeGenResult DdlCodeGenerator::generateDrop(const DropStatementNode& node) {
        std::vector<CodegenWarning> warnings;
        if (node.schemaName) {
            warnings.push_back(
                "schema-qualified name in DROP is not represented in sqlite_orm; generated call uses unqualified "
                "name only");
        }
        const std::string literal = identifierToCppStringLiteral(node.objectName);
        switch (node.objectKind) {
            case DropObjectKind::table:
                return CodeGenResult{(node.ifExists ? "storage.drop_table_if_exists(" : "storage.drop_table(") +
                                         literal + ");",
                                     {},
                                     std::move(warnings)};
            case DropObjectKind::index:
                return CodeGenResult{(node.ifExists ? "storage.drop_index_if_exists(" : "storage.drop_index(") +
                                         literal + ");",
                                     {},
                                     std::move(warnings)};
            case DropObjectKind::trigger:
                return CodeGenResult{(node.ifExists ? "storage.drop_trigger_if_exists(" : "storage.drop_trigger(") +
                                         literal + ");",
                                     {},
                                     std::move(warnings)};
            case DropObjectKind::view: {
                CodeGenResult carried;
                carried.warnings = std::move(warnings);
                return unsupportedStatementPlaceholder(
                    this->context,
                    "DROP VIEW: not supported as storage.drop_* in sqlite_orm",
                    "DROP VIEW is not supported as a sqlite_orm storage method; sqlite_orm sync_schema() applies "
                    "to mapped tables/indexes/triggers, not views",
                    node,
                    std::move(carried));
            }
            default: {
                CodeGenResult carried;
                carried.warnings = std::move(warnings);
                return unsupportedStatementPlaceholder(this->context,
                                                       "DROP",
                                                       "this DROP statement is not mapped to sqlite_orm codegen",
                                                       node,
                                                       std::move(carried));
            }
        }
    }

    CodeGenResult DdlCodeGenerator::generateCreateVirtualTable(const CreateVirtualTableNode& node) {
        std::vector<CodegenWarning> warnings;
        std::vector<DecisionPoint> decisionPoints;

        if (node.tableSchemaName) {
            warnings.push_back(
                "schema-qualified VIRTUAL TABLE name is not represented in sqlite_orm; generated code uses "
                "unqualified table name only");
        }
        if (!node.ifNotExists) {
            warnings.push_back(
                "sqlite_orm serializes virtual tables as CREATE VIRTUAL TABLE IF NOT EXISTS; SQL without IF NOT "
                "EXISTS differs from serialized output");
        }
        if (node.temporary) {
            warnings.push_back(
                "TEMP/TEMPORARY VIRTUAL TABLE is not represented in sqlite_orm virtual table mapping; generated "
                "code does not mark the table as temporary");
        }

        std::string moduleLower = toLowerAscii(node.moduleName);
        std::string nameLiteral = identifierToCppStringLiteral(node.tableName);
        std::string structName = toStructName(node.tableName);
        // What a warning about one of the members the struct gets names the struct after.
        const std::string memberOwner = "virtual table " + stripIdentifierQuotes(node.tableName);

        // The first module argument sqlite_orm's using_*() forms have no place for, i.e. the one a
        // warning about unmapped arguments points at; null when every argument is a plain column.
        auto firstArgumentThatIsNoColumnRef = [&]() -> const AstNode* {
            for (const auto& moduleArgument: node.moduleArguments) {
                if (!dynamic_cast<const ColumnRefNode*>(moduleArgument.get())) {
                    return moduleArgument.get();
                }
            }
            return nullptr;
        };
        // Whatever was collected before the statement turned out to be unmappable, moved out of the
        // two vectors: exactly one of the branches below reaches this, on its way out.
        auto carriedParts = [](std::vector<DecisionPoint>& collectedDecisionPoints,
                               std::vector<CodegenWarning>& collectedWarnings) {
            CodeGenResult carried;
            carried.decisionPoints = std::move(collectedDecisionPoints);
            carried.warnings = std::move(collectedWarnings);
            return carried;
        };

        if (moduleLower == "fts5") {
            if (node.moduleArguments.empty()) {
                return unsupportedStatementPlaceholder(
                    this->context,
                    "CREATE VIRTUAL TABLE: fts5 (no columns)",
                    "FTS5 requires at least one column argument for sqlite_orm::using_fts5()",
                    node,
                    carriedParts(decisionPoints, warnings));
            }
            if (const AstNode* unmapped = firstArgumentThatIsNoColumnRef()) {
                return unsupportedStatementPlaceholder(
                    this->context,
                    "CREATE VIRTUAL TABLE: fts5 (unmapped arguments)",
                    "FTS5 module arguments that are not plain column names cannot be "
                    "mapped to sqlite_orm::using_fts5()",
                    *unmapped,
                    carriedParts(decisionPoints, warnings));
            }
            std::string code = "struct " + structName + " {\n";
            std::map<std::string, std::string> membersByName;
            for (const auto& moduleArgument: node.moduleArguments) {
                auto* columnRef = static_cast<const ColumnRefNode*>(moduleArgument.get());
                auto cppName = toCppIdentifier(columnRef->columnName);
                if (auto warning = recordMemberName(memberOwner,
                                                    stripIdentifierQuotes(columnRef->columnName),
                                                    cppName,
                                                    columnRef->sourceSpan,
                                                    membersByName)) {
                    warnings.push_back(std::move(*warning));
                }
                code += "    std::string " + cppName + ";\n";
            }
            code += "};\n\n";
            std::string savedStruct = this->context.structName;
            this->context.structName = structName;
            std::string columnParts;
            for (const auto& moduleArgument: node.moduleArguments) {
                auto* columnRef = static_cast<const ColumnRefNode*>(moduleArgument.get());
                CodeGenResult moduleArgumentCodegen = this->coordinator.generateNode(*moduleArgument);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(moduleArgumentCodegen.decisionPoints.begin()),
                                      std::make_move_iterator(moduleArgumentCodegen.decisionPoints.end()));
                warnings.insert(warnings.end(),
                                std::make_move_iterator(moduleArgumentCodegen.warnings.begin()),
                                std::make_move_iterator(moduleArgumentCodegen.warnings.end()));
                if (!columnParts.empty()) {
                    columnParts += ", ";
                }
                std::string rawColumn = stripIdentifierQuotes(columnRef->columnName);
                columnParts +=
                    "make_column(" + identifierToCppStringLiteral(rawColumn) + ", " + moduleArgumentCodegen.code + ")";
            }
            this->context.structName = savedStruct;
            code += "auto " + this->context.statementVariableName("vtab") + " = make_virtual_table<" + structName +
                    ">(" + nameLiteral + ", using_fts5(" + columnParts + "));\n";
            return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
        }

        if (moduleLower == "rtree" || moduleLower == "rtree_i32") {
            const size_t moduleArgumentsCount = node.moduleArguments.size();
            if (moduleArgumentsCount < 3 || moduleArgumentsCount > 11 || (moduleArgumentsCount % 2 == 0)) {
                return unsupportedStatementPlaceholder(
                    this->context,
                    "CREATE VIRTUAL TABLE: rtree (invalid column count)",
                    "RTREE virtual table for sqlite_orm needs 3, 5, 7, 9, or 11 simple column identifiers (id + "
                    "min/max pairs)",
                    node,
                    carriedParts(decisionPoints, warnings));
            }
            if (const AstNode* unmapped = firstArgumentThatIsNoColumnRef()) {
                return unsupportedStatementPlaceholder(
                    this->context,
                    "CREATE VIRTUAL TABLE: rtree (unmapped arguments)",
                    "RTREE module arguments that are not plain column names cannot be mapped to sqlite_orm "
                    "using_rtree() / using_rtree_i32()",
                    *unmapped,
                    carriedParts(decisionPoints, warnings));
            }
            const bool isInt32 = (moduleLower == "rtree_i32");
            std::string code = "struct " + structName + " {\n";
            std::map<std::string, std::string> membersByName;
            for (size_t columnIndex = 0; columnIndex < moduleArgumentsCount; ++columnIndex) {
                auto* columnRef = static_cast<const ColumnRefNode*>(node.moduleArguments[columnIndex].get());
                auto cppName = toCppIdentifier(columnRef->columnName);
                if (auto warning = recordMemberName(memberOwner,
                                                    stripIdentifierQuotes(columnRef->columnName),
                                                    cppName,
                                                    columnRef->sourceSpan,
                                                    membersByName)) {
                    warnings.push_back(std::move(*warning));
                }
                if (columnIndex == 0) {
                    code += "    int64_t " + cppName + " = 0;\n";
                } else if (isInt32) {
                    code += "    int32_t " + cppName + " = 0;\n";
                } else {
                    code += "    float " + cppName + " = 0.0;\n";
                }
            }
            code += "};\n\n";
            std::string savedStruct = this->context.structName;
            this->context.structName = structName;
            std::string columnParts;
            for (const auto& moduleArgument: node.moduleArguments) {
                auto* columnRef = static_cast<const ColumnRefNode*>(moduleArgument.get());
                CodeGenResult moduleArgumentCodegen = this->coordinator.generateNode(*moduleArgument);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(moduleArgumentCodegen.decisionPoints.begin()),
                                      std::make_move_iterator(moduleArgumentCodegen.decisionPoints.end()));
                warnings.insert(warnings.end(),
                                std::make_move_iterator(moduleArgumentCodegen.warnings.begin()),
                                std::make_move_iterator(moduleArgumentCodegen.warnings.end()));
                if (!columnParts.empty()) {
                    columnParts += ", ";
                }
                std::string rawColumn = stripIdentifierQuotes(columnRef->columnName);
                columnParts +=
                    "make_column(" + identifierToCppStringLiteral(rawColumn) + ", " + moduleArgumentCodegen.code + ")";
            }
            this->context.structName = savedStruct;
            const char* usingFunction = isInt32 ? "using_rtree_i32" : "using_rtree";
            code += "auto " + this->context.statementVariableName("vtab") + " = make_virtual_table<" + structName +
                    ">(" + nameLiteral + ", " + usingFunction + "(" + columnParts + "));\n";
            return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
        }

        if (moduleLower == "generate_series") {
            if (!node.moduleArguments.empty()) {
                return unsupportedStatementPlaceholder(
                    this->context,
                    "CREATE VIRTUAL TABLE: generate_series (unmapped arguments)",
                    "generate_series module arguments are not mapped to sqlite_orm; expected empty argument list "
                    "for make_virtual_table<generate_series>(..., internal::using_generate_series())",
                    *node.moduleArguments.front(),
                    carriedParts(decisionPoints, warnings));
            }
            std::string code = "auto " + this->context.statementVariableName("vtab") +
                               " = make_virtual_table<generate_series>(" + nameLiteral +
                               ", internal::using_generate_series());\n";
            return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
        }

        if (moduleLower == "dbstat") {
            if (node.moduleArguments.size() > 1) {
                return unsupportedStatementPlaceholder(
                    this->context,
                    "CREATE VIRTUAL TABLE: dbstat (too many arguments)",
                    "dbstat accepts at most one optional schema string argument for sqlite_orm::using_dbstat()",
                    *node.moduleArguments.at(1),
                    carriedParts(decisionPoints, warnings));
            }
            if (node.moduleArguments.empty()) {
                std::string code = "auto " + this->context.statementVariableName("vtab") +
                                   " = make_virtual_table<dbstat>(" + nameLiteral + ", using_dbstat());\n";
                return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
            }
            if (auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(node.moduleArguments[0].get())) {
                std::string code = "auto " + this->context.statementVariableName("vtab") +
                                   " = make_virtual_table<dbstat>(" + nameLiteral + ", using_dbstat(" +
                                   sqlStringToCpp(stringLiteral->value) + "));\n";
                return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
            }
            return unsupportedStatementPlaceholder(
                this->context,
                "CREATE VIRTUAL TABLE: dbstat (unmapped argument)",
                "dbstat optional argument should be a SQL string literal for sqlite_orm::using_dbstat(\"...\")",
                *node.moduleArguments.front(),
                carriedParts(decisionPoints, warnings));
        }

        return unsupportedStatementPlaceholder(this->context,
                                               "CREATE VIRTUAL TABLE: unknown module",
                                               "virtual table module \"" + std::string(node.moduleName) +
                                                   "\" has no sqlite_orm mapping in sqlite2orm codegen",
                                               node,
                                               carriedParts(decisionPoints, warnings));
    }

    CodeGenResult DdlCodeGenerator::generateCreateView(const CreateViewNode& node) {
        CreateViewParts parts = this->createViewParts(node);
        if (parts.makeViewExpression.empty()) {
            return CodeGenResult{"/* CREATE VIEW " + viewDisplayName(node) + " — not supported for sqlite_orm */",
                                 std::move(parts.decisionPoints),
                                 std::move(parts.warnings),
                                 {},
                                 std::move(parts.comments)};
        }
        std::string code =
            parts.structDeclaration + "\nauto storage = make_storage(\"\",\n    " + parts.makeViewExpression + ");";
        return CodeGenResult{std::move(code),
                             std::move(parts.decisionPoints),
                             std::move(parts.warnings),
                             {},
                             std::move(parts.comments)};
    }

}  // namespace sqlite2orm
