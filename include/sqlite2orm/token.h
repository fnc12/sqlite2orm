#pragma once

#include <string_view>
#include <string>
#include <optional>
#include <cstddef>

namespace sqlite2orm {

    enum class TokenType {
        // Literals
        integerLiteral,
        realLiteral,
        stringLiteral,
        blobLiteral,

        // Identifiers (regular or quoted)
        identifier,

        // Bind parameters: ?, ?NNN, :name, @name, $name
        bindParameter,

        // Operators
        plus,            // +
        minus,           // -
        star,            // *
        slash,           // /
        percent,         // %
        pipe2,           // ||
        eq,              // =
        eq2,             // ==
        ne,              // !=
        ltGt,            // <>
        lt,              // <
        le,              // <=
        gt,              // >
        ge,              // >=
        ampersand,       // &
        pipe,            // |
        tilde,           // ~
        shiftLeft,       // <<
        shiftRight,      // >>
        arrow,           // ->
        arrow2,          // ->>

        // Punctuation
        leftParen,       // (
        rightParen,      // )
        comma,           // ,
        dot,             // .
        semicolon,       // ;

        // Keywords (alphabetical)
        kwAbort,
        kwAction,
        kwAdd,
        kwAfter,
        kwAll,
        kwAlter,
        kwAlways,
        kwAnalyze,
        kwAnd,
        kwAs,
        kwAsc,
        kwAttach,
        kwAutoincrement,
        kwBefore,
        kwBegin,
        kwBetween,
        kwBy,
        kwCascade,
        kwCase,
        kwCast,
        kwCheck,
        kwCollate,
        kwColumn,
        kwCommit,
        kwConflict,
        kwConstraint,
        kwCreate,
        kwCross,
        kwCurrent,
        kwCurrentDate,
        kwCurrentTime,
        kwCurrentTimestamp,
        kwDatabase,
        kwDefault,
        kwDeferrable,
        kwDeferred,
        kwDelete,
        kwDesc,
        kwDetach,
        kwDistinct,
        kwDo,
        kwDrop,
        kwEach,
        kwElse,
        kwEnd,
        kwEscape,
        kwExcept,
        kwExclude,
        kwExclusive,
        kwExcluded,
        kwExists,
        kwExplain,
        kwFail,
        kwFalse,
        kwFilter,
        kwFirst,
        kwFollowing,
        kwFor,
        kwForeign,
        kwFrom,
        kwFull,
        kwGenerated,
        kwGlob,
        kwGroup,
        kwGroups,
        kwHaving,
        kwIf,
        kwIgnore,
        kwImmediate,
        kwIn,
        kwIndex,
        kwIndexed,
        kwInitially,
        kwInner,
        kwInsert,
        kwInstead,
        kwIntersect,
        kwInto,
        kwIs,
        kwIsnull,
        kwJoin,
        kwKey,
        kwLast,
        kwLeft,
        kwLike,
        kwLimit,
        kwMatch,
        kwMaterialized,
        kwNatural,
        kwNo,
        kwNot,
        kwNothing,
        kwNotnull,
        kwNull,
        kwNulls,
        kwOf,
        kwOffset,
        kwOn,
        kwOr,
        kwOrder,
        kwOthers,
        kwOuter,
        kwOver,
        kwPartition,
        kwPlan,
        kwPragma,
        kwPreceding,
        kwPrimary,
        kwQuery,
        kwRaise,
        kwRange,
        kwRecursive,
        kwReferences,
        kwRegexp,
        kwReindex,
        kwRelease,
        kwRename,
        kwReplace,
        kwRestrict,
        kwReturning,
        kwRight,
        kwRollback,
        kwRow,
        kwRows,
        kwSavepoint,
        kwSelect,
        kwSet,
        kwStored,
        kwStrict,
        kwTable,
        kwTemp,
        kwTemporary,
        kwThen,
        kwTies,
        kwTo,
        kwTransaction,
        kwTrigger,
        kwTrue,
        kwUnbounded,
        kwUnion,
        kwUnique,
        kwUpdate,
        kwUsing,
        kwVacuum,
        kwValues,
        kwView,
        kwVirtual,
        kwWhen,
        kwWhere,
        kwWindow,
        kwWith,
        kwWithout,

        // End of input
        eof,
    };

    struct SourceLocation {
        size_t line = 1;
        size_t column = 1;

        bool operator==(const SourceLocation&) const = default;
    };

    struct Token {
        TokenType type = TokenType::eof;
        std::string_view value;
        SourceLocation location;

        bool operator==(const Token& other) const {
            return this->type == other.type && this->value == other.value;
        }
    };

    std::string_view tokenTypeName(TokenType type);

    std::optional<TokenType> keywordFromIdentifier(std::string_view word);

}  // namespace sqlite2orm
