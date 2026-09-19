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
// no sqlite_orm form for would have stood. The generated code does not compile there, so whoever
// shows it — the playground, SQLite ORM Studio — has to be told which SQL to underline: a
// CodegenWarning carrying a location and a length. `unsupportedPlaceholder` is the single place
// that builds such a placeholder, so that the next one cannot appear without saying so; the last
// test in this file is what keeps it single.
//
// The spans below are the SQL the warning underlines, character for character.

namespace {

    /**
     *  Codegen of one node, the way the generators reach each other. `CodeGenerator::generate`
     *  answers with the accumulated errors alone and drops the warnings along with the code, which
     *  is why the one branch that raises an error is generated through here.
     */
    CodeGenResult generateNodeOnly(std::string_view sql) {
        Tokenizer tokenizer;
        Parser parser;
        auto parseResult = parser.parse(tokenizer.tokenize(sql));
        REQUIRE(parseResult);
        CodeGenerator codeGenerator;
        return codeGenerator.generateNode(*parseResult.astNodePointer);
    }

}  // namespace

TEST_CASE("codegen: a statement with no sqlite_orm form at all is placeheld and underlined") {
    REQUIRE(generateFull("ANALYZE") ==
            CodeGenResult{"/* unsupported node */",
                          {},
                          {CodegenWarning{"this statement is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 1}, 7}}});
    REQUIRE(generateFull("REINDEX") ==
            CodeGenResult{"/* unsupported node */",
                          {},
                          {CodegenWarning{"this statement is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 1}, 7}}});
    REQUIRE(generateFull("ALTER TABLE users RENAME TO people") ==
            CodeGenResult{"/* unsupported node */",
                          {},
                          {CodegenWarning{"this statement is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 1}, 34}}});
    REQUIRE(generateFull("ATTACH DATABASE 'x' AS y") ==
            CodeGenResult{"/* unsupported node */",
                          {},
                          {CodegenWarning{"this statement is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 1}, 24}}});
    REQUIRE(generateFull("DETACH y") ==
            CodeGenResult{"/* unsupported node */",
                          {},
                          {CodegenWarning{"this statement is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 1}, 8}}});
    REQUIRE(generateFull("EXPLAIN SELECT 1") ==
            CodeGenResult{"/* unsupported node */",
                          {},
                          {CodegenWarning{"this statement is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 1}, 16}}});
}

// The IS branch raises a codegen error as well, and an error makes `generate` answer with the
// errors alone — so this is the one placeholder a consumer never sees. It goes through the funnel
// all the same: what the generators hand each other says where it came from.
TEST_CASE("codegen: the IS placeholder is underlined on the operator's own expression") {
    REQUIRE(generateNodeOnly("1 IS 2") ==
            CodeGenResult{"/* unsupported IS expression */",
                          {},
                          {CodegenWarning{"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
                                          "is not supported in sqlite_orm",
                                          SourceLocation{1, 1}, 6}}});
    REQUIRE(generateNodeOnly("SELECT a IS NOT DISTINCT FROM b FROM t") ==
            CodeGenResult{"auto rows = storage.select(/* unsupported IS expression */);",
                          {},
                          {CodegenWarning{"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
                                          "is not supported in sqlite_orm",
                                          SourceLocation{1, 8}, 24}}});
}

TEST_CASE("codegen: an unmapped EXISTS subquery is underlined together with its keyword") {
    REQUIRE(generateFull("EXISTS (SELECT a FROM users GROUP BY a)") ==
            CodeGenResult{"/* EXISTS (SELECT ...) */",
                          {},
                          {CodegenWarning{"EXISTS (SELECT ...) is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 1}, 39},
                           "GROUP BY in subquery is not yet mapped to sqlite_orm select(...)"}});
}

TEST_CASE("codegen: an IN over a table name is underlined") {
    REQUIRE(generateFull("id IN t") ==
            CodeGenResult{"/* &User::id IN t */",
                          {columnRefStyleDp(1, "&User::id")},
                          {CodegenWarning{"IN table-name is not supported in sqlite_orm codegen",
                                          SourceLocation{1, 1}, 7}}});
}

TEST_CASE("codegen: an IN over an unmapped subquery is underlined") {
    REQUIRE(generateFull("id IN (SELECT a FROM users GROUP BY a)") ==
            CodeGenResult{"/* IN (SELECT ...) */",
                          {columnRefStyleDp(1, "&User::id")},
                          {"GROUP BY in subquery is not yet mapped to sqlite_orm select(...)",
                           CodegenWarning{"IN (SELECT ...) is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 1}, 38}}});
}

// SQLite refuses a bind marker with no name behind it ("unrecognized token: \":\"" on 3.51.0), so
// nothing names the C++ variable either and a placeholder stands where the value would go.
TEST_CASE("codegen: a bind parameter with no name behind it is underlined") {
    REQUIRE(generateFull(":") ==
            CodeGenResult{"/* : */",
                          {},
                          {CodegenWarning{"bind parameter : -> C++ variable '/* : */'; for prepared statements "
                                          "use storage.prepare() + get<N>(stmt)",
                                          SourceLocation{1, 1}, 1}}});
}

TEST_CASE("codegen: the SELECT an INSERT reads from is underlined when it is unmapped") {
    REQUIRE(generateFull("INSERT INTO users SELECT a FROM users GROUP BY a") ==
            CodeGenResult{"/* INSERT ... SELECT: inner SELECT not mapped to sqlite_orm */",
                          {},
                          {"GROUP BY in subquery is not yet mapped to sqlite_orm select(...)",
                           CodegenWarning{"the SELECT an INSERT reads from is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 19}, 30}}});
}

TEST_CASE("codegen: an unmapped compound SELECT is underlined whole") {
    REQUIRE(generateFull("SELECT a FROM users GROUP BY a UNION SELECT 2") ==
            CodeGenResult{"/* compound SELECT */",
                          {},
                          {CodegenWarning{"compound SELECT (UNION / INTERSECT / EXCEPT) is not mapped to "
                                          "sqlite_orm codegen",
                                          SourceLocation{1, 1}, 45},
                           "GROUP BY in subquery is not yet mapped to sqlite_orm select(...)"}});
}

TEST_CASE("codegen: DROP VIEW is underlined whole") {
    REQUIRE(generateFull("DROP VIEW v") ==
            CodeGenResult{"/* DROP VIEW: not supported as storage.drop_* in sqlite_orm */",
                          {},
                          {CodegenWarning{"DROP VIEW is not supported as a sqlite_orm storage method; sqlite_orm "
                                          "sync_schema() applies to mapped tables/indexes/triggers, not views",
                                          SourceLocation{1, 1}, 11}}});
}

// A virtual table whose whole shape is unmappable is underlined as a whole statement; where one
// module argument is the unmappable part, that argument alone is underlined.
TEST_CASE("codegen: an unmappable CREATE VIRTUAL TABLE is underlined") {
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS f USING fts5") ==
            CodeGenResult{"/* CREATE VIRTUAL TABLE: fts5 (no columns) */",
                          {},
                          {CodegenWarning{"FTS5 requires at least one column argument for "
                                          "sqlite_orm::using_fts5()",
                                          SourceLocation{1, 1}, 47}}});
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS r USING rtree(id, minX)") ==
            CodeGenResult{"/* CREATE VIRTUAL TABLE: rtree (invalid column count) */",
                          {},
                          {CodegenWarning{"RTREE virtual table for sqlite_orm needs 3, 5, 7, 9, or 11 simple "
                                          "column identifiers (id + min/max pairs)",
                                          SourceLocation{1, 1}, 58}}});
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS r USING rtree(id, lower(a), maxX)") ==
            CodeGenResult{"/* CREATE VIRTUAL TABLE: rtree (unmapped arguments) */",
                          {},
                          {CodegenWarning{"RTREE module arguments that are not plain column names cannot be "
                                          "mapped to sqlite_orm using_rtree() / using_rtree_i32()",
                                          SourceLocation{1, 54}, 8}}});
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS g USING generate_series(1)") ==
            CodeGenResult{"/* CREATE VIRTUAL TABLE: generate_series (unmapped arguments) */",
                          {},
                          {CodegenWarning{"generate_series module arguments are not mapped to sqlite_orm; "
                                          "expected empty argument list for "
                                          "make_virtual_table<generate_series>(..., "
                                          "internal::using_generate_series())",
                                          SourceLocation{1, 60}, 1}}});
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS d USING dbstat('main', 'x')") ==
            CodeGenResult{"/* CREATE VIRTUAL TABLE: dbstat (too many arguments) */",
                          {},
                          {CodegenWarning{"dbstat accepts at most one optional schema string argument for "
                                          "sqlite_orm::using_dbstat()",
                                          SourceLocation{1, 59}, 3}}});
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS d USING dbstat(1)") ==
            CodeGenResult{"/* CREATE VIRTUAL TABLE: dbstat (unmapped argument) */",
                          {},
                          {CodegenWarning{"dbstat optional argument should be a SQL string literal for "
                                          "sqlite_orm::using_dbstat(\"...\")",
                                          SourceLocation{1, 51}, 1}}});
}

// A consumer draws the underline along one line, so a span written across lines is cut at the end
// of the line it starts on — here the subquery `(SELECT a` and not the whole `(SELECT a\nFROM …)`.
TEST_CASE("codegen: a placeholder's underline stops at the end of the line it starts on") {
    REQUIRE(generateFull("SELECT name FROM users LIMIT (SELECT a\nFROM users GROUP BY a)") ==
            CodeGenResult{"auto rows = storage.select(&Users::name, limit(/* (SELECT ...) */));",
                          {columnRefStyleDp(1, "&Users::name")},
                          {CodegenWarning{"scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 30}, 9},
                           "GROUP BY in subquery is not yet mapped to sqlite_orm select(...)"}});
}

namespace {

    /**
     *  Every string literal in `source` that opens a C comment, i.e. every placeholder text a
     *  generator writes into the code it produces. Comments and character literals are stepped
     *  over, so what the scan reports is only what can reach generated output.
     */
    std::vector<std::string> placeholderLiteralsIn(const std::string& source) {
        std::vector<std::string> literals;
        for(size_t index = 0; index < source.size();) {
            if(source.compare(index, 2, "//") == 0) {
                const size_t lineEnd = source.find('\n', index);
                if(lineEnd == std::string::npos) break;
                index = lineEnd;
            } else if(source.compare(index, 2, "/*") == 0) {
                const size_t commentEnd = source.find("*/", index + 2);
                if(commentEnd == std::string::npos) break;
                index = commentEnd + 2;
            } else if(source[index] == '\'' || source[index] == '"') {
                const char quote = source[index];
                const size_t start = index++;
                while(index < source.size() && source[index] != quote) {
                    index += source[index] == '\\' ? 2 : 1;
                }
                ++index;
                std::string literal = source.substr(start, std::min(index, source.size()) - start);
                if(quote == '"' && literal.find("/*") != std::string::npos) {
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
    for(const auto& entry : std::filesystem::directory_iterator{sourceDirectory}) {
        const std::string fileName = entry.path().filename().string();
        if(fileName.rfind("codegen", 0) == 0 && entry.path().extension() == ".cpp") {
            sources.push_back(entry.path());
        }
    }
    REQUIRE(!sources.empty());
    std::sort(sources.begin(), sources.end());

    std::vector<std::string> found;
    for(const std::filesystem::path& source : sources) {
        std::ifstream stream{source};
        REQUIRE(stream);
        std::stringstream buffer;
        buffer << stream.rdbuf();
        for(const std::string& literal : placeholderLiteralsIn(buffer.str())) {
            std::string entry = source.filename().string() + ": " + literal;
            if(std::find(found.begin(), found.end(), entry) == found.end()) {
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
    REQUIRE(generateFull("SELECT *, a FROM t") ==
            CodeGenResult{"auto rows = storage.select(columns(/* * among other result columns */, &T::a));",
                          {columnRefStyleDp(1, "&T::a")},
                          {CodegenWarning{message, SourceLocation{1, 1}, 18}}});
    REQUIRE(generateFull("SELECT a, * FROM t") ==
            CodeGenResult{"auto rows = storage.select(columns(&T::a, /* * among other result columns */));",
                          {columnRefStyleDp(1, "&T::a")},
                          {CodegenWarning{message, SourceLocation{1, 1}, 18}}});
    REQUIRE(generateFull("SELECT count(*), * FROM t") ==
            CodeGenResult{
                "auto rows = storage.select(columns(count<T>(), /* * among other result columns */));",
                {},
                {CodegenWarning{message, SourceLocation{1, 1}, 25}}});
    REQUIRE(generateFull("SELECT t.*, * FROM t") ==
            CodeGenResult{
                "auto rows = storage.select(columns(asterisk<T>(), /* * among other result columns */));",
                {},
                {CodegenWarning{message, SourceLocation{1, 1}, 20}}});
    REQUIRE(generateFull("SELECT DISTINCT *, a FROM t") ==
            CodeGenResult{
                "auto rows = storage.select(distinct(columns(/* * among other result columns */, &T::a)));",
                {columnRefStyleDp(1, "&T::a")},
                {CodegenWarning{message, SourceLocation{1, 1}, 27}}});
    // One warning for a result list holding two of them, underlined at the SELECT all the same.
    REQUIRE(generateFull("SELECT *, a, * FROM t") ==
            CodeGenResult{"auto rows = storage.select(columns(/* * among other result columns */, &T::a, "
                          "/* * among other result columns */));",
                          {columnRefStyleDp(1, "&T::a")},
                          {CodegenWarning{message, SourceLocation{1, 1}, 21}}});
    // The underline stops at the end of the line the SELECT starts on.
    REQUIRE(generateFull("SELECT *,\na FROM t") ==
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
    REQUIRE(generateFull("SELECT (SELECT *, a FROM t)") ==
            CodeGenResult{"auto rows = storage.select(/* (SELECT ...) */);",
                          {},
                          {CodegenWarning{"scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 8}, 20},
                           CodegenWarning{message, SourceLocation{1, 9}, 18}}});
    REQUIRE(generateFull("SELECT a FROM t WHERE b IN (SELECT *, c FROM u)") ==
            CodeGenResult{"auto rows = storage.select(&T::a, where(/* IN (SELECT ...) */));",
                          {columnRefStyleDp(1, "&T::a"), columnRefStyleDp(2, "&T::b")},
                          {CodegenWarning{message, SourceLocation{1, 29}, 18},
                           CodegenWarning{"IN (SELECT ...) is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 23}, 25}}});
    REQUIRE(generateFull("CREATE VIEW v AS SELECT *, a FROM t") ==
            CodeGenResult{"/* CREATE VIEW v — not supported for sqlite_orm */",
                          {},
                          {CodegenWarning{message, SourceLocation{1, 18}, 18},
                           CodegenWarning{"CREATE VIEW v: SELECT is not supported for sqlite_orm "
                                          "code generation"}}});
    REQUIRE(generateFull("CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT *, a FROM t; END") ==
            CodeGenResult{"make_trigger(\"tr\", after().insert().on<T>().begin());",
                          {},
                          {CodegenWarning{message, SourceLocation{1, 43}, 18}}});
}
