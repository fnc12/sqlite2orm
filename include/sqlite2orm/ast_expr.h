#pragma once

#include <sqlite2orm/ast_base.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sqlite2orm {

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

    // --- Window / ORDER BY types ---

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

}  // namespace sqlite2orm
