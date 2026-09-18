#include <sqlite2orm/schema_process.h>
#include <sqlite2orm/schema_reader.h>
#include <sqlite2orm/schema_report.h>

#include <catch2/catch_all.hpp>
#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>

using namespace sqlite2orm;

namespace {

    [[nodiscard]] std::filesystem::path makeTempDbPath() {
        static thread_local std::mt19937 gen{std::random_device{}()};
        std::uniform_int_distribution<std::uint64_t> dist{};
        return std::filesystem::temp_directory_path() / ("sqlite2orm_report_" + std::to_string(dist(gen)) + ".db");
    }

    struct TempDbFile {
        std::filesystem::path path;

        explicit TempDbFile(std::filesystem::path p) : path(std::move(p)) {}

        ~TempDbFile() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    };

    /** `sql` must be NUL-terminated (e.g. a string literal); same contract as `sqlite3_exec`. */
    void execSql(const std::filesystem::path& dbPath, std::string_view sql) {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(dbPath.string().c_str(), &db) == SQLITE_OK);
        char* errMsg = nullptr;
        const int rc = sqlite3_exec(db, sql.data(), nullptr, nullptr, &errMsg);
        const std::string errCopy = errMsg ? errMsg : std::string{};
        sqlite3_free(errMsg);
        sqlite3_close(db);
        INFO(errCopy);
        REQUIRE(rc == SQLITE_OK);
    }

    [[nodiscard]] SchemaReport report(const std::filesystem::path& dbPath, bool jsonOnly) {
        SqliteSchemaReader reader(dbPath.string());
        return reportSqliteSchema(processSqliteSchema(reader), jsonOnly);
    }

}  // namespace

// `--json` used to swallow every diagnostic and exit 0, so a script driving the CLI could not tell a
// schema that generated from one that did not without parsing the JSON itself.
TEST_CASE("reportSqliteSchema: --json reports a codegen error and exits 1") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE q (a INTEGER CHECK (a IS NOT 1));");
    const SchemaReport result = report(file.path, true);
    REQUIRE(
        result.out ==
        R"({"statements":[{"comments":[],"decisionPoints":[],"name":"q","ok":false,"tableName":"q","type":"table"}]})"
        "\n");
    REQUIRE(result.err == "codegen error [table q]: binary IS / IS NOT / IS [NOT] DISTINCT FROM is not "
                          "supported in sqlite_orm\n");
    REQUIRE(result.exitCode == 1);
}

TEST_CASE("reportSqliteSchema: a codegen error reads the same without --json") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE q (a INTEGER CHECK (a IS NOT 1));");
    const SchemaReport result = report(file.path, false);
    REQUIRE(result.out.empty());
    REQUIRE(result.err == "codegen error [table q]: binary IS / IS NOT / IS [NOT] DISTINCT FROM is not "
                          "supported in sqlite_orm\n");
    REQUIRE(result.exitCode == 1);
}

TEST_CASE("reportSqliteSchema: --json on a schema that generates stays quiet and exits 0") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE t (id INTEGER PRIMARY KEY);");
    const SchemaReport result = report(file.path, true);
    REQUIRE(
        result.out ==
        R"({"statements":[{"comments":[],"decisionPoints":[],"name":"t","ok":true,"tableName":"t","type":"table"}]})"
        "\n");
    REQUIRE(result.err.empty());
    REQUIRE(result.exitCode == 0);
}

TEST_CASE("reportSqliteSchema: a schema that generates returns the header and exits 0") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE t (id INTEGER PRIMARY KEY);");
    const SchemaReport result = report(file.path, false);
    REQUIRE(result.out == R"(#pragma once

#include <sqlite_orm/sqlite_orm.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct T {
    int64_t id = 0;
};


inline auto make_sqlite_schema_storage(const std::string& db_path) {
    using namespace sqlite_orm;
    return make_storage(db_path,
        make_table("t",
        make_column("id", &T::id, primary_key())));
}
)");
    REQUIRE(result.err.empty());
    REQUIRE(result.exitCode == 0);
}
