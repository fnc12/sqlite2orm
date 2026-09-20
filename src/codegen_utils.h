#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_policy.h>
#include <sqlite2orm/codegen_result.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sqlite2orm {

    class CodeGeneratorContext;

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

    /**
     *  Whether generated code for a column DEFAULT may stand inside a `[[= default_value(…)]]`
     *  annotation. An annotation is a constant expression, so only a literal the compiler folds —
     *  an integer, a floating-point number or a boolean — qualifies. A text default is generated
     *  as a `const char*` pointing at a string literal, which is not one, and an expression
     *  default is not a literal at all.
     */
    bool isAnnotationConstantValueCode(std::string_view code);

    bool sqliteScalarFirstArgTextContext(std::string_view functionLower);
    std::string defaultCppTypeForSyntheticColumn(std::string_view cppIdentifier);

    extern const std::string kCommentCpp20ColumnAliases;
    extern const std::string kCommentViewReflection;
    extern const std::string kCommentTableReflection;
    extern const std::string kCommentNegationAsZeroMinus;
    extern const std::string kCommentPredicateGroupingCast;
    extern const std::string kCommentNotColumnPointer;
    extern const std::string kCommentNotValueAddedToZero;
    extern const std::string kCommentNegatedConditionCast;
    extern const std::string kCommentConcatenationCast;
    extern const std::string kCommentBitwiseResultCast;
    extern const std::string kCommentOrTokenCallSpelling;
    extern const std::string kCommentAndOrPredicateArgumentCast;

    struct SourceTableColumn;
    std::vector<SourceTableColumn> sourceTableColumnsFromCreateTable(const CreateTableNode& createTable);

    void appendUniqueStrings(std::vector<std::string>& destination, const std::vector<std::string>& source);
    void appendUniqueWarnings(std::vector<CodegenWarning>& destination, const std::vector<CodegenWarning>& source);
    void appendUniqueString(std::vector<std::string>& destination, const std::string& value);

    std::string_view binaryOperatorString(BinaryOperator binaryOperator);
    std::string_view binaryFunctionalName(BinaryOperator binaryOperator);
    /**
     *  SQLite's spelling of `binaryOperator` when the sqlite_orm type it is generated as has no
     *  default constructor, and an empty view otherwise. The comparisons, AND and OR all produce a
     *  `binary_condition`, which declares one; every arithmetic, bit and concatenation operator
     *  produces a `binary_operator` and the JSON arrows a `builtin_function_t`, which do not.
     */
    std::string_view binaryOperatorWithoutDefaultConstructor(BinaryOperator binaryOperator);

    /**
     *  True when a call of `lowerFunctionName` — written with a star for its argument list or with
     *  arguments of its own — is generated as a sqlite_orm type that has a default constructor. Most
     *  functions are generated as a `builtin_function_t` or a `built_in_aggregate_function_t`, which
     *  declare a constructor and no default one; the window functions are each generated as an
     *  aggregate of their own — `row_number_t`, `lag_t` and the rest — and a MATCH in its function
     *  spelling as `match_t`, an aggregate too. `count(*)` is a `count_asterisk_t`, which holds
     *  nothing. A star under any other name is generated as `name()`, a form only the window
     *  functions that take no argument have.
     */
    bool functionCallHasDefaultConstructor(std::string_view lowerFunctionName, bool star);

    /**
     *  True when a call of `lowerFunctionName` is generated as a form sqlite_orm gives no
     *  `filter()`: the window functions and a MATCH in its function spelling hold their arguments
     *  and an `over()` and nothing else, while `count(*)` and the aggregate function calls do have
     *  one. SQLite refuses a FILTER on the same calls — `FILTER clause may only be used with
     *  aggregate window functions` — but stores a trigger or a view that holds one.
     */
    bool functionCallFormHasNoFilter(std::string_view lowerFunctionName);

    /**
     *  True when the node generates a sqlite_orm condition, i.e. a type deriving from
     *  `internal::condition_t`: a comparison, AND, OR, IN, BETWEEN, LIKE, GLOB, IS [NOT] NULL,
     *  EXISTS or NOT. MATCH is not one of them — `match_t` derives from nothing.
     */
    bool generatesSqliteOrmCondition(const AstNode& astNode);
    /**
     *  True when the node has to be generated as a call — `or_(…)` or `conc(…)` — because the C++
     *  token `||` would build the other sqlite_orm node than the SQL operator stands for. C++
     *  spells `or` and the concatenation alike, and sqlite_orm's two `operator||` overloads pick
     *  between `or_condition_t` and `conc_t` by the operands: an OR over operands that are not
     *  conditions comes out a concatenation, and a concatenation over an operand that is one comes
     *  out an OR. The call form names the node it builds, so it says which operator was written.
     */
    bool binaryOperatorNeedsCallSpelling(const BinaryOperatorNode& binaryOperatorNode);

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
    /** Precedence SQLite gives `AND`, the loosest rank but one. */
    inline constexpr int kSqlPrecedenceAnd = 8;
    /** Precedence SQLite gives `OR`, the loosest rank of all. */
    inline constexpr int kSqlPrecedenceOr = 9;
    /**
     *  Precedence SQLite parses `binaryOperator` with, numbered as its own operator table ranks it:
     *  the smaller the number, the tighter it binds. This is the SQL the serialized statement is
     *  read back with, where `cppOperatorPrecedence` is the C++ the generated code is compiled with.
     */
    int sqlOperatorPrecedence(BinaryOperator binaryOperator);
    /**
     *  Precedence of the SQL sqlite_orm serializes the node's generated code into, as that SQL
     *  stands by itself — what the node *serializes as*, before any parentheses the enclosing
     *  serializer puts around it. `kSqlPrecedenceTerm` is for SQL that reads as one term: a
     *  literal, a column, a call, CAST, CASE, a parenthesized subquery.
     */
    int serializedSqlPrecedence(const AstNode& astNode);
    /**
     *  The same precedence seen from a binary operator or condition around the node — what that
     *  parent *parenthesizes*. sqlite_orm's binary serializer puts parentheses around an operand
     *  that is itself a binary operator or condition, so such an operand reads as one term there
     *  however loosely SQLite binds it; everything else it leaves bare. The predicate serializers
     *  parenthesize no argument at all, which is why their slots ask `serializedSqlPrecedence`.
     */
    int serializedSqlPrecedenceAsBinaryOperand(const AstNode& astNode);
    /**
     *  True when the node stands for an AND or an OR, the two operators SQLite binds looser than
     *  every predicate. A predicate serializer leaves its argument bare, so such an argument takes
     *  the predicate into itself: `is_null(or_(1, 0))` comes out `1 OR 0 IS NULL`, which SQLite
     *  reads as `1 OR (0 IS NULL)`, and `between(1, or_(1, 0), 3)` comes out
     *  `1 BETWEEN 1 OR 0 AND 3`, which SQLite refuses as a syntax error. The `cast<int64_t>`
     *  wrapper delimits it and leaves what it stands for alone — an AND and an OR are 0, 1 or
     *  NULL, and a CAST to INTEGER keeps all three.
     */
    bool predicateArgumentNeedsGroupingCast(const AstNode& astNode);

    std::string normalizeSqlIdentifier(std::string_view sqlIdentifier);

    bool endsWith(std::string_view text, std::string_view suffix);
    /** The `...` of `auto <variableName> = storage.select(...);`, if `generated` has exactly that form. */
    std::optional<std::string> extractStorageSelectArgument(std::string_view generated, std::string_view variableName);
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
     *  True when a `bool` field is known to hold exactly the value SQLite gives `value`: a BOOLEAN
     *  column has NUMERIC affinity, which leaves every number the way SQLite typed it, while the
     *  field holds no number besides 0 and 1 and turns `2` into `1`. TRUE and FALSE are the 1 and
     *  0 SQLite stores for them, so they pass.
     */
    bool boolFieldCarriesValue(const AstNode& value);
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
     *  Characters of `sourceText` a warning anchored at its first character underlines. A consumer
     *  draws the underline from the warning's location along a single line, so a span written
     *  across lines — a quoted name or a string literal holding a newline, a pair of keywords split
     *  by one — is underlined up to the end of the line it starts on and no further.
     */
    size_t underlineLengthOf(std::string_view sourceText);
    /**
     *  `message` anchored at the source span `astNode` was parsed from, so that a consumer
     *  underlines the very SQL the message is about. Unanchored for a node carrying no span, which
     *  is a node no parse built — one a test constructed by hand, say.
     */
    CodegenWarning sourceSpanWarning(std::string message, const AstNode& astNode);
    /**
     *  The code generated in place of a construct sqlite_orm has no form for: a `/*` … `*\/`
     *  placeholder named by `label`, and `message` anchored at the construct appended to the
     *  warnings of `carried`, which brings along whatever was collected before the construct turned
     *  out to be unmappable. Such a placeholder stands where an EXPRESSION would have — inside the
     *  code of something larger, where a comment is not an expression and the generated code does
     *  not compile — so it is recorded in `context` as one, and the statement-level entry points
     *  leave the statement holding it out whole rather than hand out a header that cannot be built.
     *  Going through one funnel is what keeps every placeholder saying which SQL to underline and
     *  where it stands (`codegen: every generated placeholder is funnelled through
     *  unsupportedPlaceholder` pins that). The placeholders standing for a WHOLE statement that do
     *  not come here at all are the PRAGMA ones and the `CREATE TABLE` / `CREATE VIEW` headers:
     *  each leaves the generated code compiling and already carries the warning saying why the
     *  statement generated nothing.
     */
    CodeGenResult unsupportedPlaceholder(CodeGeneratorContext& context,
                                         std::string_view label,
                                         std::string message,
                                         const AstNode& astNode,
                                         CodeGenResult carried = {});
    /** A placeholder's message built from the placeholder text itself. */
    using PlaceholderMessage = std::function<std::string(const std::string& placeholder)>;
    /**
     *  The same, for a message that quotes the placeholder text itself: `message` is handed the
     *  `/*` … `*\/` the placeholder generates, so that the shape of a placeholder stays known to
     *  this one function and a caller cannot spell a second one of its own.
     */
    CodeGenResult unsupportedPlaceholder(CodeGeneratorContext& context,
                                         std::string_view label,
                                         const PlaceholderMessage& message,
                                         const AstNode& astNode,
                                         CodeGenResult carried = {});
    /**
     *  The same funnel for a placeholder that is the WHOLE code of its statement: a `/*` … `*\/`
     *  line of its own compiles, and it is the one thing left saying, in the generated file, that
     *  a statement was read and not mapped. A generator whose code is embedded by whoever asked
     *  for it — a trigger step, say — has to treat one of these as unmappable all the same, which
     *  is what `CodeGeneratorContext::placeheldSince` is for.
     */
    CodeGenResult unsupportedStatementPlaceholder(CodeGeneratorContext& context,
                                                  std::string_view label,
                                                  std::string message,
                                                  const AstNode& astNode,
                                                  CodeGenResult carried = {});
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
    /**
     *  True for a node generated as sqlite_orm's `conc_t`, which every SQL `||` that really is a
     *  concatenation comes out as — the `left || right` spelling and the `conc(left, right)` one
     *  alike. `conc_t` is `binary_operator<L, R, conc_string>` and nothing else: not negatable, not
     *  an arithmetic operand, not an operator argument, so nothing can be built on top of one
     *  without delimiting it first.
     */
    bool generatesConcatenation(const AstNode& astNode);
    /** True for a node that generates a bare C++ value, which `wrap` turns into a sqlite_orm expression. */
    bool isLeafNode(const AstNode& astNode);
    std::string wrap(std::string_view code);

    /**
     *  Whether SQLite can answer `astNode` with NULL. Conservative: only a node whose value is
     *  spelled out in the SQL — a literal, or an operator, a predicate or a CAST over such operands
     *  — is ruled out, and everything SQLite computes at runtime counts as nullable. `/` and `%`
     *  count whatever their operands are, because SQLite answers a division by zero with NULL
     *  rather than an error, and `+`, `-` and `*` count once the double SQLite computes them in
     *  can run into a NaN — the one value it has no storage class for, and stores as NULL:
     *  `0 * (1e300 * 1e300)` and `1e300 * 1e300 - 1e300 * 1e300` are NULL over operands that are
     *  none. A call is ruled out only for the built-ins that answer NULL for no
     *  reason other than a NULL argument; every other one — `nullif`, `date`, `unicode`, `sign`,
     *  `substr`, `printf`, `json_extract`, the math functions, and an aggregate over an empty
     *  rowset — counts as nullable whatever its arguments hold. `iif` propagates for its
     *  three-argument form alone: the `iif(X, Y)` SQLite 3.48 added is `iif(X, Y, NULL)`, NULL
     *  whenever X is false.
     */
    bool expressionMayBeNull(const AstNode& astNode);
    /**
     *  Whether a SELECT result column has to be generated as `as_optional(...)` for a NULL row to
     *  survive the round trip. sqlite_orm types a binary operator by the operator alone — `double`
     *  for the arithmetic ones, `std::string` for `||`, `bool` for a comparison —, a BETWEEN, an
     *  IN, a LIKE and a GLOB `bool`, a CAST the type the CAST asks for, and a built-in function
     *  call the return type that function declares. None of those can hold a NULL, so such a row
     *  is read back as 0 / "" / false. `as_optional` leaves the SQL untouched and yields
     *  `std::optional<T>` instead. Every other expression sqlite_orm already types nullably where
     *  it has to (a column carries its field's type, `abs(...)` is a `std::unique_ptr`, `max(...)`
     *  and `coalesce(...)` carry the type of an argument, NULL itself is a `std::nullptr_t`).
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
    /**
     *  The report for a unary plus that stands over a column reference on one side of a comparison,
     *  if `astNode` is one. A unary plus is an identity for the value, which is why codegen emits
     *  the operand and nothing else, but it is not one for the comparison around it: SQLite applies
     *  the affinity of a column to the other operand, and `+a` is no longer a column reference, so
     *  the comparison runs without it. Over `t(a TEXT)` holding '1', sqlite3 3.51 answers `a = 1`
     *  with 1 and `+a = 1` with 0, while the generated `c(&T::a) == 1` is the former either way.
     *  sqlite_orm has no spelling that takes a column's affinity away, so the loss is reported.
     *
     *  The affinity the column was declared with is not read here, so the report also fires where
     *  the plus costs nothing — over an INTEGER column, or one with no declared type, both sides
     *  answer the same — which is why it names what the plus takes away rather than asserting a
     *  divergence for the column in hand. Narrowing it to the columns whose affinity carries is
     *  card 1867077683549045974.
     */
    std::optional<CodegenWarning> comparisonUnaryPlusAffinityWarning(const AstNode& astNode);

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
        /** Start of the value in the source SQL, so a warning about it can be underlined. */
        SourceLocation location;
        /**
         *  Characters the value occupies from `location`: the value as written, its quotes and
         *  its minus sign included, which `ON` spells shorter than `text` reads it back as, and
         *  no further than the end of the line it starts on (see `underlineLengthOf`).
         */
        size_t length = 0;
    };

    /** The value of `PRAGMA name = <value>`, or nullopt for a node SQLite does not accept there. */
    std::optional<PragmaValue> pragmaValue(const AstNode& valueNode);
    /**
     *  `message` anchored at `value`, so that a consumer underlines the PRAGMA value the message is
     *  about. Unanchored for a value with no span of its own, as a node that is no PRAGMA value has.
     */
    CodegenWarning pragmaValueWarning(std::string message, const PragmaValue& value);
    CodegenWarning pragmaValueWarning(std::string message, const AstNode& valueNode);
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
