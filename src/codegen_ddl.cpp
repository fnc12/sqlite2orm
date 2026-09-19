#include "codegen_ddl.h"
#include "codegen_context.h"
#include "codegen_utils.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

namespace sqlite2orm {

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
                    typeChain += "&" + subject + "::" + toCppIdentifier(createTrigger.updateOfColumns[columnIndex]);
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
        warnings.insert(warnings.end(),
                        std::make_move_iterator(whenClauseWarnings.begin()),
                        std::make_move_iterator(whenClauseWarnings.end()));

        std::string triggerLiteral = identifierToCppStringLiteral(createTrigger.triggerName);
        std::string code = "make_trigger(" + triggerLiteral + ", " + base + ".begin(" + stepsJoined + "));";
        return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
    }

    CodeGenResult DdlCodeGenerator::generateCreateIndex(const CreateIndexNode& createIndex) {
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
                return unsupportedPlaceholder(
                    "DROP VIEW: not supported as storage.drop_* in sqlite_orm",
                    "DROP VIEW is not supported as a sqlite_orm storage method; sqlite_orm sync_schema() applies "
                    "to mapped tables/indexes/triggers, not views",
                    node,
                    std::move(carried));
            }
            default: {
                CodeGenResult carried;
                carried.warnings = std::move(warnings);
                return unsupportedPlaceholder("DROP",
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
                return unsupportedPlaceholder("CREATE VIRTUAL TABLE: fts5 (no columns)",
                                              "FTS5 requires at least one column argument for sqlite_orm::using_fts5()",
                                              node,
                                              carriedParts(decisionPoints, warnings));
            }
            if (const AstNode* unmapped = firstArgumentThatIsNoColumnRef()) {
                return unsupportedPlaceholder("CREATE VIRTUAL TABLE: fts5 (unmapped arguments)",
                                              "FTS5 module arguments that are not plain column names cannot be "
                                              "mapped to sqlite_orm::using_fts5()",
                                              *unmapped,
                                              carriedParts(decisionPoints, warnings));
            }
            std::string code = "struct " + structName + " {\n";
            for (const auto& moduleArgument: node.moduleArguments) {
                auto* columnRef = static_cast<const ColumnRefNode*>(moduleArgument.get());
                auto cppName = toCppIdentifier(columnRef->columnName);
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
                return unsupportedPlaceholder(
                    "CREATE VIRTUAL TABLE: rtree (invalid column count)",
                    "RTREE virtual table for sqlite_orm needs 3, 5, 7, 9, or 11 simple column identifiers (id + "
                    "min/max pairs)",
                    node,
                    carriedParts(decisionPoints, warnings));
            }
            if (const AstNode* unmapped = firstArgumentThatIsNoColumnRef()) {
                return unsupportedPlaceholder(
                    "CREATE VIRTUAL TABLE: rtree (unmapped arguments)",
                    "RTREE module arguments that are not plain column names cannot be mapped to sqlite_orm "
                    "using_rtree() / using_rtree_i32()",
                    *unmapped,
                    carriedParts(decisionPoints, warnings));
            }
            const bool isInt32 = (moduleLower == "rtree_i32");
            std::string code = "struct " + structName + " {\n";
            for (size_t columnIndex = 0; columnIndex < moduleArgumentsCount; ++columnIndex) {
                auto* columnRef = static_cast<const ColumnRefNode*>(node.moduleArguments[columnIndex].get());
                auto cppName = toCppIdentifier(columnRef->columnName);
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
                return unsupportedPlaceholder(
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
                return unsupportedPlaceholder(
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
            return unsupportedPlaceholder(
                "CREATE VIRTUAL TABLE: dbstat (unmapped argument)",
                "dbstat optional argument should be a SQL string literal for sqlite_orm::using_dbstat(\"...\")",
                *node.moduleArguments.front(),
                carriedParts(decisionPoints, warnings));
        }

        return unsupportedPlaceholder("CREATE VIRTUAL TABLE: unknown module",
                                      "virtual table module \"" + std::string(node.moduleName) +
                                          "\" has no sqlite_orm mapping in sqlite2orm codegen",
                                      node,
                                      carriedParts(decisionPoints, warnings));
    }

    namespace {

        struct InferredFieldType {
            std::string cppType;
            bool nullable = false;
        };

        /** Resolves view SELECT output expressions to C++ field types using tables known to the context. */
        struct ViewFieldTypeInferrer {
            const CodeGeneratorContext& context;
            /** Raw FROM table names in order; aliases resolved separately. */
            std::vector<std::string> fromTables;
            std::map<std::string, std::string> tableByAliasNorm;

            explicit ViewFieldTypeInferrer(const CodeGeneratorContext& context, const SelectNode& selectNode) :
                context(context) {
                for (const FromClauseItem& fromItem: selectNode.fromClause) {
                    if (fromItem.table.derivedSelect || !fromItem.table.tableFunctionArgs.empty()) {
                        continue;
                    }
                    const std::string rawTable = stripIdentifierQuotes(fromItem.table.tableName);
                    this->fromTables.push_back(rawTable);
                    if (fromItem.table.alias) {
                        this->tableByAliasNorm[normalizeSqlIdentifier(*fromItem.table.alias)] = rawTable;
                    }
                }
            }

            const SourceTableColumn* resolveQualified(std::string_view tableOrAlias,
                                                      std::string_view columnName) const {
                const auto aliasIterator = this->tableByAliasNorm.find(normalizeSqlIdentifier(tableOrAlias));
                const std::string_view tableName = aliasIterator != this->tableByAliasNorm.end()
                                                       ? std::string_view(aliasIterator->second)
                                                       : tableOrAlias;
                return this->context.findSourceTableColumn(tableName, columnName);
            }

            const SourceTableColumn* resolveUnqualified(std::string_view columnName) const {
                for (const std::string& tableName: this->fromTables) {
                    if (const SourceTableColumn* column = this->context.findSourceTableColumn(tableName, columnName)) {
                        return column;
                    }
                }
                return nullptr;
            }

            std::optional<InferredFieldType> inferFunctionCall(const FunctionCallNode& functionCall) const {
                const std::string lower = toLowerAscii(functionCall.name);
                auto firstArgument = [&]() -> std::optional<InferredFieldType> {
                    if (functionCall.arguments.empty() || !functionCall.arguments.front()) {
                        return std::nullopt;
                    }
                    return this->infer(*functionCall.arguments.front());
                };
                if (lower == "count" || lower == "row_number" || lower == "rank" || lower == "dense_rank" ||
                    lower == "ntile" || lower == "length" || lower == "octet_length" || lower == "instr" ||
                    lower == "unicode" || lower == "changes" || lower == "total_changes" ||
                    lower == "last_insert_rowid") {
                    return InferredFieldType{"int"};
                }
                if (lower == "avg" || lower == "total" || lower == "sum" || lower == "round" || lower == "julianday" ||
                    lower == "percent_rank" || lower == "cume_dist" || lower == "unixepoch" || lower == "pow" ||
                    lower == "power" || lower == "sqrt" || lower == "exp" || lower == "ln" || lower == "log" ||
                    lower == "log2" || lower == "log10" || lower == "sin" || lower == "cos" || lower == "tan" ||
                    lower == "asin" || lower == "acos" || lower == "atan" || lower == "atan2" || lower == "degrees" ||
                    lower == "radians" || lower == "pi" || lower == "ceil" || lower == "ceiling" || lower == "floor" ||
                    lower == "trunc" || lower == "mod") {
                    return InferredFieldType{"double"};
                }
                if (lower == "group_concat" || lower == "string_agg" || lower == "upper" || lower == "lower" ||
                    lower == "substr" || lower == "substring" || lower == "trim" || lower == "ltrim" ||
                    lower == "rtrim" || lower == "replace" || lower == "hex" || lower == "quote" || lower == "printf" ||
                    lower == "format" || lower == "typeof" || lower == "char" || lower == "date" || lower == "time" ||
                    lower == "datetime" || lower == "strftime" || lower == "concat" || lower == "concat_ws" ||
                    lower == "json" || lower == "json_extract" || lower == "json_array" || lower == "json_object" ||
                    lower == "json_quote" || lower == "json_group_array" || lower == "json_group_object") {
                    return InferredFieldType{"std::string"};
                }
                if (lower == "random") {
                    return InferredFieldType{"int64_t"};
                }
                if (lower == "randomblob" || lower == "zeroblob") {
                    return InferredFieldType{"std::vector<char>"};
                }
                if (lower == "abs" || lower == "min" || lower == "max" || lower == "coalesce" || lower == "ifnull" ||
                    lower == "nullif" || lower == "lag" || lower == "lead" || lower == "first_value" ||
                    lower == "last_value" || lower == "nth_value") {
                    return firstArgument();
                }
                if (lower == "iif") {
                    if (functionCall.arguments.size() >= 2 && functionCall.arguments.at(1)) {
                        return this->infer(*functionCall.arguments.at(1));
                    }
                    return std::nullopt;
                }
                return std::nullopt;
            }

            std::optional<InferredFieldType> infer(const AstNode& node) const {
                if (auto* columnRef = dynamic_cast<const ColumnRefNode*>(&node)) {
                    if (const SourceTableColumn* column = this->resolveUnqualified(columnRef->columnName)) {
                        return InferredFieldType{column->cppType, column->nullable};
                    }
                    return std::nullopt;
                }
                if (auto* qualifiedRef = dynamic_cast<const QualifiedColumnRefNode*>(&node)) {
                    if (const SourceTableColumn* column =
                            this->resolveQualified(qualifiedRef->tableName, qualifiedRef->columnName)) {
                        return InferredFieldType{column->cppType, column->nullable};
                    }
                    return std::nullopt;
                }
                if (auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&node)) {
                    // An integer literal an int64 cannot hold is a REAL for SQLite, so the field
                    // holding it has to be a double as well.
                    return InferredFieldType{integerLiteralExceedsInt64(integerLiteral->value) ? "double" : "int64_t"};
                }
                if (dynamic_cast<const RealLiteralNode*>(&node))
                    return InferredFieldType{"double"};
                if (dynamic_cast<const StringLiteralNode*>(&node))
                    return InferredFieldType{"std::string"};
                if (dynamic_cast<const BoolLiteralNode*>(&node))
                    return InferredFieldType{"bool"};
                if (dynamic_cast<const BlobLiteralNode*>(&node))
                    return InferredFieldType{"std::vector<char>"};
                if (dynamic_cast<const CurrentDatetimeLiteralNode*>(&node)) {
                    return InferredFieldType{"std::string"};
                }
                if (auto* cast = dynamic_cast<const CastNode*>(&node)) {
                    return InferredFieldType{sqliteTypeToCpp(cast->typeName)};
                }
                if (auto* collate = dynamic_cast<const CollateNode*>(&node)) {
                    if (collate->operand) {
                        return this->infer(*collate->operand);
                    }
                    return std::nullopt;
                }
                if (auto* functionCall = dynamic_cast<const FunctionCallNode*>(&node)) {
                    return this->inferFunctionCall(*functionCall);
                }
                if (auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&node)) {
                    switch (unaryOperator->unaryOperator) {
                        case UnaryOperator::minus:
                        case UnaryOperator::plus:
                            if (unaryOperator->operand) {
                                return this->infer(*unaryOperator->operand);
                            }
                            return std::nullopt;
                        case UnaryOperator::bitwiseNot:
                            return InferredFieldType{"int64_t"};
                        case UnaryOperator::logicalNot:
                            return InferredFieldType{"bool"};
                    }
                    return std::nullopt;
                }
                if (auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&node)) {
                    switch (binaryOperator->binaryOperator) {
                        case BinaryOperator::concatenate:
                        case BinaryOperator::jsonArrow:
                        case BinaryOperator::jsonArrow2:
                            return InferredFieldType{"std::string"};
                        case BinaryOperator::add:
                        case BinaryOperator::subtract:
                        case BinaryOperator::multiply:
                        case BinaryOperator::divide:
                        case BinaryOperator::modulo: {
                            auto lhsType = binaryOperator->lhs ? this->infer(*binaryOperator->lhs) : std::nullopt;
                            auto rhsType = binaryOperator->rhs ? this->infer(*binaryOperator->rhs) : std::nullopt;
                            if ((lhsType && lhsType->cppType == "double") ||
                                (rhsType && rhsType->cppType == "double")) {
                                return InferredFieldType{"double"};
                            }
                            if (lhsType || rhsType) {
                                return InferredFieldType{"int64_t"};
                            }
                            return std::nullopt;
                        }
                        case BinaryOperator::bitwiseAnd:
                        case BinaryOperator::bitwiseOr:
                        case BinaryOperator::shiftLeft:
                        case BinaryOperator::shiftRight:
                            return InferredFieldType{"int64_t"};
                        default:
                            return InferredFieldType{"bool"};
                    }
                }
                if (dynamic_cast<const IsNullNode*>(&node) || dynamic_cast<const IsNotNullNode*>(&node) ||
                    dynamic_cast<const BetweenNode*>(&node) || dynamic_cast<const InNode*>(&node) ||
                    dynamic_cast<const ExistsNode*>(&node) || dynamic_cast<const LikeNode*>(&node) ||
                    dynamic_cast<const GlobNode*>(&node) || dynamic_cast<const MatchNode*>(&node)) {
                    return InferredFieldType{"bool"};
                }
                if (auto* caseNode = dynamic_cast<const CaseNode*>(&node)) {
                    if (!caseNode->branches.empty() && caseNode->branches.front().result) {
                        return this->infer(*caseNode->branches.front().result);
                    }
                    return std::nullopt;
                }
                return std::nullopt;
            }
        };

        std::string viewDisplayName(const CreateViewNode& node) {
            std::string displayName;
            if (node.viewSchemaName) {
                displayName = *node.viewSchemaName;
                displayName += '.';
            }
            displayName += node.viewName;
            return displayName;
        }

    }  // namespace

    CreateViewParts DdlCodeGenerator::createViewParts(const CreateViewNode& node) {
        CreateViewParts parts = this->viewParts(node);
        if (parts.makeViewExpression.empty()) {
            // A view sqlite_orm has no make_view() for is a name it has no type for either, so
            // whatever rests on the view — a trigger INSTEAD OF it, a view selecting from it —
            // has to go with it, exactly as it does for a table left out of the storage.
            this->context.markUngeneratableView(node.viewName);
        }
        return parts;
    }

    CreateViewParts DdlCodeGenerator::viewParts(const CreateViewNode& node) {
        CreateViewParts parts;
        const std::string displayName = viewDisplayName(node);
        if (!node.selectQuery) {
            parts.warnings.push_back("CREATE VIEW " + displayName + " has no SELECT; cannot generate make_view()");
            return parts;
        }
        if (node.viewSchemaName) {
            parts.warnings.push_back(
                "schema-qualified view name is not represented in sqlite_orm; generated code uses unqualified "
                "view name only");
        }

        // A view body is stored, never compiled, so SQLite accepts a hex literal in it that it
        // refuses in a query; C++ has no literal for one, so the view cannot be generated.
        this->context.storedHexLiteralsTooBig.clear();
        const bool viewWasStoredExpression = this->context.storedExpression;
        this->context.storedExpression = true;
        CodeGenResult selectExpression = this->coordinator.tryCodegenSelectLikeSubquery(*node.selectQuery);
        this->context.storedExpression = viewWasStoredExpression;
        for (const std::string& literal: this->context.storedHexLiteralsTooBig) {
            parts.warnings.push_back("CREATE VIEW " + displayName + " uses " + literal +
                                     ", too big for a signed 64-bit integer: SQLite stores the view but refuses "
                                     "every query against it, and C++ has no literal for it, so the view is not "
                                     "generated");
        }
        if (!this->context.storedHexLiteralsTooBig.empty()) {
            return parts;
        }
        parts.decisionPoints.insert(parts.decisionPoints.end(),
                                    std::make_move_iterator(selectExpression.decisionPoints.begin()),
                                    std::make_move_iterator(selectExpression.decisionPoints.end()));
        appendUniqueWarnings(parts.warnings, selectExpression.warnings);
        appendUniqueStrings(parts.comments, selectExpression.comments);
        if (selectExpression.code.empty()) {
            parts.warnings.push_back("CREATE VIEW " + displayName +
                                     ": SELECT is not supported for sqlite_orm code generation");
            return parts;
        }

        const SelectNode* firstSelect = dynamic_cast<const SelectNode*>(node.selectQuery.get());
        if (!firstSelect) {
            if (auto* compound = dynamic_cast<const CompoundSelectNode*>(node.selectQuery.get())) {
                if (!compound->selects.empty()) {
                    firstSelect = dynamic_cast<const SelectNode*>(compound->selects.front().get());
                }
            }
        }
        if (!firstSelect) {
            parts.warnings.push_back("CREATE VIEW " + displayName + ": cannot derive view columns from its SELECT");
            return parts;
        }

        const ViewFieldTypeInferrer inferrer(this->context, *firstSelect);
        const std::string rawViewName = stripIdentifierQuotes(node.viewName);
        const std::string structName = toStructName(node.viewName);

        struct ViewField {
            std::string cppName;
            std::string cppType;
            bool nullable = false;
        };
        std::vector<ViewField> fields;
        std::vector<SourceTableColumn> registeredColumns;

        auto appendField = [&](std::string sqlName,
                               const std::optional<InferredFieldType>& inferred,
                               std::optional<SourceLocation> location,
                               size_t underlineLength) {
            ViewField field;
            field.cppName = toCppIdentifier(sqlName);
            if (inferred) {
                field.cppType = inferred->cppType;
                field.nullable = inferred->nullable;
            } else {
                field.cppType = defaultCppTypeForSyntheticColumn(field.cppName);
                std::string message = "view " + rawViewName + ": type of column `" + sqlName +
                                      "` could not be inferred; defaulting to " + field.cppType;
                if (location) {
                    parts.warnings.push_back(CodegenWarning{std::move(message), *location, underlineLength});
                } else {
                    parts.warnings.push_back(CodegenWarning{std::move(message)});
                }
            }
            registeredColumns.push_back(SourceTableColumn{std::move(sqlName), field.cppType, field.nullable});
            fields.push_back(std::move(field));
        };

        auto expandAllColumnsOf = [&](std::string_view tableName) -> bool {
            const auto tableIterator =
                this->context.sourceTableColumnsByNormalizedName.find(normalizeSqlIdentifier(tableName));
            if (tableIterator == this->context.sourceTableColumnsByNormalizedName.end()) {
                return false;
            }
            for (const SourceTableColumn& column: tableIterator->second) {
                appendField(column.sqlName, InferredFieldType{column.cppType, column.nullable}, std::nullopt, 0);
            }
            return true;
        };

        {
            if (!node.columnNames.empty() && node.columnNames.size() != firstSelect->columns.size()) {
                parts.warnings.push_back("view " + rawViewName +
                                         ": explicit column list size differs from SELECT column count");
            }
            for (size_t columnIndex = 0; columnIndex < firstSelect->columns.size(); ++columnIndex) {
                const SelectColumn& selectColumn = firstSelect->columns.at(columnIndex);
                if (!selectColumn.expression) {
                    // Bare `SELECT *`: expand every FROM table's columns.
                    if (inferrer.fromTables.empty()) {
                        parts.warnings.push_back("CREATE VIEW " + displayName +
                                                 ": cannot derive view columns from SELECT *");
                        return parts;
                    }
                    for (const std::string& tableName: inferrer.fromTables) {
                        if (!expandAllColumnsOf(tableName)) {
                            parts.warnings.push_back("CREATE VIEW " + displayName + ": columns of table `" + tableName +
                                                     "` are unknown; cannot derive view columns from SELECT *");
                            return parts;
                        }
                    }
                    continue;
                }
                if (auto* qualifiedAsterisk =
                        dynamic_cast<const QualifiedAsteriskNode*>(selectColumn.expression.get())) {
                    std::string_view sourceTable = qualifiedAsterisk->tableName;
                    const auto aliasIterator = inferrer.tableByAliasNorm.find(normalizeSqlIdentifier(sourceTable));
                    if (aliasIterator != inferrer.tableByAliasNorm.end()) {
                        sourceTable = aliasIterator->second;
                    }
                    if (!expandAllColumnsOf(sourceTable)) {
                        parts.warnings.push_back("CREATE VIEW " + displayName + ": columns of table `" +
                                                 std::string(sourceTable) +
                                                 "` are unknown; cannot derive view columns from `.*`");
                        return parts;
                    }
                    continue;
                }
                std::string sqlName;
                if (columnIndex < node.columnNames.size()) {
                    sqlName = stripIdentifierQuotes(node.columnNames.at(columnIndex));
                } else if (!selectColumn.alias.empty()) {
                    sqlName = stripIdentifierQuotes(selectColumn.alias);
                } else if (auto* columnRef = dynamic_cast<const ColumnRefNode*>(selectColumn.expression.get())) {
                    sqlName = stripIdentifierQuotes(columnRef->columnName);
                } else if (auto* qualifiedRef =
                               dynamic_cast<const QualifiedColumnRefNode*>(selectColumn.expression.get())) {
                    sqlName = stripIdentifierQuotes(qualifiedRef->columnName);
                } else {
                    sqlName = "column_" + std::to_string(columnIndex + 1);
                    parts.warnings.push_back("view " + rawViewName + ": SELECT column " +
                                             std::to_string(columnIndex + 1) +
                                             " has no name; using synthesized field name `" + sqlName + "`");
                }
                std::optional<InferredFieldType> inferred;
                std::optional<SourceLocation> columnLocation;
                size_t underlineLength = 0;
                if (selectColumn.expression) {
                    inferred = inferrer.infer(*selectColumn.expression);
                    // The underline covers the SELECT expression whose type could not be
                    // inferred, as the source spells it, and only when that expression is a bare
                    // column reference — whatever name the field ends up carrying, be it one from
                    // the view's column list or an alias. A qualified reference, or any other
                    // expression, has no single token to measure at that location, so such a
                    // warning is left unanchored rather than underlining whatever is there.
                    if (const auto* columnRef = dynamic_cast<const ColumnRefNode*>(selectColumn.expression.get())) {
                        columnLocation = columnRef->location;
                        underlineLength = underlineLengthOf(columnRef->columnName);
                    }
                }
                appendField(std::move(sqlName), inferred, columnLocation, underlineLength);
            }
        }

        std::string structDeclaration = "struct [[= \"" + rawViewName + "\"_orm_name]] " + structName + " {\n";
        for (const ViewField& field: fields) {
            if (field.nullable) {
                structDeclaration += "    std::optional<" + field.cppType + "> " + field.cppName + ";\n";
            } else {
                structDeclaration +=
                    "    " + field.cppType + " " + field.cppName + defaultInitializer(field.cppType) + ";\n";
            }
        }
        structDeclaration += "};\n";

        this->context.registerSourceTable(rawViewName, std::move(registeredColumns));

        parts.structDeclaration = std::move(structDeclaration);
        parts.makeViewExpression = "make_view<" + structName + ">(" + selectExpression.code + ")";
        appendUniqueString(parts.comments, kCommentViewReflection);
        // sqlite_orm maps views to C++26 reflection (make_view + [[= "…"_orm_name]]); there is no
        // pre-C++26 form. Surface a visible warning (anchored at the statement's opening keywords,
        // as the source spells them) when the target is lower.
        if (policyTargetCppStandard(this->context.codeGenPolicy) < 26) {
            std::string message =
                "CREATE VIEW " + displayName +
                ": sqlite_orm views use C++26 reflection (make_view + [[= \"…\"_orm_name]]); this code "
                "requires C++26 and will not compile under the selected C++ standard";
            if (node.headerText.empty()) {
                parts.warnings.push_back(CodegenWarning{std::move(message)});
            } else {
                parts.warnings.push_back(
                    CodegenWarning{std::move(message), node.location, underlineLengthOf(node.headerText)});
            }
        }
        return parts;
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

    CreateTableParts DdlCodeGenerator::createTableParts(const CreateTableNode& createTable) {
        const auto structName = toStructName(createTable.tableName);
        this->context.structName = structName;
        const auto rawTableName = stripIdentifierQuotes(createTable.tableName);
        std::vector<CodegenWarning> warnings;
        bool tableIsGeneratable = true;
        // A DEFAULT / CHECK / generated-column expression is the one place an expression clause
        // reaches codegen without going through the validator, so its warnings are the only word a
        // user gets about it — `-0x8000000000000000` is refused by SQLite wherever it is used, and a
        // negated predicate has no sqlite_orm form at all.
        //
        // SQLite stores the text of such a clause and compiles it only when the clause is used, so
        // it keeps a hex literal no int64 can hold where a query is refused: `stored` asks for the
        // literal to be collected in `storedHexLiteralsTooBig`, which the caller reads right after,
        // rather than to fail the statement. A VIRTUAL generated column is the one clause SQLite
        // compiles at CREATE TABLE time, so it is generated as an ordinary expression.
        auto clauseExpressionCode = [this, &warnings](const AstNode& expression, bool stored) {
            auto result = stored ? this->coordinator.generateStoredExpression(expression)
                                 : this->coordinator.generateNode(expression);
            warnings.insert(warnings.end(),
                            std::make_move_iterator(result.warnings.begin()),
                            std::make_move_iterator(result.warnings.end()));
            return std::move(result.code);
        };

        // The reflected form of the table (C++26 `make_table<T>()` over an annotated struct,
        // sqlite_orm #1492) is built beside the classical one: the members are the same, a column
        // constraint becomes a member annotation instead of a `make_column()` argument, and the
        // table name comes from a class-scope `[[= "…"_orm_name]]` annotation. `reflectionBlocker`
        // names the first construct of this table that has no annotation form; while it is empty
        // the reflected form is offered, and targeting C++26 it is the one chosen.
        std::string reflectionBlocker;
        const auto blockReflection = [&reflectionBlocker](std::string reason) {
            if (reflectionBlocker.empty()) {
                reflectionBlocker = std::move(reason);
            }
        };
        std::vector<std::string> memberDeclarations;
        memberDeclarations.reserve(createTable.columns.size());

        std::string structDeclaration = "struct " + structName + " {\n";
        for (const auto& column: createTable.columns) {
            const auto cppName = toCppIdentifier(column.name);
            const auto cppType = column.typeName.empty() ? "std::vector<char>" : sqliteTypeToCpp(column.typeName);
            const bool nullable = !column.primaryKey && !column.notNull;
            std::string memberDeclaration = nullable ? "std::optional<" + cppType + "> " + cppName + ";\n"
                                                     : cppType + " " + cppName + defaultInitializer(cppType) + ";\n";
            structDeclaration += "    " + memberDeclaration;
            memberDeclarations.push_back(std::move(memberDeclaration));
            // A reflected column carries the member's identifier as its name — there is no
            // renaming in sqlite_orm's reflected `make_table<T>()` — so a name C++ cannot spell,
            // or one this generator had to rewrite, has no reflected form at all.
            if (stripIdentifierQuotes(column.name) != cppName) {
                blockReflection("column `" + stripIdentifierQuotes(column.name) +
                                "` is not a C++ identifier, and a reflected column is named after the member it "
                                "reflects");
            }
        }
        structDeclaration += "};\n";
        this->context.registerSourceTable(rawTableName, sourceTableColumnsFromCreateTable(createTable));

        std::vector<std::string> memberAnnotations(createTable.columns.size());

        std::string makeExpression = "make_table(\"" + rawTableName + "\"";
        for (size_t columnIndex = 0; columnIndex < createTable.columns.size(); ++columnIndex) {
            const auto& column = createTable.columns.at(columnIndex);
            std::string& annotations = memberAnnotations.at(columnIndex);
            const auto annotate = [&annotations](const std::string& constraintCode) {
                annotations += "[[= " + constraintCode + "]] ";
            };
            const auto cppName = toCppIdentifier(column.name);
            const auto rawColumnName = stripIdentifierQuotes(column.name);
            makeExpression += ",\n        make_column(\"" + rawColumnName + "\", &" + structName + "::" + cppName;
            if (column.primaryKey) {
                std::string primaryKey = "primary_key()";
                switch (column.primaryKeyConflict) {
                    case ConflictClause::rollback:
                        primaryKey += ".on_conflict_rollback()";
                        break;
                    case ConflictClause::abort:
                        primaryKey += ".on_conflict_abort()";
                        break;
                    case ConflictClause::fail:
                        primaryKey += ".on_conflict_fail()";
                        break;
                    case ConflictClause::ignore:
                        primaryKey += ".on_conflict_ignore()";
                        break;
                    case ConflictClause::replace:
                        primaryKey += ".on_conflict_replace()";
                        break;
                    case ConflictClause::none:
                        break;
                }
                if (column.autoincrement) {
                    primaryKey += ".autoincrement()";
                }
                makeExpression += ", " + primaryKey;
                annotate(primaryKey);
            }
            if (column.defaultValue) {
                const auto defaultCode = clauseExpressionCode(*column.defaultValue, true);
                if (this->context.storedHexLiteralsTooBig.empty()) {
                    makeExpression += ", default_value(" + defaultCode + ")";
                    // An annotation is a constant expression, so only a default the compiler folds
                    // can stand as one: a text default is a pointer to a string literal and an
                    // expression default is no literal at all.
                    if (isAnnotationConstantValueCode(defaultCode)) {
                        annotate("default_value(" + defaultCode + ")");
                    } else {
                        blockReflection("the DEFAULT of column `" + rawColumnName + "` is generated as `" +
                                        defaultCode +
                                        "`, which is not a constant expression an annotation can "
                                        "carry");
                    }
                } else {
                    for (const std::string& literal: this->context.storedHexLiteralsTooBig) {
                        warnings.push_back("DEFAULT " + literal + " on column '" + rawColumnName +
                                           "' is too big for a signed 64-bit integer: SQLite stores it but "
                                           "refuses every use of the default, and C++ has no literal for it, so "
                                           "the generated column has no default_value()");
                    }
                }
            }
            if (column.unique) {
                makeExpression += ", unique()";
                // sqlite_orm accepts `unique()` as a column constraint, but its reflected form is
                // not exercised upstream, so a column-level UNIQUE keeps the table on the
                // classical mapping rather than betting on an annotation that may not compile.
                blockReflection("UNIQUE on column `" + rawColumnName + "` has no annotation form in sqlite_orm");
                if (column.uniqueConflict != ConflictClause::none) {
                    warnings.push_back("UNIQUE ON CONFLICT clause on column '" + rawColumnName +
                                       "' is not supported by sqlite_orm::unique()");
                }
            }
            if (column.checkExpression) {
                const auto checkCode = clauseExpressionCode(*column.checkExpression, true);
                if (this->context.storedHexLiteralsTooBig.empty()) {
                    makeExpression += ", check(" + checkCode + ")";
                    // A column CHECK names the table's own members, and a member annotation is
                    // parsed inside the class, where the members it names are not all declared yet
                    // — upstream sqlite_orm states such annotations are not expressible in C++26.
                    blockReflection("CHECK on column `" + rawColumnName +
                                    "` names members of the struct being declared, which an annotation cannot");
                } else {
                    for (const std::string& literal: this->context.storedHexLiteralsTooBig) {
                        warnings.push_back("CHECK on column '" + rawColumnName + "' uses " + literal +
                                           ", too big for a signed 64-bit integer: SQLite stores it but refuses "
                                           "every use of the constraint, and C++ has no literal for it, so the "
                                           "generated column has no check()");
                    }
                }
            }
            if (!column.collation.empty()) {
                const auto lower = toLowerAscii(column.collation);
                if (lower == "nocase") {
                    makeExpression += ", collate_nocase()";
                    annotate("collate_nocase()");
                } else if (lower == "binary") {
                    makeExpression += ", collate_binary()";
                    annotate("collate_binary()");
                } else if (lower == "rtrim") {
                    makeExpression += ", collate_rtrim()";
                    annotate("collate_rtrim()");
                } else {
                    warnings.push_back("COLLATE " + column.collation + " on column '" + rawColumnName +
                                       "' is not a built-in collation in sqlite_orm");
                }
            }
            if (column.generatedExpression) {
                // Only a STORED generated column is stored text: SQLite compiles it when a row is
                // written, so it keeps a hex literal past the int64 range that it refuses in a
                // VIRTUAL one (the default) already at CREATE TABLE time. C++ has no literal for
                // it, and a column that lost its as(...) would be an ordinary column instead of a
                // generated one, so the whole table is left out rather than reshaped.
                const bool storedGenerated = column.generatedStorage == ColumnDef::GeneratedStorage::stored;
                const auto expressionCode = clauseExpressionCode(*column.generatedExpression, storedGenerated);
                if (storedGenerated && !this->context.storedHexLiteralsTooBig.empty()) {
                    for (const std::string& literal: this->context.storedHexLiteralsTooBig) {
                        warnings.push_back("STORED generated column '" + rawColumnName + "' uses " + literal +
                                           ", too big for a signed 64-bit integer: SQLite stores the table but "
                                           "refuses every row written to it, and C++ has no literal for it, so "
                                           "the table is not generated");
                    }
                    tableIsGeneratable = false;
                } else {
                    if (column.generatedAlways) {
                        makeExpression += ", generated_always_as(" + expressionCode + ")";
                    } else {
                        makeExpression += ", as(" + expressionCode + ")";
                    }
                    // Same as a column CHECK: the expression names the struct's own members.
                    blockReflection("generated column `" + rawColumnName +
                                    "` names members of the struct being declared, which an annotation cannot");
                    if (column.generatedStorage == ColumnDef::GeneratedStorage::stored) {
                        makeExpression += ".stored()";
                    } else if (column.generatedStorage == ColumnDef::GeneratedStorage::virtual_) {
                        makeExpression += ".virtual_()";
                    }
                }
            }
            makeExpression += ")";
        }
        // Table-level constraints are collected rather than appended: the classical form spells
        // them after the columns of `make_table("name", …)`, the reflected form passes the same
        // text to `make_table<T>(…)`, whose columns come from the struct instead.
        std::vector<std::string> tableConstraints;

        auto findPrimaryKeyColumnCpp = [&createTable](std::string_view refTable) -> std::string {
            if (toLowerAscii(std::string(refTable)) == toLowerAscii(createTable.tableName)) {
                for (const auto& column: createTable.columns) {
                    if (column.primaryKey) {
                        return toCppIdentifier(column.name);
                    }
                }
                if (!createTable.primaryKeys.empty() && !createTable.primaryKeys[0].columns.empty()) {
                    return toCppIdentifier(createTable.primaryKeys[0].columns[0]);
                }
            }
            return {};
        };
        for (const auto& column: createTable.columns) {
            if (!column.foreignKey) {
                continue;
            }
            auto& foreignKey = *column.foreignKey;
            const auto cppName = toCppIdentifier(column.name);
            // sqlite_orm resolves a foreign key against the table it maps for the referenced
            // type, so a key into a table this batch cannot map would not compile at all.
            if (this->context.isUngeneratableTable(foreignKey.table)) {
                warnings.push_back("foreign key on column '" + stripIdentifierQuotes(column.name) + "' references " +
                                   stripIdentifierQuotes(foreignKey.table) +
                                   ", which is not generated, so the generated table has no foreign_key()");
                continue;
            }
            const auto referencedStructName = toStructName(foreignKey.table);
            std::string referencedColumnName;
            if (!foreignKey.column.empty()) {
                referencedColumnName = toCppIdentifier(foreignKey.column);
            } else {
                referencedColumnName = findPrimaryKeyColumnCpp(foreignKey.table);
                if (referencedColumnName.empty()) {
                    referencedColumnName = cppName;
                }
            }
            std::string constraint = "foreign_key(&" + structName + "::" + cppName + ").references(&" +
                                     referencedStructName + "::" + referencedColumnName + ")";
            const auto actionString = [](ForeignKeyAction action) -> std::string {
                switch (action) {
                    case ForeignKeyAction::cascade:
                        return ".cascade()";
                    case ForeignKeyAction::restrict_:
                        return ".restrict_()";
                    case ForeignKeyAction::setNull:
                        return ".set_null()";
                    case ForeignKeyAction::setDefault:
                        return ".set_default()";
                    case ForeignKeyAction::noAction:
                        return ".no_action()";
                    case ForeignKeyAction::none:
                        return "";
                }
                return "";
            };
            if (foreignKey.onDelete != ForeignKeyAction::none) {
                constraint += ".on_delete" + actionString(foreignKey.onDelete);
            }
            if (foreignKey.onUpdate != ForeignKeyAction::none) {
                constraint += ".on_update" + actionString(foreignKey.onUpdate);
            }
            tableConstraints.push_back(std::move(constraint));
            if (foreignKey.deferrability != Deferrability::none) {
                std::string description =
                    foreignKey.deferrability == Deferrability::deferrable ? "DEFERRABLE" : "NOT DEFERRABLE";
                if (foreignKey.initially == InitialConstraintMode::deferred)
                    description += " INITIALLY DEFERRED";
                else if (foreignKey.initially == InitialConstraintMode::immediate)
                    description += " INITIALLY IMMEDIATE";
                warnings.push_back(description + " on foreign key for column '" + column.name +
                                   "' is not supported in sqlite_orm — ignored in codegen");
            }
        }
        for (const auto& tableForeignKey: createTable.foreignKeys) {
            const auto cppName = toCppIdentifier(tableForeignKey.column);
            if (this->context.isUngeneratableTable(tableForeignKey.references.table)) {
                warnings.push_back("table-level foreign key on column '" +
                                   stripIdentifierQuotes(tableForeignKey.column) + "' references " +
                                   stripIdentifierQuotes(tableForeignKey.references.table) +
                                   ", which is not generated, so the generated table has no foreign_key()");
                continue;
            }
            const auto referencedStructName = toStructName(tableForeignKey.references.table);
            std::string referencedColumnName;
            if (!tableForeignKey.references.column.empty()) {
                referencedColumnName = toCppIdentifier(tableForeignKey.references.column);
            } else {
                referencedColumnName = findPrimaryKeyColumnCpp(tableForeignKey.references.table);
                if (referencedColumnName.empty()) {
                    referencedColumnName = cppName;
                }
            }
            std::string constraint = "foreign_key(&" + structName + "::" + cppName + ").references(&" +
                                     referencedStructName + "::" + referencedColumnName + ")";
            const auto actionString = [](ForeignKeyAction action) -> std::string {
                switch (action) {
                    case ForeignKeyAction::cascade:
                        return ".cascade()";
                    case ForeignKeyAction::restrict_:
                        return ".restrict_()";
                    case ForeignKeyAction::setNull:
                        return ".set_null()";
                    case ForeignKeyAction::setDefault:
                        return ".set_default()";
                    case ForeignKeyAction::noAction:
                        return ".no_action()";
                    case ForeignKeyAction::none:
                        return "";
                }
                return "";
            };
            if (tableForeignKey.references.onDelete != ForeignKeyAction::none) {
                constraint += ".on_delete" + actionString(tableForeignKey.references.onDelete);
            }
            if (tableForeignKey.references.onUpdate != ForeignKeyAction::none) {
                constraint += ".on_update" + actionString(tableForeignKey.references.onUpdate);
            }
            tableConstraints.push_back(std::move(constraint));
            if (tableForeignKey.references.deferrability != Deferrability::none) {
                std::string description = tableForeignKey.references.deferrability == Deferrability::deferrable
                                              ? "DEFERRABLE"
                                              : "NOT DEFERRABLE";
                if (tableForeignKey.references.initially == InitialConstraintMode::deferred)
                    description += " INITIALLY DEFERRED";
                else if (tableForeignKey.references.initially == InitialConstraintMode::immediate)
                    description += " INITIALLY IMMEDIATE";
                warnings.push_back(description + " on table-level foreign key for column '" + tableForeignKey.column +
                                   "' is not supported in sqlite_orm — ignored in codegen");
            }
        }
        for (const auto& tablePrimaryKey: createTable.primaryKeys) {
            std::string constraint = "primary_key(";
            for (size_t columnIndex = 0; columnIndex < tablePrimaryKey.columns.size(); ++columnIndex) {
                if (columnIndex > 0) {
                    constraint += ", ";
                }
                constraint += "&" + structName + "::" + toCppIdentifier(tablePrimaryKey.columns.at(columnIndex));
            }
            constraint += ")";
            tableConstraints.push_back(std::move(constraint));
        }
        for (const auto& tableUnique: createTable.uniques) {
            std::string constraint = "unique(";
            for (size_t columnIndex = 0; columnIndex < tableUnique.columns.size(); ++columnIndex) {
                if (columnIndex > 0) {
                    constraint += ", ";
                }
                constraint += "&" + structName + "::" + toCppIdentifier(tableUnique.columns.at(columnIndex));
            }
            constraint += ")";
            tableConstraints.push_back(std::move(constraint));
        }
        for (const auto& tableCheck: createTable.checks) {
            if (tableCheck.expression) {
                const auto checkCode = clauseExpressionCode(*tableCheck.expression, true);
                if (!this->context.storedHexLiteralsTooBig.empty()) {
                    for (const std::string& literal: this->context.storedHexLiteralsTooBig) {
                        warnings.push_back("table CHECK uses " + literal +
                                           ", too big for a signed 64-bit integer: SQLite stores it but refuses "
                                           "every use of the constraint, and C++ has no literal for it, so the "
                                           "generated table has no check()");
                    }
                    continue;
                }
                tableConstraints.push_back("check(" + checkCode + ")");
            }
        }
        for (const std::string& constraint: tableConstraints) {
            makeExpression += ",\n        " + constraint;
        }
        makeExpression += ")";
        if (createTable.withoutRowid) {
            makeExpression += ".without_rowid()";
        }
        if (createTable.strict) {
            warnings.push_back("STRICT is not yet supported in sqlite_orm and was ignored for table " +
                               stripIdentifierQuotes(createTable.tableName) + " (converted as a regular table)");
        }

        if (!tableIsGeneratable) {
            // Nothing sqlite_orm can map this table to, so every statement naming it is left out
            // by whoever assembles the batch; the mark is what tells them which name that is.
            this->context.markUngeneratableTable(createTable.tableName);
            CreateTableParts parts;
            parts.warnings = std::move(warnings);
            return parts;
        }

        CreateTableParts parts;
        // Below C++26 the reflected form does not compile, so it is neither offered nor chosen and
        // the output is the classical one, unchanged.
        if (policyTargetCppStandard(this->context.codeGenPolicy) >= 26) {
            std::string reflectedStructDeclaration =
                "struct [[= " + identifierToCppStringLiteral(rawTableName) + "_orm_name]] " + structName + " {\n";
            for (size_t columnIndex = 0; columnIndex < memberDeclarations.size(); ++columnIndex) {
                reflectedStructDeclaration +=
                    "    " + memberAnnotations.at(columnIndex) + memberDeclarations.at(columnIndex);
            }
            reflectedStructDeclaration += "};\n";

            std::string reflectedMakeExpression = "make_table<" + structName + ">(";
            for (size_t constraintIndex = 0; constraintIndex < tableConstraints.size(); ++constraintIndex) {
                reflectedMakeExpression +=
                    (constraintIndex == 0 ? "\n        " : ",\n        ") + tableConstraints.at(constraintIndex);
            }
            reflectedMakeExpression += ")";
            if (createTable.withoutRowid) {
                reflectedMakeExpression += ".without_rowid()";
            }

            const std::string classicalCode = structDeclaration + "\n" + makeExpression;
            const std::string reflectedCode = reflectedStructDeclaration + "\n" + reflectedMakeExpression;
            // The reflected form is the default of the two once the target allows it, but a
            // consumer targeting C++26 on a compiler that has no reflection yet has to be able to
            // ask for the classical one back — no released compiler implements P2996 today.
            const bool reflectionOffered = reflectionBlocker.empty();
            const bool reflected =
                reflectionOffered && !policyEquals(this->context.codeGenPolicy, "table_mapping_style", "make_table");

            Option classicalOption{"make_table",
                                   classicalCode,
                                   "make_table(\"name\", make_column(…)) over a plain struct (wider compiler "
                                   "support)"};
            if (!reflectionOffered) {
                classicalOption.comments.push_back("the C++26 reflection alternative is not offered for this table: " +
                                                   reflectionBlocker);
            }
            std::vector<Option> options{std::move(classicalOption)};
            if (reflectionOffered) {
                Option reflectedOption{"reflection",
                                       reflectedCode,
                                       "C++26 reflection: annotated struct + make_table<T>()"};
                reflectedOption.minCppStandard = 26;
                reflectedOption.comments.push_back(kCommentTableReflection);
                options.push_back(std::move(reflectedOption));
            }
            parts.decisionPoints.push_back(DecisionPoint{this->context.nextDecisionPointId++,
                                                         "table_mapping_style",
                                                         reflected ? "reflection" : "make_table",
                                                         reflected ? reflectedCode : classicalCode,
                                                         std::move(options)});
            if (reflected) {
                structDeclaration = std::move(reflectedStructDeclaration);
                makeExpression = std::move(reflectedMakeExpression);
                parts.comments.push_back(kCommentTableReflection);
            }
        }

        parts.structDeclaration = std::move(structDeclaration);
        parts.makeTableExpression = std::move(makeExpression);
        parts.warnings = std::move(warnings);
        return parts;
    }

}  // namespace sqlite2orm
