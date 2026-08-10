#include "codegen_tests_common.hpp"

#include <sqlite2orm/process.h>

namespace {

    /** Codegen of the last statement in a multi-statement batch (views see prior CREATE TABLEs). */
    CodeGenResult generateLastOfBatch(std::string_view sql) {
        const std::vector<ProcessSqlResult> results = processMultiSql(sql, nullptr);
        REQUIRE(!results.empty());
        REQUIRE(results.back().ok());
        return results.back().codegen;
    }

}  // namespace

TEST_CASE("codegen: CREATE VIEW - standalone, types fall back to name heuristics") {
    auto result = generateFull("CREATE VIEW v AS SELECT id, name FROM users;");
    REQUIRE(result.code ==
        "struct [[= \"v\"_orm_name]] V {\n"
        "    int id = 0;\n"
        "    std::string name;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_view<V>(select(columns(&Users::id, &Users::name))));");
    REQUIRE(result.warnings ==
        std::vector<std::string>{
            "view v: type of column `id` could not be inferred; defaulting to int",
            "view v: type of column `name` could not be inferred; defaulting to std::string"});
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

TEST_CASE("codegen: CREATE VIEW - field types from CREATE TABLE in same batch") {
    auto result = generateLastOfBatch(
        "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER NOT NULL);\n"
        "CREATE VIEW adults AS SELECT id, name FROM users WHERE age >= 18;");
    REQUIRE(result.code ==
        "struct [[= \"adults\"_orm_name]] Adults {\n"
        "    int64_t id = 0;\n"
        "    std::optional<std::string> name;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_view<Adults>(select(columns(&Users::id, &Users::name), where(c(&Users::age) >= 18))));");
    REQUIRE(result.warnings.empty());
}

TEST_CASE("codegen: CREATE VIEW - explicit column list names the fields") {
    auto result = generateLastOfBatch(
        "CREATE TABLE t (x INTEGER NOT NULL);\n"
        "CREATE VIEW v2(doubled) AS SELECT x * 2 FROM t;");
    REQUIRE(result.code ==
        "struct [[= \"v2\"_orm_name]] V2 {\n"
        "    int64_t doubled = 0;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_view<V2>(select(c(&T::x) * 2)));");
}

TEST_CASE("codegen: CREATE VIEW - SELECT * expands source table columns") {
    auto result = generateLastOfBatch(
        "CREATE TABLE point (x REAL NOT NULL, y REAL NOT NULL);\n"
        "CREATE VIEW pts AS SELECT * FROM point;");
    REQUIRE(result.code ==
        "struct [[= \"pts\"_orm_name]] Pts {\n"
        "    double x = 0.0;\n"
        "    double y = 0.0;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_view<Pts>(select(asterisk<Point>())));");
}

TEST_CASE("codegen: CREATE VIEW - qualified star with alias expands source table columns") {
    auto result = generateLastOfBatch(
        "CREATE TABLE point (x REAL NOT NULL, y REAL NOT NULL);\n"
        "CREATE VIEW pts2 AS SELECT p.* FROM point p;");
    REQUIRE(result.code ==
        "struct [[= \"pts2\"_orm_name]] Pts2 {\n"
        "    double x = 0.0;\n"
        "    double y = 0.0;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_view<Pts2>(select(asterisk<alias_a<Point>>())));");
}

TEST_CASE("codegen: CREATE VIEW - aggregate functions infer int/double") {
    auto result = generateLastOfBatch(
        "CREATE TABLE emp (salary REAL NOT NULL);\n"
        "CREATE VIEW stats AS SELECT count(*) AS cnt, avg(salary) AS avg_salary FROM emp;");
    REQUIRE(result.code ==
        "struct [[= \"stats\"_orm_name]] Stats {\n"
        "    int cnt = 0;\n"
        "    double avg_salary = 0.0;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_view<Stats>(select(columns(count<Emp>(), avg(&Emp::salary)))));");
    REQUIRE(result.warnings.empty());
}

TEST_CASE("codegen: CREATE VIEW - schema-qualified name warns and uses bare name") {
    auto result = generateFull("CREATE VIEW main.v AS SELECT 1;");
    REQUIRE(result.code ==
        "struct [[= \"v\"_orm_name]] V {\n"
        "    int64_t column_1 = 0;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_view<V>(select(1)));");
    REQUIRE(result.warnings ==
        std::vector<std::string>{
            "schema-qualified view name is not represented in sqlite_orm; generated code uses unqualified "
            "view name only",
            "view v: SELECT column 1 has no name; using synthesized field name `column_1`"});
}
