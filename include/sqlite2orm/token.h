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
        plus,  // +
        minus,  // -
        star,  // *
        slash,  // /
        percent,  // %
        pipe2,  // ||
        eq,  // =
        eq2,  // ==
        ne,  // !=
        ltGt,  // <>
        lt,  // <
        le,  // <=
        gt,  // >
        ge,  // >=
        ampersand,  // &
        pipe,  // |
        tilde,  // ~
        shiftLeft,  // <<
        shiftRight,  // >>
        arrow,  // ->
        arrow2,  // ->>

        // Punctuation
        leftParen,  // (
        rightParen,  // )
        comma,  // ,
        dot,  // .
        semicolon,  // ;

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

    /**
     *  A position in the SQL, counted from 1. `column` counts characters rather than bytes: a
     *  multi-byte UTF-8 character moves it by one, so a diagnostic anchored here points at the
     *  same place a consumer holding the SQL as text does; a leading byte-order mark is not a
     *  character of its own and takes no column.
     */
    struct SourceLocation {
        size_t line = 1;
        size_t column = 1;

        bool operator==(const SourceLocation&) const = default;
    };

    /**
     *  A stretch of the SQL something was parsed from: where it starts and the text it covers. The
     *  text is a copy of the very characters a diagnostic about that something underlines, not a
     *  spelling built for the message, and it is owned: the AST a span sits on stays readable once
     *  the SQL it was parsed from is gone. Empty for anything no parse recorded a span for, a node
     *  a test builds by hand among them.
     */
    struct SourceSpan {
        SourceLocation location;
        std::string text;

        bool operator==(const SourceSpan&) const = default;
    };

    struct Token {
        TokenType type = TokenType::eof;
        /**
         *  The token as written, a view into the SQL it was tokenized from: that SQL has to outlive
         *  every token made from it. What a parse keeps, on the other hand, does not view the SQL —
         *  see `ParseResult`.
         */
        std::string_view value;
        SourceLocation location;

        bool operator==(const Token& other) const {
            return this->type == other.type && this->value == other.value;
        }
    };

    std::string_view tokenTypeName(TokenType type);

    std::optional<TokenType> keywordFromIdentifier(std::string_view word);

    /**
     *  Whether SQLite folds this keyword back into an identifier, so that it stands wherever the
     *  grammar asks for a name. Its parser does this with a `%fallback ID …` list, and the join
     *  keywords reach the same rule through `idj`. `ON`, `DELETE` and `DEFAULT` are not among
     *  these — they are reserved, and the rules that take them anyway, `nmnum` of a PRAGMA value
     *  for one, name them on top of a name.
     */
    bool isKeywordUsableAsName(TokenType type);

}  // namespace sqlite2orm
