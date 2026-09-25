#include "codegen_dml.h"
#include "codegen_context.h"
#include "codegen_utils.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

#include <cctype>
#include <string_view>

namespace sqlite2orm {

    namespace {

        /**
         *  The C++ field types that hold whole numbers only, so a value SQLite keeps a REAL reaches
         *  the database converted. `bool`, which a BOOLEAN column maps to, holds no whole number
         *  besides 0 and 1 and belongs here all the more; `boolFieldCarriesValue` rules on the
         *  range of that field, this predicate on the storage class alone.
         */
        bool isWholeNumberFieldType(std::string_view cppType) {
            return cppType == "int64_t" || cppType == "int" || cppType == "bool";
        }

        /**
         *  True when the object form of an insert hands `column` exactly the value SQLite gives
         *  `value`. The form sends every value through a struct field, which holds one storage
         *  class: a text or blob literal does not even initialize a numeric field — `CREATE TABLE
         *  ch(x); INSERT INTO ch VALUES (1)` generated `Ch{1}` for a `std::vector<char>` field,
         *  which does not compile — and an expression, whose value only SQLite knows, initializes
         *  no field at all. A field of the right storage class still has a range: a braced
         *  initializer refuses a whole number past the int64 range, a `double` one refuses every
         *  integer constant it would round, and a `bool` one holds no number besides 0 and 1.
         */
        bool objectFormCarriesValue(const SourceTableColumn& column, const AstNode& value) {
            const ValueStorageClass fieldClass = fieldTypeStorageClass(column.cppType);
            if (fieldClass == ValueStorageClass::unknown) {
                // A field type this rule knows nothing about: leave the statement the way it was.
                return true;
            }
            const ValueStorageClass storageClass = valueStorageClass(value);
            if (storageClass == ValueStorageClass::null) {
                // `std::nullopt` initializes an optional field; a NOT NULL column has a bare one.
                return column.nullable;
            }
            if (storageClass != fieldClass) {
                return false;
            }
            if (column.cppType == "bool") {
                return boolFieldCarriesValue(value);
            }
            if (isWholeNumberFieldType(column.cppType)) {
                return integerFieldCarriesValue(value);
            }
            return column.cppType != "double" || doubleFieldCarriesValue(value);
        }

    }  // namespace

    DmlCodeGenerator::DmlCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context) :
        coordinator(coordinator), context(context) {}

    std::vector<std::string>
    DmlCodeGenerator::columnListForcedByFieldTypes(const InsertNode& insertNode,
                                                   std::vector<CodegenWarning>& warnings) const {
        const auto tableIterator =
            this->context.sourceTableColumnsByNormalizedName.find(normalizeSqlIdentifier(insertNode.tableName));
        if (tableIterator == this->context.sourceTableColumnsByNormalizedName.end()) {
            return {};
        }
        // A generated column is computed by SQLite, so a VALUES row never holds one and the column
        // list of such an insert leaves it out.
        std::vector<const SourceTableColumn*> columns;
        for (const SourceTableColumn& column: tableIterator->second) {
            if (!column.generated) {
                columns.push_back(&column);
            }
        }

        std::vector<CodegenWarning> pastRangeWarnings;
        bool columnListForced = false;
        for (const std::vector<AstNodePointer>& row: insertNode.valueRows) {
            if (row.size() != columns.size()) {
                // A row that does not line up with the columns is a statement SQLite refuses
                // anyway, and says nothing about which field a value reaches.
                return {};
            }
            for (size_t columnIndex = 0; columnIndex < row.size(); ++columnIndex) {
                const SourceTableColumn& column = *columns[columnIndex];
                const AstNode& value = *row[columnIndex];
                if (objectFormCarriesValue(column, value)) {
                    continue;
                }
                columnListForced = true;
                if (isWholeNumberFieldType(column.cppType) && isIntegerLiteralPastIntegerFieldRange(value)) {
                    pastRangeWarnings.push_back(numericLiteralWarning(
                        "INSERT into column '" + column.sqlName + "' of table '" + insertNode.tableName + "' uses " +
                            numericLiteralSqlText(value) +
                            ", past the signed 64-bit integer range: SQLite types a value before it applies "
                            "the column affinity and keeps such a one a REAL, and the " +
                            column.cppType +
                            " field cannot hold it, so the row is generated through columns()/values(), which "
                            "writes the value SQLite stores, rather than as a struct, which would write a "
                            "different one",
                        value));
                }
            }
        }
        if (!columnListForced) {
            return {};
        }

        std::vector<std::string> columnNames;
        columnNames.reserve(columns.size());
        for (const SourceTableColumn* column: columns) {
            columnNames.push_back(column->sqlName);
        }
        warnings.insert(warnings.end(),
                        std::make_move_iterator(pastRangeWarnings.begin()),
                        std::make_move_iterator(pastRangeWarnings.end()));
        return columnNames;
    }

    CodeGenResult DmlCodeGenerator::generateInsert(const InsertNode& insertNode) {
        std::vector<CodegenWarning> warnings;
        if (insertNode.schemaName) {
            warnings.push_back("schema-qualified table in INSERT is not represented in sqlite_orm mapping");
        }
        std::string tableStruct = this->context.structNameForTable(insertNode.tableName);
        std::string savedStruct = this->context.structName;
        this->context.structName = tableStruct;

        std::string verb = insertNode.replaceInto ? "replace" : "insert";
        std::string orPrefix = insertNode.replaceInto ? std::string() : dmlInsertOrPrefix(insertNode.orConflict);

        std::vector<std::string> valueColumnNames = insertNode.columnNames;
        if (insertNode.dataKind == InsertDataKind::values && valueColumnNames.empty()) {
            valueColumnNames = this->columnListForcedByFieldTypes(insertNode, warnings);
        }

        std::vector<DecisionPoint> dps;
        std::string middle;
        if (insertNode.dataKind == InsertDataKind::defaultValues) {
            middle = "default_values()";
        } else if (insertNode.dataKind == InsertDataKind::values && valueColumnNames.empty()) {
            std::string code;
            for (size_t valueRowIndex = 0; valueRowIndex < insertNode.valueRows.size(); ++valueRowIndex) {
                if (valueRowIndex > 0) {
                    code += "\n";
                }
                code += "storage." + verb + "(" + tableStruct + "{";
                const auto& row = insertNode.valueRows[valueRowIndex];
                for (size_t columnIndex = 0; columnIndex < row.size(); ++columnIndex) {
                    if (columnIndex > 0) {
                        code += ", ";
                    }
                    if (dynamic_cast<const NullLiteralNode*>(row[columnIndex].get())) {
                        code += "std::nullopt";
                    } else {
                        auto cell = this->coordinator.generateNode(*row[columnIndex]);
                        dps.insert(dps.end(),
                                   std::make_move_iterator(cell.decisionPoints.begin()),
                                   std::make_move_iterator(cell.decisionPoints.end()));
                        warnings.insert(warnings.end(),
                                        std::make_move_iterator(cell.warnings.begin()),
                                        std::make_move_iterator(cell.warnings.end()));
                        code += cell.code;
                    }
                }
                code += "});";
            }
            this->context.structName = savedStruct;
            return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
        } else if (insertNode.dataKind == InsertDataKind::values) {
            std::string cols = "columns(";
            for (size_t columnIndex = 0; columnIndex < valueColumnNames.size(); ++columnIndex) {
                if (columnIndex > 0) {
                    cols += ", ";
                }
                cols += "&" + tableStruct + "::" + toCppIdentifier(valueColumnNames[columnIndex]);
            }
            cols += ")";
            std::string vals = "values(";
            for (size_t rowIndex = 0; rowIndex < insertNode.valueRows.size(); ++rowIndex) {
                if (rowIndex > 0) {
                    vals += ", ";
                }
                vals += "std::make_tuple(";
                const auto& row = insertNode.valueRows[rowIndex];
                for (size_t columnIndex = 0; columnIndex < row.size(); ++columnIndex) {
                    if (columnIndex > 0) {
                        vals += ", ";
                    }
                    auto cell = this->coordinator.generateNode(*row[columnIndex]);
                    dps.insert(dps.end(),
                               std::make_move_iterator(cell.decisionPoints.begin()),
                               std::make_move_iterator(cell.decisionPoints.end()));
                    warnings.insert(warnings.end(),
                                    std::make_move_iterator(cell.warnings.begin()),
                                    std::make_move_iterator(cell.warnings.end()));
                    vals += cell.code;
                }
                vals += ")";
            }
            vals += ")";
            middle = cols + ", " + vals;
        } else {
            bool compoundSource = false;
            auto sub =
                selectLikeSubqueryForm(this->coordinator, this->context, *insertNode.selectStatement, compoundSource);
            warnings.insert(warnings.end(),
                            std::make_move_iterator(sub.warnings.begin()),
                            std::make_move_iterator(sub.warnings.end()));
            dps.insert(dps.end(),
                       std::make_move_iterator(sub.decisionPoints.begin()),
                       std::make_move_iterator(sub.decisionPoints.end()));
            if (sub.code.empty() || compoundSource) {
                this->context.structName = savedStruct;
                CodeGenResult carried;
                carried.decisionPoints = std::move(dps);
                carried.warnings = std::move(warnings);
                // sqlite_orm's raw insert takes a `select(...)` and nothing else — a compound form
                // trips its `static_assert(… "Raw insert has invalid arguments")` — so the statement
                // stands as a placeholder of its own rather than as a header that does not build.
                return unsupportedStatementPlaceholder(
                    this->context,
                    "INSERT ... SELECT: inner SELECT not mapped to sqlite_orm",
                    compoundSource
                        ? "the compound SELECT an INSERT reads from is not mapped to sqlite_orm codegen: a raw "
                          "insert takes a select(...), and its union_()/intersect()/except() form is not one"
                        : "the SELECT an INSERT reads from is not mapped to sqlite_orm codegen",
                    *insertNode.selectStatement,
                    std::move(carried));
            }
            if (!insertNode.columnNames.empty()) {
                std::string cols = "columns(";
                for (size_t columnIndex = 0; columnIndex < insertNode.columnNames.size(); ++columnIndex) {
                    if (columnIndex > 0) {
                        cols += ", ";
                    }
                    cols += "&" + tableStruct + "::" + toCppIdentifier(insertNode.columnNames[columnIndex]);
                }
                cols += ")";
                middle = cols + ", " + sub.code;
            } else {
                middle = sub.code;
            }
        }

        std::string upsertSuffix;
        if (insertNode.hasUpsertClause) {
            if (insertNode.upsertConflictWhere) {
                warnings.push_back(
                    "ON CONFLICT target WHERE is not represented in sqlite_orm on_conflict(); generated code "
                    "omits that predicate");
            }
            std::string onTarget;
            if (insertNode.upsertConflictColumns.empty()) {
                onTarget = "on_conflict()";
            } else if (insertNode.upsertConflictColumns.size() == 1u) {
                onTarget =
                    "on_conflict(&" + tableStruct + "::" + toCppIdentifier(insertNode.upsertConflictColumns[0]) + ")";
            } else {
                onTarget = "on_conflict(columns(";
                for (size_t upsertIndex = 0; upsertIndex < insertNode.upsertConflictColumns.size(); ++upsertIndex) {
                    if (upsertIndex > 0) {
                        onTarget += ", ";
                    }
                    onTarget +=
                        "&" + tableStruct + "::" + toCppIdentifier(insertNode.upsertConflictColumns[upsertIndex]);
                }
                onTarget += "))";
            }
            if (insertNode.upsertAction == InsertUpsertAction::doNothing) {
                upsertSuffix = ", " + onTarget + ".do_nothing()";
            } else if (insertNode.upsertAction == InsertUpsertAction::doUpdate) {
                std::string setArgs;
                for (size_t assignmentIndex = 0; assignmentIndex < insertNode.upsertUpdateAssignments.size();
                     ++assignmentIndex) {
                    if (assignmentIndex > 0) {
                        setArgs += ", ";
                    }
                    auto valueResult =
                        this->coordinator.generateNode(*insertNode.upsertUpdateAssignments[assignmentIndex].value);
                    dps.insert(dps.end(),
                               std::make_move_iterator(valueResult.decisionPoints.begin()),
                               std::make_move_iterator(valueResult.decisionPoints.end()));
                    warnings.insert(warnings.end(),
                                    std::make_move_iterator(valueResult.warnings.begin()),
                                    std::make_move_iterator(valueResult.warnings.end()));
                    std::string cppCol =
                        toCppIdentifier(insertNode.upsertUpdateAssignments[assignmentIndex].columnName);
                    setArgs += "c(&" + tableStruct + "::" + cppCol + ") = " + valueResult.code;
                }
                upsertSuffix = ", " + onTarget + ".do_update(set(" + setArgs + ")";
                if (insertNode.upsertUpdateWhere) {
                    const ParenthesizedConditionScope conditionScope{this->context, *insertNode.upsertUpdateWhere};
                    auto whereResult = this->coordinator.generateNode(*insertNode.upsertUpdateWhere);
                    dps.insert(dps.end(),
                               std::make_move_iterator(whereResult.decisionPoints.begin()),
                               std::make_move_iterator(whereResult.decisionPoints.end()));
                    warnings.insert(warnings.end(),
                                    std::make_move_iterator(whereResult.warnings.begin()),
                                    std::make_move_iterator(whereResult.warnings.end()));
                    upsertSuffix += ", where(" + whereResult.code + ")";
                }
                upsertSuffix += ")";
            }
        }

        this->context.structName = savedStruct;
        std::string code =
            "storage." + verb + "(" + orPrefix + "into<" + tableStruct + ">(), " + middle + upsertSuffix + ");";
        if (insertNode.replaceInto) {
            std::string insertOrReplace =
                "storage.insert(or_replace(), into<" + tableStruct + ">(), " + middle + upsertSuffix + ");";
            // options lists every variant (the chosen one included).
            dps.push_back(DecisionPoint{this->context.nextDecisionPointId++,
                                        "replace_style",
                                        "replace_call",
                                        code,
                                        {Option{"replace_call", code, "storage.replace(into<T>(), ...)"},
                                         Option{"insert_or_replace",
                                                insertOrReplace,
                                                "same semantics via raw insert(or_replace(), into<T>(), ...)"}}});
        }
        return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
    }

    CodeGenResult DmlCodeGenerator::generateUpdate(const UpdateNode& updateNode) {
        std::vector<CodegenWarning> warnings;
        if (updateNode.schemaName) {
            warnings.push_back("schema-qualified table in UPDATE is not represented in sqlite_orm mapping");
        }
        if (updateNode.orConflict != ConflictClause::none) {
            warnings.push_back(
                "UPDATE OR modifier is not represented in sqlite_orm; generated code uses update_all(...) without OR");
        }
        if (!updateNode.fromClause.empty()) {
            warnings.push_back("UPDATE ... FROM ... is not supported in sqlite_orm — "
                               "FROM clause is ignored in codegen");
        }
        std::string tableStruct = this->context.structNameForTable(updateNode.tableName);
        std::string savedStruct = this->context.structName;
        this->context.structName = tableStruct;
        std::vector<DecisionPoint> dps;
        std::string setArgs;
        for (size_t assignmentIndex = 0; assignmentIndex < updateNode.assignments.size(); ++assignmentIndex) {
            if (assignmentIndex > 0) {
                setArgs += ", ";
            }
            auto valueResult = this->coordinator.generateNode(*updateNode.assignments[assignmentIndex].value);
            dps.insert(dps.end(),
                       std::make_move_iterator(valueResult.decisionPoints.begin()),
                       std::make_move_iterator(valueResult.decisionPoints.end()));
            warnings.insert(warnings.end(),
                            std::make_move_iterator(valueResult.warnings.begin()),
                            std::make_move_iterator(valueResult.warnings.end()));
            std::string cppCol = toCppIdentifier(updateNode.assignments[assignmentIndex].columnName);
            setArgs += "c(&" + tableStruct + "::" + cppCol + ") = " + valueResult.code;
        }
        std::string code = "storage.update_all(set(" + setArgs + ")";
        if (updateNode.whereClause) {
            const ParenthesizedConditionScope conditionScope{this->context, *updateNode.whereClause};
            auto whereResult = this->coordinator.generateNode(*updateNode.whereClause);
            dps.insert(dps.end(),
                       std::make_move_iterator(whereResult.decisionPoints.begin()),
                       std::make_move_iterator(whereResult.decisionPoints.end()));
            warnings.insert(warnings.end(),
                            std::make_move_iterator(whereResult.warnings.begin()),
                            std::make_move_iterator(whereResult.warnings.end()));
            code += ", where(" + whereResult.code + ")";
        }
        code += ");";
        this->context.structName = savedStruct;
        return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
    }

    CodeGenResult DmlCodeGenerator::generateDelete(const DeleteNode& deleteNode) {
        std::vector<CodegenWarning> warnings;
        if (deleteNode.schemaName) {
            warnings.push_back("schema-qualified table in DELETE is not represented in sqlite_orm mapping");
        }
        std::string tableStruct = this->context.structNameForTable(deleteNode.tableName);
        std::string savedStruct = this->context.structName;
        this->context.structName = tableStruct;
        std::vector<DecisionPoint> dps;
        std::string code = "storage.remove_all<" + tableStruct + ">()";
        if (deleteNode.whereClause) {
            const ParenthesizedConditionScope conditionScope{this->context, *deleteNode.whereClause};
            auto whereResult = this->coordinator.generateNode(*deleteNode.whereClause);
            dps.insert(dps.end(),
                       std::make_move_iterator(whereResult.decisionPoints.begin()),
                       std::make_move_iterator(whereResult.decisionPoints.end()));
            warnings.insert(warnings.end(),
                            std::make_move_iterator(whereResult.warnings.begin()),
                            std::make_move_iterator(whereResult.warnings.end()));
            code = "storage.remove_all<" + tableStruct + ">(where(" + whereResult.code + "))";
        }
        code += ";";
        this->context.structName = savedStruct;
        return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
    }

    CodeGenResult DmlCodeGenerator::generateTriggerStep(const AstNode& statement,
                                                        const std::string& subjectTableStruct) {
        struct TriggerStepScope {
            CodeGeneratorContext* ctx;
            std::string savedStruct;
            std::map<std::string, std::string> savedAliases;
            std::map<std::string, std::string> savedColumnAliases;
            std::map<std::string, std::string> savedColumnAliasCpp20Vars;

            TriggerStepScope(CodeGeneratorContext* ctx, const std::string& subject) :
                ctx(ctx), savedStruct(ctx->structName), savedAliases(ctx->fromTableAliasToStructName),
                savedColumnAliases(ctx->activeSelectColumnAliases),
                savedColumnAliasCpp20Vars(ctx->activeSelectColumnAliasCpp20Vars) {
                ctx->structName = subject;
                ctx->triggerSubjectStructName = subject;
                ctx->fromTableAliasToStructName.clear();
                ctx->activeSelectColumnAliases.clear();
                ctx->activeSelectColumnAliasCpp20Vars.clear();
            }
            ~TriggerStepScope() {
                ctx->structName = std::move(savedStruct);
                ctx->triggerSubjectStructName.reset();
                ctx->fromTableAliasToStructName = std::move(savedAliases);
                ctx->activeSelectColumnAliases = std::move(savedColumnAliases);
                ctx->activeSelectColumnAliasCpp20Vars = std::move(savedColumnAliasCpp20Vars);
            }
        } scope{&this->context, subjectTableStruct};

        // `make_trigger(...).begin(step, step)` joins the steps with commas, so a step answering
        // with no code at all would leave a dangling comma behind — and, as the only step of a
        // trigger, would leave `begin()` installing a trigger that does less than the schema it was
        // read from, with nothing in the generated code saying so. A step is a placeholder site
        // like every other one: the placeholder stands where the step would have, and its warning
        // names the SQL to underline.
        //
        // A step that generated a placeholder of its own is unmappable too, whatever code came
        // with it: the placeholders standing for a whole statement read as a comment line only
        // where a statement stands, and a trigger body is not such a place — `begin(/* INSERT ...
        // SELECT: ... */)` is not C++ any more than an empty step is.
        const size_t stepPlaceholderMark = this->context.placeholderMark();
        auto stepOrPlaceholder = [&statement, this, stepPlaceholderMark](CodeGenResult step) {
            if (!step.code.empty() && !this->context.placeheldSince(stepPlaceholderMark)) {
                return step;
            }
            CodeGenResult carried;
            carried.decisionPoints = std::move(step.decisionPoints);
            carried.warnings = std::move(step.warnings);
            return unsupportedPlaceholder(this->context,
                                          "trigger step not mapped to sqlite_orm",
                                          "a statement in the trigger body is not mapped to sqlite_orm codegen",
                                          statement,
                                          std::move(carried));
        };

        if (auto* selectNode = dynamic_cast<const SelectNode*>(&statement)) {
            return stepOrPlaceholder(this->coordinator.tryCodegenSqliteSelectSubexpression(*selectNode));
        }
        if (auto* compoundSelectNode = dynamic_cast<const CompoundSelectNode*>(&statement)) {
            return stepOrPlaceholder(this->coordinator.tryCodegenCompoundSelectSubexpression(*compoundSelectNode));
        }
        auto outer = this->coordinator.generateNode(statement);
        std::string code = outer.code;
        static constexpr std::string_view kStoragePrefix = "storage.";
        if (code.size() >= kStoragePrefix.size() && code.compare(0, kStoragePrefix.size(), kStoragePrefix) == 0) {
            code.erase(0, kStoragePrefix.size());
        }
        while (!code.empty() && std::isspace(static_cast<unsigned char>(code.back()))) {
            code.pop_back();
        }
        if (!code.empty() && code.back() == ';') {
            code.pop_back();
        }
        while (!code.empty() && std::isspace(static_cast<unsigned char>(code.back()))) {
            code.pop_back();
        }
        return stepOrPlaceholder(
            CodeGenResult{std::move(code), std::move(outer.decisionPoints), std::move(outer.warnings)});
    }

}  // namespace sqlite2orm
