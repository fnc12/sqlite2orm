#include <sqlite2orm/process.h>
#include <sqlite2orm/tokenizer.h>
#include <sqlite2orm/parser.h>
#include <sqlite2orm/validator.h>
#include <sqlite2orm/codegen.h>
#include <catch2/catch_all.hpp>

#include <string>

using namespace sqlite2orm;

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

TEST_CASE("processSql: CREATE VIEW stops at validation") {
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

TEST_CASE("processMultiSql: validation error does not block other statements") {
    std::vector<ProcessSqlResult> expected;
    expected.push_back(processSql("INSERT INTO t VALUES (1);"));
    expected.push_back(processSql("SELECT 42;"));
    REQUIRE(processMultiSql("INSERT INTO t VALUES (1); SELECT 42;") == expected);
}

TEST_CASE("processSql: BEGIN TRANSACTION") {
    const ProcessSqlResult expected = expectedFromPipeline("BEGIN TRANSACTION;");
    REQUIRE(processSql("BEGIN TRANSACTION;") == expected);
}

TEST_CASE("processSql: VACUUM") {
    const ProcessSqlResult expected = expectedFromPipeline("VACUUM;");
    REQUIRE(processSql("VACUUM;") == expected);
}

TEST_CASE("processSql: DROP TABLE IF EXISTS") {
    const ProcessSqlResult expected = expectedFromPipeline("DROP TABLE IF EXISTS t;");
    REQUIRE(processSql("DROP TABLE IF EXISTS t;") == expected);
}
