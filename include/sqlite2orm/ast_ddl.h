#pragma once

#include <sqlite2orm/ast_base.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sqlite2orm {

    struct ForeignKeyClause {
        std::string table;
        std::string column;
        ForeignKeyAction onDelete = ForeignKeyAction::none;
        ForeignKeyAction onUpdate = ForeignKeyAction::none;
        Deferrability deferrability = Deferrability::none;
        InitialConstraintMode initially = InitialConstraintMode::none;

        bool operator==(const ForeignKeyClause&) const = default;
    };

    struct ColumnDef {
        std::string name;
        std::string typeName;
        bool primaryKey = false;
        bool autoincrement = false;
        bool notNull = false;
        ConflictClause primaryKeyConflict = ConflictClause::none;
        std::shared_ptr<AstNode> defaultValue;
        bool unique = false;
        ConflictClause uniqueConflict = ConflictClause::none;
        std::shared_ptr<AstNode> checkExpression;
        std::string collation;
        std::optional<ForeignKeyClause> foreignKey;
        std::shared_ptr<AstNode> generatedExpression;
        bool generatedAlways = false;
        enum class GeneratedStorage { none, stored, virtual_ };
        GeneratedStorage generatedStorage = GeneratedStorage::none;

        bool operator==(const ColumnDef& other) const {
            if(this->name != other.name || this->typeName != other.typeName ||
               this->primaryKey != other.primaryKey || this->autoincrement != other.autoincrement ||
               this->notNull != other.notNull || this->primaryKeyConflict != other.primaryKeyConflict ||
               this->unique != other.unique || this->uniqueConflict != other.uniqueConflict ||
               this->collation != other.collation || this->foreignKey != other.foreignKey ||
               this->generatedAlways != other.generatedAlways ||
               this->generatedStorage != other.generatedStorage)
                return false;
            auto sharedEqual = [](const std::shared_ptr<AstNode>& a, const std::shared_ptr<AstNode>& b) {
                if(!a && !b) return true;
                if(!a || !b) return false;
                return *a == *b;
            };
            return sharedEqual(this->defaultValue, other.defaultValue) &&
                   sharedEqual(this->checkExpression, other.checkExpression) &&
                   sharedEqual(this->generatedExpression, other.generatedExpression);
        }
    };

    struct TableForeignKey {
        std::string column;
        ForeignKeyClause references;

        bool operator==(const TableForeignKey&) const = default;
    };

    struct TablePrimaryKey {
        std::vector<std::string> columns;

        bool operator==(const TablePrimaryKey&) const = default;
    };

    struct TableUnique {
        std::vector<std::string> columns;

        bool operator==(const TableUnique&) const = default;
    };

    struct TableCheck {
        std::shared_ptr<AstNode> expression;

        bool operator==(const TableCheck& other) const {
            if(!this->expression && !other.expression) return true;
            if(!this->expression || !other.expression) return false;
            return *this->expression == *other.expression;
        }
    };

    struct CreateTableNode : AstNode {
        std::string tableName;
        std::vector<ColumnDef> columns;
        std::vector<TableForeignKey> foreignKeys;
        std::vector<TablePrimaryKey> primaryKeys;
        std::vector<TableUnique> uniques;
        std::vector<TableCheck> checks;
        bool ifNotExists = false;
        bool withoutRowid = false;
        bool strict = false;

        CreateTableNode(std::string tableName, std::vector<ColumnDef> columns,
                         bool ifNotExists, SourceLocation location)
            : AstNode(location), tableName(std::move(tableName)),
              columns(std::move(columns)), ifNotExists(ifNotExists) {}

        CreateTableNode(std::string tableName, std::vector<ColumnDef> columns,
                         std::vector<TableForeignKey> foreignKeys,
                         bool ifNotExists, SourceLocation location)
            : AstNode(location), tableName(std::move(tableName)),
              columns(std::move(columns)), foreignKeys(std::move(foreignKeys)),
              ifNotExists(ifNotExists) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const CreateTableNode*>(&other);
            return o && this->tableName == o->tableName &&
                   this->columns == o->columns &&
                   this->foreignKeys == o->foreignKeys &&
                   this->primaryKeys == o->primaryKeys &&
                   this->uniques == o->uniques &&
                   this->checks == o->checks &&
                   this->ifNotExists == o->ifNotExists &&
                   this->withoutRowid == o->withoutRowid &&
                   this->strict == o->strict;
        }
    };

    struct CreateTriggerNode : AstNode {
        std::optional<std::string> triggerSchemaName;
        std::string triggerName;
        bool ifNotExists = false;
        bool temporary = false;
        TriggerTiming timing = TriggerTiming::before;
        TriggerEventKind eventKind = TriggerEventKind::insert_;
        std::vector<std::string> updateOfColumns;
        std::optional<std::string> tableSchemaName;
        std::string tableName;
        bool forEachRow = false;
        AstNodePointer whenClause;
        std::vector<AstNodePointer> bodyStatements;

        explicit CreateTriggerNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const CreateTriggerNode*>(&other);
            if(!o || this->triggerSchemaName != o->triggerSchemaName || this->triggerName != o->triggerName ||
               this->ifNotExists != o->ifNotExists || this->temporary != o->temporary || this->timing != o->timing ||
               this->eventKind != o->eventKind || this->updateOfColumns != o->updateOfColumns ||
               this->tableSchemaName != o->tableSchemaName || this->tableName != o->tableName ||
               this->forEachRow != o->forEachRow) {
                return false;
            }
            if(!astNodesEqual(this->whenClause, o->whenClause)) return false;
            if(this->bodyStatements.size() != o->bodyStatements.size()) return false;
            for(size_t i = 0; i < this->bodyStatements.size(); ++i) {
                if(!astNodesEqual(this->bodyStatements.at(i), o->bodyStatements.at(i))) return false;
            }
            return true;
        }
    };

    struct IndexColumnSpec {
        AstNodePointer expression;
        SortDirection sortDirection = SortDirection::none;
        std::string collation;

        bool operator==(const IndexColumnSpec& other) const {
            return astNodesEqual(this->expression, other.expression) &&
                   this->sortDirection == other.sortDirection && this->collation == other.collation;
        }
    };

    struct CreateIndexNode : AstNode {
        std::optional<std::string> indexSchemaName;
        std::string indexName;
        bool unique = false;
        bool ifNotExists = false;
        std::optional<std::string> tableSchemaName;
        std::string tableName;
        std::vector<IndexColumnSpec> indexedColumns;
        AstNodePointer whereClause;

        explicit CreateIndexNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const CreateIndexNode*>(&other);
            if(!o || this->indexSchemaName != o->indexSchemaName || this->indexName != o->indexName ||
               this->unique != o->unique || this->ifNotExists != o->ifNotExists ||
               this->tableSchemaName != o->tableSchemaName || this->tableName != o->tableName ||
               this->indexedColumns != o->indexedColumns) {
                return false;
            }
            return astNodesEqual(this->whereClause, o->whereClause);
        }
    };

    struct CreateVirtualTableNode : AstNode {
        bool temporary = false;
        bool ifNotExists = false;
        std::optional<std::string> tableSchemaName;
        std::string tableName;
        std::string moduleName;
        std::vector<AstNodePointer> moduleArguments;

        explicit CreateVirtualTableNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const CreateVirtualTableNode*>(&other);
            if(!o || this->temporary != o->temporary || this->ifNotExists != o->ifNotExists ||
               this->tableSchemaName != o->tableSchemaName || this->tableName != o->tableName ||
               this->moduleName != o->moduleName || this->moduleArguments.size() != o->moduleArguments.size()) {
                return false;
            }
            for(size_t i = 0; i < this->moduleArguments.size(); ++i) {
                if(!astNodesEqual(this->moduleArguments[i], o->moduleArguments[i])) {
                    return false;
                }
            }
            return true;
        }
    };

    /** Parsed CREATE VIEW … AS <select>; codegen may be added incrementally on this node. */
    struct CreateViewNode : AstNode {
        bool ifNotExists = false;
        std::optional<std::string> viewSchemaName;
        std::string viewName;
        std::vector<std::string> columnNames;
        AstNodePointer selectQuery;

        CreateViewNode(SourceLocation location, bool ifNotExists, std::optional<std::string> viewSchemaName,
                       std::string viewName, std::vector<std::string> columnNames, AstNodePointer selectQuery)
            : AstNode(location), ifNotExists(ifNotExists), viewSchemaName(std::move(viewSchemaName)),
              viewName(std::move(viewName)), columnNames(std::move(columnNames)),
              selectQuery(std::move(selectQuery)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const CreateViewNode*>(&other);
            if(!o || this->ifNotExists != o->ifNotExists || this->viewSchemaName != o->viewSchemaName ||
               this->viewName != o->viewName || this->columnNames != o->columnNames) {
                return false;
            }
            return astNodesEqual(this->selectQuery, o->selectQuery);
        }
    };

    struct TransactionControlNode : AstNode {
        enum class Kind { begin, commit, rollback };
        Kind kind = Kind::begin;
        BeginTransactionMode beginMode = BeginTransactionMode::plain;
        std::optional<std::string> rollbackToSavepoint;

        explicit TransactionControlNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const TransactionControlNode*>(&other);
            return o && kind == o->kind && beginMode == o->beginMode && rollbackToSavepoint == o->rollbackToSavepoint;
        }
    };

    struct VacuumStatementNode : AstNode {
        std::optional<std::string> schemaName;

        explicit VacuumStatementNode(SourceLocation location, std::optional<std::string> schemaName = std::nullopt)
            : AstNode(location), schemaName(std::move(schemaName)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const VacuumStatementNode*>(&other);
            return o && schemaName == o->schemaName;
        }
    };

    struct DropStatementNode : AstNode {
        DropObjectKind objectKind = DropObjectKind::table;
        bool ifExists = false;
        std::optional<std::string> schemaName;
        std::string objectName;

        explicit DropStatementNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const DropStatementNode*>(&other);
            return o && objectKind == o->objectKind && ifExists == o->ifExists && schemaName == o->schemaName &&
                   objectName == o->objectName;
        }
    };

    /** `ALTER TABLE` … (tail not modeled); schema migration in sqlite_orm is driven by `storage.sync_schema()`. */
    struct AlterTableStatementNode : AstNode {
        std::optional<std::string> tableSchemaName;
        std::string tableName;

        AlterTableStatementNode(SourceLocation location, std::optional<std::string> tableSchemaName, std::string tableName)
            : AstNode(location), tableSchemaName(std::move(tableSchemaName)), tableName(std::move(tableName)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const AlterTableStatementNode*>(&other);
            return o && tableSchemaName == o->tableSchemaName && tableName == o->tableName;
        }
    };

    struct SavepointNode : AstNode {
        std::string name;

        SavepointNode(SourceLocation location, std::string name)
            : AstNode(location), name(std::move(name)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const SavepointNode*>(&other);
            return o && name == o->name;
        }
    };

    struct ReleaseNode : AstNode {
        std::string name;

        ReleaseNode(SourceLocation location, std::string name)
            : AstNode(location), name(std::move(name)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const ReleaseNode*>(&other);
            return o && name == o->name;
        }
    };

    struct AttachDatabaseNode : AstNode {
        AstNodePointer fileExpression;
        std::string schemaName;

        AttachDatabaseNode(SourceLocation location, AstNodePointer fileExpression, std::string schemaName)
            : AstNode(location), fileExpression(std::move(fileExpression)), schemaName(std::move(schemaName)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const AttachDatabaseNode*>(&other);
            return o && schemaName == o->schemaName && astNodesEqual(fileExpression, o->fileExpression);
        }
    };

    struct DetachDatabaseNode : AstNode {
        std::string schemaName;

        DetachDatabaseNode(SourceLocation location, std::string schemaName)
            : AstNode(location), schemaName(std::move(schemaName)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const DetachDatabaseNode*>(&other);
            return o && schemaName == o->schemaName;
        }
    };

    struct AnalyzeNode : AstNode {
        std::optional<std::string> schemaOrTableName;
        std::optional<std::string> tableName;

        explicit AnalyzeNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const AnalyzeNode*>(&other);
            return o && schemaOrTableName == o->schemaOrTableName && tableName == o->tableName;
        }
    };

    struct ReindexNode : AstNode {
        std::optional<std::string> schemaOrObjectName;
        std::optional<std::string> objectName;

        explicit ReindexNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const ReindexNode*>(&other);
            return o && schemaOrObjectName == o->schemaOrObjectName && objectName == o->objectName;
        }
    };

    struct PragmaNode : AstNode {
        std::optional<std::string> schemaName;
        std::string pragmaName;
        AstNodePointer value;

        explicit PragmaNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const PragmaNode*>(&other);
            return o && schemaName == o->schemaName && pragmaName == o->pragmaName &&
                   astNodesEqual(value, o->value);
        }
    };

    struct ExplainNode : AstNode {
        bool queryPlan = false;
        AstNodePointer statement;

        ExplainNode(SourceLocation location, bool queryPlan, AstNodePointer statement)
            : AstNode(location), queryPlan(queryPlan), statement(std::move(statement)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const ExplainNode*>(&other);
            return o && queryPlan == o->queryPlan && astNodesEqual(statement, o->statement);
        }
    };

}  // namespace sqlite2orm
