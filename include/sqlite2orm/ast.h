#pragma once

#include <sqlite2orm/token.h>

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace sqlite2orm {

    struct AstNode {
        SourceLocation location;

        virtual ~AstNode() = default;
        AstNode() = default;
        explicit AstNode(SourceLocation location) : location(location) {}

        virtual bool operator==(const AstNode& other) const = 0;
        bool operator!=(const AstNode& other) const { return !(*this == other); }
    };

    using AstNodePointer = std::unique_ptr<AstNode>;

    struct IntegerLiteralNode : AstNode {
        std::string_view value;

        IntegerLiteralNode(std::string_view value, SourceLocation location) : AstNode(location), value(value) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const IntegerLiteralNode*>(&other);
            return o && this->value == o->value;
        }
    };

    struct RealLiteralNode : AstNode {
        std::string_view value;

        RealLiteralNode(std::string_view value, SourceLocation location) : AstNode(location), value(value) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const RealLiteralNode*>(&other);
            return o && this->value == o->value;
        }
    };

    struct StringLiteralNode : AstNode {
        std::string_view value;

        StringLiteralNode(std::string_view value, SourceLocation location) : AstNode(location), value(value) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const StringLiteralNode*>(&other);
            return o && this->value == o->value;
        }
    };

    struct NullLiteralNode : AstNode {
        using AstNode::AstNode;

        bool operator==(const AstNode& other) const override {
            return dynamic_cast<const NullLiteralNode*>(&other) != nullptr;
        }
    };

    struct BoolLiteralNode : AstNode {
        bool value;

        BoolLiteralNode(bool value, SourceLocation location) : AstNode(location), value(value) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const BoolLiteralNode*>(&other);
            return o && this->value == o->value;
        }
    };

    struct BlobLiteralNode : AstNode {
        std::string_view value;

        BlobLiteralNode(std::string_view value, SourceLocation location) : AstNode(location), value(value) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const BlobLiteralNode*>(&other);
            return o && this->value == o->value;
        }
    };

    struct ColumnRefNode : AstNode {
        std::string_view columnName;

        ColumnRefNode(std::string_view columnName, SourceLocation location) : AstNode(location), columnName(columnName) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const ColumnRefNode*>(&other);
            return o && this->columnName == o->columnName;
        }
    };

    struct QualifiedColumnRefNode : AstNode {
        /** When set, `schema.table.column` (SQLite); otherwise `table.column`. */
        std::optional<std::string_view> schemaName;
        std::string_view tableName;
        std::string_view columnName;

        QualifiedColumnRefNode(std::string_view tableName, std::string_view columnName, SourceLocation location)
            : AstNode(location), tableName(tableName), columnName(columnName) {}

        QualifiedColumnRefNode(std::string_view schemaName, std::string_view tableName, std::string_view columnName,
                               SourceLocation location)
            : AstNode(location), schemaName(schemaName), tableName(tableName), columnName(columnName) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const QualifiedColumnRefNode*>(&other);
            return o && this->schemaName == o->schemaName && this->tableName == o->tableName &&
                   this->columnName == o->columnName;
        }
    };

    enum class CurrentDatetimeKind { time, date, timestamp };

    struct CurrentDatetimeLiteralNode : AstNode {
        CurrentDatetimeKind kind;

        CurrentDatetimeLiteralNode(CurrentDatetimeKind kind, SourceLocation location)
            : AstNode(location), kind(kind) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const CurrentDatetimeLiteralNode*>(&other);
            return o && this->kind == o->kind;
        }
    };

    struct QualifiedAsteriskNode : AstNode {
        std::optional<std::string> schemaName;
        std::string tableName;

        QualifiedAsteriskNode(std::string tableName, SourceLocation location)
            : AstNode(location), tableName(std::move(tableName)) {}

        QualifiedAsteriskNode(std::string schemaName, std::string tableName, SourceLocation location)
            : AstNode(location), schemaName(std::move(schemaName)), tableName(std::move(tableName)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const QualifiedAsteriskNode*>(&other);
            return o && this->tableName == o->tableName && this->schemaName == o->schemaName;
        }
    };

    struct NewRefNode : AstNode {
        std::string_view columnName;

        NewRefNode(std::string_view columnName, SourceLocation location) : AstNode(location), columnName(columnName) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const NewRefNode*>(&other);
            return o && this->columnName == o->columnName;
        }
    };

    struct OldRefNode : AstNode {
        std::string_view columnName;

        OldRefNode(std::string_view columnName, SourceLocation location) : AstNode(location), columnName(columnName) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const OldRefNode*>(&other);
            return o && this->columnName == o->columnName;
        }
    };

    /** UPSERT: excluded.column in DO UPDATE SET */
    struct ExcludedRefNode : AstNode {
        std::string_view columnName;

        ExcludedRefNode(std::string_view columnName, SourceLocation location)
            : AstNode(location), columnName(columnName) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const ExcludedRefNode*>(&other);
            return o && this->columnName == o->columnName;
        }
    };

    enum class BinaryOperator {
        logicalOr,
        logicalAnd,
        equals,
        notEquals,
        lessThan,
        lessOrEqual,
        greaterThan,
        greaterOrEqual,
        add,
        subtract,
        multiply,
        divide,
        modulo,
        concatenate,
        bitwiseAnd,
        bitwiseOr,
        shiftLeft,
        shiftRight,
        isOp,
        isNot,
        isDistinctFrom,
        isNotDistinctFrom,
        jsonArrow,
        jsonArrow2,
    };

    inline bool astNodesEqual(const AstNodePointer& a, const AstNodePointer& b) {
        return (!a && !b) || (a && b && *a == *b);
    }

    struct BinaryOperatorNode : AstNode {
        BinaryOperator binaryOperator;
        AstNodePointer lhs;
        AstNodePointer rhs;

        BinaryOperatorNode(BinaryOperator binaryOperator, AstNodePointer lhs, AstNodePointer rhs, SourceLocation location)
            : AstNode(location), binaryOperator(binaryOperator), lhs(std::move(lhs)), rhs(std::move(rhs)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const BinaryOperatorNode*>(&other);
            return o && this->binaryOperator == o->binaryOperator &&
                   astNodesEqual(this->lhs, o->lhs) && astNodesEqual(this->rhs, o->rhs);
        }
    };

    enum class UnaryOperator {
        minus,
        plus,
        bitwiseNot,
        logicalNot,
    };

    struct UnaryOperatorNode : AstNode {
        UnaryOperator unaryOperator;
        AstNodePointer operand;

        UnaryOperatorNode(UnaryOperator unaryOperator, AstNodePointer operand, SourceLocation location)
            : AstNode(location), unaryOperator(unaryOperator), operand(std::move(operand)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const UnaryOperatorNode*>(&other);
            return o && this->unaryOperator == o->unaryOperator && astNodesEqual(this->operand, o->operand);
        }
    };

    struct IsNullNode : AstNode {
        AstNodePointer operand;

        IsNullNode(AstNodePointer operand, SourceLocation location)
            : AstNode(location), operand(std::move(operand)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const IsNullNode*>(&other);
            return o && astNodesEqual(this->operand, o->operand);
        }
    };

    struct IsNotNullNode : AstNode {
        AstNodePointer operand;

        IsNotNullNode(AstNodePointer operand, SourceLocation location)
            : AstNode(location), operand(std::move(operand)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const IsNotNullNode*>(&other);
            return o && astNodesEqual(this->operand, o->operand);
        }
    };

    struct BetweenNode : AstNode {
        AstNodePointer operand;
        AstNodePointer low;
        AstNodePointer high;
        bool negated;

        BetweenNode(AstNodePointer operand, AstNodePointer low, AstNodePointer high,
                     bool negated, SourceLocation location)
            : AstNode(location), operand(std::move(operand)), low(std::move(low)),
              high(std::move(high)), negated(negated) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const BetweenNode*>(&other);
            return o && this->negated == o->negated &&
                   astNodesEqual(this->operand, o->operand) &&
                   astNodesEqual(this->low, o->low) && astNodesEqual(this->high, o->high);
        }
    };

    struct SubqueryNode : AstNode {
        AstNodePointer select;

        SubqueryNode(AstNodePointer select, SourceLocation location)
            : AstNode(location), select(std::move(select)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const SubqueryNode*>(&other);
            return o && astNodesEqual(this->select, o->select);
        }
    };

    struct ExistsNode : AstNode {
        AstNodePointer select;

        ExistsNode(AstNodePointer select, SourceLocation location)
            : AstNode(location), select(std::move(select)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const ExistsNode*>(&other);
            return o && astNodesEqual(this->select, o->select);
        }
    };

    struct InNode : AstNode {
        AstNodePointer operand;
        std::vector<AstNodePointer> values;
        AstNodePointer subquerySelect;
        std::string tableName;
        bool negated;

        InNode(AstNodePointer operand, std::vector<AstNodePointer> values, AstNodePointer subquerySelect,
               bool negated, SourceLocation location)
            : AstNode(location), operand(std::move(operand)), values(std::move(values)),
              subquerySelect(std::move(subquerySelect)), negated(negated) {}

        InNode(AstNodePointer operand, std::string tableName, bool negated, SourceLocation location)
            : AstNode(location), operand(std::move(operand)), tableName(std::move(tableName)), negated(negated) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const InNode*>(&other);
            if(!o || this->negated != o->negated || !astNodesEqual(this->operand, o->operand))
                return false;
            if(this->tableName != o->tableName) return false;
            if(!astNodesEqual(this->subquerySelect, o->subquerySelect)) return false;
            if(this->values.size() != o->values.size()) return false;
            for(size_t i = 0; i < this->values.size(); ++i) {
                if(!astNodesEqual(this->values.at(i), o->values.at(i))) return false;
            }
            return true;
        }
    };

    struct LikeNode : AstNode {
        AstNodePointer operand;
        AstNodePointer pattern;
        AstNodePointer escape;
        bool negated;

        LikeNode(AstNodePointer operand, AstNodePointer pattern, AstNodePointer escape,
                  bool negated, SourceLocation location)
            : AstNode(location), operand(std::move(operand)), pattern(std::move(pattern)),
              escape(std::move(escape)), negated(negated) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const LikeNode*>(&other);
            return o && this->negated == o->negated &&
                   astNodesEqual(this->operand, o->operand) &&
                   astNodesEqual(this->pattern, o->pattern) &&
                   astNodesEqual(this->escape, o->escape);
        }
    };

    struct GlobNode : AstNode {
        AstNodePointer operand;
        AstNodePointer pattern;
        bool negated;

        GlobNode(AstNodePointer operand, AstNodePointer pattern,
                  bool negated, SourceLocation location)
            : AstNode(location), operand(std::move(operand)),
              pattern(std::move(pattern)), negated(negated) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const GlobNode*>(&other);
            return o && this->negated == o->negated &&
                   astNodesEqual(this->operand, o->operand) &&
                   astNodesEqual(this->pattern, o->pattern);
        }
    };

    struct OverClause;

    struct FunctionCallNode : AstNode {
        std::string name;
        std::vector<AstNodePointer> arguments;
        bool distinct = false;
        bool star = false;
        /** Optional `FILTER (WHERE expr)` before `OVER`. */
        AstNodePointer filterWhere;
        std::unique_ptr<OverClause> over;

        FunctionCallNode(std::string name, std::vector<AstNodePointer> arguments,
                          bool distinct, bool star, SourceLocation location)
            : AstNode(location), name(std::move(name)), arguments(std::move(arguments)),
              distinct(distinct), star(star) {}

        bool operator==(const AstNode& other) const override;
    };

    struct CastNode : AstNode {
        AstNodePointer operand;
        std::string typeName;

        CastNode(AstNodePointer operand, std::string typeName, SourceLocation location)
            : AstNode(location), operand(std::move(operand)), typeName(std::move(typeName)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const CastNode*>(&other);
            return o && this->typeName == o->typeName &&
                   astNodesEqual(this->operand, o->operand);
        }
    };

    struct CaseBranch {
        AstNodePointer condition;
        AstNodePointer result;

        bool operator==(const CaseBranch& other) const {
            return astNodesEqual(this->condition, other.condition) &&
                   astNodesEqual(this->result, other.result);
        }
    };

    struct CaseNode : AstNode {
        AstNodePointer operand;
        std::vector<CaseBranch> branches;
        AstNodePointer elseResult;

        CaseNode(AstNodePointer operand, std::vector<CaseBranch> branches,
                  AstNodePointer elseResult, SourceLocation location)
            : AstNode(location), operand(std::move(operand)),
              branches(std::move(branches)), elseResult(std::move(elseResult)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const CaseNode*>(&other);
            return o && astNodesEqual(this->operand, o->operand) &&
                   this->branches == o->branches &&
                   astNodesEqual(this->elseResult, o->elseResult);
        }
    };

    struct BindParameterNode : AstNode {
        std::string_view value;

        BindParameterNode(std::string_view value, SourceLocation location) : AstNode(location), value(value) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const BindParameterNode*>(&other);
            return o && this->value == o->value;
        }
    };

    struct CollateNode : AstNode {
        AstNodePointer operand;
        std::string collationName;

        CollateNode(AstNodePointer operand, std::string collationName, SourceLocation location)
            : AstNode(location), operand(std::move(operand)), collationName(std::move(collationName)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const CollateNode*>(&other);
            return o && this->collationName == o->collationName && astNodesEqual(this->operand, o->operand);
        }
    };

    // --- DDL nodes ---

    enum class ConflictClause {
        none,
        rollback,
        abort,
        fail,
        ignore,
        replace,
    };

    enum class ForeignKeyAction {
        none,
        noAction,
        restrict_,
        setNull,
        setDefault,
        cascade,
    };

    enum class Deferrability { none, deferrable, notDeferrable };
    enum class InitialConstraintMode { none, deferred, immediate };

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

    enum class TriggerTiming { before, after, insteadOf };

    enum class TriggerEventKind { delete_, insert_, update_, updateOf };

    enum class RaiseKind { ignore, rollback, abort, fail };

    struct RaiseNode : AstNode {
        RaiseKind kind = RaiseKind::ignore;
        AstNodePointer message;

        RaiseNode(RaiseKind kind, AstNodePointer message, SourceLocation location)
            : AstNode(location), kind(kind), message(std::move(message)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const RaiseNode*>(&other);
            return o && this->kind == o->kind && astNodesEqual(this->message, o->message);
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

    struct SelectColumn {
        std::shared_ptr<AstNode> expression;
        std::string alias;

        bool operator==(const SelectColumn& other) const {
            if(this->alias != other.alias) return false;
            if(!this->expression && !other.expression) return true;
            if(!this->expression || !other.expression) return false;
            return *this->expression == *other.expression;
        }
    };

    enum class SortDirection { none, asc, desc };

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

    enum class BeginTransactionMode { plain, deferred, immediate, exclusive };

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

    /** Object type in `DROP TABLE|INDEX|TRIGGER|VIEW`. */
    enum class DropObjectKind { table, index, trigger, view };

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

    enum class NullsOrdering { none, first, last };

    struct OrderByTerm {
        std::shared_ptr<AstNode> expression;
        SortDirection direction = SortDirection::none;
        std::string collation;
        NullsOrdering nulls = NullsOrdering::none;

        bool operator==(const OrderByTerm& other) const {
            if(this->direction != other.direction || this->collation != other.collation ||
               this->nulls != other.nulls)
                return false;
            if(!this->expression && !other.expression) return true;
            if(!this->expression || !other.expression) return false;
            return *this->expression == *other.expression;
        }
    };

    enum class WindowFrameUnit { rows, range, groups };

    enum class WindowFrameExcludeKind { none, currentRow, group, ties };

    enum class WindowFrameBoundKind {
        unboundedPreceding,
        exprPreceding,
        currentRow,
        exprFollowing,
        unboundedFollowing
    };

    struct WindowFrameBound {
        WindowFrameBoundKind kind = WindowFrameBoundKind::currentRow;
        AstNodePointer expr;

        bool operator==(const WindowFrameBound& other) const {
            return kind == other.kind && astNodesEqual(expr, other.expr);
        }
    };

    struct WindowFrameSpec {
        WindowFrameUnit unit = WindowFrameUnit::rows;
        WindowFrameBound start{};
        WindowFrameBound end{};
        WindowFrameExcludeKind exclude = WindowFrameExcludeKind::none;

        bool operator==(const WindowFrameSpec& other) const {
            return unit == other.unit && start == other.start && end == other.end && exclude == other.exclude;
        }
    };

    struct OverClause {
        std::optional<std::string> namedWindow;
        std::vector<AstNodePointer> partitionBy;
        std::vector<OrderByTerm> orderBy;
        std::unique_ptr<WindowFrameSpec> frame;

        bool operator==(const OverClause& other) const {
            if(namedWindow != other.namedWindow) return false;
            if(partitionBy.size() != other.partitionBy.size()) return false;
            for(size_t i = 0; i < partitionBy.size(); ++i) {
                if(!astNodesEqual(partitionBy.at(i), other.partitionBy.at(i))) return false;
            }
            if(orderBy != other.orderBy) return false;
            if(static_cast<bool>(frame) != static_cast<bool>(other.frame)) return false;
            if(frame && other.frame && *frame != *other.frame) return false;
            return true;
        }
    };

    /** `WINDOW name AS (window-defn)` — same shape as `OVER` body without the `OVER` keyword. */
    struct NamedWindowDefinition {
        std::string name;
        std::unique_ptr<OverClause> definition;

        bool operator==(const NamedWindowDefinition& o) const {
            return name == o.name && static_cast<bool>(definition) == static_cast<bool>(o.definition) &&
                   (!definition || !o.definition || *definition == *o.definition);
        }
    };

    struct FromTableClause {
        std::optional<std::string> schemaName;
        std::string tableName;
        std::optional<std::string> alias;
        /** When non-null, `FROM (SELECT ...)` derived table; `tableName` is unused. */
        std::shared_ptr<AstNode> derivedSelect;
        /** For table-valued functions: `FROM func(arg1, arg2)` */
        std::vector<std::shared_ptr<AstNode>> tableFunctionArgs;

        bool operator==(const FromTableClause& other) const {
            if(this->schemaName != other.schemaName || this->tableName != other.tableName ||
               this->alias != other.alias) {
                return false;
            }
            if(static_cast<bool>(this->derivedSelect) != static_cast<bool>(other.derivedSelect)) {
                return false;
            }
            if(this->derivedSelect && !(*this->derivedSelect == *other.derivedSelect)) {
                return false;
            }
            if(this->tableFunctionArgs.size() != other.tableFunctionArgs.size()) {
                return false;
            }
            for(size_t i = 0; i < this->tableFunctionArgs.size(); ++i) {
                auto& a = this->tableFunctionArgs[i];
                auto& b = other.tableFunctionArgs[i];
                if(!a && !b) continue;
                if(!a || !b) return false;
                if(*a != *b) return false;
            }
            return true;
        }
    };

    enum class JoinKind {
        none,
        crossJoin,
        innerJoin,
        leftJoin,
        leftOuterJoin,
        joinPlain,
        naturalInnerJoin,
        naturalLeftJoin,
    };

    struct FromClauseItem {
        JoinKind leadingJoin = JoinKind::none;
        FromTableClause table;
        std::shared_ptr<AstNode> onExpression;
        std::vector<std::string> usingColumnNames;

        bool operator==(const FromClauseItem& other) const {
            if(this->leadingJoin != other.leadingJoin || this->table != other.table ||
               this->usingColumnNames != other.usingColumnNames)
                return false;
            if(!this->onExpression && !other.onExpression) return true;
            if(!this->onExpression || !other.onExpression) return false;
            return *this->onExpression == *other.onExpression;
        }
    };

    struct GroupByClause {
        std::vector<std::shared_ptr<AstNode>> expressions;
        std::shared_ptr<AstNode> having;

        bool operator==(const GroupByClause& other) const {
            if(this->expressions.size() != other.expressions.size()) return false;
            for(size_t i = 0; i < this->expressions.size(); ++i) {
                if(!this->expressions.at(i) && !other.expressions.at(i)) continue;
                if(!this->expressions.at(i) || !other.expressions.at(i)) return false;
                if(*this->expressions.at(i) != *other.expressions.at(i)) return false;
            }
            if(!this->having && !other.having) return true;
            if(!this->having || !other.having) return false;
            return *this->having == *other.having;
        }
    };

    enum class CompoundSelectOperator {
        unionDistinct,
        unionAll,
        intersect,
        except,
    };

    enum class CteMaterialization { none, materialized, notMaterialized };

    /** Single CTE: `name [(col,...)] AS [MATERIALIZED|NOT MATERIALIZED] (subquery)` — `query` is the full SELECT. */
    struct CommonTableExpression {
        std::string cteName;
        std::vector<std::string> columnNames;
        CteMaterialization materialization = CteMaterialization::none;
        AstNodePointer query;

        bool operator==(const CommonTableExpression& o) const {
            return cteName == o.cteName && columnNames == o.columnNames && materialization == o.materialization &&
                   astNodesEqual(query, o.query);
        }
    };

    struct WithClause {
        bool recursive = false;
        std::vector<CommonTableExpression> tables;

        bool operator==(const WithClause& o) const = default;
    };

    /** `WITH [RECURSIVE] …` followed by SELECT / INSERT / REPLACE INTO / UPDATE / DELETE. */
    struct WithQueryNode : AstNode {
        WithClause clause;
        AstNodePointer statement;

        WithQueryNode(WithClause clause, AstNodePointer statement, SourceLocation location)
            : AstNode(location), clause(std::move(clause)), statement(std::move(statement)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const WithQueryNode*>(&other);
            return o && clause == o->clause && astNodesEqual(statement, o->statement);
        }
    };

    struct CompoundSelectNode : AstNode {
        std::vector<AstNodePointer> selects;
        std::vector<CompoundSelectOperator> operators;

        CompoundSelectNode(std::vector<AstNodePointer> selects, std::vector<CompoundSelectOperator> operators,
                           SourceLocation location)
            : AstNode(location), selects(std::move(selects)), operators(std::move(operators)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const CompoundSelectNode*>(&other);
            if(!o || this->operators != o->operators || this->selects.size() != o->selects.size()) {
                return false;
            }
            for(size_t i = 0; i < this->selects.size(); ++i) {
                if(!astNodesEqual(this->selects.at(i), o->selects.at(i))) {
                    return false;
                }
            }
            return true;
        }
    };

    struct SelectNode : AstNode {
        bool distinct = false;
        bool selectAll = false;
        std::vector<SelectColumn> columns;
        std::vector<FromClauseItem> fromClause;
        std::shared_ptr<AstNode> whereClause;
        std::vector<OrderByTerm> orderBy;
        std::optional<GroupByClause> groupBy;
        std::vector<NamedWindowDefinition> namedWindows;
        int limitValue = -1;
        int offsetValue = -1;

        SelectNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const SelectNode*>(&other);
            if(!o) return false;
            if(this->distinct != o->distinct || this->selectAll != o->selectAll ||
               this->fromClause != o->fromClause || this->columns != o->columns ||
               this->orderBy != o->orderBy || this->groupBy != o->groupBy ||
               this->namedWindows != o->namedWindows || this->limitValue != o->limitValue ||
               this->offsetValue != o->offsetValue)
                return false;
            if(!this->whereClause && !o->whereClause) return true;
            if(!this->whereClause || !o->whereClause) return false;
            return *this->whereClause == *o->whereClause;
        }
    };

    struct UpdateAssignment {
        std::string columnName;
        AstNodePointer value;

        bool operator==(const UpdateAssignment& other) const {
            return this->columnName == other.columnName && astNodesEqual(this->value, other.value);
        }
    };

    enum class InsertDataKind { values, defaultValues, selectQuery };

    enum class InsertUpsertAction { none, doNothing, doUpdate };

    struct ReturningColumn {
        AstNodePointer expression;
        std::string alias;

        bool operator==(const ReturningColumn& other) const {
            return alias == other.alias && astNodesEqual(expression, other.expression);
        }
    };

    struct InsertNode : AstNode {
        bool replaceInto = false;
        ConflictClause orConflict = ConflictClause::none;
        std::optional<std::string> schemaName;
        std::string tableName;
        std::vector<std::string> columnNames;
        InsertDataKind dataKind = InsertDataKind::values;
        std::vector<std::vector<AstNodePointer>> valueRows;
        AstNodePointer selectStatement;

        bool hasUpsertClause = false;
        std::vector<std::string> upsertConflictColumns;
        AstNodePointer upsertConflictWhere;
        InsertUpsertAction upsertAction = InsertUpsertAction::none;
        std::vector<UpdateAssignment> upsertUpdateAssignments;
        AstNodePointer upsertUpdateWhere;

        std::vector<ReturningColumn> returning;

        InsertNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const InsertNode*>(&other);
            if(!o || this->replaceInto != o->replaceInto || this->orConflict != o->orConflict ||
               this->schemaName != o->schemaName || this->tableName != o->tableName ||
               this->columnNames != o->columnNames || this->dataKind != o->dataKind ||
               this->hasUpsertClause != o->hasUpsertClause ||
               this->upsertConflictColumns != o->upsertConflictColumns ||
               this->upsertAction != o->upsertAction ||
               this->upsertUpdateAssignments != o->upsertUpdateAssignments ||
               this->returning != o->returning) {
                return false;
            }
            if(this->valueRows.size() != o->valueRows.size()) return false;
            for(size_t i = 0; i < this->valueRows.size(); ++i) {
                const auto& a = this->valueRows[i];
                const auto& b = o->valueRows[i];
                if(a.size() != b.size()) return false;
                for(size_t j = 0; j < a.size(); ++j) {
                    if(!astNodesEqual(a[j], b[j])) return false;
                }
            }
            if(!astNodesEqual(this->selectStatement, o->selectStatement)) return false;
            if(!astNodesEqual(this->upsertConflictWhere, o->upsertConflictWhere)) return false;
            return astNodesEqual(this->upsertUpdateWhere, o->upsertUpdateWhere);
        }
    };

    struct UpdateNode : AstNode {
        ConflictClause orConflict = ConflictClause::none;
        std::optional<std::string> schemaName;
        std::string tableName;
        std::vector<UpdateAssignment> assignments;
        std::vector<FromClauseItem> fromClause;
        AstNodePointer whereClause;
        std::vector<ReturningColumn> returning;

        UpdateNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const UpdateNode*>(&other);
            if(!o || this->orConflict != o->orConflict || this->schemaName != o->schemaName ||
               this->tableName != o->tableName || this->assignments != o->assignments ||
               this->fromClause != o->fromClause || this->returning != o->returning) {
                return false;
            }
            return astNodesEqual(this->whereClause, o->whereClause);
        }
    };

    struct DeleteNode : AstNode {
        std::optional<std::string> schemaName;
        std::string tableName;
        AstNodePointer whereClause;
        std::vector<ReturningColumn> returning;

        DeleteNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const DeleteNode*>(&other);
            if(!o || this->schemaName != o->schemaName || this->tableName != o->tableName ||
               this->returning != o->returning)
                return false;
            return astNodesEqual(this->whereClause, o->whereClause);
        }
    };

}  // namespace sqlite2orm
