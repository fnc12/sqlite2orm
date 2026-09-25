#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_policy.h>
#include <sqlite2orm/codegen_result.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sqlite2orm {

    class CodeGenerator;
    class CodeGeneratorContext;
    struct SourceTableColumn;

    bool policyEquals(const CodeGenPolicy* policy, std::string_view category, std::string_view value);
    CodeGenPolicy policyWithOverride(const CodeGenPolicy* base, std::string_view category, std::string_view value);

    /** Target C++ standard from the policy (a null policy means the default, C++20). */
    int policyTargetCppStandard(const CodeGenPolicy* policy);
    /** Whether C++20-only variants may be offered/chosen under the policy's target standard. */
    bool cpp20Allowed(const CodeGenPolicy* policy);

    std::string colaliasBuiltinSlot(size_t slotIndex);

    std::string stripIdentifierQuotes(std::string_view identifier);
    /**
     *  `sqlName` as a C++ identifier: an SQL name reaches the generated code as the name of a
     *  struct member or of a variable, and SQLite takes names C++ does not. The rewriting works
     *  character by character rather than byte by byte, so a name written in a script of its own
     *  keeps as many characters as it was written with — `üü` and `ää` are two characters each,
     *  not four bytes each, and the two names stay apart instead of both collapsing into the same
     *  member. An ASCII letter, digit or `_` stands for itself; any other ASCII character becomes
     *  `_`; and a character C++ has no letter for is spelled the way C++ spells a universal
     *  character name, `u` and four hex digits (`ü` → `u00FC`) or `U` and eight above the basic
     *  multilingual plane (`🙂` → `U0001F642`), with a byte that is no character at all — SQLite
     *  takes those in an identifier too — spelled `x` and its two hex digits.
     *
     *  Distinct names can still meet here: `a b` and `a-b` are both `a_b`, and a name spelled
     *  `u00FC` in ASCII is what `ü` is rewritten to. Whoever names the members of one struct
     *  reports that through `recordMemberName()` — the rewriting itself cannot, as it sees one
     *  name at a time.
     */
    std::string toCppIdentifier(std::string_view sqlName);
    std::string identifierToCppStringLiteral(std::string_view sqlIdentifier);

    /** C++ variable name for the RAII guard of a savepoint (`sp 1` -> `sp_1_savepoint`). */
    std::string savepointGuardVariableName(std::string_view savepointName);
    std::string sqlStringToCpp(std::string_view sqlString);
    /** Contents of a quoted SQL string literal, with doubled quotes collapsed (`'it''s'` -> `it's`). */
    std::string sqlStringLiteralText(std::string_view literal);
    /** `text` as a C++ string literal, quotes included. */
    std::string cppStringLiteral(std::string_view text);

    /**
     *  The order the arguments of a generated `make_storage()` are written in.
     *
     *  sqlite_orm up to and including the v1.9.1 release syncs the database objects of a storage in
     *  reverse declaration order, so an index or a trigger written after the table it is made for
     *  reaches SQLite first and `sync_schema()` throws `no such table`. Revisions after the release
     *  sort the objects by dependency and take either order. The order valid on both is therefore
     *  the reverse of the creation order: the indexes and the triggers first, the tables and the
     *  views they are made for after them.
     */
    template<class Argument>
    std::vector<Argument> storageArgumentOrder(std::vector<Argument> tablesAndViews,
                                               std::vector<Argument> indexesAndTriggers) {
        std::vector<Argument> ordered = std::move(indexesAndTriggers);
        ordered.insert(ordered.end(),
                       std::make_move_iterator(tablesAndViews.begin()),
                       std::make_move_iterator(tablesAndViews.end()));
        return ordered;
    }

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
     *  The span of the alias of the first result column written with one, and an empty span when no
     *  column carries one the parse recorded: a hint about the style every alias of a select is
     *  generated in underlines the first alias it is read on.
     */
    SourceSpan firstColumnAliasSpan(const std::vector<SelectColumn>& columns);

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
    /**
     *  The one C++ type that carries every value two inferred types hold, for the types
     *  `CodeGeneratorContext::inferTypeFromNode` names: `bool` widens to `int`, `int` to
     *  `int64_t` and to `double`, and anything to `std::string`, which reads back every storage
     *  class SQLite has. `int64_t` and `double` widen into neither one another nor a third
     *  number — a `double` drops every integer past 2^53, an `int64_t` the fractional part of a
     *  REAL — so the type over that pair is the `std::string` that keeps both. SQLite types a
     *  value rather than an expression, so a CASE answers with whichever branch matched and the
     *  single C++ type it is read through has to hold all of them. The `std::vector<char>` of a
     *  BLOB column sits over `std::string`: it reads every storage class whole, bytes and all,
     *  where an `std::string` stops at a NUL byte. A type the order does not name
     *  never reaches here; `left` comes back unchanged so that a fold over one stays total.
     */
    std::string widerInferredCppType(std::string_view left, std::string_view right);

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
    extern const std::string kCommentAndOrQuotedOperand;
    extern const std::string kCommentAndOrPredicateArgumentCast;
    extern const std::string kCommentBetweenBoundsWidened;
    extern const std::string kCommentInValuesWidened;
    extern const std::string kCommentAliasedFromSources;

    /**
     *  Whether the member `column` is mapped to holds a `std::optional`. The answer belongs to the
     *  database column alone: `sync_schema()` compares a mapped column against `PRAGMA
     *  table_info`'s `notnull` flag, and sqlite_orm reads that flag off the member's type, so a
     *  member promising more than the stored column does makes sqlite_orm see a changed column and
     *  drop the table to rebuild it — with everything in it.
     *
     *  A PRIMARY KEY is not NOT NULL by itself: in an ordinary rowid table SQLite keeps the
     *  historical behaviour of letting a NULL into a PRIMARY KEY column (an INTEGER PRIMARY KEY,
     *  being the rowid alias, turns it into the next rowid instead), and `PRAGMA table_info`
     *  reports `notnull = 0` for it. Two table options take that back:
     *  - WITHOUT ROWID makes every column of the key implicitly NOT NULL, whether the key is
     *    spelled on the column or on the table;
     *  - STRICT does the same, except for the rowid alias, which stays nullable.
     *  Checked against sqlite3 3.51.0 through `PRAGMA table_info`.
     */
    bool columnMemberIsNullable(const CreateTableNode& createTable, const ColumnDef& column);

    /**
     *  The C++ type the member `column` is mapped to. It is `sqliteTypeToCpp` of the declared type
     *  everywhere but on `ANY` in a STRICT table, the one place where SQLite reads a type name as
     *  something other than an affinity: the column takes a value of any storage class and stores
     *  it as it came, so the affinity rule — which has nothing for `ANY` and falls through to
     *  NUMERIC, that is `double` — is not what the column holds. A CAST is left to
     *  `sqliteTypeToCpp`, because there `ANY` *is* the affinity rule: sqlite3 3.51 answers
     *  `CAST('x' AS ANY)` with the integer 0, exactly as it answers `CAST('x' AS NUMERIC)`.
     *
     *  sqlite_orm has no type that holds a storage class of its own, so the mapping for `ANY` is
     *  `std::vector<char>`, the one type whose extractor reads a value of every storage class
     *  rather than one kind of value and zeroes the rest. What that costs is
     *  `anyColumnTypeWarning`'s to report.
     */
    std::string sqliteColumnTypeToCpp(const CreateTableNode& createTable, const ColumnDef& column);

    /**
     *  The report for a column declared `ANY`, if `column` is one. ANY is a datatype of STRICT
     *  tables only, and the two tables owe the reader different things:
     *  - in a STRICT table the column really does hold any storage class, and the
     *    `std::vector<char>` it maps to reads all of them — as the bytes SQLite renders the value
     *    as, which is not the value's storage class and, for a REAL, not all of its digits;
     *  - anywhere else `ANY` is a type name SQLite does not know, so the column gets NUMERIC
     *    affinity and maps to `double`, and the text NUMERIC affinity could not convert stays
     *    text in the column and reads back as 0.
     *  The span covers the type name, so a consumer underlines the `ANY` rather than the column.
     *  Checked against sqlite3 3.51.0 and the pinned sqlite_orm on a live database.
     */
    std::optional<CodegenWarning> anyColumnTypeWarning(const CreateTableNode& createTable, const ColumnDef& column);

    /**
     *  Whether a table-level PRIMARY KEY naming `column` and nothing besides it would make the
     *  column the rowid alias of `createTable` — the column SQLite stores the rowid itself in
     *  rather than beside. This is the rule `columnIsRowidAlias` reads a written key by, asked
     *  about a key that is not written that way: the generated table names a repeated key column
     *  once, so a `PRIMARY KEY(a, a)` — two terms, and never an alias — reaches sqlite_orm as a
     *  key over one column, which this answers for.
     *
     *  Everything about the answer is spelling: the declared type has to be INTEGER and nothing
     *  else (an `INT PRIMARY KEY` is an ordinary column with a rowid of its own behind it), the
     *  table must hold no other key, and a WITHOUT ROWID table has no rowid to alias at all. A
     *  DESC is not asked about: in the table-level form it leaves the alias, unlike the
     *  column-level `INTEGER PRIMARY KEY DESC`. Checked against sqlite3 3.51.0.
     */
    bool columnAloneInTableKeyIsRowidAlias(const CreateTableNode& createTable, const ColumnDef& column);

    struct SourceTableColumn;
    std::vector<SourceTableColumn> sourceTableColumnsFromCreateTable(const CreateTableNode& createTable);

    void appendUniqueWarnings(std::vector<CodegenWarning>& destination, const std::vector<CodegenWarning>& source);
    /**
     *  Appends the comments of `source` that `destination` has none of yet, compared by message
     *  alone as warnings are compared: a form met twice is reported once, with the anchor of the
     *  first of them, so a hint stands for the same thing wherever it is read from.
     */
    void appendUniqueComments(std::vector<CodegenComment>& destination, const std::vector<CodegenComment>& source);
    void appendUniqueComment(std::vector<CodegenComment>& destination, const CodegenComment& comment);

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
     *  The result type a generated call of `lowerFunctionName` spells between angle brackets —
     *  `"<std::string>"` — and an empty view for a call that spells none. `json_extract` and
     *  `json_quote` are the two sqlite_orm builtins declared with a result type parameter that has
     *  no default (`template<class R, class X, class... Args> json_extract(X, Args...)`), so a call
     *  generated without one does not compile at all: JSON_EXTRACT answers a value of whatever
     *  storage class the JSON holds, and there is nothing in the arguments to deduce that from.
     *  `std::string` is the type that reads every storage class back — sqlite_orm reads it through
     *  `sqlite3_column_text`, which renders an INTEGER or a REAL as its text — and the only one
     *  JSON_QUOTE ever answers.
     */
    std::string_view functionCallResultTypeArgument(std::string_view lowerFunctionName);

    /**
     *  The result type a generated call of `functionCall` spells between angle brackets, and an
     *  empty string for a call that spells none. On top of the two names above, this answers for
     *  the four builtins sqlite_orm declares as the COMMON TYPE of their arguments — COALESCE
     *  (`common_argument_type<>`), IFNULL and NULLIF (`common_argument_type<0, 1>`) and IIF
     *  (`common_argument_type<1, 2>`, the two branches and not the condition). SQLite takes any
     *  arguments there, its own result type being whichever branch it answers with, while
     *  `std::common_type` reduces the C++ types of two arguments only where one converts to the
     *  other: a text next to a number has no common type, and neither has a BLOB next to anything
     *  else, so the call — and with it the whole `storage.select(...)` around it — does not
     *  compile. Where the generated types say there is no common type, the call spells a type of
     *  its own, picked over the arguments that carry a value — a NULL carries none, SQLite
     *  answering such a call with one of the others, and the `std::optional` a nullable column
     *  arrives in is a wrapper around a value rather than a value:
     *  - where every one of them carries a NUMBER, the number they reduce to over the lattice
     *    `bool` < `int` < {`int64_t`, `double`} — the very type `std::common_type` would have
     *    answered had the NULLs and the wrappers not been in the way. `int64_t` and `double` are
     *    siblings with nothing above them: a `double` loses every integer past 2^53 and an
     *    `int64_t` the fractional part of a REAL, so that pair carries no number at all;
     *  - `std::vector<char>` where a BLOB takes part, which carries its bytes whole;
     *  - `std::string` otherwise, which reads every storage class back as its text — a number
     *    comes back as its digits, which is what `commonArgumentTypeWarning` reports.
     */
    std::string functionCallResultTypeArgument(const FunctionCallNode& functionCall,
                                               const CodeGeneratorContext& context);

    /**
     *  How a caller answers the schema column an argument of a call is read back through, and
     *  `nullptr` where it names none. The two models that ask for a spelled result type resolve a
     *  name differently — the emitter by the struct it writes the reference as a member of, the
     *  inferrer of a view's fields by the FROM clause of the view's own SELECT, which it holds
     *  and the context does not — while the rule that reduces the argument types to one is the
     *  same for both and lives in one place.
     */
    using ReferencedColumnResolver = std::function<const SourceTableColumn*(const AstNode&)>;

    /**
     *  The same answer without the angle brackets — `"std::string"`, and an empty string for a
     *  call that spells no result type. This is the type the row is read back into, so it is also
     *  the type a field holding that value has to be: a view column computed with such a call is
     *  inferred from here rather than from the argument the call is otherwise typed as.
     */
    std::string functionCallSpelledResultType(const FunctionCallNode& functionCall,
                                              const CodeGeneratorContext& context);

    /** The same answer, resolving a column argument with `resolveColumn` instead of the emitter's rule. */
    std::string functionCallSpelledResultType(const FunctionCallNode& functionCall,
                                              const ReferencedColumnResolver& resolveColumn);

    /** `"->"`, `"->>"` or an empty view for any other operator — the text a JSON arrow is written as. */
    std::string_view jsonArrowOperatorText(BinaryOperator binaryOperator);

    /**
     *  What a `->` generated as a JSON_EXTRACT call answers that `->` does not, underlined at
     *  `location`. sqlite_orm has no form for the operator itself.
     */
    CodegenWarning jsonTextArrowWarning(SourceLocation location);

    /**
     *  The report a JSON arrow carries whose path operand `jsonArrowPathExpansion` cannot expand,
     *  underlined at the operator.
     */
    CodegenWarning jsonArrowPathNotExpandedWarning(const BinaryOperatorNode& arrow);

    /**
     *  The JSON path `X -> P` and `X ->> P` look P up under, for a P the operand spells out, and
     *  nullopt for every other operand. The operators take an abbreviated path the JSON_EXTRACT
     *  call they are generated as does not: SQLite expands an INTEGER operand into `$[N]` (and a
     *  negative one into `$[#-N]`, counted from the right of the array), a text one starting with
     *  `$` into itself, one of nothing but ASCII letters, digits and `_` into `$.label`, one
     *  wrapped in brackets into `$[…]`, and every other one into `$."label"` — so
     *  `'{"x":5}' ->> 'x'` is 5 while `json_extract('{"x":5}', 'x')` is the error `bad JSON path`.
     *  Read off sqlite3 3.51 (`jsonExtractFunc`); an operand SQLite reads as a REAL or a BLOB is
     *  left unexpanded, along with every operand that is not a literal at all.
     */
    std::optional<std::string> jsonArrowPathExpansion(const AstNode& pathOperand);

    /**
     *  True when the node generates a sqlite_orm condition, i.e. a type deriving from
     *  `internal::condition_t`: a comparison, AND, OR, IN, BETWEEN, LIKE, GLOB, IS [NOT] NULL,
     *  EXISTS or NOT. MATCH is not one of them — `match_t` derives from nothing.
     */
    bool generatesSqliteOrmCondition(const AstNode& astNode);
    /**
     *  True when the node generates a sqlite_orm operator argument, i.e. a type its
     *  `is_operator_argument` recognizes: a built-in function call (aggregate and `func<>()`
     *  included), a CAST, a CASE, a `json_extract()` — what the JSON arrows generate — and the
     *  `new_()` / `old()` / `excluded()` references. A window function, a call carrying a FILTER
     *  or an OVER and a MATCH in its function spelling are not: the window aggregates, `match_t`,
     *  `filtered_aggregate_function_t` and `over_t` are recognized nowhere. Column references are
     *  left out on purpose — whether one generates a column pointer or an alias, both operator
     *  arguments, or a plain member pointer, which is none, is the generator's business and not
     *  the node's.
     */
    bool generatesSqliteOrmOperatorArgument(const AstNode& astNode);
    /**
     *  True when the node generates something `or_()` and `and_()` accept as an argument, i.e. a
     *  type their `is_operand_or_bindable` static assertion holds for. Everything the expression
     *  generator emits is one — a member pointer, a bindable value, an arithmetic or bitwise
     *  operator, a concatenation, a condition, an operator argument, a scalar subquery or a
     *  compound operator — except a MATCH in either spelling (`match_t` derives from nothing), a
     *  CURRENT_DATE / CURRENT_TIME / CURRENT_TIMESTAMP, a window function and a call carrying a
     *  FILTER or an OVER.
     */
    bool generatesSqliteOrmOperandOrBindable(const AstNode& astNode);
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
     *  Whether a COLLATE written after the SQL sqlite_orm serializes the node as applies to the
     *  whole of it. SQLite binds COLLATE tighter than every binary operator and than `NOT`, so a
     *  COLLATE after such an expression is taken by the operand it ends in instead:
     *  `"b" || 'x' COLLATE nocase` is `"b" || ('x' COLLATE nocase)`, and the collation lands on the
     *  literal. What ends in no operand at all takes the COLLATE whole — a literal, a column, a
     *  call, CAST, CASE, an IN over a value list — and so does a prefix unary operator, which
     *  SQLite binds tighter than COLLATE. Checked against sqlite3 3.51.0.
     */
    bool trailingCollateBindsWholeExpression(const AstNode& astNode);
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

    /**
     *  The same key as `normalizeSqlIdentifier`, for a name that is already the name of the object
     *  rather than the identifier a statement spelled it with. `sqlite_master.name` holds such a
     *  name: SQLite took the quotes off it when it created the object and compares it as it stands
     *  — `sqlite3_strnicmp(zName, "sqlite_", 7)` for a reserved name, `sqlite3ShadowTableName()`
     *  for a module's own table — so only the case folding is left to do here. Taking quotes off
     *  it a second time would read a name whose own first and last character are quotes as another
     *  name than the database has: sqlite3 3.51 creates `CREATE TABLE "'sqlite_foo'"(x)` without a
     *  word, under the name `'sqlite_foo'`, and files the storage of `CREATE VIRTUAL TABLE "[x]"
     *  USING fts5(a)` under `[x]_data` and the four names beside it.
     */
    std::string normalizeSchemaObjectName(std::string_view objectName);

    /**
     *  Whether a column name is one of the three names SQLite answers a rowid table's implicit row id
     *  with — `rowid`, `oid`, `_rowid_` — and no column of that name was declared. Where that name
     *  stands for the row id, quoting it changes nothing; where it does not, the double quotes make
     *  it a string literal and the statement is taken, while any other spelling is refused. Measured
     *  on sqlite3 3.51.0 with `CHECK(typeof(X) = 'integer')` as the discriminator, for a name of the
     *  three the table declares no column of:
     *
     *      clause                     | table         | bare                | "double-quoted"
     *      CHECK (table and column)   | rowid         | the row id, taken   | the row id, taken
     *      CHECK                      | WITHOUT ROWID | "no such column"    | a string, taken
     *      generated column           | either        | "no such column"    | a string, taken
     *      PRIMARY KEY / UNIQUE       | either        | "no such column"    | "expressions prohibited"
     *      foreign key column list    | either        | "unknown column"    | "unknown column"
     */
    bool isImplicitRowIdName(std::string_view sqlIdentifier);

    bool endsWith(std::string_view text, std::string_view suffix);
    /** The `...` of `auto <variableName> = storage.select(...);`, if `generated` has exactly that form. */
    std::optional<std::string> extractStorageSelectArgument(std::string_view generated, std::string_view variableName);
    std::string stripStoragePrefixAndTrailingSemicolon(std::string code);

    std::string blobToCpp(std::string_view blobLiteral);
    /**
     *  Whether a BLOB literal carries no bytes — `x''`, the one blob sqlite_orm serializes into a
     *  DDL statement as the value it stands for.
     */
    bool blobLiteralIsEmpty(std::string_view blobLiteral);
    /** SQL numeric literal to C++: SQLite's `_` digit separators become C++'s `'` (1_000 -> 1'000). */
    std::string numericLiteralToCpp(std::string_view numericLiteral);
    /**
     *  The value C++ has no floating literal for: SQLite answers a literal past the range of a
     *  double with an Inf, while [lex.fcon] makes the same spelling ill-formed in C++, where gcc
     *  and clang warn (`floating constant exceeds range of 'double'`) and a consumer building the
     *  generated code with `-Werror` gets an error. The generated header takes `<limits>` along
     *  with this, which is why the emitter tells the context it was spelled.
     */
    inline constexpr std::string_view kInfinityCppExpression = "std::numeric_limits<double>::infinity()";
    /** Whether codegen spells `numericLiteral` as `kInfinityCppExpression` rather than as itself. */
    bool numericLiteralGeneratesInfinity(std::string_view numericLiteral);
    /**
     *  SQL real literal to C++, as the value SQLite reads it as: a literal past the range of a
     *  double becomes an infinity, one too small for a double the zero it rounds to, and every
     *  other one keeps the spelling it was written with.
     */
    std::string realLiteralToCpp(std::string_view realLiteral);
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
     *  Characters of `sourceText` a warning anchored at its first character underlines, counted the
     *  way `CodegenWarning::length` promises: one Unicode code point is one character, not as many
     *  as it takes UTF-8 bytes. A consumer draws the underline from the warning's location along a
     *  single line, so a span written across lines — a quoted name or a string literal holding a
     *  newline, a pair of keywords split by one — is underlined up to the end of the line it starts
     *  on and no further. Every warning whose length is measured from source text measures it here,
     *  so that count cannot drift from the column the tokenizer moved over the same text. The
     *  anchors that pass a length inline instead pass 1 for a single ASCII operator — a unary `+`
     *  or `-` — which is one character in either count; a warning underlining anything longer than
     *  that belongs here.
     */
    size_t underlineLengthOf(std::string_view sourceText);
    /**
     *  Records in `membersByName` that the member `memberName` of the struct generated for `owner`
     *  (`"table t"`, `"view v"`) holds the column `sqlName`, and answers with what that name has
     *  to be reported as, anchored at `nameSpan` when the parse recorded one.
     *
     *  Two things are worth a warning here. A member whose name is not the column's own tells the
     *  reader which member a column ended up in — SQL takes names C++ has no letters for, so
     *  `toCppIdentifier()` rewrites them. And a member two columns are both rewritten to is a
     *  member the struct declares twice, which does not compile at all: the generated code looks
     *  fine and only a compiler ever says so, which is why the collision is reported here, where
     *  the struct is being named. The collision is what gets reported when a name does both, as
     *  it names the member the rewriting would have named anyway.
     *
     *  `mappedByMemberName` says that the mapping reads the column's SQL name off the member
     *  rather than being given it, which is what sqlite_orm's reflected `make_view<V>()` does: a
     *  rewritten member there renames the column in the mapping as well, so the warning says so.
     *  A classical `make_column("…", &T::x)` is handed the name and leaves it alone.
     */
    std::optional<CodegenWarning> recordMemberName(std::string_view owner,
                                                   std::string_view sqlName,
                                                   std::string_view memberName,
                                                   const SourceSpan& nameSpan,
                                                   std::map<std::string, std::string>& membersByName,
                                                   bool mappedByMemberName = false);
    /**
     *  `message` anchored at the source span `astNode` was parsed from, so that a consumer
     *  underlines the very SQL the message is about. Unanchored for a node carrying no span, which
     *  is a node no parse built — one a test constructed by hand, say.
     */
    CodegenWarning sourceSpanWarning(std::string message, const AstNode& astNode);
    /** The same for a span kept on its own, away from the node it was parsed into. */
    CodegenWarning sourceSpanWarning(std::string message, const SourceSpan& sourceSpan);
    /** A hint anchored the way `sourceSpanWarning` anchors a warning, at the span of `astNode`. */
    CodegenComment sourceSpanComment(std::string message, const AstNode& astNode);
    /** The same for a span kept on its own — the alias of a result column, a statement's opening keywords. */
    CodegenComment sourceSpanComment(std::string message, const SourceSpan& sourceSpan);
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
     *  Generates the SELECT-like `node` through `coordinator` and sets `compound` to whether the
     *  form the emitter handed back is a compound one — `union_(...)` and its kin. The answer comes
     *  from the emitter rather than from the shape of the node: a `WITH` in front of a compound
     *  arrives as a `WithQueryNode`, and a compound whose arms are not mapped hands back no code to
     *  place at all. A compound is a statement to sqlite_orm, so the slots that have a form only for
     *  an expression ask this before they embed what came back.
     */
    CodeGenResult selectLikeSubqueryForm(CodeGenerator& coordinator,
                                         CodeGeneratorContext& context,
                                         const AstNode& node,
                                         bool& compound);
    /**
     *  Marks `condition` as the node a `where(...)` is about to be built around while it is
     *  generated. `where_t` is the one clause that serializes its argument inside parentheses, so a
     *  compound subquery standing there — and only there — comes out as the SQL it was read from.
     *  The mark is the node itself and not a flag: `where(a > (SELECT … UNION …))` generates the
     *  same subquery one level down, where the parentheses are not written, and pointer identity is
     *  what tells the two apart. The previous mark is restored rather than cleared, so the mark
     *  never outlives the clause it was taken for.
     */
    class ParenthesizedConditionScope {
      public:
        ParenthesizedConditionScope(CodeGeneratorContext& context, const AstNode& condition);
        ~ParenthesizedConditionScope();

        ParenthesizedConditionScope(const ParenthesizedConditionScope&) = delete;
        ParenthesizedConditionScope& operator=(const ParenthesizedConditionScope&) = delete;

      private:
        CodeGeneratorContext& context;
        const AstNode* enclosing;
    };
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
    /** The C++ type of the value a node generated as a bound value is spelled with. */
    enum class GeneratedValueCppType {
        /** `1`, `0xFF` — a constant an `int` holds. */
        integer32,
        /**
         *  `3000000000` — a constant past the `int` range, which C++ gives the first of `long` and
         *  `long long` it fits in. Which of the two that is differs by platform, and the two are
         *  distinct types wherever both are 64 bits wide, so this is not `int64_t`.
         */
        integer64Literal,
        /** `static_cast<int64_t>(0xFFFFFFFF)` — a constant the generated code already types. */
        integer64Cast,
        /** `true` / `false`. */
        boolean,
        /** `2.5`, and the `99999999999999999999.0` an integer literal past the int64 range becomes. */
        real,
        /** A string literal, which decays to a `const char*` wherever a type is deduced from it. */
        text,
        /** `std::vector<char>{…}`. */
        blob,
        /** The `nullptr` a NULL literal becomes. */
        null,
    };
    /**
     *  The C++ type the code generated for `astNode` has, for the nodes `generatesBoundValue`
     *  answers for; `std::nullopt` for every other node, whose type only the compiler knows.
     *  Integers come in three types because C++ types a constant by its magnitude rather than by
     *  the column it is compared against: `1` is an `int` and `3000000000` a 64-bit integer that
     *  is still not the `int64_t` a hex literal is already cast to.
     */
    std::optional<GeneratedValueCppType> generatedValueCppType(const AstNode& astNode);
    /**
     *  How a group of expressions sqlite_orm deduces ONE C++ type from has to be spelled. Both
     *  bounds of a `between(A, T, T)` and every value of the `in(A, {…})` initializer list are
     *  such a group, so members generated as different C++ types do not compile at all.
     */
    enum class OneDeducedTypeForm {
        /** Every member generates the one type already, or nothing here can tell that they do not. */
        asWritten,
        /** Integer members of different types, which a cast on each of them gives the one type. */
        widenedToInt64,
        /** Types with nothing to widen to; codegen warns, and the generated code does not compile. */
        noCommonType,
    };
    /** The shape such a group is generated in — the single home of that rule. */
    OneDeducedTypeForm oneDeducedTypeForm(const std::vector<const AstNode*>& nodes);
    /**
     *  The code for `node` given the one 64-bit type the group it belongs to is widened to. Only a
     *  member the generated code already casts carries the type `int64_t` itself; a constant past
     *  the `int` range is typed by its magnitude, as the `long` that is not the `long long` an
     *  `int64_t` is on macOS. So every member but that one is cast, rather than the narrower ones.
     */
    std::string widenToInt64(const AstNode& node, std::string code);
    /**
     *  The shape the values of an IN list are generated in. sqlite_orm collects the initializer
     *  list into a `std::vector<E>`, so those values are under one rule more than the bounds of a
     *  BETWEEN: a list of `bool` values is one type already and still comes out a
     *  `std::vector<bool>`, whose proxy references the walk over a statement's bound values cannot
     *  take (`cannot bind non-const lvalue reference of type 'bool&'`). `in(&User::a, {true})`
     *  builds as an expression on its own and fails the moment a statement holds it, so such a
     *  list is widened as well: SQLite carries TRUE and FALSE as the integers 1 and 0 anyway.
     *  A bind parameter beside those values does not stop the widening, the way it does not stop
     *  the widening of two integer widths: it is typed by the caller, and the `int64_t` he
     *  declares is what the cast on the values beside it meets.
     */
    OneDeducedTypeForm inValuesForm(const std::vector<const AstNode*>& values);
    /** How a member of such a group is named in the warning about the two types, e.g. "an `int`". */
    std::string generatedValueTypeDescription(const AstNode& node);
    /**
     *  The first two members of `nodes` that are known not to meet in one C++ type — the pair that
     *  proves `oneDeducedTypeForm` answered `noCommonType`, and so the pair a warning names. A
     *  group of more than two has members that are not the reason it has no common type: a bind
     *  parameter is typed by the caller, and two integer constants of different widths would have
     *  been widened were they alone. Every reason `oneDeducedTypeForm` has to answer `noCommonType`
     *  today is proven by such a pair; `{nullptr, nullptr}` when a reason without one is added, so
     *  that a caller can say less rather than read through nothing.
     */
    std::pair<const AstNode*, const AstNode*> firstNoCommonTypePair(const std::vector<const AstNode*>& nodes);
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
     *  is read back as 0 / "" / false. A scalar subquery is typed from the result column of the
     *  nested `select(...)`, so it needs the widening exactly when that column does. `as_optional`
     *  leaves the SQL untouched and yields `std::optional<T>` instead. Every other expression
     *  sqlite_orm already types nullably where it has to (a column carries its field's type,
     *  `abs(...)` is a `std::unique_ptr`, `max(...)` and `coalesce(...)` carry the type of an
     *  argument, NULL itself is a `std::nullptr_t`).
     */
    bool selectResultNeedsAsOptional(const AstNode& astNode, const CodeGeneratorContext& context);
    /**
     *  The C++ type sqlite_orm reads a result column back through, spelled the way the generated
     *  code spells it, for the expressions whose type the operator or the CAST alone settles;
     *  nullopt for every other one, whose type the schema, the storage or the compiler decides.
     *  A column reference is one of those: it carries the type of the struct field it names.
     */
    std::optional<std::string> generatedResultColumnCppType(const AstNode& astNode);
    /**
     *  Which result columns of a compound SELECT are generated as `as_optional(...)` — the single
     *  home of that rule. One entry per result column position, true where EVERY arm widens that
     *  column; empty where none does. sqlite_orm reads a compound back through
     *  `std::common_type` of the types its arms come out as, so a column is widened in every arm
     *  at once or in none: one `std::optional<double>` beside a plain `int` still reads the row
     *  optionally, but beside an `std::optional<int>` it has no common type at all, and the
     *  generated code stops compiling. That is why the widening the ordinary SELECT settles per
     *  result column is settled here for the whole statement.
     *
     *  A column is widened when some arm's expression needs it by the rule every result column
     *  follows (`selectResultNeedsAsOptional`) and every arm generates that column as the same C++
     *  type, which is what keeps that common type defined. An arm whose type nothing here can name
     *  — a column reference, a call, a literal — leaves the column as written, since widening one
     *  arm beside it is exactly what may have no common type. Arms disagreeing on how many columns
     *  they carry are left alone too: SQLite refuses such a compound outright ("SELECTs to the
     *  left and right of UNION do not have the same number of result columns").
     */
    std::vector<bool> compoundSelectResultWidening(const CompoundSelectNode& compoundNode,
                                                   const CodeGeneratorContext& context);
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
     *  The warning a SELECT result column read back through a JSON_EXTRACT call over a single path
     *  carries — a `->>` and a `json_extract(X, P)` written as a call alike — and nullopt for
     *  every other column. What SQLite answers there is the value at the path, of whatever storage
     *  class the JSON holds; sqlite_orm deduces no result type for the call, and the one generated
     *  for it (see `functionCallResultTypeArgument`) reads every storage class back as its text.
     *  JSON_EXTRACT over two paths or more answers the JSON array of what it found, which is text
     *  whatever the JSON holds, so that form is left alone. `->` is left alone too: it answers the
     *  JSON text of the value rather than the value, so it is a text itself and a `std::string`
     *  costs it nothing — where the call it is generated as differs from it is what
     *  `jsonTextArrowWarning` already reports, at the same two characters.
     */
    std::optional<CodegenWarning> selectResultJsonExtractTypeWarning(const AstNode& astNode);
    /**
     *  The warning a column read back through a COALESCE, IFNULL, NULLIF or IIF call whose
     *  arguments have no common C++ type carries, and nullopt for every other column. Such a call
     *  spells its result type (see `functionCallResultTypeArgument`) to compile at all, and where
     *  that type is the text fallback it reads every storage class back as its text — a number
     *  comes back as its digits — while the type sqlite_orm deduces for the same call over
     *  arguments of one type carries the value as it is. A call whose arguments all carry a number
     *  spells that number instead and has nothing to lose, so it carries no warning either. The report belongs to the column rather than to the call: the type is what
     *  a value is read back into, and a call in a WHERE or an ORDER BY hands its value to nobody.
     *  `subject` names the column the message opens with — `"result column"` for a column of a
     *  SELECT, `"view v: column `c`"` for a field of a view's struct, which is read back through
     *  that same spelled type.
     */
    std::optional<CodegenWarning> commonArgumentTypeWarning(const AstNode& astNode,
                                                            std::string_view subject,
                                                            const ReferencedColumnResolver& resolveColumn);
    /** `commonArgumentTypeWarning` for a SELECT result column, resolving columns the emitter's way. */
    std::optional<CodegenWarning> selectResultCommonArgumentTypeWarning(const AstNode& astNode,
                                                                        const CodeGeneratorContext& context);
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

    /**
     *  The C++ type a `CAST(… AS typeName)` is read back through. A CAST converts by the affinity
     *  rule alone, so unlike `sqliteTypeToCpp` it has no `bool` for a name holding `BOOL`: `BOOLEAN`
     *  is no affinity SQLite knows and falls through to NUMERIC, and sqlite3 3.51 answers
     *  `CAST(7 AS BOOLEAN)` with 7 and `CAST(1.5 AS BOOLEAN)` with 1.5, where `cast<bool>` read the
     *  first back as 1. Such a name takes the `double` every other NUMERIC one does, and a name
     *  holding `INT` as well stays INTEGER, the rule reading `INT` first.
     */
    std::string castTypeToCpp(std::string_view typeName);
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
    /**
     *  SQLite's `getSafetyLevel(z, 0, 1)`, which is how a value reaches `PRAGMA synchronous`: a text
     *  starting with a digit is `sqlite3Atoi()` cut to the low byte the function returns, the names
     *  `off`/`no`/`false` are 0, `on`/`yes`/`true` are 1, `full` is 2 and `extra` is 3, and anything
     *  else — a name it does not know included — is the PRAGMA's default 1.
     */
    std::int32_t sqlitePragmaSafetyLevel(std::string_view valueText);
    /**
     *  SQLite's `getAutoVacuum()`, which is how a value reaches `PRAGMA auto_vacuum`: the names
     *  `none`/`full`/`incremental` are 0/1/2, and everything else is `sqlite3Atoi()` when that lands
     *  in 0..2 and 0 when it does not.
     */
    std::int32_t sqlitePragmaAutoVacuum(std::string_view valueText);
    /**
     *  What `PRAGMA max_page_count = <valueText>` sets the limit to: `sqlite3DecOrHexToI64()` over
     *  the whole text, clamped to 0..0xfffffffe. Zero for a text it refuses — a name, `'12abc'`,
     *  one past the int64 range — and zero means "leave the limit alone and just report it", which
     *  is what SQLite does with such a value.
     */
    std::int64_t sqlitePragmaMaxPageCount(std::string_view valueText);

}  // namespace sqlite2orm
