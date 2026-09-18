#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_policy.h>
#include <sqlite2orm/codegen_result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sqlite2orm {

    bool policyEquals(const CodeGenPolicy* policy, std::string_view category, std::string_view value);
    CodeGenPolicy policyWithOverride(const CodeGenPolicy* base, std::string_view category, std::string_view value);

    /** Target C++ standard from the policy (a null policy means the default, C++20). */
    int policyTargetCppStandard(const CodeGenPolicy* policy);
    /** Whether C++20-only variants may be offered/chosen under the policy's target standard. */
    bool cpp20Allowed(const CodeGenPolicy* policy);

    std::string colaliasBuiltinSlot(size_t slotIndex);

    std::string stripIdentifierQuotes(std::string_view identifier);
    std::string toCppIdentifier(std::string_view sqlName);
    std::string identifierToCppStringLiteral(std::string_view sqlIdentifier);

    /** C++ variable name for the RAII guard of a savepoint (`sp 1` -> `sp_1_savepoint`). */
    std::string savepointGuardVariableName(std::string_view savepointName);
    std::string sqlStringToCpp(std::string_view sqlString);

    std::string stripColumnAliasQuotes(std::string_view alias);
    bool isBuiltinColalias(std::string_view stripped);
    std::string columnAliasTypeName(std::string_view rawAlias);
    bool needsCustomAliasStruct(std::string_view rawAlias);
    std::string generateColumnAliasPreamble(const std::vector<SelectColumn>& columns);
    std::string columnAliasCpp20VarName(std::string_view rawAlias);
    std::string generateCpp20ColumnAliasPreamble(const std::vector<SelectColumn>& columns);
    std::string wrapWithColumnAlias(const std::string& expressionCode, const std::string& rawAlias, bool cpp20Style);
    bool hasAnyColumnAlias(const std::vector<SelectColumn>& columns);

    bool sqliteScalarFirstArgTextContext(std::string_view functionLower);
    std::string defaultCppTypeForSyntheticColumn(std::string_view cppIdentifier);

    extern const std::string kCommentCpp20ColumnAliases;
    extern const std::string kCommentViewReflection;
    extern const std::string kCommentNegationAsZeroMinus;
    extern const std::string kCommentPredicateGroupingCast;
    extern const std::string kCommentNotColumnPointer;
    extern const std::string kCommentNotValueAddedToZero;
    extern const std::string kCommentNegatedConditionCast;
    extern const std::string kCommentBitwiseResultCast;

    struct SourceTableColumn;
    std::vector<SourceTableColumn> sourceTableColumnsFromCreateTable(const CreateTableNode& createTable);

    void appendUniqueStrings(std::vector<std::string>& destination, const std::vector<std::string>& source);
    void appendUniqueWarnings(std::vector<CodegenWarning>& destination, const std::vector<CodegenWarning>& source);
    void appendUniqueString(std::vector<std::string>& destination, const std::string& value);

    std::string_view binaryOperatorString(BinaryOperator binaryOperator);
    std::string_view binaryFunctionalName(BinaryOperator binaryOperator);

    /** Precedence of code that is one C++ term already, so no operator around it can regroup it. */
    inline constexpr int kCppPrecedencePrimary = 0;
    /**
     *  Precedence of the C++ operator `binaryOperator` is emitted as, numbered as the C++ grammar
     *  ranks it: the smaller the number, the tighter it binds. It has little to do with the SQL
     *  precedence the parser applied — SQL's `||` binds tightest of the binary operators and C++'s
     *  binds loosest — so the generated grouping has to be spelled out rather than inherited.
     */
    int cppOperatorPrecedence(BinaryOperator binaryOperator);
    /**
     *  Precedence of the top-level C++ operator the node's generated code carries, or
     *  `kCppPrecedencePrimary` when that code is a literal, a call, a unary expression or an
     *  already parenthesized one. Asking the AST is what keeps this out of the generated string.
     */
    int generatedCppPrecedence(const AstNode& astNode, const CodeGenPolicy* policy);

    /** Precedence of SQL that already reads as one term: parenthesized, a literal, a call, a column. */
    inline constexpr int kSqlPrecedenceTerm = 0;
    /** Precedence SQLite gives `=` and the predicates that share its rank (`IN`, `LIKE`, `IS NULL`, …). */
    inline constexpr int kSqlPrecedencePredicate = 6;
    /** Precedence SQLite gives `NOT expr`, one rank looser than the predicates. */
    inline constexpr int kSqlPrecedenceNot = 7;
    /**
     *  Precedence SQLite parses `binaryOperator` with, numbered as its own operator table ranks it:
     *  the smaller the number, the tighter it binds. This is the SQL the serialized statement is
     *  read back with, where `cppOperatorPrecedence` is the C++ the generated code is compiled with.
     */
    int sqlOperatorPrecedence(BinaryOperator binaryOperator);
    /**
     *  Precedence of the SQL sqlite_orm serializes the node's generated code into, or
     *  `kSqlPrecedenceTerm` when that SQL reads as one term. Only the predicates sqlite_orm leaves
     *  unparenthesized — the ones `sqlPredicateLooserThanMinus` names — answer anything else.
     */
    int serializedSqlPrecedence(const AstNode& astNode);

    std::string normalizeSqlIdentifier(std::string_view sqlIdentifier);

    bool endsWith(std::string_view text, std::string_view suffix);
    /** The `...` of `auto <variableName> = storage.select(...);`, if `generated` has exactly that form. */
    std::optional<std::string> extractStorageSelectArgument(std::string_view generated,
                                                            std::string_view variableName);
    std::string stripStoragePrefixAndTrailingSemicolon(std::string code);

    std::string blobToCpp(std::string_view blobLiteral);
    /** SQL numeric literal to C++: SQLite's `_` digit separators become C++'s `'` (1_000 -> 1'000). */
    std::string numericLiteralToCpp(std::string_view numericLiteral);
    /** Same for an integer literal, whose leading zeros C++ would read as an octal prefix (`010` is 10 in SQLite, 8 in C++). */
    std::string integerLiteralToCpp(std::string_view integerLiteral);
    /**
     *  True when SQLite reads a decimal integer literal as a REAL because an int64 cannot hold it.
     *  `negated` tells whether a minus sign is folded into the literal, which moves the limit by
     *  one: SQLite gives `-9223372036854775808` an INTEGER and `9223372036854775808` a REAL.
     */
    bool integerLiteralExceedsInt64(std::string_view integerLiteral, bool negated = false);
    /**
     *  `value` with the signs standing in front of it taken off, i.e. the node they apply to.
     *  SQLite's parser drops a unary plus altogether, so that `-+5` is the `-5` it prints, and
     *  folds a minus into the literal behind it. `foldedMinusSigns` receives how many minus signs
     *  stood there: only the innermost one goes into the literal, and SQLite computes the rest
     *  while it runs the statement, where negating the int64 minimum leaves the integer range.
     */
    const AstNode* withoutFoldedSigns(const AstNode& value, std::size_t& foldedMinusSigns);
    /**
     *  True when `value` denotes a decimal integer literal SQLite keeps a REAL — one past the int64
     *  range with the innermost folded sign in it, or one an outer sign takes out of the range,
     *  which only the int64 minimum reaches. SQLite types a value by itself and applies column
     *  affinity only afterwards, so such a literal stays a REAL even in an INTEGER column, where a
     *  C++ `int64_t` field would convert it.
     */
    bool isIntegerLiteralPastIntegerFieldRange(const AstNode& value);
    /**
     *  True when an `int64_t` field is known to hold exactly the value SQLite gives `value`: false
     *  for a decimal integer literal past the int64 range and for every REAL literal, whose storage
     *  class depends on an affinity conversion SQLite alone decides, true for anything else.
     */
    bool integerFieldCarriesValue(const AstNode& value);
    /**
     *  True when a `double` field is known to hold exactly the value SQLite gives `value`: false
     *  for an integer literal inside the int64 range that no `double` holds exactly, because the
     *  object form brace-initializes the field and a braced initializer refuses a constant it
     *  would narrow, true for anything else.
     */
    bool doubleFieldCarriesValue(const AstNode& value);
    /**
     *  The storage class SQLite gives a value before it applies any column affinity, as far as the
     *  SQL spells it out. A value that is not a literal is an expression SQLite computes while it
     *  runs the statement, and `unknown` stands for it.
     */
    enum class ValueStorageClass {
        null,
        numeric,
        text,
        blob,
        unknown,
    };
    /** The storage class the literal `value` denotes; folded minus signs belong to the number. */
    ValueStorageClass valueStorageClass(const AstNode& value);
    /**
     *  The storage class a C++ field of `cppType` can be initialized from, for the types
     *  `sqliteTypeToCpp` gives a table column; `unknown` for any other type.
     */
    ValueStorageClass fieldTypeStorageClass(std::string_view cppType);
    /**
     *  The SQL text of the numeric literal `value` denotes, folded minus signs included and digit
     *  separators gone, the way SQLite spells it back in a diagnostic; empty for anything else.
     */
    std::string numericLiteralSqlText(const AstNode& value);
    /**
     *  `message` anchored at the numeric literal `value` denotes: the underline covers the literal
     *  token together with the minus signs folded into it, which `numericLiteralSqlText` quotes
     *  along with the digits, as far as they share the literal's line. Unanchored for anything
     *  else.
     */
    CodegenWarning numericLiteralWarning(std::string message, const AstNode& value);
    /**
     *  True when a 32-bit int cannot hold the value SQLite gives an integer literal, i.e. the field
     *  standing for it has to be an int64_t. A hex literal is measured after the wrap-around SQLite
     *  applies to it, so that `0xFFFFFFFFFFFFFFFF`, which is -1, still fits.
     *  `negated` tells whether a minus sign is folded into the literal, which moves the range by
     *  one: `-0x80000000` is the -2147483648 an int32 still holds, while `-0xFFFFFFFF80000000` is
     *  the 2147483648 it no longer does.
     */
    bool integerLiteralExceedsInt32(std::string_view integerLiteral, bool negated = false);
    /**
     *  True when a signed 64-bit integer cannot hold a hex literal, i.e. it needs a seventeenth
     *  significant digit. SQLite refuses such a literal in `codeInteger()`, when it compiles an
     *  expression, so only the statements that never compile one accept it.
     */
    bool hexLiteralExceedsInt64(std::string_view integerLiteral);
    /** True for an integer literal written with SQLite's `0x` prefix rather than in decimal. */
    bool isHexadecimalIntegerLiteral(std::string_view integerLiteral);
    /**
     *  The node whose generated code an operand's code really is. A COLLATE, which sqlite_orm has
     *  no form for, and a unary plus emit their operand and nothing else, so every question about
     *  the SHAPE of the generated operand — the `c(…)` wrap it needs, the C++ precedence it is
     *  topped by, the sqlite_orm node it comes out as — is a question about what stands under them.
     *  Questions about the SQL itself are not: SQLite's own parser reads a sign through parentheses
     *  but not through a COLLATE, which is why `negationFormFor` takes the operand as written.
     */
    const AstNode& generatedOperandNode(const AstNode& astNode);
    /** True for an integer or real literal, the two kinds a minus sign is folded into. */
    bool isNumericLiteral(const AstNode& astNode);
    /**
     *  True for an integer literal whose C++ constant cannot carry a folded-in minus sign, which is
     *  `0x8000000000000000` alone: it is INT64_MIN, so `-static_cast<int64_t>(0x8000000000000000)`
     *  overflows. SQLite refuses the same SQL with `hex literal too big`.
     */
    bool numericLiteralRejectsFoldedSign(const AstNode& astNode);
    /** The three shapes a unary minus is generated in, decided by its operand alone. */
    enum class NegationForm {
        /** The sign is folded into the numeric constant the operand generates, as SQLite's parser does. */
        foldedIntoConstant,
        /** `(c(0) - x)` / `sub(0, x)`, which is what SQLite computes for `-x`. */
        zeroMinusSubtraction,
        /** No form reproduces the negation; the unary minus stays and codegen warns. */
        unaryOverPredicate,
    };
    /** The shape a unary minus over `operand` is generated in — the single home of that rule. */
    NegationForm negationFormFor(const AstNode& operand);
    /**
     *  Name of the SQL predicate a node stands for when sqlite_orm serializes it without
     *  parentheses and SQLite binds it looser than a binary `-`, empty for every other node.
     *  Such an operand cannot carry the `0 - expr` spelling of a negation.
     */
    std::string_view sqlPredicateLooserThanMinus(const AstNode& astNode);
    /** True for a negation generated as a plain C++ constant, e.g. `-2` (see `isLeafNode`). */
    bool generatesFoldedNegation(const AstNode& astNode);
    /**
     *  True for a node generated as the parenthesized `(c(0) - …)` subtraction a negation becomes,
     *  which is already one C++ term and needs no parentheses of its own around it.
     */
    bool generatesZeroMinusSubtraction(const AstNode& astNode);
    /**
     *  True for a node generated as a plain C++ value that sqlite_orm binds into the prepared
     *  statement — a literal, or a numeric literal with a sign folded into it. Every other leaf
     *  names something (a column, NEW/OLD, a function call) and is serialized into the SQL itself.
     */
    bool generatesBoundValue(const AstNode& astNode);
    /**
     *  True for a node generated as sqlite_orm's `negated_condition_t`: a logical NOT, and the
     *  `!predicate` spelling a negated BETWEEN, LIKE, GLOB or MATCH takes. sqlite_orm classifies
     *  that type as neither negatable nor an operator argument, so nothing can be built on top of
     *  one without delimiting it first.
     */
    bool generatesNegatedCondition(const AstNode& astNode);
    /** True for a node that generates a bare C++ value, which `wrap` turns into a sqlite_orm expression. */
    bool isLeafNode(const AstNode& astNode);
    std::string wrap(std::string_view code);

    /**
     *  Whether SQLite can answer `astNode` with NULL. Conservative: only a node whose value is
     *  spelled out in the SQL — a literal, or an operator over such operands — is ruled out, and
     *  everything SQLite computes at runtime counts as nullable. `/` and `%` count whatever their
     *  operands are, because SQLite answers a division by zero with NULL rather than an error.
     */
    bool expressionMayBeNull(const AstNode& astNode);
    /**
     *  Whether a SELECT result column has to be generated as `as_optional(...)` for a NULL row to
     *  survive the round trip. sqlite_orm types a binary operator by the operator alone — `double`
     *  for the arithmetic ones, `std::string` for `||`, `bool` for a comparison — none of which can
     *  hold a NULL, so such a row is read back as 0 / "" / false. `as_optional` leaves the SQL
     *  untouched and yields `std::optional<T>` instead. Every other expression sqlite_orm already
     *  types nullably where it has to (a column carries its field's type, `abs(...)` is a
     *  `std::unique_ptr`, NULL itself is a `std::nullptr_t`).
     */
    bool selectResultNeedsAsOptional(const AstNode& astNode);
    /**
     *  Whether a SELECT result column has to be generated as `cast<int64_t>(...)` for the integer
     *  SQLite computes to reach the caller whole. sqlite_orm types the bitwise operators `int`, so
     *  a result outside the int32 range is truncated — `9223372036854775807 & -1` reads back as -1.
     *  SQLite answers `&`, `|`, `<<`, `>>` and `~` with an INTEGER or a NULL whatever their
     *  operands hold, and a CAST to INTEGER keeps both, `typeof` included, so the CAST only widens
     *  the C++ type. Every other operator answers a REAL for some operands, where a CAST would
     *  truncate the value instead of carrying it.
     */
    bool selectResultNeedsIntegerCast(const AstNode& astNode);
    /**
     *  The warning a SELECT result column whose value sqlite_orm reads back through a `double`
     *  carries, or nullopt when a `double` is known to hold it. sqlite_orm types `+`, `-`, `*`,
     *  `/` and `%` as `double` whatever their operands are, while SQLite answers them with an
     *  INTEGER whenever the operands are integers, so an integer result past the range a double
     *  holds exactly comes back rounded. Nothing in sqlite_orm reads such a column as an int64
     *  without a CAST, and a CAST would truncate the REAL results of the very same operators, so
     *  the generated code is left alone and the loss is reported instead. The bound the report is
     *  left out on is an upper bound on the magnitude of an INTEGER answer, computed in the int64
     *  domain SQLite computes in rather than in the `double` the warning is about; a REAL or a
     *  NULL operand takes the expression out of it altogether, since these operators answer a REAL
     *  or a NULL whenever an operand is one.
     */
    std::optional<CodegenWarning> selectResultDoublePrecisionWarning(const AstNode& astNode);

    std::string sqliteTypeToCpp(std::string_view typeName);
    std::string defaultInitializer(std::string_view cppType);
    std::string toStructName(std::string_view sqlName);

    std::string_view joinSqliteOrmApiName(JoinKind joinKind);
    std::string_view compoundSelectApi(CompoundSelectOperator compoundOperator);
    std::string dmlInsertOrPrefix(ConflictClause conflictClause);

    std::optional<std::string> journalModeSqlTokenToCppEnum(std::string_view token);
    std::optional<std::string> lockingModeSqlTokenToCppEnum(std::string_view token);
    std::optional<std::string> pragmaTableNameLiteral(const AstNode& valueNode);
    std::optional<std::string> pragmaJournalOrLockingValueToken(const AstNode& valueNode);
    /** The value of `PRAGMA name = <value>`, both as SQLite reads it and as the user wrote it. */
    struct PragmaValue {
        /** What SQLite sees: a string literal's contents, a name with its quotes removed. */
        std::string text;
        /** The same value as written in the SQL, quotes and all, for diagnostics. */
        std::string sqlText;
    };

    /** The value of `PRAGMA name = <value>`, or nullopt for a node SQLite does not accept there. */
    std::optional<PragmaValue> pragmaValue(const AstNode& valueNode);
    /**
     *  SQLite's `sqlite3GetInt32()`, which is how a PRAGMA value reaches every PRAGMA that takes a
     *  number: the leading decimal — or `0x…` hexadecimal — digits of `valueText` as an int32.
     *  Nullopt where SQLite refuses the text, which `sqlite3Atoi()` turns into 0 for `user_version`
     *  and friends and `PRAGMA integrity_check` takes for a table name. A hexadecimal value with
     *  the sign bit set is refused, and so is a decimal one of more than ten digits or above
     *  2147483647, which is why `PRAGMA user_version = 0x80000000` and `= 2147483648` both set 0.
     *  A sign shuts the hexadecimal branch off, the way it does in SQLite, so `-0x10` is 0 — though
     *  a PRAGMA value never reaches here with a leading `+`, which SQLite's grammar drops.
     */
    std::optional<std::int32_t> sqlitePragmaInt32(std::string_view valueText);
    /**
     *  SQLite's `sqlite3GetBoolean()`: `on`/`yes`/`true` are true, and so is a number whose int32
     *  value has a non-zero low byte — `getSafetyLevel()` returns a u8, so `256` is false where
     *  `255` and `257` are true. Everything else, names included, is false.
     */
    bool sqlitePragmaBoolean(std::string_view valueText);
    /** Whether `valueText` is one of the boolean spellings SQLite documents (`0`/`1`, `ON`/`OFF`, …). */
    bool isCanonicalPragmaBooleanText(std::string_view valueText);

}  // namespace sqlite2orm
