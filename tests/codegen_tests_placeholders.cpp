#include "codegen_tests_common.hpp"

#include <sqlite2orm/parser.h>
#include <sqlite2orm/tokenizer.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// A placeholder is the `/* … */` comment codegen writes where an expression or a statement it has
// no sqlite_orm form for would have stood. Whoever shows it — the playground, SQLite ORM Studio —
// has to be told which SQL to underline: a CodegenWarning carrying a location and a length.
// `unsupportedPlaceholder` is the single place that builds such a placeholder, so that the next one
// cannot appear without saying so; the last test in this file is what keeps it single.
//
// A placeholder that is the whole code of its statement is a comment line of its own and compiles,
// and it is what says, in the generated file, that a statement was read and not mapped. One
// standing in an expression slot does not compile — `storage.select(as<XAlias>(/* … */))` is not
// C++ — so it never reaches a consumer: the statement holding it is left out whole, and the tests
// that pin such a placeholder reach it through `generateNodeOnly`, the channel the generators hand
// each other. What the consumer gets instead is pinned in "codegen: a statement whose code would
// hold a placeholder is not generated".
//
// The spans below are the SQL the warning underlines, character for character.

namespace {

    /**
     *  Codegen of one node, the way the generators reach each other: a placeholder standing in an
     *  expression slot survives here and nowhere else. `CodeGenerator::generate` leaves out the
     *  statement that would hold one, and answers with the accumulated errors alone when a branch
     *  raised an error, dropping the warnings along with the code.
     */
    CodeGenResult generateNodeOnly(std::string_view sql) {
        Tokenizer tokenizer;
        Parser parser;
        auto parseResult = parser.parse(tokenizer.tokenize(sql));
        REQUIRE(parseResult);
        CodeGenerator codeGenerator;
        return codeGenerator.generateNode(*parseResult.astNodePointer);
    }

    const std::string kTriggerStepMessage = "a statement in the trigger body is not mapped to sqlite_orm "
                                            "codegen";

}  // namespace

TEST_CASE("codegen: a statement with no sqlite_orm form at all is placeheld and underlined") {
    REQUIRE(
        generateFull("ANALYZE") ==
        CodeGenResult{"/* unsupported node */",
                      {},
                      {CodegenWarning{"this statement is not mapped to sqlite_orm codegen", SourceLocation{1, 1}, 7}}});
    REQUIRE(
        generateFull("REINDEX") ==
        CodeGenResult{"/* unsupported node */",
                      {},
                      {CodegenWarning{"this statement is not mapped to sqlite_orm codegen", SourceLocation{1, 1}, 7}}});
    REQUIRE(generateFull("ALTER TABLE users RENAME TO people") ==
            CodeGenResult{
                "/* unsupported node */",
                {},
                {CodegenWarning{"this statement is not mapped to sqlite_orm codegen", SourceLocation{1, 1}, 34}}});
    REQUIRE(generateFull("ATTACH DATABASE 'x' AS y") ==
            CodeGenResult{
                "/* unsupported node */",
                {},
                {CodegenWarning{"this statement is not mapped to sqlite_orm codegen", SourceLocation{1, 1}, 24}}});
    REQUIRE(
        generateFull("DETACH y") ==
        CodeGenResult{"/* unsupported node */",
                      {},
                      {CodegenWarning{"this statement is not mapped to sqlite_orm codegen", SourceLocation{1, 1}, 8}}});
    REQUIRE(generateFull("EXPLAIN SELECT 1") ==
            CodeGenResult{
                "/* unsupported node */",
                {},
                {CodegenWarning{"this statement is not mapped to sqlite_orm codegen", SourceLocation{1, 1}, 16}}});
}

// The IS branch raises a codegen error as well, and an error makes `generate` answer with the
// errors alone — so this is the one placeholder a consumer never sees. It goes through the funnel
// all the same: what the generators hand each other says where it came from.
TEST_CASE("codegen: the IS placeholder is underlined on the operator's own expression") {
    REQUIRE(generateNodeOnly("1 IS 2") == CodeGenResult{"/* unsupported IS expression */",
                                                        {},
                                                        {CodegenWarning{"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
                                                                        "is not supported in sqlite_orm",
                                                                        SourceLocation{1, 1},
                                                                        6}}});
    REQUIRE(generateNodeOnly("SELECT a IS NOT DISTINCT FROM b FROM t") ==
            CodeGenResult{"auto rows = storage.select(/* unsupported IS expression */, from<T>());",
                          {},
                          {CodegenWarning{"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
                                          "is not supported in sqlite_orm",
                                          SourceLocation{1, 8},
                                          24}}});
}

// The span a placeholder underlines is measured in characters as well: `ключ IS b` is nine
// characters written with thirteen bytes, and the warning carries the nine a consumer draws.
TEST_CASE("codegen: a placeholder over non-ASCII SQL is underlined in characters") {
    REQUIRE(generateNodeOnly("SELECT ключ IS b FROM t") ==
            CodeGenResult{"auto rows = storage.select(/* unsupported IS expression */, from<T>());",
                          {},
                          {CodegenWarning{"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
                                          "is not supported in sqlite_orm",
                                          SourceLocation{1, 8},
                                          9}}});
}

TEST_CASE("codegen: an unmapped EXISTS subquery is underlined together with its keyword") {
    REQUIRE(generateNodeOnly("EXISTS (SELECT a FROM users GROUP BY a)") ==
            CodeGenResult{
                "/* EXISTS (SELECT ...) */",
                {},
                {CodegenWarning{"EXISTS (SELECT ...) is not mapped to sqlite_orm codegen", SourceLocation{1, 1}, 39},
                 "GROUP BY in subquery is not yet mapped to sqlite_orm select(...)"}});
}

TEST_CASE("codegen: an IN over a table name is underlined") {
    REQUIRE(generateNodeOnly("id IN t") ==
            CodeGenResult{
                "/* &User::id IN t */",
                {columnRefStyleDp(1, "&User::id")},
                {CodegenWarning{"IN table-name is not supported in sqlite_orm codegen", SourceLocation{1, 1}, 7}}});
}

TEST_CASE("codegen: an IN over an unmapped subquery is underlined") {
    REQUIRE(generateNodeOnly("id IN (SELECT a FROM users GROUP BY a)") ==
            CodeGenResult{
                "/* IN (SELECT ...) */",
                {columnRefStyleDp(1, "&User::id")},
                {"GROUP BY in subquery is not yet mapped to sqlite_orm select(...)",
                 CodegenWarning{"IN (SELECT ...) is not mapped to sqlite_orm codegen", SourceLocation{1, 1}, 38}}});
}

// SQLite refuses a bind marker with no name behind it ("unrecognized token: \":\"" on 3.51.0), so
// nothing names the C++ variable either and a placeholder stands where the value would go.
TEST_CASE("codegen: a bind parameter with no name behind it is underlined") {
    REQUIRE(generateNodeOnly(":") ==
            CodeGenResult{"/* : */",
                          {},
                          {CodegenWarning{"bind parameter : -> C++ variable '/* : */'; for prepared statements "
                                          "use storage.prepare() + get<N>(stmt)",
                                          SourceLocation{1, 1},
                                          1}}});
}

TEST_CASE("codegen: the SELECT an INSERT reads from is underlined when it is unmapped") {
    REQUIRE(generateFull("INSERT INTO users SELECT a FROM users GROUP BY a") ==
            CodeGenResult{"/* INSERT ... SELECT: inner SELECT not mapped to sqlite_orm */",
                          {},
                          {"GROUP BY in subquery is not yet mapped to sqlite_orm select(...)",
                           CodegenWarning{"the SELECT an INSERT reads from is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 19},
                                          30}}});
}

TEST_CASE("codegen: an unmapped compound SELECT is underlined whole") {
    REQUIRE(generateFull("SELECT a FROM users GROUP BY a UNION SELECT 2") ==
            CodeGenResult{"/* compound SELECT */",
                          {},
                          {CodegenWarning{"compound SELECT (UNION / INTERSECT / EXCEPT) is not mapped to "
                                          "sqlite_orm codegen",
                                          SourceLocation{1, 1},
                                          45},
                           "GROUP BY in subquery is not yet mapped to sqlite_orm select(...)"}});
}

TEST_CASE("codegen: DROP VIEW is underlined whole") {
    REQUIRE(generateFull("DROP VIEW v") ==
            CodeGenResult{"/* DROP VIEW: not supported as storage.drop_* in sqlite_orm */",
                          {},
                          {CodegenWarning{"DROP VIEW is not supported as a sqlite_orm storage method; sqlite_orm "
                                          "sync_schema() applies to mapped tables/indexes/triggers, not views",
                                          SourceLocation{1, 1},
                                          11}}});
}

// A virtual table whose whole shape is unmappable is underlined as a whole statement; where one
// module argument is the unmappable part, that argument alone is underlined.
TEST_CASE("codegen: an unmappable CREATE VIRTUAL TABLE is underlined") {
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS f USING fts5") ==
            CodeGenResult{"/* CREATE VIRTUAL TABLE: fts5 (no columns) */",
                          {},
                          {CodegenWarning{"FTS5 requires at least one column argument for "
                                          "sqlite_orm::using_fts5()",
                                          SourceLocation{1, 1},
                                          47}}});
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS r USING rtree(id, minX)") ==
            CodeGenResult{"/* CREATE VIRTUAL TABLE: rtree (invalid column count) */",
                          {},
                          {CodegenWarning{"RTREE virtual table for sqlite_orm needs 3, 5, 7, 9, or 11 simple "
                                          "column identifiers (id + min/max pairs)",
                                          SourceLocation{1, 1},
                                          58}}});
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS r USING rtree(id, lower(a), maxX)") ==
            CodeGenResult{"/* CREATE VIRTUAL TABLE: rtree (unmapped arguments) */",
                          {},
                          {CodegenWarning{"RTREE module arguments that are not plain column names cannot be "
                                          "mapped to sqlite_orm using_rtree() / using_rtree_i32()",
                                          SourceLocation{1, 54},
                                          8}}});
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS g USING generate_series(1)") ==
            CodeGenResult{"/* CREATE VIRTUAL TABLE: generate_series (unmapped arguments) */",
                          {},
                          {CodegenWarning{"generate_series module arguments are not mapped to sqlite_orm; "
                                          "expected empty argument list for "
                                          "make_virtual_table<generate_series>(..., "
                                          "internal::using_generate_series())",
                                          SourceLocation{1, 60},
                                          1}}});
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS d USING dbstat('main', 'x')") ==
            CodeGenResult{"/* CREATE VIRTUAL TABLE: dbstat (too many arguments) */",
                          {},
                          {CodegenWarning{"dbstat accepts at most one optional schema string argument for "
                                          "sqlite_orm::using_dbstat()",
                                          SourceLocation{1, 59},
                                          3}}});
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS d USING dbstat(1)") ==
            CodeGenResult{"/* CREATE VIRTUAL TABLE: dbstat (unmapped argument) */",
                          {},
                          {CodegenWarning{"dbstat optional argument should be a SQL string literal for "
                                          "sqlite_orm::using_dbstat(\"...\")",
                                          SourceLocation{1, 51},
                                          1}}});
}

// A consumer draws the underline along one line, so a span written across lines is cut at the end
// of the line it starts on — here the subquery `(SELECT a` and not the whole `(SELECT a\nFROM …)`.
TEST_CASE("codegen: a placeholder's underline stops at the end of the line it starts on") {
    REQUIRE(generateNodeOnly("SELECT name FROM users LIMIT (SELECT a\nFROM users GROUP BY a)") ==
            CodeGenResult{"auto rows = storage.select(&Users::name, limit(/* (SELECT ...) */));",
                          {columnRefStyleDp(1, "&Users::name")},
                          {CodegenWarning{"scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 30},
                                          9},
                           "GROUP BY in subquery is not yet mapped to sqlite_orm select(...)"}});
}

// The rule every placeholder above is held to: a header handed out at exit 0 has to compile, and
// `storage.select(as<XAlias>(/* (SELECT ...) */))` does not — g++ answers `expected
// primary-expression before ')' token`. So a statement whose code would hold a placeholder standing
// in an expression slot is left out whole instead, the way a statement the pipeline could not carry
// through already is, and the warnings that name the construct and underline it are what is left.
// sqlite3 3.51 accepts and runs every input here (the CHECK below is the one it refuses, and it is
// reachable from `-e` all the same).
TEST_CASE("codegen: a statement whose code would hold a placeholder is not generated") {
    const std::string groupByMessage = "GROUP BY in subquery is not yet mapped to sqlite_orm select(...)";

    // The card's own input: `sqlite2orm -e "CREATE TABLE t(a INTEGER); SELECT (SELECT a FROM t
    // GROUP BY a) AS x;"` used to answer `storage.select(as<XAlias>(/* (SELECT ...) */));` at
    // exit 0.
    REQUIRE(generateFull("SELECT (SELECT a FROM t GROUP BY a) AS x") ==
            CodeGenResult{{},
                          {},
                          {CodegenWarning{"scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 8},
                                          28},
                           groupByMessage,
                           "SELECT column alias uses as<AliasTag>() with a generated sqlite_orm::alias_tag struct",
                           kStatementNotGenerated}});
    REQUIRE(generateFull("SELECT a FROM t WHERE EXISTS (SELECT a FROM t GROUP BY a)") ==
            CodeGenResult{
                {},
                {},
                {CodegenWarning{"EXISTS (SELECT ...) is not mapped to sqlite_orm codegen", SourceLocation{1, 23}, 35},
                 groupByMessage,
                 kStatementNotGenerated}});
    // The same subquery in a trigger WHEN clause, the other half of the card.
    REQUIRE(generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.a = (SELECT a FROM u GROUP BY a) "
                         "BEGIN DELETE FROM t; END") ==
            CodeGenResult{{},
                          {},
                          {CodegenWarning{"scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 50},
                                          28},
                           groupByMessage,
                           kStatementNotGenerated}});
    // A `*` next to other result columns is placeheld in the same slot, `columns(…)`.
    REQUIRE(generateFull("SELECT *, a FROM t") ==
            CodeGenResult{{},
                          {},
                          {CodegenWarning{"a `*` result column next to other result columns is not mapped to "
                                          "sqlite_orm codegen",
                                          SourceLocation{1, 1},
                                          18},
                           kStatementNotGenerated}});
    // A trigger step is a slot too, and the placeholder standing in it may be one that would read
    // as a comment line of its own where a statement stands — `begin(/* INSERT ... SELECT: … */)`
    // is not C++ either, so the step counts as unmappable and the trigger goes.
    REQUIRE(generateFull("CREATE TRIGGER tr AFTER DELETE ON t BEGIN INSERT INTO t SELECT a FROM t GROUP BY a; "
                         "END") ==
            CodeGenResult{{},
                          {},
                          {groupByMessage,
                           CodegenWarning{"the SELECT an INSERT reads from is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 57},
                                          26},
                           CodegenWarning{kTriggerStepMessage, SourceLocation{1, 43}, 40},
                           kStatementNotGenerated}});

    // The two DDL statements keep the header placeholder they already had for a statement they
    // cannot map: it is a comment line of its own, it compiles, and it is what says in the
    // generated file that the statement was read. What they drop is the make-expression that
    // would have held the placeholder.
    REQUIRE(
        generateFull("CREATE TABLE q (a INTEGER, CHECK(a IN (SELECT b FROM t GROUP BY b)))") ==
        CodeGenResult{"/* CREATE TABLE q — not supported for sqlite_orm */",
                      {},
                      {groupByMessage,
                       CodegenWarning{"IN (SELECT ...) is not mapped to sqlite_orm codegen", SourceLocation{1, 34}, 33},
                       "a column or table constraint of q holds a construct that is not mapped to "
                       "sqlite_orm, so the table is not generated"}});
    REQUIRE(generateFull("CREATE VIEW v AS SELECT (SELECT a FROM t GROUP BY a)") ==
            CodeGenResult{"/* CREATE VIEW v — not supported for sqlite_orm */",
                          {},
                          {CodegenWarning{"scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 25},
                                          28},
                           groupByMessage,
                           "view v: SELECT column 1 has no name; using synthesized field name `column_1`",
                           "view v: type of column `column_1` could not be inferred; defaulting to int",
                           CodegenWarning{"CREATE VIEW v: sqlite_orm views use C++26 reflection (make_view + "
                                          "[[= \"…\"_orm_name]]); this code requires C++26 and will not compile "
                                          "under the selected C++ standard",
                                          SourceLocation{1, 1},
                                          11},
                           "CREATE VIEW v: the SELECT holds a construct that is not mapped to sqlite_orm, so "
                           "the view is not generated"}});
}

namespace {

    /**
     *  Every string literal in `source` that opens a C comment, i.e. every placeholder text a
     *  generator writes into the code it produces. Comments and character literals are stepped
     *  over, so what the scan reports is only what can reach generated output.
     */
    std::vector<std::string> placeholderLiteralsIn(const std::string& source) {
        std::vector<std::string> literals;
        for (size_t index = 0; index < source.size();) {
            if (source.compare(index, 2, "//") == 0) {
                const size_t lineEnd = source.find('\n', index);
                if (lineEnd == std::string::npos)
                    break;
                index = lineEnd;
            } else if (source.compare(index, 2, "/*") == 0) {
                const size_t commentEnd = source.find("*/", index + 2);
                if (commentEnd == std::string::npos)
                    break;
                index = commentEnd + 2;
            } else if (source[index] == '\'' || source[index] == '"') {
                const char quote = source[index];
                const size_t start = index++;
                while (index < source.size() && source[index] != quote) {
                    index += source[index] == '\\' ? 2 : 1;
                }
                ++index;
                std::string literal = source.substr(start, std::min(index, source.size()) - start);
                if (quote == '"' && literal.find("/*") != std::string::npos) {
                    literals.push_back(std::move(literal));
                }
            } else {
                ++index;
            }
        }
        return literals;
    }

}  // namespace

// The funnel is only a funnel as long as nothing walks around it. Every placeholder the generators
// write is built by `unsupportedPlaceholder`, so the only string literals opening a C comment left
// in the codegen sources are the funnel's own two halves and the placeholders that stand for a
// WHOLE statement and leave the generated code compiling: the PRAGMA ones, and the CREATE TABLE /
// CREATE VIEW headers, each of which already carries the warning saying why the statement
// generated nothing. A placeholder written by hand anywhere else shows up here as an extra
// literal, which is the point.
TEST_CASE("codegen: every generated placeholder is funnelled through unsupportedPlaceholder") {
    const std::vector<std::string> expected{
        R"(codegen_ddl.cpp: "/* CREATE TABLE ")",
        R"(codegen_ddl.cpp: "/* CREATE VIEW ")",
        R"(codegen_pragma.cpp: "/* PRAGMA ")",
        R"(codegen_pragma.cpp: "/* PRAGMA */")",
        R"(codegen_pragma.cpp: "/* PRAGMA integrity_check */")",
        R"(codegen_pragma.cpp: "/* PRAGMA journal_mode */")",
        R"(codegen_pragma.cpp: "/* PRAGMA locking_mode */")",
        R"(codegen_pragma.cpp: "/* PRAGMA recursive_triggers */")",
        R"(codegen_pragma.cpp: "/* PRAGMA table_info */")",
        R"(codegen_pragma.cpp: "/* PRAGMA table_xinfo */")",
        R"(codegen_utils.cpp: "/* ")",
    };

    std::vector<std::filesystem::path> sources;
    const std::filesystem::path sourceDirectory = std::filesystem::path{SQLITE2ORM_TEST_SOURCE_DIR} / "src";
    for (const auto& entry: std::filesystem::directory_iterator{sourceDirectory}) {
        const std::string fileName = entry.path().filename().string();
        if (fileName.rfind("codegen", 0) == 0 && entry.path().extension() == ".cpp") {
            sources.push_back(entry.path());
        }
    }
    REQUIRE(!sources.empty());
    std::sort(sources.begin(), sources.end());

    std::vector<std::string> found;
    for (const std::filesystem::path& source: sources) {
        std::ifstream stream{source};
        REQUIRE(stream);
        std::stringstream buffer;
        buffer << stream.rdbuf();
        for (const std::string& literal: placeholderLiteralsIn(buffer.str())) {
            std::string entry = source.filename().string() + ": " + literal;
            if (std::find(found.begin(), found.end(), entry) == found.end()) {
                found.push_back(std::move(entry));
            }
        }
    }
    std::sort(found.begin(), found.end());
    REQUIRE(found == expected);
}

// A bare `*` is a result list of its own in sqlite_orm — `get_all<T>()`, or `asterisk<T>()` in a
// subquery — so a `*` standing next to other result columns has no form there, while SQLite runs
// it (`SELECT *, a FROM t` on 3.51.0 answers with every column of `t` and then `a` again). The `*`
// is parsed as a result column carrying no expression of its own, so the SELECT it sits in is what
// the warning underlines.
TEST_CASE("codegen: a `*` next to other result columns is placeheld and the SELECT underlined") {
    const std::string message = "a `*` result column next to other result columns is not mapped to "
                                "sqlite_orm codegen";
    REQUIRE(generateNodeOnly("SELECT *, a FROM t") ==
            CodeGenResult{"auto rows = storage.select(columns(/* * among other result columns */, &T::a));",
                          {columnRefStyleDp(1, "&T::a")},
                          {CodegenWarning{message, SourceLocation{1, 1}, 18}}});
    REQUIRE(generateNodeOnly("SELECT a, * FROM t") ==
            CodeGenResult{"auto rows = storage.select(columns(&T::a, /* * among other result columns */));",
                          {columnRefStyleDp(1, "&T::a")},
                          {CodegenWarning{message, SourceLocation{1, 1}, 18}}});
    REQUIRE(generateNodeOnly("SELECT count(*), * FROM t") ==
            CodeGenResult{"auto rows = storage.select(columns(count<T>(), /* * among other result columns */));",
                          {},
                          {CodegenWarning{message, SourceLocation{1, 1}, 25}}});
    REQUIRE(generateNodeOnly("SELECT t.*, * FROM t") ==
            CodeGenResult{"auto rows = storage.select(columns(asterisk<T>(), /* * among other result columns */));",
                          {},
                          {CodegenWarning{message, SourceLocation{1, 1}, 20}}});
    REQUIRE(generateNodeOnly("SELECT DISTINCT *, a FROM t") ==
            CodeGenResult{"auto rows = storage.select(distinct(columns(/* * among other result columns */, &T::a)));",
                          {columnRefStyleDp(1, "&T::a")},
                          {CodegenWarning{message, SourceLocation{1, 1}, 27}}});
    // One warning for a result list holding two of them, underlined at the SELECT all the same.
    REQUIRE(generateNodeOnly("SELECT *, a, * FROM t") ==
            CodeGenResult{"auto rows = storage.select(columns(/* * among other result columns */, &T::a, "
                          "/* * among other result columns */));",
                          {columnRefStyleDp(1, "&T::a")},
                          {CodegenWarning{message, SourceLocation{1, 1}, 21}}});
    // The underline stops at the end of the line the SELECT starts on.
    REQUIRE(generateNodeOnly("SELECT *,\na FROM t") ==
            CodeGenResult{"auto rows = storage.select(columns(/* * among other result columns */, &T::a));",
                          {columnRefStyleDp(1, "&T::a")},
                          {CodegenWarning{message, SourceLocation{1, 1}, 9}}});
}

// The same result list inside a subquery: `asterisk<T>()` is the form for a subquery that selects
// a `*` and nothing else, so a `*` next to other columns leaves the subquery unmapped and the
// placeholder standing for it — the scalar subquery, the IN one, the SELECT a CREATE VIEW rests on
// or a trigger body statement — is what the code shows. Both the subquery and what it stands in
// are underlined, innermost first.
TEST_CASE("codegen: a subquery selecting a `*` next to other columns is left unmapped") {
    const std::string message = "a `*` result column next to other result columns is not mapped to "
                                "sqlite_orm select(...)";
    REQUIRE(generateNodeOnly("SELECT (SELECT *, a FROM t)") ==
            CodeGenResult{"auto rows = storage.select(/* (SELECT ...) */);",
                          {},
                          {CodegenWarning{"scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 8},
                                          20},
                           CodegenWarning{message, SourceLocation{1, 9}, 18}}});
    REQUIRE(generateNodeOnly("SELECT a FROM t WHERE b IN (SELECT *, c FROM u)") ==
            CodeGenResult{
                "auto rows = storage.select(&T::a, where(/* IN (SELECT ...) */));",
                {columnRefStyleDp(1, "&T::a"), columnRefStyleDp(2, "&T::b")},
                {CodegenWarning{message, SourceLocation{1, 29}, 18},
                 CodegenWarning{"IN (SELECT ...) is not mapped to sqlite_orm codegen", SourceLocation{1, 23}, 25}}});
    REQUIRE(generateFull("CREATE VIEW v AS SELECT *, a FROM t") ==
            CodeGenResult{"/* CREATE VIEW v — not supported for sqlite_orm */",
                          {},
                          {CodegenWarning{message, SourceLocation{1, 18}, 18},
                           CodegenWarning{"CREATE VIEW v: SELECT is not supported for sqlite_orm "
                                          "code generation"}}});
    REQUIRE(generateNodeOnly("CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT *, a FROM t; END") ==
            CodeGenResult{"make_trigger(\"tr\", after().insert().on<T>().begin(/* trigger step not mapped to "
                          "sqlite_orm */));",
                          {},
                          {CodegenWarning{message, SourceLocation{1, 43}, 18},
                           CodegenWarning{kTriggerStepMessage, SourceLocation{1, 43}, 18}}});
}

// `make_trigger(...).begin(step, step)` joins the steps with commas, so a step answering with no
// code at all used to leave a dangling comma behind — `begin(select(&T::b), )`, which is not C++ —
// or, as the only step, to leave `begin()` installing a trigger that does less than the schema it
// was read from, with nothing in the generated code saying so. A step is a placeholder site like
// every other one, wherever in the body it stands.
TEST_CASE("codegen: a trigger step with no sqlite_orm form is placeheld and underlined") {
    const std::string starMessage = "a `*` result column next to other result columns is not mapped to "
                                    "sqlite_orm select(...)";
    REQUIRE(generateNodeOnly("CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT b FROM t; SELECT *, a FROM t; END") ==
            CodeGenResult{"make_trigger(\"tr\", after().insert().on<T>().begin(select(&T::b), "
                          "/* trigger step not mapped to sqlite_orm */));",
                          {columnRefStyleDp(1, "&T::b")},
                          {CodegenWarning{starMessage, SourceLocation{1, 60}, 18},
                           CodegenWarning{kTriggerStepMessage, SourceLocation{1, 60}, 18}}});
    REQUIRE(generateNodeOnly("CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT b FROM t; SELECT *, a FROM t; "
                             "SELECT b FROM t; END") ==
            CodeGenResult{"make_trigger(\"tr\", after().insert().on<T>().begin(select(&T::b), "
                          "/* trigger step not mapped to sqlite_orm */, select(&T::b)));",
                          {columnRefStyleDp(1, "&T::b"), columnRefStyleDp(2, "&T::b")},
                          {CodegenWarning{starMessage, SourceLocation{1, 60}, 18},
                           CodegenWarning{kTriggerStepMessage, SourceLocation{1, 60}, 18}}});
    // The step that generated nothing is the first one: it used to disappear from the body without
    // a trace, leaving a trigger that compiles and runs and does less than the one imported.
    REQUIRE(generateNodeOnly("CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT *, a FROM t; SELECT b FROM t; END") ==
            CodeGenResult{"make_trigger(\"tr\", after().insert().on<T>().begin(/* trigger step not mapped to "
                          "sqlite_orm */, select(&T::b)));",
                          {columnRefStyleDp(1, "&T::b")},
                          {CodegenWarning{starMessage, SourceLocation{1, 43}, 18},
                           CodegenWarning{kTriggerStepMessage, SourceLocation{1, 43}, 18}}});
    // A compound step is underlined whole, arms included.
    REQUIRE(generateNodeOnly("CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT *, a FROM t UNION "
                             "SELECT b FROM t; END") ==
            CodeGenResult{"make_trigger(\"tr\", after().insert().on<T>().begin(/* trigger step not mapped to "
                          "sqlite_orm */));",
                          {},
                          {CodegenWarning{starMessage, SourceLocation{1, 43}, 18},
                           CodegenWarning{kTriggerStepMessage, SourceLocation{1, 43}, 40}}});
    // The arm that answered with no code before this branch existed: a `*` needs a FROM to name the
    // row type `asterisk<T>()` is taken over, and a step of one is a placeholder site all the same.
    REQUIRE(generateNodeOnly("CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT b FROM t; SELECT *; END") ==
            CodeGenResult{"make_trigger(\"tr\", after().insert().on<T>().begin(select(&T::b), "
                          "/* trigger step not mapped to sqlite_orm */));",
                          {columnRefStyleDp(1, "&T::b")},
                          {CodegenWarning{"SELECT * subexpression requires FROM for sqlite_orm asterisk<...>()"},
                           CodegenWarning{kTriggerStepMessage, SourceLocation{1, 60}, 8}}});
    // The underline stops at the end of the line the step starts on.
    REQUIRE(generateNodeOnly("CREATE TRIGGER tr AFTER INSERT ON t BEGIN\nSELECT *,\na FROM t; END") ==
            CodeGenResult{"make_trigger(\"tr\", after().insert().on<T>().begin(/* trigger step not mapped to "
                          "sqlite_orm */));",
                          {},
                          {CodegenWarning{starMessage, SourceLocation{2, 1}, 9},
                           CodegenWarning{kTriggerStepMessage, SourceLocation{2, 1}, 9}}});
}

// A WITH nests the outer statement's code inside `storage.with(cte…, <outer>)`, which turns a
// placeholder standing for the whole outer statement — a comment line of its own, which compiles —
// into a comment where C++ expects an expression. `INSERT … SELECT` whose SELECT has no sqlite_orm
// form is exactly that: `storage.with(cte<cte_0>().as(select(&T::a)), /* INSERT ... SELECT: … */);`
// was handed out at exit 0, and g++ answers `expected primary-expression before ')' token`. The
// wrap is given up instead and the plain DML — the placeholder line — is what the statement
// generates. Every input here is prepared by sqlite3 3.51.
TEST_CASE("codegen: a WITH does not wrap a placeholder standing for its outer statement") {
    const std::string insertSelectPlaceholder = "/* INSERT ... SELECT: inner SELECT not mapped to sqlite_orm */";
    const std::string unwrappedMessage =
        "WITH … DML: outer statement codegen could not be wrapped in storage.with(); emitted plain DML";
    const std::string insertReadsMessage = "the SELECT an INSERT reads from is not mapped to sqlite_orm codegen";

    REQUIRE(generateFull("WITH c AS (SELECT a FROM t) INSERT INTO u SELECT a FROM t GROUP BY a") ==
            CodeGenResult{insertSelectPlaceholder,
                          {columnRefStyleDp(1, "&T::a")},
                          {"GROUP BY in subquery is not yet mapped to sqlite_orm select(...)",
                           CodegenWarning{insertReadsMessage, SourceLocation{1, 43}, 26},
                           unwrappedMessage}});
    REQUIRE(generateFull("WITH RECURSIVE c(x) AS (SELECT 1) INSERT INTO u SELECT *, a FROM t") ==
            CodeGenResult{insertSelectPlaceholder,
                          {},
                          {CodegenWarning{"a `*` result column next to other result columns is not mapped to "
                                          "sqlite_orm select(...)",
                                          SourceLocation{1, 49},
                                          18},
                           CodegenWarning{insertReadsMessage, SourceLocation{1, 49}, 18},
                           unwrappedMessage}});

    // The outer SELECT branch nests its argument the same way. `extractStorageSelectArgument`
    // happens to turn a placeholder down today, which makes this safe by accident; the journal is
    // what makes it safe by construction, and the fallback it lands in is the one below.
    REQUIRE(generateFull("WITH c AS (SELECT a FROM t) SELECT a FROM t UNION SELECT a FROM t GROUP BY a") ==
            CodeGenResult{"/* compound SELECT */",
                          {columnRefStyleDp(1, "&T::a"), columnRefStyleDp(2, "&T::a")},
                          {CodegenWarning{"compound SELECT (UNION / INTERSECT / EXCEPT) is not mapped to "
                                          "sqlite_orm codegen",
                                          SourceLocation{1, 29},
                                          48},
                           "GROUP BY in subquery is not yet mapped to sqlite_orm select(...)",
                           "WITH: outer SELECT is not in the expected `auto rows = storage.select(...);` form; "
                           "emitted as plain outer codegen"}});

    // A WITH whose outer statement generates is untouched: the wrap is given up only for a
    // placeholder, not for every DML that a CTE stands in front of.
    REQUIRE(generate("WITH c AS (SELECT a FROM t) INSERT INTO u SELECT a FROM c") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "storage.with(cte<cte_0>().as(select(&T::a)), insert(into<U>(), select(column<cte_0>(&T::a))));");
}

namespace {

    /**
     *  The lines of `code` that hold a placeholder next to other code. A placeholder standing on a
     *  line of its own is a comment where a statement stands and compiles; one sharing its line
     *  with anything else is a comment where C++ expects an expression, and the file does not
     *  build. The scan is over the generated text because that is where the property is visible:
     *  what decides it is which funnel a generator called and whether the statement holding the
     *  placeholder asked the journal before embedding its code.
     */
    std::vector<std::string> linesHoldingAPlaceholderBesideCode(const std::string& code) {
        std::vector<std::string> lines;
        for (size_t start = 0; start < code.size();) {
            const size_t lineEnd = std::min(code.find('\n', start), code.size());
            const std::string line = code.substr(start, lineEnd - start);
            start = lineEnd + 1;
            const size_t firstVisible = line.find_first_not_of(" \t");
            if (line.find("/*") == std::string::npos) {
                continue;
            }
            const bool standsAlone = firstVisible != std::string::npos && line.compare(firstVisible, 2, "/*") == 0 &&
                                     line.substr(firstVisible).find("*/") == line.size() - firstVisible - 2;
            if (!standsAlone) {
                lines.push_back(line);
            }
        }
        return lines;
    }

    /** A statement that holds an unmapped expression, and exactly what it generates once it does. */
    struct PlaceholderCase {
        /** The SQL, with `$` standing where the unmapped expression goes. */
        std::string sql;
        /** The whole generated code, for every expression put in its place. */
        std::string code;
    };

    std::string withExpression(const std::string& sql, const std::string& expression) {
        const size_t slot = sql.find('$');
        REQUIRE(slot != std::string::npos);
        return sql.substr(0, slot) + expression + sql.substr(slot + 1);
    }

}  // namespace

// The funnel test above pins that every placeholder is BUILT in one place. This one pins the
// property that makes the two funnels worth telling apart: whatever a generator placeheld, no line
// a statement hands out holds a placeholder beside other code. It is the property a call site
// breaks by reaching for the wrong funnel, and the one an embedder breaks by nesting a statement's
// code without asking the journal — `storage.with(cte…, /* INSERT ... SELECT: … */)` was the second
// kind, and the next one will be too.
//
// The corpus is the placeholder sites, each crossed with the expressions codegen has no sqlite_orm
// form for, plus the shapes that are placeheld whole. sqlite3 3.51 prepares every input except the
// three whose expression it refuses outright — a subquery in a CHECK, in a column DEFAULT and in an
// index expression — which `-e` still reads, and `-e` is the path the playground runs on.
TEST_CASE("codegen: no statement hands out a line holding a placeholder beside other code") {
    const std::vector<std::string> unmappedExpressions{
        "(SELECT b FROM u GROUP BY b)",
        "(SELECT b FROM u GROUP BY b HAVING count(*) > 0)",
        "1 IN (SELECT b FROM u GROUP BY b)",
        "EXISTS (SELECT b FROM u GROUP BY b)",
    };
    const std::vector<PlaceholderCase> contexts{
        {"SELECT $ AS x", {}},
        {"SELECT a FROM t WHERE $", {}},
        {"SELECT max(a) FROM t GROUP BY a HAVING $", {}},
        {"SELECT a FROM t ORDER BY $", {}},
        {"SELECT a FROM t LIMIT $", {}},
        {"UPDATE t SET a = $", {}},
        {"DELETE FROM t WHERE $", {}},
        {"INSERT INTO t(a) VALUES ($)", {}},
        {"CREATE VIEW v2 AS SELECT $", "/* CREATE VIEW v2 — not supported for sqlite_orm */"},
        {"CREATE TABLE q (a INTEGER, CHECK($))", "/* CREATE TABLE q — not supported for sqlite_orm */"},
        {"CREATE TABLE q (a INTEGER DEFAULT ($))", "/* CREATE TABLE q — not supported for sqlite_orm */"},
        {"CREATE TRIGGER tr AFTER INSERT ON t WHEN $ BEGIN DELETE FROM t; END", {}},
        {"CREATE TRIGGER tr AFTER INSERT ON t BEGIN DELETE FROM t WHERE $; END", {}},
        {"CREATE INDEX i ON t(($))", {}},
        {"WITH c AS (SELECT a FROM t) SELECT $ AS x", {}},
        {"WITH c AS (SELECT a FROM t) DELETE FROM t WHERE $", {}},
        {"WITH RECURSIVE c(x) AS (SELECT 1) UPDATE t SET a = $", {}},
        {"WITH c AS (SELECT a FROM t) INSERT INTO u(a) VALUES ($)", {}},
    };
    const std::vector<PlaceholderCase> shapes{
        {"ANALYZE", "/* unsupported node */"},
        {"DROP VIEW v", "/* DROP VIEW: not supported as storage.drop_* in sqlite_orm */"},
        {"PRAGMA journal_mode", "storage.pragma.journal_mode();"},
        {"SELECT *, a FROM t", {}},
        {"INSERT INTO u SELECT a FROM t GROUP BY a", "/* INSERT ... SELECT: inner SELECT not mapped to sqlite_orm */"},
        {"WITH c AS (SELECT a FROM t) INSERT INTO u SELECT a FROM t GROUP BY a",
         "/* INSERT ... SELECT: inner SELECT not mapped to sqlite_orm */"},
        {"WITH RECURSIVE c(x) AS (SELECT 1) INSERT INTO u SELECT *, a FROM t",
         "/* INSERT ... SELECT: inner SELECT not mapped to sqlite_orm */"},
        {"SELECT a FROM t UNION SELECT a FROM t GROUP BY a", "/* compound SELECT */"},
        {"WITH c AS (SELECT a FROM t) SELECT a FROM t UNION SELECT a FROM t GROUP BY a", "/* compound SELECT */"},
        {"CREATE TRIGGER tr AFTER DELETE ON t BEGIN INSERT INTO u SELECT a FROM t GROUP BY a; END", {}},
        // The mirror class, where the form was found and the slot has none for it: a compound
        // SELECT standing as a scalar subquery, and a subquery standing as a whole column of a CTE.
        // These stay shapes rather than expressions crossed with the contexts above, because
        // whether they are placeheld depends on the slot — `where(union_(…))` brings the parentheses
        // a compound needs and keeps generating.
        {"SELECT (SELECT b FROM u UNION SELECT b FROM w) FROM t", {}},
        {"INSERT INTO u(b) SELECT b FROM u UNION SELECT b FROM w",
         "/* INSERT ... SELECT: inner SELECT not mapped to sqlite_orm */"},
        {"WITH c AS (SELECT a FROM t) INSERT INTO u(b) SELECT b FROM u UNION SELECT b FROM w",
         "/* INSERT ... SELECT: inner SELECT not mapped to sqlite_orm */"},
        {"WITH c AS (SELECT (SELECT b FROM u) AS y FROM t) SELECT y FROM c", {}},
    };

    size_t checked = 0;
    for (const PlaceholderCase& context: contexts) {
        for (const std::string& expression: unmappedExpressions) {
            const std::string sql = withExpression(context.sql, expression);
            INFO(sql);
            const CodeGenResult result = generateFull(sql);
            REQUIRE(result.code == context.code);
            REQUIRE(linesHoldingAPlaceholderBesideCode(result.code) == std::vector<std::string>{});
            ++checked;
        }
    }
    for (const PlaceholderCase& shape: shapes) {
        INFO(shape.sql);
        const CodeGenResult result = generateFull(shape.sql);
        REQUIRE(result.code == shape.code);
        REQUIRE(linesHoldingAPlaceholderBesideCode(result.code) == std::vector<std::string>{});
        ++checked;
    }
    REQUIRE(checked == 86);
}

namespace {

    const std::string kCompoundSubqueryMessage =
        "a compound SELECT as a scalar subquery is not mapped to sqlite_orm codegen: its "
        "union_()/intersect()/except() form is a statement, which sqlite_orm serializes without the parentheses "
        "this position needs";

}  // namespace

// The mirror of the placeholder above: a subquery codegen DID find a form for, standing in a slot
// sqlite_orm has none for. A compound SELECT is a statement to sqlite_orm — `union_(select(…),
// select(…))` — and `statement_serializer` writes it without parentheses of its own, so outside the
// one clause that brings them the generated code either does not build (`storage.select(union_(…),
// from<T>())` trips `static_assert(… "Cannot use args with a compound operator")`) or serializes
// SQL sqlite3 3.51 refuses with `SQL logic error`: `COALESCE(SELECT … UNION SELECT …, 1)`,
// `"t"."a" > SELECT …`, `EXISTS SELECT …`, `ORDER BY SELECT …`. All of it was handed out at exit 0.
TEST_CASE("codegen: a compound SELECT as a scalar subquery leaves the statement out") {
    REQUIRE(
        generateFull("SELECT coalesce((SELECT b FROM u UNION SELECT b FROM w), 1) FROM t") ==
        CodeGenResult{{},
                      {},
                      {CodegenWarning{kCompoundSubqueryMessage, SourceLocation{1, 17}, 39}, kStatementNotGenerated}});
    REQUIRE(
        generateFull("SELECT (SELECT b FROM u UNION SELECT b FROM w) FROM t") ==
        CodeGenResult{{},
                      {},
                      {CodegenWarning{kCompoundSubqueryMessage, SourceLocation{1, 8}, 39}, kStatementNotGenerated}});
    REQUIRE(
        generateFull("SELECT a FROM t WHERE a > (SELECT b FROM u UNION SELECT b FROM w)") ==
        CodeGenResult{{},
                      {},
                      {CodegenWarning{kCompoundSubqueryMessage, SourceLocation{1, 27}, 39}, kStatementNotGenerated}});
    // One level down from the clause that parenthesizes is already a value slot: `and_` writes its
    // operands bare, and `WHERE (SELECT … UNION SELECT … AND "t"."a" > 1)` is not the SQL read.
    REQUIRE(
        generateFull("SELECT a FROM t WHERE a > 1 AND (SELECT b FROM u UNION SELECT b FROM w)") ==
        CodeGenResult{{},
                      {},
                      {CodegenWarning{kCompoundSubqueryMessage, SourceLocation{1, 33}, 39}, kStatementNotGenerated}});
    // `exists()` writes its argument bare as well, and sqlite_orm's own `in(x, union_(…))` is the
    // one form that does parenthesize a compound — the case below keeps it generating.
    REQUIRE(generateFull("SELECT a FROM t WHERE EXISTS (SELECT b FROM u UNION SELECT b FROM w)") ==
            CodeGenResult{{},
                          {},
                          {CodegenWarning{"EXISTS over a compound SELECT is not mapped to sqlite_orm codegen: its "
                                          "union_()/intersect()/except() form is a statement, which sqlite_orm "
                                          "serializes without the parentheses EXISTS needs",
                                          SourceLocation{1, 23},
                                          46},
                           kStatementNotGenerated}});
    // A trigger's WHEN clause used to answer with the compound's own diagnostic, which said the
    // trigger does not compile while handing it out; the statement now goes instead.
    REQUIRE(generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN (SELECT b FROM u UNION SELECT b FROM w) "
                         "BEGIN DELETE FROM t; END") ==
            CodeGenResult{{},
                          {},
                          {CodegenWarning{kCompoundSubqueryMessage, SourceLocation{1, 42}, 39},
                           "CREATE TRIGGER tr uses a compound SELECT in its WHEN clause, a form sqlite_orm gives "
                           "no default constructor: make_trigger() keeps a trigger's WHEN expression in an "
                           "optional_container, which default-constructs the expression before assigning it, so "
                           "the generated trigger does not compile",
                           kStatementNotGenerated}});

    // `where_t` serializes as `WHERE (…)`, so the whole condition of a WHERE is the one position a
    // compound subquery reads back as the SQL it was written as. sqlite3 3.51 answers the same rows
    // for the generated statement as for the input, checked by running both.
    REQUIRE(generate("SELECT a FROM t WHERE (SELECT b FROM u UNION SELECT b FROM w)") ==
            "auto rows = storage.select(&T::a, from<T>(), where(union_(select(&U::b), select(&W::b))));");
    REQUIRE(generate("DELETE FROM t WHERE (SELECT b FROM u UNION SELECT b FROM w)") ==
            "storage.remove_all<T>(where(union_(select(&U::b), select(&W::b))));");
    REQUIRE(generate("UPDATE t SET a = 1 WHERE (SELECT b FROM u UNION SELECT b FROM w)") ==
            "storage.update_all(set(c(&T::a) = 1), where(union_(select(&U::b), select(&W::b))));");
    REQUIRE(generate("SELECT a FROM t WHERE a IN (SELECT b FROM u UNION SELECT b FROM w)") ==
            "auto rows = storage.select(&T::a, from<T>(), where(in(&T::a, union_(select(&U::b), select(&W::b)))));");
}

// The other slot with a form for no subquery at all: sqlite_orm reads the columns of a CTE through
// `extract_colref_expressions`, whose overload for a `select_t` is deleted, so a subquery standing
// as a WHOLE column of a CTE does not build — `cte<cte_0>().as(select(select(&U::b)))`. It is that
// slot alone: the same subquery in the CTE's own WHERE, or under an operator or a call inside the
// column, compiles and is left as it is.
TEST_CASE("codegen: a subquery as a whole column of a CTE leaves the statement out") {
    const std::string cteColumnMessage =
        "a subquery as a whole column of a CTE is not mapped to sqlite_orm codegen: sqlite_orm reads the columns "
        "of a CTE with extract_colref_expressions(), which declares no overload for a select(...)";
    const std::string withRequirements =
        "WITH: requires SQLite ≥ 3.8.3, sqlite_orm built with SQLITE_ORM_WITH_CTE, and `using namespace "
        "sqlite_orm::literals` scope for `_ctealias`";

    REQUIRE(
        generateFull("WITH c AS (SELECT (SELECT b FROM u) AS y FROM t) SELECT y FROM c") ==
        CodeGenResult{
            {},
            {},
            {CodegenWarning{cteColumnMessage, SourceLocation{1, 19}, 17}, withRequirements, kStatementNotGenerated}});
    // The card's own input, where the subquery carries a NATURAL JOIN of its own.
    REQUIRE(
        generateFull("WITH c AS (SELECT (SELECT u.b FROM u NATURAL JOIN w) AS y) SELECT a FROM t") ==
        CodeGenResult{
            {},
            {},
            {CodegenWarning{cteColumnMessage, SourceLocation{1, 19}, 34}, withRequirements, kStatementNotGenerated}});

    // The same subquery inside the column, under a call or a CAST, is generated and builds.
    REQUIRE(generate("WITH c AS (SELECT abs((SELECT b FROM u)) AS y FROM t) SELECT * FROM c") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "auto rows = storage.with(cte<cte_0>().as(select(abs(select(&U::b)), from<T>())), "
            "select(asterisk<cte_0>()));");
    REQUIRE(generate("WITH c AS (SELECT CAST((SELECT b FROM u) AS TEXT) AS y FROM t) SELECT * FROM c") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "auto rows = storage.with(cte<cte_0>().as(select(cast<std::string>(select(&U::b)), from<T>())), "
            "select(asterisk<cte_0>()));");
    // Under an operator it is generated too, and does NOT build: a subquery as the left operand of
    // an operator is written bare, which is not this rule's slot and not the CTE's — the plain
    // `SELECT (SELECT b FROM u) + 1 FROM t` comes out the same way (see COVERAGE.md).
    REQUIRE(generate("WITH c AS (SELECT (SELECT b FROM u) + 1 AS y FROM t) SELECT y FROM c") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "auto rows = storage.with(cte<cte_0>().as(select(select(&U::b) + 1, from<T>())), "
            "select(column<cte_0>(&T::y)));");
    REQUIRE(generate("WITH c AS (SELECT a FROM t WHERE a > (SELECT b FROM u)) SELECT a FROM c") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "auto rows = storage.with(cte<cte_0>().as(select(&T::a, from<T>(), where(c(&T::a) > select(&U::b)))), "
            "select(column<cte_0>(&T::a)));");
}
