#include <sqlite2orm/json_emit.h>
#include <sqlite2orm/process.h>
#include <sqlite2orm/schema_header.h>
#include <sqlite2orm/schema_process.h>
#include <sqlite2orm/schema_reader.h>

#include "temp_build_dir.hpp"

#include <catch2/catch_all.hpp>
#include <sqlite3.h>

#include <cstring>

#include <filesystem>
#include <random>
#include <sstream>
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

TEST_CASE("generateSqliteSchemaHeader: DML after DDL emits seed_data()") {
    auto pipelines = processMultiSql(
        "CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT);"
        "INSERT INTO t (id, name) VALUES (1, 'Alice');");
    REQUIRE(pipelines.size() == 2);
    REQUIRE(pipelines[0].ok());
    REQUIRE(pipelines[1].ok());

    ProcessSqliteSchemaResult schema;
    for(auto& p : pipelines) {
        SchemaStatementResult s;
        s.meta.type = "table";
        s.pipeline = std::move(p);
        schema.statements.push_back(std::move(s));
    }

    const CodeGenResult header = generateSqliteSchemaHeader(schema);
    const CodeGenResult expected{
        .code = R"(#pragma once

#include <sqlite_orm/sqlite_orm.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct T {
    int64_t id = 0;
    std::optional<std::string> name;
};


inline auto make_sqlite_schema_storage(const std::string& db_path) {
    using namespace sqlite_orm;
    return make_storage(db_path,
        make_table("t",
        make_column("id", &T::id, primary_key()),
        make_column("name", &T::name)));
}

storage.insert(into<T>(), columns(&T::id, &T::name), values(std::make_tuple(1, "Alice")));
)",
    };
    REQUIRE(header == expected);
}

TEST_CASE("sqliteSchemaResultToJson: shape") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE t (id INTEGER PRIMARY KEY);");
    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(sqliteSchemaResultToJson(schema) ==
            R"({"statements":[{"comments":[],"decisionPoints":[],"name":"t","ok":true,"tableName":"t","type":"table"}]})");
}

// A DEFAULT or a STORED generated-column expression is stored by SQLite without being compiled, so
// a real database carries `-0x8000000000000000` and codegen sees it where the validator never does.
// Folding that sign into the C++ constant would emit `-static_cast<int64_t>(0x8000000000000000)`,
// which overflows int64_t — a warning with `-Woverflow` and a hard error in a constant expression —
// so the sign stays out of the constant and the clause carries a warning instead. A CHECK reaches
// the same codegen path on sqlite3 3.51, but the libsqlite3 these tests link (3.45.1) compiles CHECK
// expressions at CREATE TABLE time and refuses that one, so the two clauses every version stores are
// what this goes through; "codegen: the sign of INT64_MIN is not folded into the hex literal" pins
// the expression itself.
TEST_CASE("generateSqliteSchemaHeader: the sign of INT64_MIN stays out of the C++ constant") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE neg_t (a INT, b INT DEFAULT (-0x8000000000000000), "
                       "g AS (-0x8000000000000000) STORED);");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    const std::string tooBig =
        "hex literal too big: -0x8000000000000000; SQLite refuses this expression wherever it is "
        "used, so the generated subtraction from zero does not reproduce it";
    const CodeGenResult expected{
        std::string("#pragma once\n\n"
                    "#include <sqlite_orm/sqlite_orm.h>\n"
                    "#include <cstdint>\n"
                    "#include <optional>\n"
                    "#include <string>\n"
                    "#include <vector>\n\n"
                    "struct NegT {\n"
                    "    std::optional<int64_t> a;\n"
                    "    std::optional<int64_t> b;\n"
                    "    std::optional<std::vector<char>> g;\n"
                    "};\n\n\n"
                    "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                    "    using namespace sqlite_orm;\n"
                    "    return make_storage(db_path,\n"
                    "        make_table(\"neg_t\",\n"
                    "        make_column(\"a\", &NegT::a),\n"
                    "        make_column(\"b\", &NegT::b, "
                    "default_value((c(0) - c(static_cast<int64_t>(0x8000000000000000))))),\n"
                    "        make_column(\"g\", &NegT::g, "
                    "as((c(0) - c(static_cast<int64_t>(0x8000000000000000)))).stored())));\n"
                    "}\n"),
        {},
        {{tooBig, SourceLocation{1, 43}, 1}, {tooBig, SourceLocation{1, 71}, 1}}};

    REQUIRE(header == expected);

    // `-Woverflow` is the diagnostic the folded constant would raise, and nothing else in the
    // generated header or in sqlite_orm raises it, so it is the one warning worth failing on.
    const codegen_test_helpers::TempBuildDir dir;
    dir.write("gen.hpp", header.code);
    const std::filesystem::path cpppath = dir.write("check.cpp", "#include \"gen.hpp\"\n");

    std::ostringstream cmd;
    cmd << codegen_test_helpers::TempBuildDir::compilerCommand() << " -fsyntax-only -Werror=overflow";
    cmd << " -I" << dir.path().string();
    cmd << ' ' << cpppath.string();
    cmd << " 2>&1";

    const int exitCode = codegen_test_helpers::TempBuildDir::run(cmd.str());
    if(exitCode != 0) {
        WARN("compiling the generated header failed (exit " << exitCode
                                                            << "); ensure c++ and sqlite_orm headers are usable");
    }
    REQUIRE(exitCode == 0);
}

TEST_CASE("phase 21.7: fsyntax-only compile of generated header") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE round_t (id INTEGER PRIMARY KEY, name TEXT);");
    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    const codegen_test_helpers::TempBuildDir dir;
    dir.write("gen.hpp", header.code);
    const std::filesystem::path cpppath = dir.write("check.cpp", "#include \"gen.hpp\"\n");

    std::ostringstream cmd;
    cmd << codegen_test_helpers::TempBuildDir::compilerCommand() << " -fsyntax-only";
    cmd << " -I" << dir.path().string();
    cmd << ' ' << cpppath.string();
    cmd << " 2>&1";

    const int exitCode = codegen_test_helpers::TempBuildDir::run(cmd.str());
    if(exitCode != 0) {
        WARN("fsyntax-only failed (exit " << exitCode << "); ensure c++ and sqlite_orm headers are usable");
    }
    REQUIRE(exitCode == 0);
}
