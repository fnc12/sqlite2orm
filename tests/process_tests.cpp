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

TEST_CASE("processSql: validator rejects unary plus") {
    const ProcessSqlResult expectedOutcome = expectedFromPipeline("+1");
    REQUIRE(processSql("+1") == expectedOutcome);
}

TEST_CASE("processSql: CREATE VIEW generates make_view") {
    const ProcessSqlResult expected = expectedFromPipeline("CREATE VIEW v AS SELECT 1;");
    REQUIRE(processSql("CREATE VIEW v AS SELECT 1;") == expected);
}

TEST_CASE("processSql: PRAGMA user_version maps to storage.pragma") {
    const ProcessSqlResult expected = expectedFromPipeline("PRAGMA user_version=1;");
    REQUIRE(processSql("PRAGMA user_version=1;") == expected);
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
    std::vector<ProcessSqlResult> expected;
    expected.push_back(processSql("SELECT 1;"));
    expected.push_back(processSql("SELECT 2;"));
    REQUIRE(processMultiSql("SELECT 1; SELECT 2;") == expected);
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

TEST_CASE("joinGeneratedCode: DML-only batch stays unchanged") {
    const auto results = processMultiSql("SELECT 1; SELECT 2;");
    REQUIRE(joinGeneratedCode(results) ==
        "auto rows = storage.select(1);\n"
        "auto rows = storage.select(2);\n");
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
