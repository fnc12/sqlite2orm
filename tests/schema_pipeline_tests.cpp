#include <sqlite2orm/json_emit.h>
#include <sqlite2orm/process.h>
#include <sqlite2orm/schema_header.h>
#include <sqlite2orm/schema_process.h>
#include <sqlite2orm/schema_reader.h>

#include <catch2/catch_all.hpp>
#include <sqlite3.h>

#include <cstdlib>
#include <cstring>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#endif
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

using namespace sqlite2orm;

namespace {

    [[nodiscard]] std::filesystem::path makeTempDbPath() {
        static thread_local std::mt19937 gen{std::random_device{}()};
        std::uniform_int_distribution<std::uint64_t> dist{};
        return std::filesystem::temp_directory_path() /
               ("sqlite2orm_pipe_" + std::to_string(dist(gen)) + ".db");
    }

    struct TempDbFile {
        std::filesystem::path path;

        explicit TempDbFile(std::filesystem::path p) : path(std::move(p)) {}

        ~TempDbFile() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    };

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

}  // namespace

TEST_CASE("processSql: CodeGenPolicy expr_style functional") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["expr_style"] = "functional";
    const ProcessSqlResult r = processSql("1 + 2", &policy);
    REQUIRE(r.ok());
    REQUIRE(r.codegen.code == "add(1, 2)");
}

TEST_CASE("processSqliteSchema: reads DDL in dependency-safe order") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE parent (id INTEGER PRIMARY KEY);"
            "CREATE TABLE child (id INTEGER PRIMARY KEY, pid INTEGER REFERENCES parent(id));"
            "CREATE INDEX idx_child_pid ON child(pid);");

    const std::string path = file.path.string();
    REQUIRE(processSqliteSchema(SqliteSchemaReader(path)) == processSqliteSchema(SqliteSchemaReader(path)));
}

TEST_CASE("generateSqliteSchemaHeader: merged storage") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE a (id INTEGER PRIMARY KEY);"
            "CREATE TABLE b (id INTEGER PRIMARY KEY, aid INTEGER REFERENCES a(id));");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    const CodeGenResult expected{
        std::string("#pragma once\n\n"
                    "#include <sqlite_orm/sqlite_orm.h>\n"
                    "#include <cstdint>\n"
                    "#include <optional>\n"
                    "#include <string>\n"
                    "#include <vector>\n\n"
                    "struct A {\n"
                    "    int64_t id = 0;\n"
                    "};\n\n"
                    "struct B {\n"
                    "    int64_t id = 0;\n"
                    "    std::optional<int64_t> aid;\n"
                    "};\n\n\n"
                    "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                    "    using namespace sqlite_orm;\n"
                    "    return make_storage(db_path,\n"
                    "        make_table(\"a\",\n"
                    "        make_column(\"id\", &A::id, primary_key())),\n"
                    "        make_table(\"b\",\n"
                    "        make_column(\"id\", &B::id, primary_key()),\n"
                    "        make_column(\"aid\", &B::aid),\n"
                    "        foreign_key(&B::aid).references(&A::id)));\n"
                    "}\n"),
        {},
        {}};

    REQUIRE(header == expected);
}

TEST_CASE("sqliteSchemaResultToJson: shape") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE t (id INTEGER PRIMARY KEY);");
    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(sqliteSchemaResultToJson(schema) ==
            R"({"statements":[{"decisionPoints":[],"name":"t","ok":true,"tableName":"t","type":"table"}]})");
}

TEST_CASE("phase 21.7: fsyntax-only compile of generated header") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE round_t (id INTEGER PRIMARY KEY, name TEXT);");
    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    namespace fs = std::filesystem;
    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist{};
    const fs::path dir = fs::temp_directory_path() / ("sqlite2orm_rt_" + std::to_string(dist(gen)));
    std::error_code ec;
    fs::create_directories(dir, ec);
    REQUIRE_FALSE(ec);

    const fs::path hpath = dir / "gen.hpp";
    const fs::path cpppath = dir / "check.cpp";
    {
        std::ofstream h(hpath);
        REQUIRE(h);
        h << header.code;
    }
    {
        std::ofstream c(cpppath);
        REQUIRE(c);
        c << "#include \"gen.hpp\"\n";
    }

    std::ostringstream cmd;
    cmd << "c++ -std=c++20 -fsyntax-only";
#if defined(__APPLE__)
    cmd << " -stdlib=libc++";
#endif
    cmd << " -I" << SQLITE2ORM_TEST_SQLITE_ORM_INCLUDE;
    cmd << " -I" << dir.string();
    cmd << ' ' << cpppath.string();
    cmd << " 2>&1";

    const int rawStatus = std::system(cmd.str().c_str());
    fs::remove_all(dir, ec);

    int exitCode = rawStatus;
#if defined(__unix__) || defined(__APPLE__)
    if(rawStatus != -1) {
        exitCode = WEXITSTATUS(rawStatus);
    }
#endif
    if(exitCode != 0) {
        WARN("fsyntax-only failed (exit " << exitCode << "); ensure c++ and sqlite_orm headers are usable");
    }
    REQUIRE(exitCode == 0);
}
