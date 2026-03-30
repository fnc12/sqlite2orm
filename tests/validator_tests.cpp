#include <sqlite2orm/tokenizer.h>
#include <sqlite2orm/parser.h>
#include <sqlite2orm/validator.h>
#include <catch2/catch_all.hpp>

using namespace sqlite2orm;

namespace {
    std::vector<ValidationError> validate(std::string_view sql) {
        Tokenizer tokenizer;
        auto tokens = tokenizer.tokenize(sql);
        Parser parser;
        auto parseResult = parser.parse(std::move(tokens));
        REQUIRE(parseResult);
        Validator validator;
        return validator.validate(*parseResult.astNodePointer);
    }
}

TEST_CASE("validator: unary plus not supported") {
    REQUIRE(validate("+5") == std::vector<ValidationError>{
        {"unary plus (+expr) is not supported in sqlite_orm", {1, 1}, "UnaryOperatorNode"}
    });
}

TEST_CASE("validator: unary minus is valid") {
    REQUIRE(validate("-5").empty());
}

TEST_CASE("validator: bitwise not is valid") {
    REQUIRE(validate("~5").empty());
}

TEST_CASE("validator: nested unary plus in expression") {
    REQUIRE(validate("a + +b") == std::vector<ValidationError>{
        {"unary plus (+expr) is not supported in sqlite_orm", {1, 5}, "UnaryOperatorNode"}
    });
}

TEST_CASE("validator: no errors for binary operators") {
    REQUIRE(validate("a + b * c").empty());
}

TEST_CASE("validator: literals and columns have no errors") {
    REQUIRE(validate("42").empty());
    REQUIRE(validate("a").empty());
    REQUIRE(validate("'hello'").empty());
}

TEST_CASE("validator: known functions are valid") {
    REQUIRE(validate("abs(a)").empty());
    REQUIRE(validate("count(*)").empty());
    REQUIRE(validate("coalesce(a, b, 0)").empty());
    REQUIRE(validate("lower(name)").empty());
    REQUIRE(validate("round(x, 2)").empty());
    REQUIRE(validate("date('now')").empty());
    REQUIRE(validate("json_extract(data, '$.key')").empty());
}

TEST_CASE("validator: unknown function") {
    REQUIRE(validate("my_custom_func(a)") == std::vector<ValidationError>{
        {"unknown function: my_custom_func", {1, 1}, "FunctionCallNode"}
    });
}

TEST_CASE("validator: known function case insensitive") {
    REQUIRE(validate("ABS(a)").empty());
    REQUIRE(validate("Count(*)").empty());
}

TEST_CASE("validator: CAST is valid") {
    REQUIRE(validate("CAST(a AS INTEGER)").empty());
}

TEST_CASE("validator: CAST validates operand") {
    REQUIRE(validate("CAST(+a AS INTEGER)") == std::vector<ValidationError>{
        {"unary plus (+expr) is not supported in sqlite_orm", {1, 6}, "UnaryOperatorNode"}
    });
}

TEST_CASE("validator: NATURAL LEFT JOIN not supported") {
    REQUIRE(validate("SELECT * FROM users NATURAL LEFT JOIN posts") ==
        std::vector<ValidationError>{
            {"NATURAL LEFT JOIN is not supported in sqlite_orm", {1, 1}, "SelectNode"}
        });
}

TEST_CASE("validator: INNER JOIN without ON fails") {
    REQUIRE(validate("SELECT * FROM users INNER JOIN posts") ==
        std::vector<ValidationError>{
            {"JOIN requires ON or USING clause", {1, 1}, "SelectNode"}
        });
}

TEST_CASE("validator: subselect in FROM not supported") {
    REQUIRE(validate("SELECT n FROM (SELECT 1 AS n) AS t") ==
        std::vector<ValidationError>{
            {"subselect in FROM is not supported in sqlite_orm", {1, 16}, "SelectNode"}});
}

TEST_CASE("validator: CASE is valid") {
    REQUIRE(validate("CASE WHEN a > 0 THEN 1 ELSE 0 END").empty());
}

TEST_CASE("validator: CASE validates branches") {
    REQUIRE(validate("CASE WHEN +a THEN 1 END") == std::vector<ValidationError>{
        {"unary plus (+expr) is not supported in sqlite_orm", {1, 11}, "UnaryOperatorNode"}
    });
}

TEST_CASE("validator: validates function arguments recursively") {
    REQUIRE(validate("abs(+a)") == std::vector<ValidationError>{
        {"unary plus (+expr) is not supported in sqlite_orm", {1, 5}, "UnaryOperatorNode"}
    });
}

// --- Phase 9: CREATE TABLE ---

TEST_CASE("validator: CREATE TABLE is valid") {
    REQUIRE(validate("CREATE TABLE users (id INTEGER, name TEXT)").empty());
}

TEST_CASE("validator: CREATE TABLE - duplicate column") {
    auto errors = validate("CREATE TABLE t (a INTEGER, a TEXT)");
    REQUIRE(errors == std::vector<ValidationError>{
        {"duplicate column name: a", {}, "CreateTableNode"}
    });
}

TEST_CASE("validator: CREATE TABLE - AUTOINCREMENT without PRIMARY KEY") {
    auto errors = validate("CREATE TABLE t (id INTEGER AUTOINCREMENT)");
    REQUIRE(errors == std::vector<ValidationError>{
        {"AUTOINCREMENT requires PRIMARY KEY on column: id", {}, "CreateTableNode"}
    });
}

TEST_CASE("validator: CREATE TABLE - PRIMARY KEY AUTOINCREMENT is valid") {
    REQUIRE(validate("CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT)").empty());
}

TEST_CASE("validator: CREATE TABLE - NOT NULL is valid") {
    REQUIRE(validate("CREATE TABLE t (name TEXT NOT NULL)").empty());
}

TEST_CASE("validator: INSERT VALUES without column list") {
    const std::vector<ValidationError> expectedErrors{
        {"INSERT ... VALUES requires an explicit (column, ...) list for sqlite_orm codegen", {}, "InsertNode"}};
    REQUIRE(validate("INSERT INTO users VALUES (1, 'a')") == expectedErrors);
}

TEST_CASE("validator: CREATE TRIGGER with body") {
    REQUIRE(validate("CREATE TRIGGER tr AFTER INSERT ON users BEGIN DELETE FROM users; END").empty());
}

TEST_CASE("validator: CREATE TRIGGER empty body") {
    CreateTriggerNode node(SourceLocation{});
    node.triggerName = "x";
    node.tableName = "t";
    const std::vector<ValidationError> expectedErrors{
        {"CREATE TRIGGER body must contain at least one statement", {}, "CreateTriggerNode"}};
    Validator validator;
    REQUIRE(validator.validate(node) == expectedErrors);
}

TEST_CASE("validator: RAISE FAIL without message") {
    RaiseNode node(RaiseKind::fail, nullptr, SourceLocation{});
    const std::vector<ValidationError> expectedErrors{
        {"RAISE(ROLLBACK|ABORT|FAIL) requires a message expression", {}, "RaiseNode"}};
    Validator validator;
    REQUIRE(validator.validate(node) == expectedErrors);
}

TEST_CASE("validator: CREATE INDEX with columns") {
    REQUIRE(validate("CREATE INDEX i ON users (id)").empty());
}

TEST_CASE("validator: CREATE INDEX empty column list") {
    CreateIndexNode node(SourceLocation{});
    node.indexName = "bad";
    node.tableName = "t";
    const std::vector<ValidationError> expectedErrors{
        {"CREATE INDEX requires at least one indexed column", {}, "CreateIndexNode"}};
    Validator validator;
    REQUIRE(validator.validate(node) == expectedErrors);
}

TEST_CASE("validator: FTS5 virtual table requires column arguments") {
    const std::vector<ValidationError> expectedErrors{
        {"FTS5 virtual table requires at least one module argument (column)", {}, "CreateVirtualTableNode"}};
    REQUIRE(validate("CREATE VIRTUAL TABLE v USING fts5()") == expectedErrors);
}

TEST_CASE("validator: dbstat at most one argument") {
    const std::vector<ValidationError> expectedErrors{
        {"dbstat virtual table accepts at most one optional schema argument", {}, "CreateVirtualTableNode"}};
    REQUIRE(validate("CREATE VIRTUAL TABLE d USING dbstat('a', 'b')") == expectedErrors);
}

TEST_CASE("validator: RTREE wrong column count") {
    const std::vector<ValidationError> expectedErrors{
        {"RTREE virtual table must declare 3, 5, 7, 9, or 11 column arguments", {}, "CreateVirtualTableNode"}};
    REQUIRE(validate("CREATE VIRTUAL TABLE r USING rtree(a, b)") == expectedErrors);
}

TEST_CASE("validator: RTREE requires arguments") {
    const std::vector<ValidationError> expectedErrors{
        {"RTREE virtual table requires module column arguments", {}, "CreateVirtualTableNode"}};
    REQUIRE(validate("CREATE VIRTUAL TABLE r USING rtree") == expectedErrors);
}

TEST_CASE("validator: valid virtual table statements") {
    REQUIRE(validate("CREATE VIRTUAL TABLE x USING fts5(t)").empty());
    REQUIRE(validate("CREATE VIRTUAL TABLE y USING rtree(i, a, b)").empty());
    REQUIRE(validate("CREATE VIRTUAL TABLE z USING dbstat").empty());
}

TEST_CASE("validator: CREATE VIEW — not supported for codegen (AST is full parse)") {
    REQUIRE(validate("CREATE VIEW v AS SELECT 1;") ==
            std::vector<ValidationError>{
                {"CREATE VIEW v is not supported for sqlite_orm code generation yet", {1, 1}, "CreateViewNode"}});
}

TEST_CASE("validator: CREATE VIEW schema-qualified name in error") {
    REQUIRE(validate("CREATE VIEW main.v AS SELECT 1;") ==
            std::vector<ValidationError>{
                {"CREATE VIEW main.v is not supported for sqlite_orm code generation yet", {1, 1}, "CreateViewNode"}});
}

TEST_CASE("validator: ALTER TABLE points to sync_schema") {
    REQUIRE(validate("ALTER TABLE users ADD COLUMN x INTEGER;") ==
            std::vector<ValidationError>{
                {"ALTER TABLE users is not mapped to standalone calls; in sqlite_orm schema alignment is done via "
                 "storage.sync_schema() from your make_storage(...) definition",
                 {1, 1},
                 "AlterTableStatementNode"}});
}

TEST_CASE("validator: table-function in FROM gives validation error") {
    REQUIRE(validate("SELECT * FROM generate_series(1, 10)") ==
        std::vector<ValidationError>{
            {"table-valued function in FROM is not supported in sqlite_orm codegen",
             {1, 1}, "SelectNode"}});
}

TEST_CASE("validator: bind parameter gives validation error") {
    REQUIRE(validate("SELECT ? FROM t") ==
        std::vector<ValidationError>{
            {"bind parameter ? is not supported — use C++ variables instead",
             {1, 8}, "BindParameterNode"}});
}

TEST_CASE("validator: IN table-name gives validation error") {
    REQUIRE(validate("SELECT * FROM t WHERE x IN tbl") ==
        std::vector<ValidationError>{
            {"IN table-name is not supported in sqlite_orm", {1, 25}, "InNode"}});
}

TEST_CASE("validator: IS expr gives validation error") {
    REQUIRE(validate("SELECT a IS b FROM t") ==
        std::vector<ValidationError>{
            {"binary IS / IS NOT is not supported in sqlite_orm "
             "(only is_null / is_not_null for NULL checks)",
             {1, 10}, "BinaryOperatorNode"}});
}

TEST_CASE("validator: IS NOT expr gives validation error") {
    REQUIRE(validate("SELECT a IS NOT b FROM t") ==
        std::vector<ValidationError>{
            {"binary IS / IS NOT is not supported in sqlite_orm "
             "(only is_null / is_not_null for NULL checks)",
             {1, 10}, "BinaryOperatorNode"}});
}

TEST_CASE("validator: IS DISTINCT FROM gives validation error") {
    REQUIRE(validate("SELECT a IS DISTINCT FROM b FROM t") ==
        std::vector<ValidationError>{
            {"IS [NOT] DISTINCT FROM is not supported in sqlite_orm",
             {1, 10}, "BinaryOperatorNode"}});
}

TEST_CASE("validator: STRICT table gives validation error") {
    REQUIRE(validate("CREATE TABLE t (a TEXT) STRICT") ==
        std::vector<ValidationError>{
            {"STRICT tables are not supported in sqlite_orm",
             {1, 1}, "CreateTableNode"}});
}

TEST_CASE("validator: UPDATE FROM gives validation error") {
    REQUIRE(validate("UPDATE t SET a = 1 FROM b WHERE t.id = b.id") ==
        std::vector<ValidationError>{
            {"UPDATE ... FROM ... is not supported in sqlite_orm",
             {1, 1}, "UpdateNode"}});
}

TEST_CASE("validator: SAVEPOINT gives validation error") {
    REQUIRE(validate("SAVEPOINT sp1") ==
        std::vector<ValidationError>{
            {"SAVEPOINT is not supported in sqlite_orm",
             {1, 1}, "SavepointNode"}});
}

TEST_CASE("validator: RELEASE gives validation error") {
    REQUIRE(validate("RELEASE sp1") ==
        std::vector<ValidationError>{
            {"RELEASE is not supported in sqlite_orm",
             {1, 1}, "ReleaseNode"}});
}

TEST_CASE("validator: ATTACH DATABASE gives validation error") {
    REQUIRE(validate("ATTACH 'test.db' AS aux") ==
        std::vector<ValidationError>{
            {"ATTACH DATABASE is not supported in sqlite_orm",
             {1, 1}, "AttachDatabaseNode"}});
}

TEST_CASE("validator: DETACH DATABASE gives validation error") {
    REQUIRE(validate("DETACH aux") ==
        std::vector<ValidationError>{
            {"DETACH DATABASE is not supported in sqlite_orm",
             {1, 1}, "DetachDatabaseNode"}});
}

TEST_CASE("validator: ANALYZE gives validation error") {
    REQUIRE(validate("ANALYZE") ==
        std::vector<ValidationError>{
            {"ANALYZE is not supported in sqlite_orm",
             {1, 1}, "AnalyzeNode"}});
}

TEST_CASE("validator: REINDEX gives validation error") {
    REQUIRE(validate("REINDEX") ==
        std::vector<ValidationError>{
            {"REINDEX is not supported in sqlite_orm",
             {1, 1}, "ReindexNode"}});
}

TEST_CASE("validator: PRAGMA journal_mode is allowed") {
    REQUIRE(validate("PRAGMA journal_mode") == std::vector<ValidationError>{});
}

TEST_CASE("validator: unknown PRAGMA gives validation error") {
    REQUIRE(validate("PRAGMA foreign_keys") ==
        std::vector<ValidationError>{
            {"PRAGMA foreign_keys is not wrapped by sqlite_orm::storage::pragma "
             "(see sqlite_orm dev/pragma.h for supported pragmas)",
             {1, 1}, "PragmaNode"}});
}

TEST_CASE("validator: schema-qualified PRAGMA gives validation error") {
    REQUIRE(validate("PRAGMA main.journal_mode") ==
        std::vector<ValidationError>{
            {"schema-qualified PRAGMA is not represented in sqlite_orm::storage::pragma "
             "(use the main database connection only)",
             {1, 1}, "PragmaNode"}});
}

TEST_CASE("validator: EXPLAIN gives validation error") {
    REQUIRE(validate("EXPLAIN SELECT 1") ==
        std::vector<ValidationError>{
            {"EXPLAIN is not supported in sqlite_orm",
             {1, 1}, "ExplainNode"}});
}

TEST_CASE("validator: NULLS FIRST gives validation error") {
    REQUIRE(validate("SELECT * FROM t ORDER BY a NULLS FIRST") ==
        std::vector<ValidationError>{
            {"NULLS FIRST / NULLS LAST is not supported in sqlite_orm",
             {1, 1}, "SelectNode"}});
}

TEST_CASE("validator: INSERT RETURNING gives validation error") {
    REQUIRE(validate("INSERT INTO t (a) VALUES (1) RETURNING id") ==
        std::vector<ValidationError>{
            {"RETURNING clause is not supported in sqlite_orm",
             {1, 1}, "InsertNode"}});
}

TEST_CASE("validator: DELETE RETURNING gives validation error") {
    REQUIRE(validate("DELETE FROM t WHERE id = 1 RETURNING id") ==
        std::vector<ValidationError>{
            {"RETURNING clause is not supported in sqlite_orm",
             {1, 1}, "DeleteNode"}});
}
