#include <sqlite2orm/process.h>
#include <sqlite2orm/ast.h>
#include <sqlite2orm/tokenizer.h>
#include <sqlite2orm/parser.h>
#include <sqlite2orm/validator.h>
#include <sqlite2orm/codegen.h>
#include <catch2/catch_all.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

using namespace sqlite2orm;

namespace {

    template<typename NodeType, typename... Args>
    AstNodePointer makeNode(Args&&... args) {
        return std::make_unique<NodeType>(std::forward<Args>(args)..., SourceLocation{});
    }

}

namespace {

    ProcessSqlResult expectedFromPipeline(std::string_view sql) {
        ProcessSqlResult processSqlResult;
        Tokenizer tokenizer;
        auto tokens = tokenizer.tokenize(sql);
        Parser parser;
        processSqlResult.parseResult = parser.parse(std::move(tokens));
        if(!processSqlResult.parseResult.astNodePointer) {
            return processSqlResult;
        }
        Validator validator;
        processSqlResult.validationErrors =
            validator.validate(*processSqlResult.parseResult.astNodePointer);
        if(!processSqlResult.validationErrors.empty()) {
            return processSqlResult;
        }
        CodeGenerator codeGenerator;
        processSqlResult.codegen =
            codeGenerator.generate(*processSqlResult.parseResult.astNodePointer);
        return processSqlResult;
    }

    ProcessSqlResult expectedTokenizerFailure(std::string_view sql) {
        ProcessSqlResult processSqlResult;
        try {
            Tokenizer tokenizer;
            tokenizer.tokenize(sql);
        } catch(const TokenizeError& error) {
            processSqlResult.parseResult.errors.push_back(
                ParseError{std::string(error.what()), error.location});
        }
        return processSqlResult;
    }

}  // namespace

TEST_CASE("processSql: valid SELECT") {
    const ProcessSqlResult expectedOutcome = expectedFromPipeline("SELECT 1");
    REQUIRE(processSql("SELECT 1") == expectedOutcome);
}

TEST_CASE("processSql: UTF-8 BOM then SQL") {
    const std::string sqlWithBom = std::string("\xEF\xBB\xBF") + "SELECT 1";
    const ProcessSqlResult expectedOutcome = expectedFromPipeline(sqlWithBom);
    REQUIRE(processSql(sqlWithBom) == expectedOutcome);
}

TEST_CASE("processSql: tokenizer error becomes parseResult.errors") {
    const ProcessSqlResult expectedOutcome = expectedTokenizerFailure("'");
    REQUIRE(processSql("'") == expectedOutcome);
}

// A unary plus is an identity SQLite applies to any expression, so the pipeline carries the
// statement through to code instead of stopping at validation the way it used to: `sqlite2orm -e
// \'SELECT +a;\'` exited 1 on `unary plus (+expr) is not supported in sqlite_orm`, which took the
// statement out of a generated schema altogether.
TEST_CASE("processSql: a unary plus reaches codegen") {
    const ProcessSqlResult result = processSql("SELECT +a;");
    REQUIRE(result.parseResult.errors.empty());
    REQUIRE(result.validationErrors.empty());
    REQUIRE(result.ok());
    REQUIRE(result.codegen.code == "auto rows = storage.select(&User::a);");
}

TEST_CASE("processSql: CREATE VIEW generates make_view") {
    const ProcessSqlResult expected = expectedFromPipeline("CREATE VIEW v AS SELECT 1;");
    REQUIRE(processSql("CREATE VIEW v AS SELECT 1;") == expected);
}

TEST_CASE("processSql: PRAGMA user_version maps to storage.pragma") {
    const ProcessSqlResult expected = expectedFromPipeline("PRAGMA user_version=1;");
    REQUIRE(processSql("PRAGMA user_version=1;") == expected);
}

TEST_CASE("processSql: a codegen error is not an ok result") {
    const ProcessSqlResult result = processSql("PRAGMA recursive_triggers = NULL;");
    REQUIRE(result.codegen.code == "");
    REQUIRE(result.codegen.errors ==
            std::vector<std::string>{
                "PRAGMA recursive_triggers = …: expected a number or a name, as in 0/1, TRUE/FALSE or ON/OFF"});
    REQUIRE(result.ok() == false);
}

TEST_CASE("processSql: WITH … SELECT pipeline") {
    const ProcessSqlResult expected = expectedFromPipeline("WITH c AS (SELECT 1) SELECT 1;");
    REQUIRE(processSql("WITH c AS (SELECT 1) SELECT 1;") == expected);
}

TEST_CASE("processSql: WITH … INSERT pipeline") {
    const ProcessSqlResult expected = expectedFromPipeline("WITH c AS (SELECT 1) INSERT INTO t (x) VALUES (1);");
    REQUIRE(processSql("WITH c AS (SELECT 1) INSERT INTO t (x) VALUES (1);") == expected);
}

TEST_CASE("processMultiSql: multiple statements") {
    const auto results = processMultiSql("SELECT 1; SELECT 2;");
    REQUIRE(results.size() == 2);
    REQUIRE(results[0] == processSql("SELECT 1;"));
    // The second statement is the same pipeline, except that its result variable is unique in the batch.
    REQUIRE(results[1].codegen.code == "auto rows2 = storage.select(2);");
    REQUIRE(results[1].parseResult == processSql("SELECT 2;").parseResult);
    REQUIRE(results[1].validationErrors.empty());
}

TEST_CASE("processMultiSql: CREATE TABLE + INSERT") {
    std::vector<ProcessSqlResult> expected;
    expected.push_back(processSql("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT);"));
    expected.push_back(processSql("INSERT INTO t (id, name) VALUES (1, 'Alice');"));
    REQUIRE(processMultiSql(
        "CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT);"
        "INSERT INTO t (id, name) VALUES (1, 'Alice');") == expected);
}

TEST_CASE("processMultiSql: CREATE TRIGGER body semicolons do not split the statement") {
    std::vector<ProcessSqlResult> expected;
    expected.push_back(processSql(
        "CREATE TRIGGER tx_delete AFTER DELETE ON transactions BEGIN "
        "DELETE FROM tx_rtree WHERE id = old.id; END;"));
    expected.push_back(processSql("SELECT 1;"));
    REQUIRE(processMultiSql(
        "CREATE TRIGGER tx_delete AFTER DELETE ON transactions BEGIN\n"
        "    DELETE FROM tx_rtree WHERE id = old.id;\n"
        "END;\n"
        "SELECT 1;") == expected);
}

TEST_CASE("processMultiSql: CREATE TRIGGER with multiple body statements and CASE END") {
    std::vector<ProcessSqlResult> expected;
    expected.push_back(processSql("CREATE TABLE a (id INTEGER PRIMARY KEY, x INTEGER);"));
    expected.push_back(processSql(
        "CREATE TRIGGER t AFTER INSERT ON a BEGIN "
        "UPDATE a SET x = CASE WHEN new.x > 0 THEN 1 ELSE 0 END; "
        "DELETE FROM a WHERE id = old.id; END;"));
    expected.push_back(processSql("SELECT 2;"));
    REQUIRE(processMultiSql(
        "CREATE TABLE a (id INTEGER PRIMARY KEY, x INTEGER);"
        "CREATE TRIGGER t AFTER INSERT ON a BEGIN "
        "UPDATE a SET x = CASE WHEN new.x > 0 THEN 1 ELSE 0 END; "
        "DELETE FROM a WHERE id = old.id; END;"
        "SELECT 2;") == expected);
}

TEST_CASE("processMultiSql: CREATE TEMP TRIGGER body semicolons do not split the statement") {
    std::vector<ProcessSqlResult> expected;
    expected.push_back(processSql("CREATE TEMP TRIGGER tt BEFORE INSERT ON x BEGIN DELETE FROM x; END;"));
    expected.push_back(processSql("SELECT 3;"));
    REQUIRE(processMultiSql(
        "CREATE TEMP TRIGGER tt BEFORE INSERT ON x BEGIN DELETE FROM x; END; SELECT 3;") == expected);
}

TEST_CASE("joinGeneratedCode: DDL statements merge into a single make_storage") {
    const auto results = processMultiSql(
        "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER NOT NULL);\n"
        "CREATE VIEW adults AS SELECT id, name FROM users WHERE age >= 18;");
    REQUIRE(joinGeneratedCode(results) ==
        "struct Users {\n"
        "    int64_t id = 0;\n"
        "    std::optional<std::string> name;\n"
        "    int64_t age = 0;\n"
        "};\n"
        "\n"
        "struct [[= \"adults\"_orm_name]] Adults {\n"
        "    int64_t id = 0;\n"
        "    std::optional<std::string> name;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"users\",\n"
        "        make_column(\"id\", &Users::id, primary_key()),\n"
        "        make_column(\"name\", &Users::name),\n"
        "        make_column(\"age\", &Users::age)),\n"
        "    make_view<Adults>(select(columns(&Users::id, &Users::name), where(c(&Users::age) >= 18))));\n");
}

TEST_CASE("joinGeneratedCode: index merges into make_storage, DML follows after blank line") {
    const auto results = processMultiSql(
        "CREATE TABLE t (id INTEGER PRIMARY KEY, x INTEGER);"
        "CREATE INDEX idx ON t(x);"
        "SELECT x FROM t;");
    REQUIRE(joinGeneratedCode(results) ==
        "struct T {\n"
        "    int64_t id = 0;\n"
        "    std::optional<int64_t> x;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"id\", &T::id, primary_key()),\n"
        "        make_column(\"x\", &T::x)),\n"
        "    make_index(\"idx\", indexed_column(&T::x)));\n"
        "\n"
        "auto rows = storage.select(&T::x);\n");
}

TEST_CASE("joinGeneratedCode: DML-only batch keeps statements, uniques the result names") {
    const auto results = processMultiSql("SELECT 1; SELECT 2;");
    REQUIRE(joinGeneratedCode(results) ==
        "auto rows = storage.select(1);\n"
        "auto rows2 = storage.select(2);\n");
}

TEST_CASE("processMultiSql: validation error does not block other statements") {
    std::vector<ProcessSqlResult> expected;
    expected.push_back(processSql("INSERT INTO t VALUES (1);"));
    expected.push_back(processSql("SELECT 42;"));
    REQUIRE(processMultiSql("INSERT INTO t VALUES (1); SELECT 42;") == expected);
}

TEST_CASE("processMultiSql: CREATE TABLE org + INSERTs without column list") {
    auto makeCreateTableNode = [] {
        ColumnDef nameCol;
        nameCol.name = "name";
        nameCol.typeName = "TEXT";
        nameCol.primaryKey = true;
        ColumnDef bossCol;
        bossCol.name = "boss";
        bossCol.typeName = "TEXT";
        bossCol.foreignKey = ForeignKeyClause{.table = "org", .column = ""};
        auto node = std::make_unique<CreateTableNode>("org",
            std::vector<ColumnDef>{nameCol, bossCol}, false, SourceLocation{});
        node->withoutRowid = true;
        return node;
    };

    auto makeInsertNode = [](std::vector<AstNodePointer> row) {
        auto node = std::make_unique<InsertNode>(SourceLocation{});
        node->tableName = "org";
        node->dataKind = InsertDataKind::values;
        node->valueRows.push_back(std::move(row));
        return node;
    };

    std::vector<ProcessSqlResult> expected;

    ProcessSqlResult createResult;
    createResult.parseResult = ParseResult{makeCreateTableNode(), {}};
    createResult.codegen = CodeGenResult{
        .code = "struct Org {\n"
                "    std::string name;\n"
                "    std::optional<std::string> boss;\n"
                "};\n"
                "\n"
                "auto storage = make_storage(\"\",\n"
                "    make_table(\"org\",\n"
                "        make_column(\"name\", &Org::name, primary_key()),\n"
                "        make_column(\"boss\", &Org::boss),\n"
                "        foreign_key(&Org::boss).references(&Org::name)).without_rowid());"};
    expected.push_back(std::move(createResult));

    struct InsertRow { std::string_view name; bool bossNull; std::string_view boss; std::string_view code; };
    const std::array insertRows{
        InsertRow{"'Alice'", true, {}, R"(storage.insert(Org{"Alice", std::nullopt});)"},
        InsertRow{"'Bob'", false, "'Alice'", R"(storage.insert(Org{"Bob", "Alice"});)"},
        InsertRow{"'Cindy'", false, "'Alice'", R"(storage.insert(Org{"Cindy", "Alice"});)"},
        InsertRow{"'Dave'", false, "'Bob'", R"(storage.insert(Org{"Dave", "Bob"});)"},
        InsertRow{"'Emma'", false, "'Bob'", R"(storage.insert(Org{"Emma", "Bob"});)"},
        InsertRow{"'Fred'", false, "'Cindy'", R"(storage.insert(Org{"Fred", "Cindy"});)"},
        InsertRow{"'Gail'", false, "'Cindy'", R"(storage.insert(Org{"Gail", "Cindy"});)"},
    };
    for(const auto& [name, bossNull, boss, code] : insertRows) {
        std::vector<AstNodePointer> row;
        row.push_back(makeNode<StringLiteralNode>(name));
        if(bossNull) {
            row.push_back(makeNode<NullLiteralNode>());
        } else {
            row.push_back(makeNode<StringLiteralNode>(boss));
        }
        ProcessSqlResult insertResult;
        insertResult.parseResult = ParseResult{makeInsertNode(std::move(row)), {}};
        insertResult.codegen = CodeGenResult{.code = std::string(code)};
        expected.push_back(std::move(insertResult));
    }

    REQUIRE(processMultiSql(
        "CREATE TABLE org(name TEXT PRIMARY KEY, boss TEXT REFERENCES org) WITHOUT ROWID;"
        "INSERT INTO org VALUES('Alice', NULL);"
        "INSERT INTO org VALUES('Bob', 'Alice');"
        "INSERT INTO org VALUES('Cindy', 'Alice');"
        "INSERT INTO org VALUES('Dave', 'Bob');"
        "INSERT INTO org VALUES('Emma', 'Bob');"
        "INSERT INTO org VALUES('Fred', 'Cindy');"
        "INSERT INTO org VALUES('Gail', 'Cindy');") == expected);
}

TEST_CASE("processMultiSql: CREATE TABLE org + INSERTs with column list") {
    auto makeCreateTableNode = [] {
        ColumnDef nameCol;
        nameCol.name = "name";
        nameCol.typeName = "TEXT";
        nameCol.primaryKey = true;
        ColumnDef bossCol;
        bossCol.name = "boss";
        bossCol.typeName = "TEXT";
        bossCol.foreignKey = ForeignKeyClause{.table = "org", .column = ""};
        auto node = std::make_unique<CreateTableNode>("org",
            std::vector<ColumnDef>{nameCol, bossCol}, false, SourceLocation{});
        node->withoutRowid = true;
        return node;
    };

    auto makeInsertNode = [](std::string_view nameVal, bool bossNull, std::string_view bossVal) {
        auto node = std::make_unique<InsertNode>(SourceLocation{});
        node->tableName = "org";
        node->columnNames = {"name", "boss"};
        node->dataKind = InsertDataKind::values;
        std::vector<AstNodePointer> row;
        row.push_back(makeNode<StringLiteralNode>(nameVal));
        if(bossNull) {
            row.push_back(makeNode<NullLiteralNode>());
        } else {
            row.push_back(makeNode<StringLiteralNode>(bossVal));
        }
        node->valueRows.push_back(std::move(row));
        return node;
    };


    std::vector<ProcessSqlResult> expected;

    ProcessSqlResult createResult;
    createResult.parseResult = ParseResult{makeCreateTableNode(), {}};
    createResult.codegen = CodeGenResult{
        .code = "struct Org {\n"
                "    std::string name;\n"
                "    std::optional<std::string> boss;\n"
                "};\n"
                "\n"
                "auto storage = make_storage(\"\",\n"
                "    make_table(\"org\",\n"
                "        make_column(\"name\", &Org::name, primary_key()),\n"
                "        make_column(\"boss\", &Org::boss),\n"
                "        foreign_key(&Org::boss).references(&Org::name)).without_rowid());"};
    expected.push_back(std::move(createResult));

    struct InsertCase { std::string_view name; bool bossNull; std::string_view boss; std::string_view code; };
    const std::array insertCases{
        InsertCase{"'Alice'", true, {},
                   R"(storage.insert(into<Org>(), columns(&Org::name, &Org::boss), values(std::make_tuple("Alice", nullptr)));)"},
        InsertCase{"'Bob'", false, "'Alice'",
                   R"(storage.insert(into<Org>(), columns(&Org::name, &Org::boss), values(std::make_tuple("Bob", "Alice")));)"},
        InsertCase{"'Cindy'", false, "'Alice'",
                   R"(storage.insert(into<Org>(), columns(&Org::name, &Org::boss), values(std::make_tuple("Cindy", "Alice")));)"},
    };
    for(const auto& [name, bossNull, boss, code] : insertCases) {
        ProcessSqlResult insertResult;
        insertResult.parseResult = ParseResult{makeInsertNode(name, bossNull, boss), {}};
        insertResult.codegen = CodeGenResult{.code = std::string(code)};
        expected.push_back(std::move(insertResult));
    }

    REQUIRE(processMultiSql(
        "CREATE TABLE org(name TEXT PRIMARY KEY, boss TEXT REFERENCES org) WITHOUT ROWID;"
        "INSERT INTO org(name, boss) VALUES('Alice', NULL);"
        "INSERT INTO org(name, boss) VALUES('Bob', 'Alice');"
        "INSERT INTO org(name, boss) VALUES('Cindy', 'Alice');") == expected);
}

TEST_CASE("processSql: BEGIN TRANSACTION") {
    const ProcessSqlResult expected = expectedFromPipeline("BEGIN TRANSACTION;");
    REQUIRE(processSql("BEGIN TRANSACTION;") == expected);
}

TEST_CASE("processSql: SAVEPOINT lifecycle") {
    const ProcessSqlResult expected =
        expectedFromPipeline("SAVEPOINT sp1; ROLLBACK TO SAVEPOINT sp1; RELEASE SAVEPOINT sp1;");
    REQUIRE(processSql("SAVEPOINT sp1; ROLLBACK TO SAVEPOINT sp1; RELEASE SAVEPOINT sp1;") == expected);
}

TEST_CASE("joinGeneratedCode: functional savepoints wrap the statements up to RELEASE") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["savepoint_style"] = "functional";
    const auto results = processMultiSql(
        "SAVEPOINT sp1; DELETE FROM t; RELEASE sp1;", &policy);
    REQUIRE(joinGeneratedCode(results) ==
        "storage.savepoint(\"sp1\", [&] {\n"
        "    storage.remove_all<T>();\n"
        "    return true;\n"
        "});\n");
}

TEST_CASE("joinGeneratedCode: functional savepoints nest") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["savepoint_style"] = "functional";
    const auto results = processMultiSql(
        "SAVEPOINT outer_sp; DELETE FROM t; SAVEPOINT inner_sp; DELETE FROM u; RELEASE inner_sp; "
        "RELEASE outer_sp;",
        &policy);
    REQUIRE(joinGeneratedCode(results) ==
        "storage.savepoint(\"outer_sp\", [&] {\n"
        "    storage.remove_all<T>();\n"
        "    storage.savepoint(\"inner_sp\", [&] {\n"
        "        storage.remove_all<U>();\n"
        "        return true;\n"
        "    });\n"
        "    return true;\n"
        "});\n");
}

TEST_CASE("joinGeneratedCode: functional savepoint without RELEASE degrades to the manual call") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["savepoint_style"] = "functional";
    const auto results = processMultiSql("SAVEPOINT sp1; DELETE FROM t;", &policy);
    REQUIRE(joinGeneratedCode(results) ==
        "storage.savepoint(\"sp1\");\n"
        "storage.remove_all<T>();\n");
}

TEST_CASE("joinGeneratedCode: ROLLBACK TO inside a functional savepoint keeps the direct call") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["savepoint_style"] = "functional";
    const auto results = processMultiSql(
        "SAVEPOINT sp1; DELETE FROM t; ROLLBACK TO SAVEPOINT sp1; RELEASE sp1;", &policy);
    REQUIRE(joinGeneratedCode(results) ==
        "storage.savepoint(\"sp1\", [&] {\n"
        "    storage.remove_all<T>();\n"
        "    storage.rollback_to_savepoint(\"sp1\");\n"
        "    return true;\n"
        "});\n");
}

TEST_CASE("joinGeneratedCode: guard style - same-name savepoints get unique variables") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["savepoint_style"] = "guard";
    const auto results = processMultiSql(
        "SAVEPOINT sp; SAVEPOINT sp; RELEASE sp; RELEASE sp;", &policy);
    REQUIRE(joinGeneratedCode(results) ==
        "auto sp_savepoint = storage.savepoint_guard(\"sp\");\n"
        "auto sp_savepoint_2 = storage.savepoint_guard(\"sp\");\n"
        "sp_savepoint_2.release();\n"
        "sp_savepoint.release();\n");
}

TEST_CASE("joinGeneratedCode: guard style - RELEASE of an outer savepoint pops the inner ones") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["savepoint_style"] = "guard";
    // RELEASE a also releases b (SQLite stack semantics), so a later SAVEPOINT b
    // must get a fresh variable, not collide with the popped one.
    const auto results = processMultiSql(
        "SAVEPOINT a; SAVEPOINT b; RELEASE a; SAVEPOINT b; RELEASE b;", &policy);
    REQUIRE(joinGeneratedCode(results) ==
        "auto a_savepoint = storage.savepoint_guard(\"a\");\n"
        "auto b_savepoint = storage.savepoint_guard(\"b\");\n"
        "a_savepoint.release();\n"
        "auto b_savepoint_2 = storage.savepoint_guard(\"b\");\n"
        "b_savepoint_2.release();\n");
}

TEST_CASE("joinGeneratedCode: guard style - ROLLBACK TO resolves to the innermost same-name savepoint") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["savepoint_style"] = "guard";
    const auto results = processMultiSql(
        "SAVEPOINT sp; SAVEPOINT sp; ROLLBACK TO sp; RELEASE sp; RELEASE sp;", &policy);
    REQUIRE(joinGeneratedCode(results) ==
        "auto sp_savepoint = storage.savepoint_guard(\"sp\");\n"
        "auto sp_savepoint_2 = storage.savepoint_guard(\"sp\");\n"
        "sp_savepoint_2.rollback_to();\n"
        "sp_savepoint_2.release();\n"
        "sp_savepoint.release();\n");
}

TEST_CASE("joinGeneratedCode: guard style leaves statements flat") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["savepoint_style"] = "guard";
    const auto results = processMultiSql(
        "SAVEPOINT sp1; DELETE FROM t; ROLLBACK TO SAVEPOINT sp1; RELEASE sp1;", &policy);
    REQUIRE(joinGeneratedCode(results) ==
        "auto sp1_savepoint = storage.savepoint_guard(\"sp1\");\n"
        "storage.remove_all<T>();\n"
        "sp1_savepoint.rollback_to();\n"
        "sp1_savepoint.release();\n");
}

TEST_CASE("processSql: VACUUM") {
    const ProcessSqlResult expected = expectedFromPipeline("VACUUM;");
    REQUIRE(processSql("VACUUM;") == expected);
}

TEST_CASE("processSql: DROP TABLE IF EXISTS") {
    const ProcessSqlResult expected = expectedFromPipeline("DROP TABLE IF EXISTS t;");
    REQUIRE(processSql("DROP TABLE IF EXISTS t;") == expected);
}

TEST_CASE("process: STRICT table converts with a warning instead of failing validation") {
    auto result = processSql("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT NOT NULL) STRICT;");
    REQUIRE(result.parseResult.errors.empty());
    REQUIRE(result.validationErrors.empty());
    REQUIRE(result.codegen.code.find("make_table(\"users\"") != std::string::npos);
    REQUIRE(result.codegen.warnings ==
            std::vector<CodegenWarning>{"STRICT is not yet supported in sqlite_orm and was ignored for "
                                     "table users (converted as a regular table)"});
}

TEST_CASE("process: join gives repeated statement variables unique names") {
    auto results = processMultiSql("SELECT * FROM docs WHERE body MATCH 'a'; SELECT * FROM docs WHERE body MATCH 'b'; SELECT * FROM docs;", nullptr);
    REQUIRE(joinGeneratedCode(results) ==
            "auto rows = storage.get_all<Docs>(where(match(&Docs::body, \"a\")));\n"
            "auto rows2 = storage.get_all<Docs>(where(match(&Docs::body, \"b\")));\n"
            "auto rows3 = storage.get_all<Docs>();\n");
}

TEST_CASE("process: join gives repeated virtual table variables unique names") {
    auto results = processMultiSql("CREATE VIRTUAL TABLE a USING fts5(x); CREATE VIRTUAL TABLE b USING fts5(y);", nullptr);
    const std::string joined = joinGeneratedCode(results);
    REQUIRE(joined.find("auto vtab = make_virtual_table<A>") != std::string::npos);
    REQUIRE(joined.find("auto vtab2 = make_virtual_table<B>") != std::string::npos);
}

TEST_CASE("processMultiSql: custom-function arg types come from the schema") {
    const auto results = processMultiSql(
        "CREATE TABLE transactions (day INTEGER, cat TEXT, a_norm REAL, morton_key INTEGER);"
        "SELECT * FROM transactions WHERE morton_key = morton_encode(day, cat, a_norm);");
    REQUIRE(results.size() == 2);
    const std::string& code = results[1].codegen.code;
    // day INTEGER -> int64_t, cat TEXT -> std::string, a_norm REAL -> double (from the schema).
    CHECK(code.find("int64_t day, std::string cat, double a_norm") != std::string::npos);
    CHECK(code.find("func<MortonEncode>(&Transactions::day, &Transactions::cat, &Transactions::a_norm)") !=
          std::string::npos);
}

// A table sqlite_orm cannot map at all — a STORED generated column holding a hex literal no int64
// can hold — is left out of the storage, and there is then no C++ type behind its name. Everything
// naming it has to go with it, or the snippet references a struct it never declared and does not
// compile, at exit 0. SQLite takes this schema (`sqlite3 :memory: "<the SQL>"` succeeds; only a row
// written to `gen` is refused, with `hex literal too big`), so the rest of it still has to generate.
// Checked against sqlite3 3.51.0.
TEST_CASE("processMultiSql: a table that cannot be mapped takes what names it with it") {
    const auto results = processMultiSql(
        "CREATE TABLE gen(x INTEGER PRIMARY KEY, y AS (x + 0x10000000000000000) STORED);\n"
        "CREATE TABLE child(id INTEGER PRIMARY KEY, gid INTEGER REFERENCES gen(x));\n"
        "CREATE INDEX i ON gen(x);\n"
        "CREATE VIEW vg AS SELECT x FROM gen;\n"
        "DELETE FROM gen;");
    REQUIRE(results.size() == 5);
    REQUIRE(joinGeneratedCode(results) ==
            "struct Child {\n"
            "    int64_t id = 0;\n"
            "    std::optional<int64_t> gid;\n"
            "};\n"
            "\n"
            "auto storage = make_storage(\"\",\n"
            "    make_table(\"child\",\n"
            "        make_column(\"id\", &Child::id, primary_key()),\n"
            "        make_column(\"gid\", &Child::gid)));\n"
            "\n"
            "/* CREATE TABLE gen — not supported for sqlite_orm */\n");
    REQUIRE(results[0].codegen.warnings ==
            std::vector<CodegenWarning>{
                "STORED generated column 'y' uses 0x10000000000000000, too big for a signed 64-bit integer: "
                "SQLite stores the table but refuses every row written to it, and C++ has no literal for it, "
                "so the table is not generated"});
    REQUIRE(results[1].codegen.warnings ==
            std::vector<CodegenWarning>{"foreign key on column 'gid' references gen, which is not generated, "
                                        "so the generated table has no foreign_key()"});
    REQUIRE(results[2].codegen.warnings ==
            std::vector<CodegenWarning>{
                "`i` rests on a table that is not generated and is not merged into make_storage()"});
    REQUIRE(results[3].codegen.warnings ==
            std::vector<CodegenWarning>{
                "`vg` rests on a table that is not generated and is not merged into make_storage()"});
    REQUIRE(results[4].codegen.warnings ==
            std::vector<CodegenWarning>{
                "a statement naming `gen` rests on a table that is not generated and is left out"});
}

// SQLite takes a foreign key into a table declared later in the same script, so the batch cannot
// know from statement order alone which names it will fail to map; it is generated again once it
// does. Without the second pass `child` keeps `foreign_key(&Gen::x)` with no `struct Gen`.
TEST_CASE("processMultiSql: a table that cannot be mapped is taken out of an earlier foreign key") {
    const auto results = processMultiSql(
        "CREATE TABLE child(id INTEGER PRIMARY KEY, gid INTEGER REFERENCES gen(x));\n"
        "CREATE TABLE gen(x INTEGER PRIMARY KEY, y AS (x + 0x10000000000000000) STORED);");
    REQUIRE(results.size() == 2);
    REQUIRE(joinGeneratedCode(results) ==
            "struct Child {\n"
            "    int64_t id = 0;\n"
            "    std::optional<int64_t> gid;\n"
            "};\n"
            "\n"
            "auto storage = make_storage(\"\",\n"
            "    make_table(\"child\",\n"
            "        make_column(\"id\", &Child::id, primary_key()),\n"
            "        make_column(\"gid\", &Child::gid)));\n"
            "\n"
            "/* CREATE TABLE gen — not supported for sqlite_orm */\n");
    REQUIRE(results[0].codegen.warnings ==
            std::vector<CodegenWarning>{"foreign key on column 'gid' references gen, which is not generated, "
                                        "so the generated table has no foreign_key()"});
}

// A view left out of the storage is a name with no C++ type behind it exactly as an unmappable
// table is, so a view selecting from it and a trigger INSTEAD OF it go with it. SQLite stores a
// view body without compiling it, so it takes `0x10000000000000000` there and refuses only a query
// against the view (`Error: hex literal too big`), which is why the schema still has to generate.
TEST_CASE("processMultiSql: a view that cannot be generated takes its dependents with it") {
    const auto results = processMultiSql(
        "CREATE TABLE ok1(a INTEGER PRIMARY KEY);\n"
        "CREATE VIEW v1 AS SELECT a + 0x10000000000000000 AS b FROM ok1;\n"
        "CREATE VIEW v2 AS SELECT b FROM v1;\n"
        "CREATE TRIGGER trv INSTEAD OF INSERT ON v1 BEGIN DELETE FROM ok1; END;");
    REQUIRE(results.size() == 4);
    REQUIRE(joinGeneratedCode(results) ==
            "struct Ok1 {\n"
            "    int64_t a = 0;\n"
            "};\n"
            "\n"
            "auto storage = make_storage(\"\",\n"
            "    make_table(\"ok1\",\n"
            "        make_column(\"a\", &Ok1::a, primary_key())));\n"
            "\n"
            "/* CREATE VIEW v1 — not supported for sqlite_orm */\n");
    REQUIRE(results[1].codegen.warnings ==
            std::vector<CodegenWarning>{
                "CREATE VIEW v1 uses 0x10000000000000000, too big for a signed 64-bit integer: SQLite stores "
                "the view but refuses every query against it, and C++ has no literal for it, so the view is "
                "not generated"});
    REQUIRE(results[2].codegen.warnings ==
            std::vector<CodegenWarning>{
                "`v2` rests on a view that is not generated and is not merged into make_storage()"});
    REQUIRE(results[3].codegen.warnings ==
            std::vector<CodegenWarning>{
                "`trv` rests on a view that is not generated and is not merged into make_storage()"});
}

// A dropped statement gives back the result variable name it took, so the surviving SELECT is
// `rows` and not `rows2`.
TEST_CASE("processMultiSql: a dropped statement leaves no gap in the result variable names") {
    const auto results = processMultiSql(
        "CREATE TABLE ok(a INTEGER PRIMARY KEY);\n"
        "CREATE TABLE gen(x INTEGER PRIMARY KEY, y AS (x + 0x10000000000000000) STORED);\n"
        "SELECT * FROM gen;\n"
        "SELECT a FROM ok;");
    REQUIRE(joinGeneratedCode(results) ==
            "struct Ok {\n"
            "    int64_t a = 0;\n"
            "};\n"
            "\n"
            "auto storage = make_storage(\"\",\n"
            "    make_table(\"ok\",\n"
            "        make_column(\"a\", &Ok::a, primary_key())));\n"
            "\n"
            "/* CREATE TABLE gen — not supported for sqlite_orm */\n"
            "auto rows = storage.select(&Ok::a);\n");
}
