#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_policy.h>
#include <sqlite2orm/codegen_result.h>

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

    struct SourceTableColumn;
    std::vector<SourceTableColumn> sourceTableColumnsFromCreateTable(const CreateTableNode& createTable);

    void appendUniqueStrings(std::vector<std::string>& destination, const std::vector<std::string>& source);
    void appendUniqueWarnings(std::vector<CodegenWarning>& destination, const std::vector<CodegenWarning>& source);
    void appendUniqueString(std::vector<std::string>& destination, const std::string& value);

    std::string_view binaryOperatorString(BinaryOperator binaryOperator);
    std::string_view binaryFunctionalName(BinaryOperator binaryOperator);

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
     *  True when `value` denotes a decimal integer literal SQLite keeps a REAL — one past the int64
     *  range, any folded minus signs counted in. SQLite types a value by itself and applies column
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
     *  True when a signed 64-bit integer cannot hold a hex literal, i.e. it needs a seventeenth
     *  significant digit. SQLite refuses such a literal in `codeInteger()`, when it compiles an
     *  expression, so only the statements that never compile one accept it.
     */
    bool hexLiteralExceedsInt64(std::string_view integerLiteral);
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
     *  SQLite's `sqlite3GetBoolean()`: `on`/`yes`/`true` are true, and so is a number whose int32
     *  value has a non-zero low byte — `getSafetyLevel()` returns a u8, so `256` is false where
     *  `255` and `257` are true. Everything else, names included, is false.
     */
    bool sqlitePragmaBoolean(std::string_view valueText);
    /** Whether `valueText` is one of the boolean spellings SQLite documents (`0`/`1`, `ON`/`OFF`, …). */
    bool isCanonicalPragmaBooleanText(std::string_view valueText);

}  // namespace sqlite2orm
