#include "codegen_tests_common.hpp"

namespace {

    /**
     *  The warning every view carries below C++26 (views need reflection). `displayName` is the
     *  name as it appears in the DDL; the span underlines the statement's opening keywords as the
     *  source writes them, `CREATE VIEW` (11 chars) unless `headerLength` says otherwise.
     */
    CodegenWarning cpp26ViewWarning(const std::string& displayName, size_t line, size_t headerLength = 11) {
        return {"CREATE VIEW " + displayName +
                    ": sqlite_orm views use C++26 reflection (make_view + [[= \"…\"_orm_name]]); this code "
                    "requires C++26 and will not compile under the selected C++ standard",
                SourceLocation{line, 1},
                headerLength};
    }

}  // namespace

TEST_CASE("codegen: CREATE VIEW - standalone, types fall back to name heuristics") {
    auto result = generateFull("CREATE VIEW v AS SELECT id, name FROM users;");
    REQUIRE(result.code == "struct [[= \"v\"_orm_name]] V {\n"
                           "    int id = 0;\n"
                           "    std::string name;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_view<V>(select(columns(&Users::id, &Users::name))));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"view v: type of column `id` could not be inferred; defaulting to int", SourceLocation{1, 25}, 2},
                {"view v: type of column `name` could not be inferred; defaulting to std::string",
                 SourceLocation{1, 29},
                 4},
                cpp26ViewWarning("v", 1)});
}

TEST_CASE("codegen: CREATE VIEW - reflection comment attached") {
    auto result = generateFull("CREATE VIEW v AS SELECT id FROM users;");
    REQUIRE(result.comments ==
            std::vector<std::string>{
                "SQL views map to sqlite_orm's reflection-based `make_view<T>()`: the struct's fields and the "
                "`[[= \"…\"_orm_name]]` annotation require a C++26 compiler with reflection (P2996/P3394). "
                "sqlite_orm detects support automatically (SQLITE_ORM_REFLECTION_SUPPORTED enables "
                "SQLITE_ORM_WITH_VIEW); on older compilers this code does not compile."});
}

// The view body is generated through the subquery form of the SELECT generator, which is where the
// comments its expressions record used to stop: a consumer reading `statements[].comments` of
// `--db --json` saw the reflection note and nothing about the CAST in the generated code.
TEST_CASE("codegen: CREATE VIEW - a comment from the view body is attached too") {
    auto result = generateLastOfBatch("CREATE TABLE t (a INTEGER, b TEXT);\n"
                                      "CREATE VIEW v AS SELECT 1 - (b LIKE 'x') FROM t;");
    REQUIRE(result.code == "struct [[= \"v\"_orm_name]] V {\n"
                           "    int64_t column_1 = 0;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_view<V>(select(c(1) - cast<int64_t>(like(&T::b, \"x\")))));");
    REQUIRE(result.comments ==
            std::vector<std::string>{
                "A predicate under an operator is generated as `cast<int64_t>(predicate)`: sqlite_orm "
                "serializes IN, BETWEEN, LIKE, GLOB, MATCH, IS [NOT] NULL and NOT without parentheses, and "
                "SQLite binds them looser than the operator around them, so `1 - (a IS NULL)` would be read "
                "back as `(1 - a) IS NULL`. The CAST delimits the predicate and leaves what it stands for "
                "alone — a predicate is 0, 1 or NULL, and a CAST to INTEGER keeps all three, typeof included.",
                "SQL views map to sqlite_orm's reflection-based `make_view<T>()`: the struct's fields and the "
                "`[[= \"…\"_orm_name]]` annotation require a C++26 compiler with reflection (P2996/P3394). "
                "sqlite_orm detects support automatically (SQLITE_ORM_REFLECTION_SUPPORTED enables "
                "SQLITE_ORM_WITH_VIEW); on older compilers this code does not compile."});
}

TEST_CASE("codegen: CREATE VIEW - field types from CREATE TABLE in same batch") {
    auto result = generateLastOfBatch("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER NOT NULL);\n"
                                      "CREATE VIEW adults AS SELECT id, name FROM users WHERE age >= 18;");
    REQUIRE(result.code ==
            "struct [[= \"adults\"_orm_name]] Adults {\n"
            "    std::optional<int64_t> id;\n"
            "    std::optional<std::string> name;\n"
            "};\n"
            "\n"
            "auto storage = make_storage(\"\",\n"
            "    make_view<Adults>(select(columns(&Users::id, &Users::name), where(c(&Users::age) >= 18))));");
    REQUIRE(result.warnings == std::vector<CodegenWarning>{cpp26ViewWarning("adults", 2)});
}

TEST_CASE("codegen: CREATE VIEW - explicit column list names the fields") {
    auto result = generateLastOfBatch("CREATE TABLE t (x INTEGER NOT NULL);\n"
                                      "CREATE VIEW v2(doubled) AS SELECT x * 2 FROM t;");
    REQUIRE(result.code == "struct [[= \"v2\"_orm_name]] V2 {\n"
                           "    int64_t doubled = 0;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_view<V2>(select(c(&T::x) * 2)));");
}

TEST_CASE("codegen: CREATE VIEW - SELECT * expands source table columns") {
    auto result = generateLastOfBatch("CREATE TABLE point (x REAL NOT NULL, y REAL NOT NULL);\n"
                                      "CREATE VIEW pts AS SELECT * FROM point;");
    REQUIRE(result.code == "struct [[= \"pts\"_orm_name]] Pts {\n"
                           "    double x = 0.0;\n"
                           "    double y = 0.0;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_view<Pts>(select(asterisk<Point>())));");
}

TEST_CASE("codegen: CREATE VIEW - qualified star with alias expands source table columns") {
    auto result = generateLastOfBatch("CREATE TABLE point (x REAL NOT NULL, y REAL NOT NULL);\n"
                                      "CREATE VIEW pts2 AS SELECT p.* FROM point p;");
    REQUIRE(result.code == "struct [[= \"pts2\"_orm_name]] Pts2 {\n"
                           "    double x = 0.0;\n"
                           "    double y = 0.0;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_view<Pts2>(select(asterisk<alias_a<Point>>())));");
}

TEST_CASE("codegen: CREATE VIEW - aggregate functions infer int/double") {
    auto result =
        generateLastOfBatch("CREATE TABLE emp (salary REAL NOT NULL);\n"
                            "CREATE VIEW stats AS SELECT count(*) AS cnt, avg(salary) AS avg_salary FROM emp;");
    REQUIRE(result.code == "struct [[= \"stats\"_orm_name]] Stats {\n"
                           "    int cnt = 0;\n"
                           "    double avg_salary = 0.0;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_view<Stats>(select(columns(count<Emp>(), avg(&Emp::salary)))));");
    REQUIRE(result.warnings == std::vector<CodegenWarning>{cpp26ViewWarning("stats", 2)});
}

TEST_CASE("codegen: CREATE VIEW - schema-qualified name warns and uses bare name") {
    auto result = generateFull("CREATE VIEW main.v AS SELECT 1;");
    REQUIRE(result.code == "struct [[= \"v\"_orm_name]] V {\n"
                           "    int64_t column_1 = 0;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_view<V>(select(1)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                "schema-qualified view name is not represented in sqlite_orm; generated code uses unqualified "
                "view name only",
                "view v: SELECT column 1 has no name; using synthesized field name `column_1`",
                cpp26ViewWarning("main.v", 1)});
}

// A view column keeps the type of the literal behind it, and a literal an int64 cannot hold is a
// REAL for SQLite: `CREATE VIEW v AS SELECT 99999999999999999999` has a real column, not an
// integer one. Checked against sqlite3 3.51.
TEST_CASE("codegen: CREATE VIEW - column of an integer literal beyond int64 is a double") {
    auto result = generateFull("CREATE VIEW v AS SELECT 99999999999999999999;");
    REQUIRE(result.code == "struct [[= \"v\"_orm_name]] V {\n"
                           "    double column_1 = 0.0;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_view<V>(select(99999999999999999999.0)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{"view v: SELECT column 1 has no name; using synthesized field name `column_1`",
                                        cpp26ViewWarning("v", 1)});
}

// A CASE has no type of its own in SQLite: it answers with the value of whichever branch matched,
// and the view field every row reaches holds one type. Taken from the first branch alone, the
// field truncated the wider branches — both columns below were `int64_t` fields — and it disagreed
// with the `case_<R>` generated for that very column, which widens over all of them (card
// 1866790966522808145). The field a column of an integer and a REAL branch lands in is the
// `double` that holds both exactly; a branch only an int64 holds takes the pair to the text that
// keeps both, the same way the `case_<R>` beside it does.
TEST_CASE("codegen: CREATE VIEW - a CASE column is typed over every branch and the ELSE") {
    auto result =
        generateLastOfBatch("CREATE TABLE t (a INTEGER);\n"
                            "CREATE VIEW v AS SELECT CASE WHEN a < 0 THEN 1 ELSE 1.5 END AS x,\n"
                            "                        CASE WHEN a < 0 THEN 1 ELSE 'z' END AS y,\n"
                            "                        CASE WHEN a < 0 THEN 9223372036854775807 ELSE 1.5 END AS z\n"
                            "                 FROM t;");
    REQUIRE(result.code == "struct [[= \"v\"_orm_name]] V {\n"
                           "    double x = 0.0;\n"
                           "    std::string y;\n"
                           "    std::string z;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_view<V>(select(columns(case_<double>().when(c(&T::a) < 0, then(1)).else_(1.5)"
                           ".end(), case_<std::string>().when(c(&T::a) < 0, then(1)).else_(\"z\").end(), "
                           "case_<std::string>().when(c(&T::a) < 0, then(9223372036854775807)).else_(1.5)"
                           ".end()))));");
    REQUIRE(result.warnings == std::vector<CodegenWarning>{cpp26ViewWarning("v", 2)});
}

// The column type the FROM tables know is what a branch naming one contributes, and an INTEGER
// column holds values a `double` drops, so a column beside a REAL branch reaches the text that
// keeps both. The `case_<double>` of the same column is the expression side reading a bare column
// reference through the default `int` — the gap the CASE widening cannot close on its own — and
// the field stays the wider of the two answers, which is the side that loses nothing.
TEST_CASE("codegen: CREATE VIEW - a CASE branch naming a column is as wide as the column") {
    auto result = generateLastOfBatch("CREATE TABLE t (a INTEGER);\n"
                                      "CREATE VIEW v AS SELECT CASE WHEN a < 0 THEN a ELSE 1.5 END AS x FROM t;");
    REQUIRE(result.code == "struct [[= \"v\"_orm_name]] V {\n"
                           "    std::optional<std::string> x;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_view<V>(select(case_<double>().when(c(&T::a) < 0, then(&T::a)).else_(1.5)"
                           ".end())));");
    REQUIRE(result.warnings == std::vector<CodegenWarning>{cpp26ViewWarning("v", 2)});
}

// A CASE no branch matches and no ELSE answers is NULL — `SELECT CASE WHEN 0 THEN 1 END` is one —
// and so is a branch spelling a NULL out, so the field holds an optional either way. The
// nullability used to come off the first branch alone as well.
TEST_CASE("codegen: CREATE VIEW - a CASE that can answer NULL is an optional field") {
    auto result = generateLastOfBatch("CREATE TABLE t (a INTEGER NOT NULL);\n"
                                      "CREATE VIEW v AS SELECT CASE WHEN a < 0 THEN a END AS x,\n"
                                      "                        CASE WHEN a < 0 THEN NULL ELSE a END AS y,\n"
                                      "                        CASE WHEN a < 0 THEN a ELSE 0 END AS z FROM t;");
    REQUIRE(result.code == "struct [[= \"v\"_orm_name]] V {\n"
                           "    std::optional<int64_t> x;\n"
                           "    std::optional<int64_t> y;\n"
                           "    int64_t z = 0;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_view<V>(select(columns(case_<int>().when(c(&T::a) < 0, then(&T::a)).end(), "
                           "case_<int>().when(c(&T::a) < 0, then(nullptr)).else_(&T::a).end(), "
                           "case_<int>().when(c(&T::a) < 0, then(&T::a)).else_(0).end()))));");
    REQUIRE(result.warnings == std::vector<CodegenWarning>{cpp26ViewWarning("v", 2)});
}

// A BLOB branch beside a non-BLOB one has no field type that keeps both: an `std::string` stops at
// the first NUL byte a blob holds, and the `std::vector<char>` that does read every storage class
// whole is a type the expression side cannot name for the same column. The column is left
// uninferred — warned about and defaulted — rather than typed by the order the branches are
// written in.
TEST_CASE("codegen: CREATE VIEW - a CASE over a BLOB and a non-BLOB branch is left uninferred") {
    auto result = generateLastOfBatch("CREATE TABLE t (a INTEGER, b BLOB);\n"
                                      "CREATE VIEW v AS SELECT CASE WHEN a < 0 THEN b ELSE 1 END AS x FROM t;");
    REQUIRE(result.code == "struct [[= \"v\"_orm_name]] V {\n"
                           "    int x = 0;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_view<V>(select(case_<int>().when(c(&T::a) < 0, then(&T::b)).else_(1).end())));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{"view v: type of column `x` could not be inferred; defaulting to int",
                                        cpp26ViewWarning("v", 2)});
}

TEST_CASE("codegen: view column-type warning carries a source location to underline") {
    // `id` sits at line 1, column 25 of the SQL and is 2 characters long.
    auto result = generateFull("CREATE VIEW v AS SELECT id FROM users;");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"view v: type of column `id` could not be inferred; defaulting to int", SourceLocation{1, 25}, 2},
                cpp26ViewWarning("v", 1)});
}

// A location and a length count characters, not the bytes they take. SQLite takes non-ASCII in a
// bare identifier as readily as in a quoted one, so a view named `«ü»` — six bytes, three
// characters — must not shift the columns reported for what follows it, and a consumer holding
// the SQL as text underlines exactly `id` and `name`.
TEST_CASE("codegen: a non-ASCII view name does not shift the columns it is followed by") {
    auto result = generateFull("CREATE VIEW «ü» AS SELECT id, name FROM users;");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"view «ü»: type of column `id` could not be inferred; defaulting to int", SourceLocation{1, 27}, 2},
                {"view «ü»: type of column `name` could not be inferred; defaulting to std::string",
                 SourceLocation{1, 31},
                 4},
                cpp26ViewWarning("«ü»", 1)});
}

// The length is measured the same way: `ключ` is four characters written with eight bytes, and
// underlining eight of them would run past the column the message is about.
TEST_CASE("codegen: a non-ASCII column is underlined for as many characters as it is written with") {
    auto result = generateFull("CREATE VIEW v AS SELECT ключ FROM users;");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"view v: type of column `ключ` could not be inferred; defaulting to int", SourceLocation{1, 25}, 4},
                cpp26ViewWarning("v", 1)});
}

// The opening keywords are underlined as the source spells them, not as the message spells them
// back: SQLite takes any whitespace between CREATE and VIEW, and a consumer draws the underline
// along one line, so a statement broken across lines underlines what stands on the first one.
TEST_CASE("codegen: CREATE VIEW split across two lines underlines CREATE alone") {
    auto result = generateFull("CREATE\nVIEW v AS SELECT id FROM users;");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"view v: type of column `id` could not be inferred; defaulting to int", SourceLocation{2, 18}, 2},
                cpp26ViewWarning("v", 1, 6)});
}

TEST_CASE("codegen: CREATE VIEW written with two spaces underlines both keywords") {
    auto result = generateFull("CREATE  VIEW v AS SELECT id FROM users;");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"view v: type of column `id` could not be inferred; defaulting to int", SourceLocation{1, 26}, 2},
                cpp26ViewWarning("v", 1, 12)});
}

TEST_CASE("codegen: CREATE TEMP VIEW underlines the three keywords it is written with") {
    auto result = generateFull("CREATE TEMP VIEW v AS SELECT id FROM users;");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"view v: type of column `id` could not be inferred; defaulting to int", SourceLocation{1, 30}, 2},
                cpp26ViewWarning("v", 1, 16)});
}

// SQLite reads a newline inside a quoted name as part of the name, so the column reference spans
// two lines while its underline runs along the first one.
TEST_CASE("codegen: a view column quoted across two lines underlines its first line") {
    auto result = generateFull("CREATE VIEW v AS SELECT \"a\nb\" FROM users;");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"view v: type of column `a\nb` could not be inferred; defaulting to int", SourceLocation{1, 25}, 2},
                cpp26ViewWarning("v", 1)});
}

// The name the field ends up carrying does not move the underline: it stays on the expression
// whose type could not be inferred, so a view column list longer than the expression it renames
// still underlines the expression.
TEST_CASE("codegen: a view column list renaming a column underlines the column, not the name") {
    auto result = generateFull("CREATE VIEW v(averylongname) AS SELECT id FROM users;");
    REQUIRE(result.warnings == std::vector<CodegenWarning>{
                                   {"view v: type of column `averylongname` could not be inferred; defaulting to int",
                                    SourceLocation{1, 40},
                                    2},
                                   cpp26ViewWarning("v", 1)});
}

TEST_CASE("codegen: a view column alias underlines the aliased column, not the alias") {
    auto result = generateFull("CREATE VIEW v AS SELECT id AS averylongalias FROM users;");
    REQUIRE(result.warnings == std::vector<CodegenWarning>{
                                   {"view v: type of column `averylongalias` could not be inferred; defaulting to int",
                                    SourceLocation{1, 25},
                                    2},
                                   cpp26ViewWarning("v", 1)});
}

// The field name a qualified reference gives the view stands nowhere in the SELECT by itself: `id`
// is written after `users.`, and the expression starts at the table name. Underlining the name's
// length from there would cover text the message is not about, so the warning carries no span.
TEST_CASE("codegen: a view column-type warning over a qualified reference is left unanchored") {
    auto result = generateFull("CREATE VIEW v AS SELECT users.id FROM users;");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{{"view v: type of column `id` could not be inferred; defaulting to int"},
                                        cpp26ViewWarning("v", 1)});
}

TEST_CASE("codegen: targeting C++26 drops the reflection-not-supported view warning") {
    CodeGenPolicy policy;
    policy.targetCppStandard = 26;
    auto result = generateWithPolicy("CREATE VIEW v AS SELECT id FROM users;", policy);
    // Only the column-type inference warning remains; the C++26 gate warning is gone.
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"view v: type of column `id` could not be inferred; defaulting to int", SourceLocation{1, 25}, 2}});
}

// A view body is stored, not compiled, so SQLite accepts a hex literal in it that it refuses in a
// query of its own; `SELECT * FROM v` is then `Error: in prepare, hex literal too big`. C++ has no
// literal for the value, so the view cannot be generated — but the statement is not an error, and
// the rest of a schema holding it still generates. Checked against sqlite3 3.51.
TEST_CASE("codegen: CREATE VIEW - a hex literal too big leaves the view ungenerated") {
    auto result = generateFull("CREATE VIEW v AS SELECT 0x10000000000000000;");
    REQUIRE(result.code == "/* CREATE VIEW v — not supported for sqlite_orm */");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"CREATE VIEW v uses 0x10000000000000000, too big for a signed 64-bit integer: SQLite stores the view "
                 "but refuses every query against it, and C++ has no literal for it, so the view is not generated"}});
    REQUIRE(result.errors.empty());
}
