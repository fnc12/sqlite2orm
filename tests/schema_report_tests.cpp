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
        return std::filesystem::temp_directory_path() /
               ("sqlite2orm_report_" + std::to_string(dist(gen)) + ".db");
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
    REQUIRE(result.out ==
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
    // The statement that did not generate is left out of `make_storage()` rather than swallowing
    // the header, so the only table of this schema leaves an empty storage behind.
    REQUIRE(result.out == R"(#pragma once

#include <sqlite_orm/sqlite_orm.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>


inline auto make_sqlite_schema_storage(const std::string& db_path) {
    using namespace sqlite_orm;
    return make_storage(db_path);
}
)");
    REQUIRE(result.err == "warning: CREATE TABLE `q` did not generate and is not merged into make_storage()\n"
                          "codegen error [table q]: binary IS / IS NOT / IS [NOT] DISTINCT FROM is not "
                          "supported in sqlite_orm\n");
    REQUIRE(result.exitCode == 1);
}

TEST_CASE("reportSqliteSchema: --json on a schema that generates stays quiet and exits 0") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE t (id INTEGER PRIMARY KEY);");
    const SchemaReport result = report(file.path, true);
    REQUIRE(result.out ==
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

// SQLite compiles a view body only when the view is used, so it accepts and stores bodies
// sqlite2orm refuses: `CREATE VIEW v AS SELECT -0x8000000000000000` returns 0 and sits in
// sqlite_master, and only `SELECT * FROM v` reports `hex literal too big`. Such a view used to take
// the header for every other table with it, so a schema holding one generated nothing at all.
TEST_CASE("reportSqliteSchema: a view body sqlite2orm refuses keeps the other tables generated") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE t (id INTEGER PRIMARY KEY);"
            "CREATE VIEW v AS SELECT -0x8000000000000000;");
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
    REQUIRE(result.err == "warning: CREATE VIEW `v` did not generate and is not merged into make_storage()\n"
            "validation [view v]: hex literal too big: -0x8000000000000000 (UnaryOperatorNode)\n");
    REQUIRE(result.exitCode == 1);
}

// The dropped view is a name sqlite_orm has no type for, exactly as an ungeneratable table is, so
// a trigger resting on it has to go too rather than be emitted with a dangling `struct V`.
TEST_CASE("reportSqliteSchema: a trigger on a view that did not generate goes with it") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE t (id INTEGER PRIMARY KEY);"
            "CREATE VIEW v AS SELECT +id AS id FROM t;"
            "CREATE TRIGGER iv INSTEAD OF INSERT ON v BEGIN SELECT 1; END;");
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
    REQUIRE(result.err == "warning: CREATE VIEW `v` did not generate and is not merged into make_storage()\n"
            "warning: `iv` rests on a view that is not generated and is not merged into make_storage()\n"
            "validation [view v]: unary plus (+expr) is not supported in sqlite_orm (UnaryOperatorNode)\n");
    REQUIRE(result.exitCode == 1);
}

// A table that did not generate is marked before anything else is generated, so a view selecting
// from it is dropped by the same funnel while the table beside it still reaches make_storage().
TEST_CASE("reportSqliteSchema: a view on a table that did not generate goes with it") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE q (a INTEGER CHECK (a IS NOT 1));"
            "CREATE TABLE t (id INTEGER PRIMARY KEY);"
            "CREATE VIEW vq AS SELECT a FROM q;");
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
    REQUIRE(result.err == "warning: CREATE VIEW vq: sqlite_orm views use C++26 reflection (make_view + "
            "[[= \"…\"_orm_name]]); this code requires C++26 and will not compile under the selected "
            "C++ standard\n"
            "warning: CREATE TABLE `q` did not generate and is not merged into make_storage()\n"
            "warning: `vq` rests on a table that is not generated and is not merged into "
            "make_storage()\n"
            "codegen error [table q]: binary IS / IS NOT / IS [NOT] DISTINCT FROM is not supported in "
            "sqlite_orm\n");
    REQUIRE(result.exitCode == 1);
}
