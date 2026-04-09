#include "codegen_ddl.h"
#include "codegen_context.h"
#include "codegen_utils.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    DdlCodeGenerator::DdlCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context)
        : coordinator(coordinator), context(context) {}

    CodeGenResult DdlCodeGenerator::generateCreateTable(const CreateTableNode& createTable) {
        const CreateTableParts parts = this->createTableParts(createTable);
        std::string code = parts.structDeclaration + "\nauto storage = make_storage(\"\",\n    " +
                           parts.makeTableExpression + ");";
        return CodeGenResult{std::move(code), {}, std::vector<std::string>(parts.warnings)};
    }

    CodeGenResult DdlCodeGenerator::generateCreateTrigger(const CreateTriggerNode& createTrigger) {
        std::vector<std::string> warnings;
        if(createTrigger.ifNotExists) {
            warnings.push_back(
                "CREATE TRIGGER IF NOT EXISTS is not represented in sqlite_orm make_trigger(); generated code "
                "omits IF NOT EXISTS");
        }
        if(createTrigger.temporary) {
            warnings.push_back(
                "TEMP/TEMPORARY TRIGGER is not represented in sqlite_orm make_trigger(); generated code does not "
                "mark the trigger as temporary");
        }
        if(createTrigger.triggerSchemaName) {
            warnings.push_back(
                "schema-qualified trigger name is not represented in sqlite_orm; generated code uses unqualified "
                "trigger name only");
        }
        if(createTrigger.tableSchemaName) {
            warnings.push_back(
                "schema-qualified ON table in TRIGGER is not represented in sqlite_orm mapping");
        }

        std::string subject = toStructName(createTrigger.tableName);
        std::string savedStruct = this->context.structName;
        this->context.structName = subject;

        std::string timingHead;
        switch(createTrigger.timing) {
        case TriggerTiming::before: timingHead = "before()"; break;
        case TriggerTiming::after: timingHead = "after()"; break;
        case TriggerTiming::insteadOf: timingHead = "instead_of()"; break;
        }

        std::string typeChain = timingHead;
        switch(createTrigger.eventKind) {
        case TriggerEventKind::delete_: typeChain += ".delete_()"; break;
        case TriggerEventKind::insert_: typeChain += ".insert()"; break;
        case TriggerEventKind::update_: typeChain += ".update()"; break;
        case TriggerEventKind::updateOf:
            typeChain += ".update_of(";
            for(size_t columnIndex = 0; columnIndex < createTrigger.updateOfColumns.size(); ++columnIndex) {
                if(columnIndex > 0) {
                    typeChain += ", ";
                }
                typeChain += "&" + subject + "::" + toCppIdentifier(createTrigger.updateOfColumns[columnIndex]);
            }
            typeChain += ")";
            break;
        }

        std::string base = typeChain + ".on<" + subject + ">()";
        if(createTrigger.forEachRow) {
            base += ".for_each_row()";
        }
        std::vector<DecisionPoint> decisionPoints;
        if(createTrigger.whenClause) {
            auto whenResult = this->coordinator.generateNode(*createTrigger.whenClause);
            decisionPoints.insert(decisionPoints.end(), std::make_move_iterator(whenResult.decisionPoints.begin()),
                       std::make_move_iterator(whenResult.decisionPoints.end()));
            warnings.insert(warnings.end(), std::make_move_iterator(whenResult.warnings.begin()),
                           std::make_move_iterator(whenResult.warnings.end()));
            base += ".when(" + whenResult.code + ")";
        }

        std::string stepsJoined;
        for(const auto& step : createTrigger.bodyStatements) {
            auto stepResult = this->coordinator.generateTriggerStep(*step, subject);
            decisionPoints.insert(decisionPoints.end(), std::make_move_iterator(stepResult.decisionPoints.begin()),
                       std::make_move_iterator(stepResult.decisionPoints.end()));
            warnings.insert(warnings.end(), std::make_move_iterator(stepResult.warnings.begin()),
                           std::make_move_iterator(stepResult.warnings.end()));
            if(!stepsJoined.empty()) {
                stepsJoined += ", ";
            }
            stepsJoined += stepResult.code;
        }

        this->context.structName = savedStruct;

        std::string triggerLiteral = identifierToCppStringLiteral(createTrigger.triggerName);
        std::string code = "make_trigger(" + triggerLiteral + ", " + base + ".begin(" + stepsJoined + "));";
        return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
    }

    CodeGenResult DdlCodeGenerator::generateCreateIndex(const CreateIndexNode& createIndex) {
        std::vector<std::string> warnings;
        if(createIndex.indexSchemaName) {
            warnings.push_back(
                "schema-qualified INDEX name is not represented in sqlite_orm; generated code uses unqualified "
                "index name");
        }
        if(createIndex.tableSchemaName) {
            warnings.push_back(
                "schema-qualified table in CREATE INDEX is not represented in sqlite_orm mapping");
        }
        if(!createIndex.ifNotExists) {
            warnings.push_back(
                "sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs "
                "from serialized output");
        }

        std::string tableStruct = toStructName(createIndex.tableName);
        std::string savedStruct = this->context.structName;
        this->context.structName = tableStruct;

        std::string functionName = createIndex.unique ? "make_unique_index" : "make_index";
        std::string indexLiteral = identifierToCppStringLiteral(createIndex.indexName);
        std::vector<DecisionPoint> decisionPoints;
        std::string columnParts;
        for(const auto& indexedColumn : createIndex.indexedColumns) {
            if(!columnParts.empty()) {
                columnParts += ", ";
            }
            auto expressionResult = this->coordinator.generateNode(*indexedColumn.expression);
            decisionPoints.insert(decisionPoints.end(), std::make_move_iterator(expressionResult.decisionPoints.begin()),
                       std::make_move_iterator(expressionResult.decisionPoints.end()));
            warnings.insert(warnings.end(), std::make_move_iterator(expressionResult.warnings.begin()),
                           std::make_move_iterator(expressionResult.warnings.end()));
            std::string part = "indexed_column(" + expressionResult.code + ")";
            if(!indexedColumn.collation.empty()) {
                std::string collationLower = toLowerAscii(indexedColumn.collation);
                if(collationLower == "nocase") {
                    part += ".collate(\"nocase\")";
                } else if(collationLower == "binary") {
                    part += ".collate(\"binary\")";
                } else if(collationLower == "rtrim") {
                    part += ".collate(\"rtrim\")";
                } else {
                    warnings.push_back("COLLATE " + indexedColumn.collation +
                                         " is not a built-in collation; generated .collate(...) uses literal "
                                         "name as in SQL");
                    part += ".collate(" + identifierToCppStringLiteral(indexedColumn.collation) + ")";
                }
            }
            if(indexedColumn.sortDirection == SortDirection::asc) {
                part += ".asc()";
            } else if(indexedColumn.sortDirection == SortDirection::desc) {
                part += ".desc()";
            }
            columnParts += part;
        }

        std::string code = functionName + "(" + indexLiteral + ", " + columnParts;
        if(createIndex.whereClause) {
            auto whereResult = this->coordinator.generateNode(*createIndex.whereClause);
            decisionPoints.insert(decisionPoints.end(), std::make_move_iterator(whereResult.decisionPoints.begin()),
                       std::make_move_iterator(whereResult.decisionPoints.end()));
            warnings.insert(warnings.end(), std::make_move_iterator(whereResult.warnings.begin()),
                           std::make_move_iterator(whereResult.warnings.end()));
            code += ", where(" + whereResult.code + ")";
        }
        code += ");";

        this->context.structName = savedStruct;
        return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
    }

    CodeGenResult DdlCodeGenerator::generateTransactionControl(const TransactionControlNode& node) {
        if(node.kind == TransactionControlNode::Kind::begin) {
            switch(node.beginMode) {
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
        if(node.kind == TransactionControlNode::Kind::commit) {
            return CodeGenResult{"storage.commit();", {}, {}};
        }
        if(node.rollbackToSavepoint) {
            return CodeGenResult{"/* ROLLBACK TO SAVEPOINT */",
                                 {},
                                 {"ROLLBACK TO SAVEPOINT is not supported in the sqlite_orm storage API"}};
        }
        return CodeGenResult{"storage.rollback();", {}, {}};
    }

    CodeGenResult DdlCodeGenerator::generateVacuum(const VacuumStatementNode& node) {
        if(node.schemaName) {
            return CodeGenResult{
                "storage.vacuum();",
                {},
                {"VACUUM with explicit schema is not modeled separately in sqlite_orm::storage_base::vacuum()"}};
        }
        return CodeGenResult{"storage.vacuum();", {}, {}};
    }

    CodeGenResult DdlCodeGenerator::generateDrop(const DropStatementNode& node) {
        std::vector<std::string> warnings;
        if(node.schemaName) {
            warnings.push_back(
                "schema-qualified name in DROP is not represented in sqlite_orm; generated call uses unqualified "
                "name only");
        }
        const std::string literal = identifierToCppStringLiteral(node.objectName);
        switch(node.objectKind) {
            case DropObjectKind::table:
                return CodeGenResult{
                    (node.ifExists ? "storage.drop_table_if_exists(" : "storage.drop_table(") + literal +
                        ");",
                    {},
                    std::move(warnings)};
            case DropObjectKind::index:
                return CodeGenResult{
                    (node.ifExists ? "storage.drop_index_if_exists(" : "storage.drop_index(") + literal +
                        ");",
                    {},
                    std::move(warnings)};
            case DropObjectKind::trigger:
                return CodeGenResult{(node.ifExists ? "storage.drop_trigger_if_exists("
                                                       : "storage.drop_trigger(") +
                                         literal + ");",
                                     {},
                                     std::move(warnings)};
            case DropObjectKind::view:
                warnings.push_back(
                    "DROP VIEW is not supported as a sqlite_orm storage method; sqlite_orm sync_schema() applies "
                    "to mapped tables/indexes/triggers, not views");
                return CodeGenResult{"/* DROP VIEW: not supported as storage.drop_* in sqlite_orm */",
                                     {},
                                     std::move(warnings)};
            default:
                return CodeGenResult{"/* DROP */", {}, std::move(warnings)};
        }
    }

    CodeGenResult DdlCodeGenerator::generateCreateVirtualTable(const CreateVirtualTableNode& node) {
        std::vector<std::string> warnings;
        std::vector<DecisionPoint> decisionPoints;

        if(node.tableSchemaName) {
            warnings.push_back(
                "schema-qualified VIRTUAL TABLE name is not represented in sqlite_orm; generated code uses "
                "unqualified table name only");
        }
        if(!node.ifNotExists) {
            warnings.push_back(
                "sqlite_orm serializes virtual tables as CREATE VIRTUAL TABLE IF NOT EXISTS; SQL without IF NOT "
                "EXISTS differs from serialized output");
        }
        if(node.temporary) {
            warnings.push_back(
                "TEMP/TEMPORARY VIRTUAL TABLE is not represented in sqlite_orm virtual table mapping; generated "
                "code does not mark the table as temporary");
        }

        std::string moduleLower = toLowerAscii(node.moduleName);
        std::string nameLiteral = identifierToCppStringLiteral(node.tableName);
        std::string structName = toStructName(node.tableName);

        auto allSimpleColumnRefs = [&]() -> bool {
            for(const auto& moduleArgument : node.moduleArguments) {
                if(!dynamic_cast<const ColumnRefNode*>(moduleArgument.get())) {
                    return false;
                }
            }
            return true;
        };

        if(moduleLower == "fts5") {
            if(node.moduleArguments.empty()) {
                warnings.push_back("FTS5 requires at least one column argument for sqlite_orm::using_fts5()");
                return CodeGenResult{"/* CREATE VIRTUAL TABLE: fts5 (no columns) */", std::move(decisionPoints),
                                     std::move(warnings)};
            }
            if(!allSimpleColumnRefs()) {
                warnings.push_back(
                    "FTS5 module arguments that are not plain column names cannot be mapped to "
                    "sqlite_orm::using_fts5()");
                return CodeGenResult{"/* CREATE VIRTUAL TABLE: fts5 (unmapped arguments) */", std::move(decisionPoints),
                                     std::move(warnings)};
            }
            std::string code = "struct " + structName + " {\n";
            for(const auto& moduleArgument : node.moduleArguments) {
                auto* columnRef = static_cast<const ColumnRefNode*>(moduleArgument.get());
                auto cppName = toCppIdentifier(columnRef->columnName);
                code += "    std::string " + cppName + ";\n";
            }
            code += "};\n\n";
            std::string savedStruct = this->context.structName;
            this->context.structName = structName;
            std::string columnParts;
            for(const auto& moduleArgument : node.moduleArguments) {
                auto* columnRef = static_cast<const ColumnRefNode*>(moduleArgument.get());
                CodeGenResult moduleArgumentCodegen = this->coordinator.generateNode(*moduleArgument);
                decisionPoints.insert(decisionPoints.end(),
                           std::make_move_iterator(moduleArgumentCodegen.decisionPoints.begin()),
                           std::make_move_iterator(moduleArgumentCodegen.decisionPoints.end()));
                warnings.insert(warnings.end(),
                                std::make_move_iterator(moduleArgumentCodegen.warnings.begin()),
                                std::make_move_iterator(moduleArgumentCodegen.warnings.end()));
                if(!columnParts.empty()) {
                    columnParts += ", ";
                }
                std::string rawColumn = stripIdentifierQuotes(columnRef->columnName);
                columnParts +=
                    "make_column(" + identifierToCppStringLiteral(rawColumn) + ", " + moduleArgumentCodegen.code + ")";
            }
            this->context.structName = savedStruct;
            code += "auto vtab = make_virtual_table<" + structName + ">(" + nameLiteral + ", using_fts5(" + columnParts +
                    "));\n";
            return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
        }

        if(moduleLower == "rtree" || moduleLower == "rtree_i32") {
            const size_t moduleArgumentsCount = node.moduleArguments.size();
            if(moduleArgumentsCount < 3 || moduleArgumentsCount > 11 || (moduleArgumentsCount % 2 == 0)) {
                warnings.push_back(
                    "RTREE virtual table for sqlite_orm needs 3, 5, 7, 9, or 11 simple column identifiers (id + "
                    "min/max pairs)");
                return CodeGenResult{"/* CREATE VIRTUAL TABLE: rtree (invalid column count) */", std::move(decisionPoints),
                                     std::move(warnings)};
            }
            if(!allSimpleColumnRefs()) {
                warnings.push_back(
                    "RTREE module arguments that are not plain column names cannot be mapped to sqlite_orm "
                    "using_rtree() / using_rtree_i32()");
                return CodeGenResult{"/* CREATE VIRTUAL TABLE: rtree (unmapped arguments) */", std::move(decisionPoints),
                                     std::move(warnings)};
            }
            const bool isInt32 = (moduleLower == "rtree_i32");
            std::string code = "struct " + structName + " {\n";
            for(size_t columnIndex = 0; columnIndex < moduleArgumentsCount; ++columnIndex) {
                auto* columnRef =
                    static_cast<const ColumnRefNode*>(node.moduleArguments[columnIndex].get());
                auto cppName = toCppIdentifier(columnRef->columnName);
                if(columnIndex == 0) {
                    code += "    int64_t " + cppName + " = 0;\n";
                } else if(isInt32) {
                    code += "    int32_t " + cppName + " = 0;\n";
                } else {
                    code += "    float " + cppName + " = 0.0;\n";
                }
            }
            code += "};\n\n";
            std::string savedStruct = this->context.structName;
            this->context.structName = structName;
            std::string columnParts;
            for(const auto& moduleArgument : node.moduleArguments) {
                auto* columnRef = static_cast<const ColumnRefNode*>(moduleArgument.get());
                CodeGenResult moduleArgumentCodegen = this->coordinator.generateNode(*moduleArgument);
                decisionPoints.insert(decisionPoints.end(),
                           std::make_move_iterator(moduleArgumentCodegen.decisionPoints.begin()),
                           std::make_move_iterator(moduleArgumentCodegen.decisionPoints.end()));
                warnings.insert(warnings.end(),
                                std::make_move_iterator(moduleArgumentCodegen.warnings.begin()),
                                std::make_move_iterator(moduleArgumentCodegen.warnings.end()));
                if(!columnParts.empty()) {
                    columnParts += ", ";
                }
                std::string rawColumn = stripIdentifierQuotes(columnRef->columnName);
                columnParts +=
                    "make_column(" + identifierToCppStringLiteral(rawColumn) + ", " + moduleArgumentCodegen.code + ")";
            }
            this->context.structName = savedStruct;
            const char* usingFunction = isInt32 ? "using_rtree_i32" : "using_rtree";
            code += "auto vtab = make_virtual_table<" + structName + ">(" + nameLiteral + ", " + usingFunction + "(" + columnParts +
                    "));\n";
            return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
        }

        if(moduleLower == "generate_series") {
            if(!node.moduleArguments.empty()) {
                warnings.push_back(
                    "generate_series module arguments are not mapped to sqlite_orm; expected empty argument list "
                    "for make_virtual_table<generate_series>(..., internal::using_generate_series())");
                return CodeGenResult{"/* CREATE VIRTUAL TABLE: generate_series (unmapped arguments) */",
                                     std::move(decisionPoints), std::move(warnings)};
            }
            std::string code = "auto vtab = make_virtual_table<generate_series>(" + nameLiteral +
                               ", internal::using_generate_series());\n";
            return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
        }

        if(moduleLower == "dbstat") {
            if(node.moduleArguments.size() > 1) {
                warnings.push_back(
                    "dbstat accepts at most one optional schema string argument for sqlite_orm::using_dbstat()");
                return CodeGenResult{"/* CREATE VIRTUAL TABLE: dbstat (too many arguments) */", std::move(decisionPoints),
                                     std::move(warnings)};
            }
            if(node.moduleArguments.empty()) {
                std::string code =
                    "auto vtab = make_virtual_table<dbstat>(" + nameLiteral + ", using_dbstat());\n";
                return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
            }
            if(auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(node.moduleArguments[0].get())) {
                std::string code = "auto vtab = make_virtual_table<dbstat>(" + nameLiteral + ", using_dbstat(" +
                                   sqlStringToCpp(stringLiteral->value) + "));\n";
                return CodeGenResult{std::move(code), std::move(decisionPoints), std::move(warnings)};
            }
            warnings.push_back(
                "dbstat optional argument should be a SQL string literal for sqlite_orm::using_dbstat(\"...\")");
            return CodeGenResult{"/* CREATE VIRTUAL TABLE: dbstat (unmapped argument) */", std::move(decisionPoints),
                                 std::move(warnings)};
        }

        warnings.push_back("virtual table module \"" + std::string(node.moduleName) +
                           "\" has no sqlite_orm mapping in sqlite2orm codegen");
        return CodeGenResult{"/* CREATE VIRTUAL TABLE: unknown module */", std::move(decisionPoints), std::move(warnings)};
    }

    CodeGenResult DdlCodeGenerator::generateCreateView(const CreateViewNode& node) {
        std::string displayName;
        if(node.viewSchemaName) {
            displayName = *node.viewSchemaName;
            displayName += '.';
        }
        displayName += node.viewName;
        return CodeGenResult{"/* CREATE VIEW " + displayName + " — not supported for sqlite_orm */",
                             {},
                             {"CREATE VIEW " + displayName + " is not supported for sqlite_orm code generation"}};
    }

    CreateTableParts DdlCodeGenerator::createTableParts(const CreateTableNode& createTable) {
        const auto structName = toStructName(createTable.tableName);
        this->context.structName = structName;
        const auto rawTableName = stripIdentifierQuotes(createTable.tableName);
        std::vector<std::string> warnings;

        std::string structDeclaration = "struct " + structName + " {\n";
        for(const auto& column : createTable.columns) {
            const auto cppName = toCppIdentifier(column.name);
            const auto cppType =
                column.typeName.empty() ? "std::vector<char>" : sqliteTypeToCpp(column.typeName);
            const bool nullable = !column.primaryKey && !column.notNull;
            if(nullable) {
                structDeclaration += "    std::optional<" + cppType + "> " + cppName + ";\n";
            } else {
                structDeclaration += "    " + cppType + " " + cppName + defaultInitializer(cppType) + ";\n";
            }
        }
        structDeclaration += "};\n";

        std::string makeExpression = "make_table(\"" + rawTableName + "\"";
        for(const auto& column : createTable.columns) {
            const auto cppName = toCppIdentifier(column.name);
            const auto rawColumnName = stripIdentifierQuotes(column.name);
            makeExpression += ",\n        make_column(\"" + rawColumnName + "\", &" + structName + "::" + cppName;
            if(column.primaryKey) {
                std::string primaryKey = "primary_key()";
                switch(column.primaryKeyConflict) {
                case ConflictClause::rollback: primaryKey += ".on_conflict_rollback()"; break;
                case ConflictClause::abort: primaryKey += ".on_conflict_abort()"; break;
                case ConflictClause::fail: primaryKey += ".on_conflict_fail()"; break;
                case ConflictClause::ignore: primaryKey += ".on_conflict_ignore()"; break;
                case ConflictClause::replace: primaryKey += ".on_conflict_replace()"; break;
                case ConflictClause::none: break;
                }
                if(column.autoincrement) {
                    primaryKey += ".autoincrement()";
                }
                makeExpression += ", " + primaryKey;
            }
            if(column.defaultValue) {
                const auto defaultCode = this->coordinator.generateNode(*column.defaultValue).code;
                makeExpression += ", default_value(" + defaultCode + ")";
            }
            if(column.unique) {
                makeExpression += ", unique()";
                if(column.uniqueConflict != ConflictClause::none) {
                    warnings.push_back("UNIQUE ON CONFLICT clause on column '" + rawColumnName +
                                       "' is not supported by sqlite_orm::unique()");
                }
            }
            if(column.checkExpression) {
                const auto checkCode = this->coordinator.generateNode(*column.checkExpression).code;
                makeExpression += ", check(" + checkCode + ")";
            }
            if(!column.collation.empty()) {
                const auto lower = toLowerAscii(column.collation);
                if(lower == "nocase") {
                    makeExpression += ", collate_nocase()";
                } else if(lower == "binary") {
                    makeExpression += ", collate_binary()";
                } else if(lower == "rtrim") {
                    makeExpression += ", collate_rtrim()";
                } else {
                    warnings.push_back("COLLATE " + column.collation + " on column '" + rawColumnName +
                                       "' is not a built-in collation in sqlite_orm");
                }
            }
            if(column.generatedExpression) {
                const auto expressionCode = this->coordinator.generateNode(*column.generatedExpression).code;
                if(column.generatedAlways) {
                    makeExpression += ", generated_always_as(" + expressionCode + ")";
                } else {
                    makeExpression += ", as(" + expressionCode + ")";
                }
                if(column.generatedStorage == ColumnDef::GeneratedStorage::stored) {
                    makeExpression += ".stored()";
                } else if(column.generatedStorage == ColumnDef::GeneratedStorage::virtual_) {
                    makeExpression += ".virtual_()";
                }
            }
            makeExpression += ")";
        }
        auto findPrimaryKeyColumnCpp = [&createTable](std::string_view refTable) -> std::string {
            if(toLowerAscii(std::string(refTable)) == toLowerAscii(createTable.tableName)) {
                for(const auto& column : createTable.columns) {
                    if(column.primaryKey) {
                        return toCppIdentifier(column.name);
                    }
                }
                if(!createTable.primaryKeys.empty() && !createTable.primaryKeys[0].columns.empty()) {
                    return toCppIdentifier(createTable.primaryKeys[0].columns[0]);
                }
            }
            return {};
        };
        for(const auto& column : createTable.columns) {
            if(!column.foreignKey) {
                continue;
            }
            auto& foreignKey = *column.foreignKey;
            const auto cppName = toCppIdentifier(column.name);
            const auto referencedStructName = toStructName(foreignKey.table);
            std::string referencedColumnName;
            if(!foreignKey.column.empty()) {
                referencedColumnName = toCppIdentifier(foreignKey.column);
            } else {
                referencedColumnName = findPrimaryKeyColumnCpp(foreignKey.table);
                if(referencedColumnName.empty()) {
                    referencedColumnName = cppName;
                }
            }
            makeExpression += ",\n        foreign_key(&" + structName + "::" + cppName + ").references(&" + referencedStructName +
                        "::" + referencedColumnName + ")";
            const auto actionString = [](ForeignKeyAction action) -> std::string {
                switch(action) {
                case ForeignKeyAction::cascade: return ".cascade()";
                case ForeignKeyAction::restrict_: return ".restrict_()";
                case ForeignKeyAction::setNull: return ".set_null()";
                case ForeignKeyAction::setDefault: return ".set_default()";
                case ForeignKeyAction::noAction: return ".no_action()";
                case ForeignKeyAction::none: return "";
                }
                return "";
            };
            if(foreignKey.onDelete != ForeignKeyAction::none) {
                makeExpression += ".on_delete" + actionString(foreignKey.onDelete);
            }
            if(foreignKey.onUpdate != ForeignKeyAction::none) {
                makeExpression += ".on_update" + actionString(foreignKey.onUpdate);
            }
            if(foreignKey.deferrability != Deferrability::none) {
                std::string description = foreignKey.deferrability == Deferrability::deferrable ? "DEFERRABLE" : "NOT DEFERRABLE";
                if(foreignKey.initially == InitialConstraintMode::deferred) description += " INITIALLY DEFERRED";
                else if(foreignKey.initially == InitialConstraintMode::immediate) description += " INITIALLY IMMEDIATE";
                warnings.push_back(description + " on foreign key for column '" + column.name +
                    "' is not supported in sqlite_orm — ignored in codegen");
            }
        }
        for(const auto& tableForeignKey : createTable.foreignKeys) {
            const auto cppName = toCppIdentifier(tableForeignKey.column);
            const auto referencedStructName = toStructName(tableForeignKey.references.table);
            std::string referencedColumnName;
            if(!tableForeignKey.references.column.empty()) {
                referencedColumnName = toCppIdentifier(tableForeignKey.references.column);
            } else {
                referencedColumnName = findPrimaryKeyColumnCpp(tableForeignKey.references.table);
                if(referencedColumnName.empty()) {
                    referencedColumnName = cppName;
                }
            }
            makeExpression += ",\n        foreign_key(&" + structName + "::" + cppName + ").references(&" + referencedStructName +
                        "::" + referencedColumnName + ")";
            const auto actionString = [](ForeignKeyAction action) -> std::string {
                switch(action) {
                case ForeignKeyAction::cascade: return ".cascade()";
                case ForeignKeyAction::restrict_: return ".restrict_()";
                case ForeignKeyAction::setNull: return ".set_null()";
                case ForeignKeyAction::setDefault: return ".set_default()";
                case ForeignKeyAction::noAction: return ".no_action()";
                case ForeignKeyAction::none: return "";
                }
                return "";
            };
            if(tableForeignKey.references.onDelete != ForeignKeyAction::none) {
                makeExpression += ".on_delete" + actionString(tableForeignKey.references.onDelete);
            }
            if(tableForeignKey.references.onUpdate != ForeignKeyAction::none) {
                makeExpression += ".on_update" + actionString(tableForeignKey.references.onUpdate);
            }
            if(tableForeignKey.references.deferrability != Deferrability::none) {
                std::string description = tableForeignKey.references.deferrability == Deferrability::deferrable
                    ? "DEFERRABLE" : "NOT DEFERRABLE";
                if(tableForeignKey.references.initially == InitialConstraintMode::deferred) description += " INITIALLY DEFERRED";
                else if(tableForeignKey.references.initially == InitialConstraintMode::immediate) description += " INITIALLY IMMEDIATE";
                warnings.push_back(description + " on table-level foreign key for column '" + tableForeignKey.column +
                    "' is not supported in sqlite_orm — ignored in codegen");
            }
        }
        for(const auto& tablePrimaryKey : createTable.primaryKeys) {
            makeExpression += ",\n        primary_key(";
            for(size_t columnIndex = 0; columnIndex < tablePrimaryKey.columns.size(); ++columnIndex) {
                if(columnIndex > 0) {
                    makeExpression += ", ";
                }
                makeExpression += "&" + structName + "::" + toCppIdentifier(tablePrimaryKey.columns.at(columnIndex));
            }
            makeExpression += ")";
        }
        for(const auto& tableUnique : createTable.uniques) {
            makeExpression += ",\n        unique(";
            for(size_t columnIndex = 0; columnIndex < tableUnique.columns.size(); ++columnIndex) {
                if(columnIndex > 0) {
                    makeExpression += ", ";
                }
                makeExpression += "&" + structName + "::" + toCppIdentifier(tableUnique.columns.at(columnIndex));
            }
            makeExpression += ")";
        }
        for(const auto& tableCheck : createTable.checks) {
            if(tableCheck.expression) {
                const auto checkCode = this->coordinator.generateNode(*tableCheck.expression).code;
                makeExpression += ",\n        check(" + checkCode + ")";
            }
        }
        makeExpression += ")";
        if(createTable.withoutRowid) {
            makeExpression += ".without_rowid()";
        }
        if(createTable.strict) {
            warnings.push_back("STRICT tables are not directly supported by sqlite_orm — "
                               "the STRICT qualifier is ignored in codegen");
        }

        return CreateTableParts{std::move(structDeclaration), std::move(makeExpression), std::move(warnings)};
    }

}  // namespace sqlite2orm
