#pragma once

#include <sqlite2orm/token.h>

#include <memory>

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

    inline bool astNodesEqual(const AstNodePointer& a, const AstNodePointer& b) {
        return (!a && !b) || (a && b && *a == *b);
    }

    enum class CurrentDatetimeKind { time, date, timestamp };

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

    enum class UnaryOperator {
        minus,
        plus,
        bitwiseNot,
        logicalNot,
    };

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

    enum class TriggerTiming { before, after, insteadOf };
    enum class TriggerEventKind { delete_, insert_, update_, updateOf };
    enum class RaiseKind { ignore, rollback, abort, fail };

    enum class SortDirection { none, asc, desc };

    enum class BeginTransactionMode { plain, deferred, immediate, exclusive };

    enum class DropObjectKind { table, index, trigger, view };

    enum class NullsOrdering { none, first, last };

    enum class WindowFrameUnit { rows, range, groups };
    enum class WindowFrameExcludeKind { none, currentRow, group, ties };

    enum class WindowFrameBoundKind {
        unboundedPreceding,
        exprPreceding,
        currentRow,
        exprFollowing,
        unboundedFollowing
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

    enum class CompoundSelectOperator {
        unionDistinct,
        unionAll,
        intersect,
        except,
    };

    enum class CteMaterialization { none, materialized, notMaterialized };

    enum class InsertDataKind { values, defaultValues, selectQuery };
    enum class InsertUpsertAction { none, doNothing, doUpdate };

}  // namespace sqlite2orm
