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
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>

using namespace sqlite2orm;

namespace {

    [[nodiscard]] std::filesystem::path makeTempDbPath() {
        static thread_local std::mt19937 gen{std::random_device{}()};
        std::uniform_int_distribution<std::uint64_t> dist{};
        return std::filesystem::temp_directory_path() / ("sqlite2orm_pipe_" + std::to_string(dist(gen)) + ".db");
    }

    struct TempDbFile {
        std::filesystem::path path;

        explicit TempDbFile(std::filesystem::path p) : path(std::move(p)) {}

        ~TempDbFile() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    };

    /**
     *  Compiles `generatedCode` against the sqlite_orm headers, as the one header of a translation
     *  unit. Code that names a struct it never declared fails here and nowhere else — the text
     *  itself looks fine, and the CLI has already exited 0 by then.
     */
    void requireCompiles(std::string_view generatedCode) {
        const codegen_test_helpers::TempBuildDir dir;
        dir.write("gen.hpp", generatedCode);
        const std::filesystem::path cpppath = dir.write("check.cpp", "#include \"gen.hpp\"\n");

        std::ostringstream cmd;
        cmd << codegen_test_helpers::TempBuildDir::compilerCommand() << " -fsyntax-only";
        cmd << " -I" << dir.path().string();
        cmd << ' ' << cpppath.string();
        cmd << " 2>&1";

        const int exitCode = codegen_test_helpers::TempBuildDir::run(cmd.str());
        if (exitCode != 0) {
            WARN("fsyntax-only failed (exit " << exitCode << "); ensure c++ and sqlite_orm headers are usable");
        }
        REQUIRE(exitCode == 0);
    }

    /**
     *  One `sqlite_master` row, run through the pipeline as `processSqliteSchema` would run it.
     *  A CREATE VIRTUAL TABLE only reaches a database when its module is compiled into the
     *  sqlite3 the tests link, and which modules those are differs per platform, so the row that
     *  SQLite would have stored is handed to the header generator directly.
     */
    [[nodiscard]] SchemaStatementResult masterRow(std::string type, std::string name, std::string sql) {
        SchemaStatementResult statement;
        statement.meta = SchemaStatementMeta{std::move(type), name, name, sql};
        statement.pipeline = processSql(sql);
        return statement;
    }

    /**
     *  The program the round-trip probe builds around a generated header, up to the point the
     *  caller's own code is spliced in: it opens the very database the header was generated from,
     *  runs `sync_schema()` and prints one `table=outcome` line per mapped table. Spelling the
     *  outcomes out — rather than printing the enumerator's number — is what makes the expected
     *  text say what the probe proves.
     */
    constexpr std::string_view kSyncSchemaProbeHead = R"(#include "gen.hpp"

#include <iostream>
#include <string>

namespace {

    std::string outcomeName(sqlite_orm::sync_schema_result outcome) {
        switch (outcome) {
            case sqlite_orm::sync_schema_result::new_table_created:
                return "new_table_created";
            case sqlite_orm::sync_schema_result::already_in_sync:
                return "already_in_sync";
            case sqlite_orm::sync_schema_result::old_columns_removed:
                return "old_columns_removed";
            case sqlite_orm::sync_schema_result::new_columns_added:
                return "new_columns_added";
            case sqlite_orm::sync_schema_result::new_columns_added_and_old_columns_removed:
                return "new_columns_added_and_old_columns_removed";
            case sqlite_orm::sync_schema_result::dropped_and_recreated:
                return "dropped_and_recreated";
        }
        return "unknown";
    }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return 1;
    }
    auto storage = make_sqlite_schema_storage(argv[1]);
    for (const auto& outcome: storage.sync_schema()) {
        std::cout << outcome.first << "=" << outcomeName(outcome.second) << "\n";
    }
)";

    /** What closes the probe's `main` after the caller's own statements. */
    constexpr std::string_view kSyncSchemaProbeTail = R"(    return 0;
}
)";

    /**
     *  Builds that program around `generatedCode` with `afterSync` spliced into its `main` after
     *  the sync, runs it over `dbPath` and returns what it printed. A header that compiles can
     *  still be wrong about the database it was generated
     *  from: sqlite_orm compares a mapped column against what `PRAGMA table_info` reports and
     *  rebuilds the table over any difference, which takes the rows with it, so running
     *  `sync_schema()` is the only thing that answers whether the mapping and the schema SQLite
     *  stores are the same.
     */
    [[nodiscard]] std::string syncSchemaProbeOutput(std::string_view generatedCode,
                                                    const std::filesystem::path& dbPath,
                                                    std::string_view afterSync) {
        std::string source(kSyncSchemaProbeHead);
        source += afterSync;
        source += kSyncSchemaProbeTail;

        const codegen_test_helpers::TempBuildDir dir;
        dir.write("gen.hpp", generatedCode);
        const std::filesystem::path cpppath = dir.write("check.cpp", source);
        const std::filesystem::path binpath = dir.file("check");
        const std::filesystem::path outpath = dir.file("check.out");

        std::ostringstream cmd;
        cmd << codegen_test_helpers::TempBuildDir::compilerCommand();
        cmd << " -I" << dir.path().string();
        cmd << ' ' << cpppath.string();
        cmd << ' ' << codegen_test_helpers::TempBuildDir::sqlite3LinkFlags() << " -o " << binpath.string();
        cmd << " && " << binpath.string() << ' ' << dbPath.string() << " > " << outpath.string();
        cmd << " 2>&1";

        const int exitCode = codegen_test_helpers::TempBuildDir::run(cmd.str());
        std::ostringstream output;
        {
            std::ifstream out(outpath);
            output << out.rdbuf();
        }
        if (exitCode != 0) {
            WARN("running the generated header failed (exit " << exitCode
                                                              << "); ensure c++, sqlite_orm headers and libsqlite3 "
                                                                 "are usable\n"
                                                              << output.str());
        }
        REQUIRE(exitCode == 0);
        return output.str();
    }

    /**
     *  What each of `tables` answers when a row is inserted without naming the key column `a`:
     *  `table=<value>` with what SQLite put in the column, or `table=refused` where the INSERT was
     *  not taken at all. `kInsertWithoutKeyColumnProbe` prints the very same text for a database
     *  built from a generated header, so the schema SQLite stores and the schema the header builds
     *  are compared as one string each.
     */
    [[nodiscard]] std::string insertWithoutKeyColumnOutput(const std::filesystem::path& dbPath,
                                                           const std::vector<std::string>& tables) {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(dbPath.string().c_str(), &db) == SQLITE_OK);
        std::string output;
        for (const std::string& table: tables) {
            char* errMsg = nullptr;
            const std::string insert = "INSERT INTO " + table + "(b) VALUES (7)";
            const int rc = sqlite3_exec(db, insert.c_str(), nullptr, nullptr, &errMsg);
            sqlite3_free(errMsg);
            if (rc != SQLITE_OK) {
                output += table + "=refused\n";
                continue;
            }
            sqlite3_stmt* statement = nullptr;
            const std::string select = "SELECT quote(a) FROM " + table;
            REQUIRE(sqlite3_prepare_v2(db, select.c_str(), -1, &statement, nullptr) == SQLITE_OK);
            REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
            output += table + "=" + reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)) + "\n";
            sqlite3_finalize(statement);
        }
        sqlite3_close(db);
        return output;
    }

    /**
     *  Spliced into the probe after the sync: the INSERT and read-back `insertWithoutKeyColumnOutput`
     *  runs, against the database `sync_schema()` has just built from the generated header. The
     *  table names are written out because the probe is a program of its own — it sees the header,
     *  not the test's variables.
     */
    constexpr std::string_view kInsertWithoutKeyColumnProbe = R"probe(    sqlite3* db = nullptr;
    if (sqlite3_open(argv[1], &db) != SQLITE_OK) {
        return 1;
    }
    for (const std::string& table: {std::string("dup_int"),
                                    std::string("dup_int_notnull"),
                                    std::string("dup_text"),
                                    std::string("dup_two")}) {
        char* errMsg = nullptr;
        const int rc = sqlite3_exec(db, ("INSERT INTO " + table + "(b) VALUES (7)").c_str(), nullptr, nullptr, &errMsg);
        sqlite3_free(errMsg);
        if (rc != SQLITE_OK) {
            std::cout << table << "=refused\n";
            continue;
        }
        sqlite3_stmt* statement = nullptr;
        sqlite3_prepare_v2(db, ("SELECT quote(a) FROM " + table).c_str(), -1, &statement, nullptr);
        sqlite3_step(statement);
        std::cout << table << "=" << sqlite3_column_text(statement, 0) << "\n";
        sqlite3_finalize(statement);
    }
    sqlite3_close(db);
)probe";

    /** Text of the first column of the first row `sql` answers with. */
    [[nodiscard]] std::string queryText(const std::filesystem::path& dbPath, std::string_view sql) {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(dbPath.string().c_str(), &db) == SQLITE_OK);
        sqlite3_stmt* statement = nullptr;
        REQUIRE(sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &statement, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
        const unsigned char* text = sqlite3_column_text(statement, 0);
        const std::string result = text ? reinterpret_cast<const char*>(text) : std::string{};
        sqlite3_finalize(statement);
        sqlite3_close(db);
        return result;
    }

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

    const CodeGenResult expected{std::string("#pragma once\n\n"
                                             "#include <sqlite_orm/sqlite_orm.h>\n"
                                             "#include <cstdint>\n"
                                             "#include <optional>\n"
                                             "#include <string>\n"
                                             "#include <vector>\n\n"
                                             "struct A {\n"
                                             "    std::optional<int64_t> id;\n"
                                             "};\n\n"
                                             "struct B {\n"
                                             "    std::optional<int64_t> id;\n"
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
                                 {},
                                 {},
                                 {},
                                 // Two spans per table, one for its struct and one for its
                                 // make_table() argument, and the SQL each of them names is the
                                 // CREATE TABLE `sqlite_master` holds for that row.
                                 {{125, 45, 0, SourceLocation{1, 1}, 39},
                                  {171, 77, 1, SourceLocation{1, 1}, 69},
                                  {392, 65, 0, SourceLocation{1, 1}, 39},
                                  {467, 151, 1, SourceLocation{1, 1}, 69}}};

    REQUIRE(header == expected);
}

// The map of a generated header is what a two-pane consumer highlights from, and a schema read
// from a database holds a text per row, so every span names the row it came from as well as the
// place in its SQL. A view is on the map exactly as a table is — its reflected struct and its
// make_view() argument — and an index, written before the table it is made for, keeps its place.
TEST_CASE("generateSqliteSchemaHeader: every statement is mapped onto the code it generated") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT);"
            "CREATE VIEW v AS SELECT name FROM t;"
            "CREATE INDEX i_name ON t(name);");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == std::string("#pragma once\n"
                                       "\n"
                                       "#include <sqlite_orm/sqlite_orm.h>\n"
                                       "#include <cstdint>\n"
                                       "#include <optional>\n"
                                       "#include <string>\n"
                                       "#include <vector>\n"
                                       "\n"
                                       "using namespace sqlite_orm;\n"
                                       "\n"
                                       "struct T {\n"
                                       "    std::optional<int64_t> id;\n"
                                       "    std::optional<std::string> name;\n"
                                       "};\n"
                                       "\n"
                                       "struct [[= \"v\"_orm_name]] V {\n"
                                       "    std::optional<std::string> name;\n"
                                       "};\n"
                                       "\n"
                                       "\n"
                                       "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                                       "    using namespace sqlite_orm;\n"
                                       "    return make_storage(db_path,\n"
                                       "        make_index(\"i_name\", indexed_column(&T::name)),\n"
                                       "        make_table(\"t\",\n"
                                       "        make_column(\"id\", &T::id, primary_key()),\n"
                                       "        make_column(\"name\", &T::name)),\n"
                                       "        make_view<V>(select(&T::name)));\n"
                                       "}\n"));
    REQUIRE(header.spans == std::vector<GeneratedCodeSpan>{{154, 82, 0, SourceLocation{1, 1}, 50},
                                                           {237, 70, 1, SourceLocation{1, 1}, 35},
                                                           {451, 46, 2, SourceLocation{1, 1}, 30},
                                                           {507, 104, 0, SourceLocation{1, 1}, 50},
                                                           {621, 30, 1, SourceLocation{1, 1}, 35}});
}

// One column whose DEFAULT holds a hex literal no int64 can hold used to fail the statement and
// with it the whole import: sqlite2orm printed a parse error and not a single struct. SQLite
// raises `hex literal too big` in codeInteger(), when it compiles an expression, and a DEFAULT is
// never compiled, so it keeps the schema — `sqlite3 s.db "CREATE TABLE weird(x INTEGER DEFAULT
// 0x10000000000000000)"` succeeds and sqlite_master holds the column. Checked against sqlite3 3.51.
TEST_CASE("generateSqliteSchemaHeader: a DEFAULT SQLite stores but cannot compile keeps the schema") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE ok1 (a INTEGER);"
            "CREATE TABLE weird (x INTEGER DEFAULT 0x10000000000000000);"
            "CREATE TABLE ok2 (b TEXT);");

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
                    "struct Ok1 {\n"
                    "    std::optional<int64_t> a;\n"
                    "};\n\n"
                    "struct Ok2 {\n"
                    "    std::optional<std::string> b;\n"
                    "};\n\n"
                    "struct Weird {\n"
                    "    std::optional<int64_t> x;\n"
                    "};\n\n\n"
                    "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                    "    using namespace sqlite_orm;\n"
                    "    return make_storage(db_path,\n"
                    "        make_table(\"ok1\",\n"
                    "        make_column(\"a\", &Ok1::a)),\n"
                    "        make_table(\"ok2\",\n"
                    "        make_column(\"b\", &Ok2::b)),\n"
                    "        make_table(\"weird\",\n"
                    "        make_column(\"x\", &Weird::x)));\n"
                    "}\n"),
        {},
        {CodegenWarning{"DEFAULT 0x10000000000000000 on column 'x' is too big for a signed 64-bit integer: SQLite "
                        "stores it but refuses every use of the default, and C++ has no literal for it, so the "
                        "generated column has no default_value()"}},
        {},
        {},
        {{125, 46, 0, SourceLocation{1, 1}, 28},
         {172, 50, 1, SourceLocation{1, 1}, 25},
         {223, 48, 2, SourceLocation{1, 1}, 58},
         {415, 52, 0, SourceLocation{1, 1}, 28},
         {477, 52, 1, SourceLocation{1, 1}, 25},
         {539, 56, 2, SourceLocation{1, 1}, 58}}};

    REQUIRE(header == expected);
}

// A view and a trigger holding the same literal are stored by SQLite too, so neither may fail the
// import: each is left out of make_storage() with a warning and the tables still come through.
TEST_CASE("generateSqliteSchemaHeader: a view and a trigger SQLite stores but cannot compile are skipped") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE ok1 (a INTEGER);"
            "CREATE VIEW vw AS SELECT 0x10000000000000000 AS c;"
            "CREATE TRIGGER tr AFTER INSERT ON ok1 BEGIN UPDATE ok1 SET a = 0x10000000000000000; END;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Ok1 {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"ok1\",\n"
                           "        make_column(\"a\", &Ok1::a)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"CREATE VIEW vw uses 0x10000000000000000, too big for a signed 64-bit integer: SQLite stores the "
                 "view but refuses every query against it, and C++ has no literal for it, so the view is not "
                 "generated"},
                {"CREATE VIEW `vw` is not merged into make_storage()"},
                {"CREATE TRIGGER tr uses 0x10000000000000000, too big for a signed 64-bit integer: SQLite stores "
                 "the trigger but refuses every statement that fires it, and C++ has no literal for it, so the "
                 "trigger is not generated"},
                {"CREATE TRIGGER `tr` is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
}

// A STORED generated column is stored text too: `sqlite3 blk.db "CREATE TABLE gen(x INTEGER,
// y AS (x + 0x10000000000000000) STORED)"` succeeds and sqlite_master holds it, while every INSERT
// into it fails. The table cannot be generated — dropping the as(...) would turn a generated
// column into an ordinary one — so it is left out of make_storage() whole and its neighbours come
// through. Checked against sqlite3 3.51.
TEST_CASE("generateSqliteSchemaHeader: a STORED generated column SQLite stores but cannot compile keeps the schema") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE ok1 (a INTEGER);"
            "CREATE TABLE gen (x INTEGER, y AS (x + 0x10000000000000000) STORED);"
            "CREATE TABLE ok2 (b TEXT);");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Ok1 {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n\n"
                           "struct Ok2 {\n"
                           "    std::optional<std::string> b;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"ok1\",\n"
                           "        make_column(\"a\", &Ok1::a)),\n"
                           "        make_table(\"ok2\",\n"
                           "        make_column(\"b\", &Ok2::b)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"STORED generated column 'y' uses 0x10000000000000000, too big for a signed 64-bit integer: "
                 "SQLite stores the table but refuses every row written to it, and C++ has no literal for it, so "
                 "the table is not generated"},
                {"CREATE TABLE `gen` is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
}

// Nothing may be left pointing at a table that is not generated: sqlite_orm resolves a foreign
// key against the table it maps for the referenced type, so `foreign_key(&Child::gid)
// .references(&Gen::x)` next to no `make_table("gen", ...)` does not compile, and neither does an
// index, a trigger or a view named on it. Each is left out with its own warning, and what is left
// compiles and maps the rest of the database.
TEST_CASE("generateSqliteSchemaHeader: what rests on an ungenerated table is left out too") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE gen (x INTEGER PRIMARY KEY, y AS (x + 0x10000000000000000) STORED);"
            "CREATE TABLE child (id INTEGER PRIMARY KEY, gid INTEGER REFERENCES gen(x));"
            "CREATE INDEX i ON gen (x);"
            "CREATE TRIGGER tr AFTER INSERT ON child BEGIN DELETE FROM gen; END;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Child {\n"
                           "    std::optional<int64_t> id;\n"
                           "    std::optional<int64_t> gid;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"child\",\n"
                           "        make_column(\"id\", &Child::id, primary_key()),\n"
                           "        make_column(\"gid\", &Child::gid)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"STORED generated column 'y' uses 0x10000000000000000, too big for a signed 64-bit integer: "
                 "SQLite stores the table but refuses every row written to it, and C++ has no literal for it, so "
                 "the table is not generated"},
                {"CREATE TABLE `gen` is not merged into make_storage()"},
                {"foreign key on column 'gid' references gen, which is not generated, so the generated table has "
                 "no foreign_key()"},
                {"`i` rests on a table that is not generated and is not merged into make_storage()"},
                {"`tr` rests on a table that is not generated and is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
}

// The table-level spelling of the same foreign key goes the same way.
TEST_CASE("generateSqliteSchemaHeader: a table-level foreign key into an ungenerated table is left out") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE gen (x INTEGER PRIMARY KEY, y AS (x + 0x10000000000000000) STORED);"
            "CREATE TABLE child (id INTEGER PRIMARY KEY, gid INTEGER, FOREIGN KEY (gid) REFERENCES gen (x));");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Child {\n"
                           "    std::optional<int64_t> id;\n"
                           "    std::optional<int64_t> gid;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"child\",\n"
                           "        make_column(\"id\", &Child::id, primary_key()),\n"
                           "        make_column(\"gid\", &Child::gid)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"STORED generated column 'y' uses 0x10000000000000000, too big for a signed 64-bit integer: "
                 "SQLite stores the table but refuses every row written to it, and C++ has no literal for it, so "
                 "the table is not generated"},
                {"CREATE TABLE `gen` is not merged into make_storage()"},
                {"table-level foreign key on column 'gid' references gen, which is not generated, so the "
                 "generated table has no foreign_key()"}});
    REQUIRE(header.errors.empty());
}

// SQLite resolves a foreign key only while enforcement is on, so it stores `REFERENCES o(x)`
// whether or not `o` was ever created — `PRAGMA foreign_keys=ON; INSERT INTO t VALUES(1,'z')` is
// where it first says `no such table: main.o` (checked against sqlite3 3.51.0). The funnel for a
// parent that is in the schema and does not generate never saw such a name, so it went into the
// header as written: `foreign_key(&T::a).references(&O::x)` at exit 0, with no `struct O` anywhere
// and only the compiler to say so. A parent the schema does not create gets no struct either, so
// the key is left out with a warning of its own.
TEST_CASE("generateSqliteSchemaHeader: a foreign key into a table the schema does not create is left out") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE t (a INTEGER REFERENCES o(x) PRIMARY KEY, b TEXT);");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct T {\n"
                           "    std::optional<int64_t> a;\n"
                           "    std::optional<std::string> b;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"t\",\n"
                           "        make_column(\"a\", &T::a, primary_key()),\n"
                           "        make_column(\"b\", &T::b)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"foreign key on column 'a' references o, which this schema does not create, so the generated "
                 "table has no foreign_key()"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// The table-level spelling of the same schema, which SQLite stores just as readily.
TEST_CASE("generateSqliteSchemaHeader: a table-level foreign key into a table the schema does not create is left out") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE t2 (a INTEGER, b TEXT, FOREIGN KEY (a) REFERENCES o(x));");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct T2 {\n"
                           "    std::optional<int64_t> a;\n"
                           "    std::optional<std::string> b;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"t2\",\n"
                           "        make_column(\"a\", &T2::a),\n"
                           "        make_column(\"b\", &T2::b)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"table-level foreign key on column 'a' references o, which this schema does not create, so the "
                 "generated table has no foreign_key()"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// A parent the schema does create keeps its key, whichever order the two tables are stored in and
// however either name is spelled: SQLite matches a foreign key parent case-insensitively, and so
// does the lookup that decides whether the name is in the schema at all.
TEST_CASE("generateSqliteSchemaHeader: a foreign key into a table the schema creates is kept") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE t (a INTEGER REFERENCES \"O\"(x) PRIMARY KEY, b TEXT);"
            "CREATE TABLE \"O\" (x INTEGER PRIMARY KEY);");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct O {\n"
                           "    std::optional<int64_t> x;\n"
                           "};\n\n"
                           "struct T {\n"
                           "    std::optional<int64_t> a;\n"
                           "    std::optional<std::string> b;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"O\",\n"
                           "        make_column(\"x\", &O::x, primary_key())),\n"
                           "        make_table(\"t\",\n"
                           "        make_column(\"a\", &T::a, primary_key()),\n"
                           "        make_column(\"b\", &T::b),\n"
                           "        foreign_key(&T::a).references(&O::x)));\n"
                           "}\n");
    REQUIRE(header.warnings.empty());
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// A view reading an ungenerated table has no struct to select from either, so it goes with it.
TEST_CASE("generateSqliteSchemaHeader: a view over an ungenerated table is left out") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE gen (x INTEGER PRIMARY KEY, y AS (x + 0x10000000000000000) STORED);"
            "CREATE TABLE ok1 (a INTEGER);"
            "CREATE VIEW vw AS SELECT x FROM gen;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Ok1 {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"ok1\",\n"
                           "        make_column(\"a\", &Ok1::a)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"STORED generated column 'y' uses 0x10000000000000000, too big for a signed 64-bit integer: "
                 "SQLite stores the table but refuses every row written to it, and C++ has no literal for it, so "
                 "the table is not generated"},
                {"CREATE TABLE `gen` is not merged into make_storage()"},
                {"`vw` rests on a table that is not generated and is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
}

// A table can also be named indirectly, from a subquery that sits inside an expression. The
// struct is just as absent there, so the trigger goes the same way as one deleting from the
// table outright — while the index next to it, which names nothing ungenerated, stays.
TEST_CASE("generateSqliteSchemaHeader: a trigger naming an ungenerated table in a subquery is left out") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE gen (x INTEGER PRIMARY KEY, y AS (x + 0x10000000000000000) STORED);"
            "CREATE TABLE good (a INTEGER);"
            "CREATE TRIGGER tr_sub AFTER UPDATE ON good BEGIN DELETE FROM good WHERE a IN (SELECT x FROM gen); END;"
            "CREATE INDEX i_ok ON good (a);");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Good {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_index(\"i_ok\", indexed_column(&Good::a)),\n"
                           "        make_table(\"good\",\n"
                           "        make_column(\"a\", &Good::a)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"STORED generated column 'y' uses 0x10000000000000000, too big for a signed 64-bit integer: "
                 "SQLite stores the table but refuses every row written to it, and C++ has no literal for it, so "
                 "the table is not generated"},
                {"CREATE TABLE `gen` is not merged into make_storage()"},
                {"sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS "
                 "differs from serialized output"},
                {"`tr_sub` rests on a table that is not generated and is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
}

// A trigger WHEN clause is not part of the body, and names a table just as well.
TEST_CASE("generateSqliteSchemaHeader: a trigger WHEN clause naming an ungenerated table is left out") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE gen (x INTEGER PRIMARY KEY, y AS (x + 0x10000000000000000) STORED);"
            "CREATE TABLE good (a INTEGER);"
            "CREATE TRIGGER tr_when AFTER UPDATE ON good WHEN (SELECT count(*) FROM gen) > 0 "
            "BEGIN DELETE FROM good; END;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Good {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"good\",\n"
                           "        make_column(\"a\", &Good::a)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"STORED generated column 'y' uses 0x10000000000000000, too big for a signed 64-bit integer: "
                 "SQLite stores the table but refuses every row written to it, and C++ has no literal for it, so "
                 "the table is not generated"},
                {"CREATE TABLE `gen` is not merged into make_storage()"},
                {"`tr_when` rests on a table that is not generated and is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
}

// The same for a view whose FROM names only generated tables and whose WHERE does not.
TEST_CASE("generateSqliteSchemaHeader: a view naming an ungenerated table in a subquery is left out") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE gen (x INTEGER PRIMARY KEY, y AS (x + 0x10000000000000000) STORED);"
            "CREATE TABLE good (a INTEGER);"
            "CREATE VIEW vw_sub AS SELECT a FROM good WHERE a IN (SELECT x FROM gen);");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Good {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"good\",\n"
                           "        make_column(\"a\", &Good::a)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"STORED generated column 'y' uses 0x10000000000000000, too big for a signed 64-bit integer: "
                 "SQLite stores the table but refuses every row written to it, and C++ has no literal for it, so "
                 "the table is not generated"},
                {"CREATE TABLE `gen` is not merged into make_storage()"},
                {"`vw_sub` rests on a table that is not generated and is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
}

// A view that is left out leaves sqlite_orm without a type just as an ungenerated table does,
// so what rests on the view has to go with it. Here the view is dropped for a hex literal in its
// own body: the trigger INSTEAD OF INSERT on it needs no C++26 reflection, so a header keeping it
// would simply not compile.
TEST_CASE("generateSqliteSchemaHeader: what rests on a view dropped for a hex literal is left out too") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE ok1 (a INTEGER);"
            "CREATE VIEW v1 AS SELECT a, 0x10000000000000000 AS big FROM ok1;"
            "CREATE TRIGGER trv INSTEAD OF INSERT ON v1 BEGIN INSERT INTO ok1(a) VALUES(1); END;"
            "CREATE VIEW v2 AS SELECT a FROM v1;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Ok1 {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"ok1\",\n"
                           "        make_column(\"a\", &Ok1::a)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"CREATE VIEW v1 uses 0x10000000000000000, too big for a signed 64-bit integer: SQLite "
                 "stores the view but refuses every query against it, and C++ has no literal for it, so "
                 "the view is not generated"},
                {"CREATE VIEW `v1` is not merged into make_storage()"},
                {"`v2` rests on a view that is not generated and is not merged into make_storage()"},
                {"`trv` rests on a view that is not generated and is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
}

// The same, one level deeper: the view itself is only dropped because it rests on an ungenerated
// table, and the trigger on the view has to follow.
TEST_CASE("generateSqliteSchemaHeader: what rests on a view over an ungenerated table is left out too") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE ok1 (a INTEGER);"
            "CREATE TABLE gen (x INTEGER PRIMARY KEY, y AS (x + 0x10000000000000000) STORED);"
            "CREATE VIEW v1 AS SELECT x FROM gen;"
            "CREATE TRIGGER trv INSTEAD OF INSERT ON v1 BEGIN INSERT INTO ok1(a) VALUES(1); END;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Ok1 {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"ok1\",\n"
                           "        make_column(\"a\", &Ok1::a)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"STORED generated column 'y' uses 0x10000000000000000, too big for a signed 64-bit integer: "
                 "SQLite stores the table but refuses every row written to it, and C++ has no literal for it, so "
                 "the table is not generated"},
                {"CREATE TABLE `gen` is not merged into make_storage()"},
                {"`v1` rests on a table that is not generated and is not merged into make_storage()"},
                {"`trv` rests on a view that is not generated and is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
}

// A view is also left out when its SELECT has no sqlite_orm spelling, which predates the hex
// literal rule; the dependent trigger is dropped there too, from the same branch.
TEST_CASE("generateSqliteSchemaHeader: what rests on a view with an unsupported SELECT is left out too") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE ok1 (a INTEGER);"
            "CREATE VIEW v1 AS SELECT a FROM ok1 GROUP BY a HAVING count(*) > 1;"
            "CREATE TRIGGER trv INSTEAD OF INSERT ON v1 BEGIN INSERT INTO ok1(a) VALUES(1); END;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Ok1 {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"ok1\",\n"
                           "        make_column(\"a\", &Ok1::a)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"GROUP BY in subquery is not yet mapped to sqlite_orm select(...)"},
                {"CREATE VIEW v1: SELECT is not supported for sqlite_orm code generation"},
                {"CREATE VIEW `v1` is not merged into make_storage()"},
                {"`trv` rests on a view that is not generated and is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
}

TEST_CASE("generateSqliteSchemaHeader: DML after DDL emits seed_data()") {
    auto pipelines = processMultiSql("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT);"
                                     "INSERT INTO t (id, name) VALUES (1, 'Alice');");
    REQUIRE(pipelines.size() == 2);
    REQUIRE(pipelines[0].ok());
    REQUIRE(pipelines[1].ok());

    ProcessSqliteSchemaResult schema;
    for (auto& p: pipelines) {
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
    std::optional<int64_t> id;
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
        // Here the two statements were parsed from one text, so their locations are comparable:
        // the INSERT starts at column 52 of the same line the CREATE TABLE does.
        .spans = {{125, 82, 0, SourceLocation{1, 1}, 50},
                  {351, 104, 0, SourceLocation{1, 1}, 50},
                  {461, 90, 1, SourceLocation{1, 52}, 44}},
    };
    REQUIRE(header == expected);
}

// A whole schema follows the same decision: targeting C++26, every table it merges into
// make_storage() is mapped by reflection, and the decision points of the tables reach the caller
// along with those of the views and the indices.
TEST_CASE("generateSqliteSchemaHeader: targeting C++26 merges reflected tables") {
    auto pipelines = processMultiSql("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT);");
    REQUIRE(pipelines.size() == 1);
    REQUIRE(pipelines[0].ok());

    ProcessSqliteSchemaResult schema;
    for (auto& p: pipelines) {
        SchemaStatementResult s;
        s.meta.type = "table";
        s.pipeline = std::move(p);
        schema.statements.push_back(std::move(s));
    }

    CodeGenPolicy policy;
    policy.targetCppStandard = 26;
    const CodeGenResult header = generateSqliteSchemaHeader(schema, &policy);
    REQUIRE(header.code == R"(#pragma once

#include <sqlite_orm/sqlite_orm.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace sqlite_orm;

struct [[= "t"_orm_name]] T {
    [[= primary_key()]] std::optional<int64_t> id;
    std::optional<std::string> name;
};


inline auto make_sqlite_schema_storage(const std::string& db_path) {
    using namespace sqlite_orm;
    return make_storage(db_path,
        make_table<T>());
}
)");
    REQUIRE(header.decisionPoints.size() == 1);
    REQUIRE(header.decisionPoints.at(0).category == "table_mapping_style");
    REQUIRE(header.decisionPoints.at(0).chosenValue == "reflection");
    REQUIRE(header.warnings.empty());
    REQUIRE(header.errors.empty());
}

// The names inside an annotation get unqualified lookup where the struct is written, and the
// literal operator of `[[= "t"_orm_name]]` gets nothing else at all — not even ADL — so a header
// whose structs are annotated declares sqlite_orm's names in front of them, as upstream's own
// reflection tests do. Answering the decision point with the classical mapping takes the
// annotations away, and the directive goes with them: a header of plain structs is left as it was.
TEST_CASE("generateSqliteSchemaHeader: an explicit make_table policy leaves the header unannotated") {
    auto pipelines = processMultiSql("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT);");
    REQUIRE(pipelines.size() == 1);
    REQUIRE(pipelines[0].ok());

    ProcessSqliteSchemaResult schema;
    for (auto& p: pipelines) {
        SchemaStatementResult s;
        s.meta.type = "table";
        s.pipeline = std::move(p);
        schema.statements.push_back(std::move(s));
    }

    CodeGenPolicy policy;
    policy.targetCppStandard = 26;
    policy.chosenAlternativeValueByCategory["table_mapping_style"] = "make_table";
    const CodeGenResult header = generateSqliteSchemaHeader(schema, &policy);
    REQUIRE(header.code == R"(#pragma once

#include <sqlite_orm/sqlite_orm.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct T {
    std::optional<int64_t> id;
    std::optional<std::string> name;
};


inline auto make_sqlite_schema_storage(const std::string& db_path) {
    using namespace sqlite_orm;
    return make_storage(db_path,
        make_table("t",
        make_column("id", &T::id, primary_key()),
        make_column("name", &T::name)));
}
)");
    REQUIRE(header.decisionPoints.size() == 1);
    REQUIRE(header.decisionPoints.at(0).chosenValue == "make_table");
    REQUIRE(header.warnings.empty());
    REQUIRE(header.errors.empty());
}

// A view has no classical form at all — sqlite_orm maps every one of them by reflection — so its
// struct carries an annotation whatever standard the header targets, and the directive comes with
// it even when the tables around it are mapped classically.
TEST_CASE("generateSqliteSchemaHeader: a view makes the header declare sqlite_orm's names") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE users (id INTEGER PRIMARY KEY, age INTEGER);"
            "CREATE VIEW adults AS SELECT id FROM users WHERE age >= 18;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == R"(#pragma once

#include <sqlite_orm/sqlite_orm.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace sqlite_orm;

struct Users {
    std::optional<int64_t> id;
    std::optional<int64_t> age;
};

struct [[= "adults"_orm_name]] Adults {
    std::optional<int64_t> id;
};


inline auto make_sqlite_schema_storage(const std::string& db_path) {
    using namespace sqlite_orm;
    return make_storage(db_path,
        make_table("users",
        make_column("id", &Users::id, primary_key()),
        make_column("age", &Users::age)),
        make_view<Adults>(select(&Users::id, where(c(&Users::age) >= 18))));
}
)");
    REQUIRE(header.errors.empty());
}

TEST_CASE("sqliteSchemaResultToJson: shape") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE t (id INTEGER PRIMARY KEY);");
    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(
        sqliteSchemaResultToJson(schema) ==
        R"({"statements":[{"comments":[],"decisionPoints":[],"name":"t","ok":true,"tableName":"t","type":"table"}]})");
}

// `statements[].comments` is the only way a codegen comment reaches a consumer of `--db --json`, and
// an expression generated inside a CHECK used to record one that nothing carried up: the array came
// out empty for a table whose generated code is full of forms the comments explain.
TEST_CASE("sqliteSchemaResultToJson: a comment from a CHECK reaches its statement") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE t (a INTEGER CHECK(NOT a), b INTEGER CHECK(1 - (a LIKE 'x')));");
    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(sqliteSchemaResultToJson(schema) ==
            R"({"statements":[{"comments":["A column under a NOT is generated as `column<T>(&T::x)`: )"
            R"(`operator!` is the one sqlite_orm operator that keeps the `c(...)` its operand carries )"
            R"(instead of unwrapping it, and the walker that collects the tables a statement reads stops )"
            R"(at such a wrapper — `select(not c(&T::x))` comes out with no FROM clause at all and throws )"
            R"(`SQL logic error`. The column pointer names the same column and serializes to the same )"
            R"(SQL.","A predicate under an operator is generated as `cast<int64_t>(predicate)`: sqlite_orm )"
            R"(serializes IN, BETWEEN, LIKE, GLOB, MATCH, IS [NOT] NULL and NOT without parentheses, and )"
            R"(SQLite binds them looser than the operator around them, so `1 - (a IS NULL)` would be read )"
            R"(back as `(1 - a) IS NULL`. The CAST delimits the predicate and leaves what it stands for )"
            R"(alone — a predicate is 0, 1 or NULL, and a CAST to INTEGER keeps all three, typeof )"
            R"(included."],"decisionPoints":[],"name":"t","ok":true,"tableName":"t","type":"table"}]})");
}

// Same for a view: its body is generated through the subquery form of the SELECT generator, and the
// comments recorded there stopped at it, so a consumer saw the reflection note alone. The comments
// are asserted rather than the whole JSON because a view carries a decision point per operand.
TEST_CASE("processSqliteSchema: a comment from a view body reaches its statement") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE t (a INTEGER, b TEXT);"
            "CREATE VIEW v AS SELECT 1 - (b LIKE 'x') FROM t;");
    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.statements.size() == 2);
    REQUIRE(schema.statements.at(0).meta.name == "t");
    REQUIRE(schema.statements.at(0).pipeline.codegen.comments.empty());
    REQUIRE(schema.statements.at(1).meta.name == "v");
    REQUIRE(schema.statements.at(1).pipeline.codegen.comments ==
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

// The other side of the same channel: a statement that ends up as a `not supported` placeholder has
// no generated form left for a comment to explain, and both DDL paths reach that placeholder after
// their clauses have generated and recorded. A STORED generated column holding a hex literal past
// int64 takes the whole table out, and the CHECK beside it is generated before that is known.
TEST_CASE("sqliteSchemaResultToJson: a table that generates nothing reports no comment") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE q (a INTEGER CHECK(NOT a), g AS (0x1FFFFFFFFFFFFFFFFF) STORED);");
    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(
        sqliteSchemaResultToJson(schema) ==
        R"({"statements":[{"comments":[],"decisionPoints":[],"name":"q","ok":true,"tableName":"q","type":"table"}]})");
    REQUIRE(generateSqliteSchemaHeader(schema).comments == std::vector<std::string>{});
}

// The header assembles a table from its `CreateTableParts`, a channel of its own next to the
// per-statement one, so the comments a table's clauses record have to be taken there too: the
// `table_mapping_style` decision point the parts also carry is not the only thing on them.
TEST_CASE("generateSqliteSchemaHeader: a comment from a table clause reaches the header") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE t (a INTEGER CHECK(-a > 0));");
    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(generateSqliteSchemaHeader(schema).comments ==
            std::vector<std::string>{
                "Unary minus is generated as `0 - expr`: sqlite_orm's own unary minus reports a wrong result "
                "type, so it hands the caller 0 (and throws over a column), while `0 - expr` is what SQLite "
                "computes for `-expr` — same value and same typeof for every operand kind."});
}

// The view path of the same rule: SQLite stores a body holding that literal and refuses every query
// against it, so the view is not generated although its SELECT list generated the negation.
TEST_CASE("sqliteSchemaResultToJson: a view that generates nothing reports no comment") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE t (a INTEGER, b TEXT);"
            "CREATE VIEW v AS SELECT -a AS x, 0x1FFFFFFFFFFFFFFFFF AS y FROM t;");
    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(sqliteSchemaResultToJson(schema) ==
            R"({"statements":[{"comments":[],"decisionPoints":[],"name":"t","ok":true,"tableName":"t",)"
            R"("type":"table"},{"comments":[],"decisionPoints":[],"name":"v","ok":true,"tableName":"v",)"
            R"("type":"view"}]})");
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
    execSql(file.path,
            "CREATE TABLE neg_t (a INT, b INT DEFAULT (-0x8000000000000000), "
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
        {{tooBig, SourceLocation{1, 43}, 1}, {tooBig, SourceLocation{1, 71}, 1}},
        {},
        // Both clauses generate the negation as a subtraction from zero, so the comment that
        // explains that form reaches the header from a DEFAULT and from a generated column.
        {"Unary minus is generated as `0 - expr`: sqlite_orm's own unary minus reports a wrong result "
         "type, so it hands the caller 0 (and throws over a column), while `0 - expr` is what SQLite "
         "computes for `-expr` — same value and same typeof for every operand kind."},
        {{125, 117, 0, SourceLocation{1, 1}, 98}, {386, 263, 0, SourceLocation{1, 1}, 98}}};

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
    if (exitCode != 0) {
        WARN("compiling the generated header failed (exit " << exitCode
                                                            << "); ensure c++ and sqlite_orm headers are usable");
    }
    REQUIRE(exitCode == 0);
}

// SQLite takes a literal no double holds and answers an Inf — `SELECT 9e999` and a `DEFAULT 9e999`
// alike — while C++ has no floating literal for that value: g++ 13.3 warns `floating constant
// exceeds range of 'double'` and the header stops building under `-Werror`. The value is spelled
// `std::numeric_limits<double>::infinity()` instead, and `<limits>` comes along with it; a header
// without one is left byte for byte as it was, which the merged-storage case above pins down.
// Checked against sqlite3 3.51.
TEST_CASE("generateSqliteSchemaHeader: a literal past the double range is spelled as an infinity") {
    TempDbFile file{makeTempDbPath()};
    // The two statements after the one holding the infinity are there on purpose: `<limits>` is
    // taken along by a flag the emitter sets, which is read once the whole schema is generated, so
    // it has to outlive the `resetForGeneration()` a later statement starts with. The trigger is
    // what makes that discriminating — a table is generated through `createTableParts()`, which
    // does not reset, while an index and a trigger go through `CodeGenerator::generate()`, which
    // does. Reset the flag there and this header loses its include, as a real schema would.
    execSql(file.path,
            "CREATE TABLE inf_t (a REAL DEFAULT 9e999, b REAL DEFAULT 1e-400, c REAL CHECK (c < 1e309));"
            "CREATE TABLE tail_t (t TEXT);"
            "CREATE TRIGGER tail_tr AFTER INSERT ON tail_t BEGIN INSERT INTO tail_t (t) VALUES ('x'); END;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    const CodeGenResult expected{
        std::string("#pragma once\n\n"
                    "#include <sqlite_orm/sqlite_orm.h>\n"
                    "#include <cstdint>\n"
                    "#include <limits>\n"
                    "#include <optional>\n"
                    "#include <string>\n"
                    "#include <vector>\n\n"
                    "struct InfT {\n"
                    "    std::optional<double> a;\n"
                    "    std::optional<double> b;\n"
                    "    std::optional<double> c;\n"
                    "};\n\n"
                    "struct TailT {\n"
                    "    std::optional<std::string> t;\n"
                    "};\n\n\n"
                    "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                    "    using namespace sqlite_orm;\n"
                    "    return make_storage(db_path,\n"
                    "        make_trigger(\"tail_tr\", "
                    "after().insert().on<TailT>().begin(insert(into<TailT>(), "
                    "columns(&TailT::t), values(std::make_tuple(\"x\"))))),\n"
                    "        make_table(\"inf_t\",\n"
                    "        make_column(\"a\", &InfT::a, "
                    "default_value(std::numeric_limits<double>::infinity())),\n"
                    "        make_column(\"b\", &InfT::b, default_value(0.0)),\n"
                    "        make_column(\"c\", &InfT::c, "
                    "check(c(&InfT::c) < std::numeric_limits<double>::infinity()))),\n"
                    "        make_table(\"tail_t\",\n"
                    "        make_column(\"t\", &TailT::t)));\n"
                    "}\n"),
        {},
        // The infinity is spelled, and said not to survive `sync_schema()`:
        // sqlite_orm writes it into the DDL it creates a schema object with
        // as `inf`, a name to SQLite. The zero the literal below the range
        // rounds to is a value sqlite_orm writes back fine, so it is not
        // warned about.
        {CodegenWarning{"the DEFAULT of column 'a' uses 9e999, an infinity: sqlite_orm writes an "
                        "infinity into DDL as `inf`, which SQLite reads as a column name rather than "
                        "as a number, so sync_schema() throws instead of creating table inf_t "
                        "(sqlite_orm writes a DEFAULT in parentheses, and SQLite answers DEFAULT (inf) "
                        "with \"default value of column [a] is not constant\")",
                        SourceLocation{1, 36},
                        5},
         CodegenWarning{"the CHECK on column 'c' uses 1e309, an infinity: sqlite_orm writes an infinity "
                        "into DDL as `inf`, which SQLite reads as a column name rather than as a "
                        "number, so sync_schema() throws instead of creating table inf_t (\"no such "
                        "column: inf\")",
                        SourceLocation{1, 84},
                        5}},
        {},
        {},
        // The trigger is a storage argument of its own and stands before the tables it rests on,
        // which is where its span is; the table it fires on keeps the two spans every table has.
        {{143, 104, 0, SourceLocation{1, 1}, 90},
         {248, 52, 1, SourceLocation{1, 1}, 28},
         {444, 132, 2, SourceLocation{1, 1}, 92},
         {586, 265, 0, SourceLocation{1, 1}, 90},
         {861, 57, 1, SourceLocation{1, 1}, 28}}};

    REQUIRE(header == expected);

    // `-Woverflow` is the diagnostic the out-of-range literals would raise, and nothing else in
    // the generated header or in sqlite_orm raises it, so it is the one warning worth failing on.
    const codegen_test_helpers::TempBuildDir dir;
    dir.write("gen.hpp", header.code);
    const std::filesystem::path cpppath = dir.write("check.cpp", "#include \"gen.hpp\"\n");

    std::ostringstream cmd;
    cmd << codegen_test_helpers::TempBuildDir::compilerCommand() << " -fsyntax-only -Werror=overflow";
    cmd << " -I" << dir.path().string();
    cmd << ' ' << cpppath.string();
    cmd << " 2>&1";

    const int exitCode = codegen_test_helpers::TempBuildDir::run(cmd.str());
    if (exitCode != 0) {
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

    requireCompiles(header.code);
}

// The snippet path — a .sql file, stdin or `-e`, which is what the playground runs — leaves out
// what it cannot map exactly as the schema header does. SQLite takes both schemas below; what it
// refuses is a row written to `gen` and a query against `v1`. Before the drop reached this path the
// snippet still said `&Gen::x` and `make_view<Vg>` with no struct behind them, at exit 0, and only
// a compiler saw it. Checked against sqlite3 3.51.0.
TEST_CASE("processMultiSql: the snippet of a batch with an unmappable table compiles") {
    const auto results =
        processMultiSql("CREATE TABLE gen(x INTEGER PRIMARY KEY, y AS (x + 0x10000000000000000) STORED);\n"
                        "CREATE TABLE child(id INTEGER PRIMARY KEY, gid INTEGER REFERENCES gen(x));\n"
                        "CREATE INDEX i ON gen(x);\n"
                        "CREATE VIEW vg AS SELECT x FROM gen;\n"
                        "DELETE FROM gen;");

    requireCompiles("#include <sqlite_orm/sqlite_orm.h>\n"
                    "#include <cstdint>\n"
                    "#include <optional>\n"
                    "#include <string>\n"
                    "#include <vector>\n"
                    "using namespace sqlite_orm;\n" +
                    joinGeneratedCode(results));
}

// A table constraint names its columns in whatever case and quoting the SQL was written with, while
// the member each of them has to be written as is named after the column's declaration. SQLite reads
// `ID`, `"Id"` and `[id]` as one and the same column and takes every statement below (checked against
// sqlite3 3.51.0); before the constraint's column was resolved against the declarations, each of the
// four generated a member pointer into a member the struct never declared — `&T::ID` beside
// `std::optional<int64_t> Id` — and only a compiler ever saw it, the CLI having exited 0 with no
// warning. A literal test cannot stand in for this one: it would fix whatever text came out.
TEST_CASE("processMultiSql: a table constraint spelling its column otherwise compiles") {
    const std::string prologue = "#include <sqlite_orm/sqlite_orm.h>\n"
                                 "#include <cstdint>\n"
                                 "#include <optional>\n"
                                 "#include <string>\n"
                                 "#include <vector>\n"
                                 "using namespace sqlite_orm;\n";

    requireCompiles(prologue +
                    joinGeneratedCode(processMultiSql("CREATE TABLE t (\"Id\" INTEGER, v TEXT, PRIMARY KEY(ID));")));
    requireCompiles(prologue +
                    joinGeneratedCode(processMultiSql("CREATE TABLE t (\"Id\" INTEGER, v TEXT, UNIQUE(ID));")));
    requireCompiles(prologue + joinGeneratedCode(processMultiSql(
                                   "CREATE TABLE o (k INTEGER PRIMARY KEY);\n"
                                   "CREATE TABLE t (\"Id\" INTEGER, v TEXT, FOREIGN KEY(ID) REFERENCES o(K));")));
    requireCompiles(prologue +
                    joinGeneratedCode(processMultiSql("CREATE TABLE t (\"Id\" INTEGER, v TEXT, CHECK(ID > 0));")));

    // A key back into the table's own PRIMARY KEY without naming the parent's column takes the
    // spelling from that key's constraint, in both the places a key can be written. sqlite3 3.51.0
    // takes both statements and enforces the key with PRAGMA foreign_keys=ON, and this is the last
    // place inside a CREATE TABLE that wrote a member from a constraint's own spelling: the two
    // forms came out as `references(&T::ID)` beside `primary_key(&T::Id)` — one column of one table
    // written as two different members of one `make_table`, and only a compiler ever saw it.
    requireCompiles(prologue +
                    joinGeneratedCode(processMultiSql("CREATE TABLE t (\"Id\" INTEGER, v TEXT, PRIMARY KEY(ID), "
                                                      "FOREIGN KEY(v) REFERENCES t);")));
    requireCompiles(prologue + joinGeneratedCode(processMultiSql(
                                   "CREATE TABLE t (\"Id\" INTEGER, v TEXT REFERENCES t, PRIMARY KEY(ID));")));
}

// The names inside a table declaration that are not columns at all, and that the resolution above
// had turned into member pointers into members no struct declares. A DEFAULT written without
// parentheses is a string to SQLite in every spelling, and a double-quoted name no column answers
// in a CHECK or a generated column is a string too — sqlite3 3.51.0 takes every statement below and
// stores 'A' for the default, computes 'a' || 'zz' for the generated column and enforces the CHECK.
// A literal test says what came out; only a compiler says whether `default_value("A")`,
// `check(c(&T::v) != "zz")` and `as(c(&T::v) || "zz")` are things sqlite_orm can be handed.
TEST_CASE("processMultiSql: a name in a table declaration that is not a column compiles") {
    const std::string prologue = "#include <sqlite_orm/sqlite_orm.h>\n"
                                 "#include <cstdint>\n"
                                 "#include <optional>\n"
                                 "#include <string>\n"
                                 "#include <vector>\n"
                                 "using namespace sqlite_orm;\n";

    requireCompiles(prologue + joinGeneratedCode(processMultiSql(
                                   "CREATE TABLE t (\"a\" INT, b TEXT DEFAULT A, c TEXT DEFAULT \"a\"\"b\");")));
    requireCompiles(prologue + joinGeneratedCode(processMultiSql("CREATE TABLE t (v TEXT, CHECK(v <> \"zz\"));")));
    requireCompiles(prologue + joinGeneratedCode(processMultiSql("CREATE TABLE t (v TEXT CHECK(v <> \"zz\"));")));
    requireCompiles(prologue + joinGeneratedCode(processMultiSql("CREATE TABLE t (v TEXT, g TEXT AS (v || \"zz\"));")));
}

// A generated column is the one clause whose loss takes the whole table with it, and everything
// that rests on the table goes along — so reading a double-quoted string as a column cost this
// batch the table and the index as well as the child's foreign key. sqlite3 3.51.0 takes all three
// statements and computes `g` as 'a' || 'sfx'.
TEST_CASE("processMultiSql: a batch resting on a table with a double-quoted string compiles") {
    const auto results = processMultiSql("CREATE TABLE t (v TEXT, g TEXT AS (v || \"sfx\"));\n"
                                         "CREATE TABLE d (x INTEGER REFERENCES t(v));\n"
                                         "CREATE INDEX ix ON t(v);");

    REQUIRE(results.size() == 3);
    REQUIRE(results[0].codegen.warnings.empty());

    requireCompiles("#include <sqlite_orm/sqlite_orm.h>\n"
                    "#include <cstdint>\n"
                    "#include <optional>\n"
                    "#include <string>\n"
                    "#include <vector>\n"
                    "using namespace sqlite_orm;\n" +
                    joinGeneratedCode(results));
}

// The same two spellings a column constraint can be written with: a CHECK that qualifies the column
// with the table it is declared on, and a generated column over one. SQLite takes both.
TEST_CASE("processMultiSql: a column constraint spelling its column otherwise compiles") {
    const auto results = processMultiSql("CREATE TABLE t (\"Id\" INTEGER CHECK(t.ID > 0), g INTEGER AS (ID + 1));");

    for (const auto& result: results) {
        REQUIRE(result.codegen.warnings.empty());
    }

    requireCompiles("#include <sqlite_orm/sqlite_orm.h>\n"
                    "#include <cstdint>\n"
                    "#include <optional>\n"
                    "#include <string>\n"
                    "#include <vector>\n"
                    "using namespace sqlite_orm;\n" +
                    joinGeneratedCode(results));
}

// The snippet path carries the same hole: a batch is one database, so a parent none of its
// statements creates has no struct in the snippet either. Both spellings of the key are here,
// and SQLite stores both schemas without a word (checked against sqlite3 3.51.0).
TEST_CASE("processMultiSql: the snippet of a batch with a foreign key into a missing table compiles") {
    const auto results = processMultiSql("CREATE TABLE t(a INTEGER REFERENCES o(x) PRIMARY KEY, b TEXT);\n"
                                         "CREATE TABLE t2(a INTEGER, b TEXT, FOREIGN KEY(a) REFERENCES o(x));");

    REQUIRE(results.size() == 2);
    REQUIRE(results[0].codegen.warnings ==
            std::vector<CodegenWarning>{
                {"foreign key on column 'a' references o, which this schema does not create, so the generated "
                 "table has no foreign_key()"}});
    REQUIRE(results[1].codegen.warnings ==
            std::vector<CodegenWarning>{
                {"table-level foreign key on column 'a' references o, which this schema does not create, so the "
                 "generated table has no foreign_key()"}});

    requireCompiles("#include <sqlite_orm/sqlite_orm.h>\n"
                    "#include <cstdint>\n"
                    "#include <optional>\n"
                    "#include <string>\n"
                    "#include <vector>\n"
                    "using namespace sqlite_orm;\n" +
                    joinGeneratedCode(results));
}

// A trigger's WHEN expression lives in an `optional_container`, which default-constructs it, so
// only a WHEN clause whose every sqlite_orm type has a default constructor compiles. These are the
// forms codegen claims are safe, and the claim is worth nothing unless a compiler agrees: before
// the check existed, `WHEN NEW.a IS NULL` and `WHEN NOT NEW.a` generated silently and failed here
// with `use of deleted function optional_container<...>::optional_container()`. A count(*) with a
// FILTER or an OVER is the other side of that: `count_asterisk_t::filter()` unwraps the `where_t`
// and `over_t` is an aggregate, so those compile and warning about them would be wrong. An OR is
// here because it holds only while it is spelled `or_(...)`: the `||` token it used to be generated
// with reads as a concatenation, and `conc_t` has no default constructor. The window functions are
// the same story once more: each is generated as an aggregate of its own — `row_number_t`, `lag_t`
// and the rest — and not as the `builtin_function_t` every other function call comes out, and so is
// a MATCH written as a call. SQLite stores all ten triggers below and fires every one of them but
// the MATCH, which it stores and then refuses to run, an FTS function being direct-only — exactly
// as it does for the MATCH operator (checked against the linked sqlite3 3.45.1 and against 3.51.0).
TEST_CASE("processMultiSql: the WHEN clauses codegen does not warn about compile") {
    const auto results = processMultiSql(
        "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);\n"
        "CREATE TRIGGER tr_cmp AFTER INSERT ON t WHEN NEW.a = 0 BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_and AFTER INSERT ON t WHEN NEW.a > 0 AND NEW.b = 'x' BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_or AFTER INSERT ON t WHEN NEW.a OR NEW.b BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_cast AFTER INSERT ON t WHEN CAST(NEW.a AS INTEGER) > 0 BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_sub AFTER INSERT ON t WHEN NEW.a = (SELECT a FROM t) BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_filter AFTER INSERT ON t WHEN NEW.a = (SELECT count(*) FILTER (WHERE a > 0) FROM t) "
        "BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_over AFTER INSERT ON t WHEN NEW.a = (SELECT count(*) OVER (PARTITION BY b ROWS "
        "BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) FROM t) BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_row_number AFTER INSERT ON t WHEN NEW.a = (SELECT row_number() OVER () FROM t) "
        "BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_lag AFTER INSERT ON t WHEN NEW.a = (SELECT lag(a, 1, 0) OVER (PARTITION BY b) FROM t) "
        "BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_match AFTER INSERT ON t WHEN match(NEW.b, 'x') BEGIN DELETE FROM t; END;");

    for (const auto& result: results) {
        REQUIRE(result.codegen.warnings.empty());
    }

    requireCompiles("#include <sqlite_orm/sqlite_orm.h>\n"
                    "#include <cstdint>\n"
                    "#include <optional>\n"
                    "#include <string>\n"
                    "#include <vector>\n"
                    "using namespace sqlite_orm;\n" +
                    joinGeneratedCode(results));
}

// The WHEN clauses of the sweep this came from: SQLite stores every one of them, `--db` reads them
// back, and sqlite_orm took none of the pairs below. `operator&&` is declared only where one
// operand is a condition or an operator argument, and `or_()` asserts that both of its arguments
// are operands sqlite_orm recognizes — a MATCH in either spelling, a CURRENT_* literal and a window
// call are none of them, and a scalar subquery beside a `new_()` reference is not a pair
// `operator&&` has an overload for either. The quote each of them is handed over with unwraps at
// construction, so the WHEN expression is the one that was written, default constructor included.
TEST_CASE("processMultiSql: the WHEN clauses over operands sqlite_orm does not recognize compile") {
    const auto results = processMultiSql(
        "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);\n"
        "CREATE TRIGGER tr_match_or AFTER INSERT ON t WHEN match(NEW.b, 'x') OR match(NEW.b, 'y') "
        "BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_match_and AFTER INSERT ON t WHEN NEW.b MATCH 'x' AND NEW.b MATCH 'y' "
        "BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_current_or AFTER INSERT ON t WHEN CURRENT_TIMESTAMP OR NEW.a BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_current_right AFTER INSERT ON t WHEN NEW.a OR CURRENT_DATE BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_window_or AFTER INSERT ON t WHEN row_number() OVER () OR NEW.a BEGIN DELETE FROM t; END;\n"
        "CREATE TRIGGER tr_subquery_and AFTER INSERT ON t WHEN (SELECT count(*) FROM t) AND NEW.a "
        "BEGIN DELETE FROM t; END;");

    for (const auto& result: results) {
        REQUIRE(result.codegen.warnings.empty());
    }

    requireCompiles("#include <sqlite_orm/sqlite_orm.h>\n"
                    "#include <cstdint>\n"
                    "#include <optional>\n"
                    "#include <string>\n"
                    "#include <vector>\n"
                    "using namespace sqlite_orm;\n" +
                    joinGeneratedCode(results));
}

// `make_index` deduces the table an index is made for from its first argument, and an expression
// names none, so an index over one spells the table out. Before it did, every header of a database
// holding an index over an expression — `CREATE INDEX i_expr ON t(a + 1)`, which SQLite takes and
// stores — failed to compile as a whole: `no matching function for call to make_index(const
// char[7], indexed_column_t<...>)`. `make_unique_index` has no parameter to spell it out with, so a
// UNIQUE index over an expression is left out of the storage instead. Checked against sqlite3 3.51.0.
TEST_CASE("generateSqliteSchemaHeader: an index over an expression compiles") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE t (a INTEGER PRIMARY KEY, b TEXT);"
            "CREATE INDEX i_expr ON t (a + 1);"
            "CREATE UNIQUE INDEX u_expr ON t (b || 'x');"
            "CREATE INDEX i_col ON t (b);");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == std::string("#pragma once\n\n"
                                       "#include <sqlite_orm/sqlite_orm.h>\n"
                                       "#include <cstdint>\n"
                                       "#include <optional>\n"
                                       "#include <string>\n"
                                       "#include <vector>\n\n"
                                       "struct T {\n"
                                       "    std::optional<int64_t> a;\n"
                                       "    std::optional<std::string> b;\n"
                                       "};\n\n\n"
                                       "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                                       "    using namespace sqlite_orm;\n"
                                       "    return make_storage(db_path,\n"
                                       "        make_index(\"i_col\", indexed_column(&T::b)),\n"
                                       "        make_index<T>(\"i_expr\", indexed_column(c(&T::a) + 1)),\n"
                                       "        make_table(\"t\",\n"
                                       "        make_column(\"a\", &T::a, primary_key()),\n"
                                       "        make_column(\"b\", &T::b)));\n"
                                       "}\n"));
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                "sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs "
                "from serialized output",
                "sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs "
                "from serialized output",
                "sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs "
                "from serialized output",
                "UNIQUE index u_expr starts with an expression: sqlite_orm deduces the table an index is made for "
                "from its first indexed column, and make_unique_index has no form that spells that table out, so "
                "the index is not generated",
                "CREATE INDEX `u_expr` is not merged into make_storage()"});

    requireCompiles(header.code);
}

// The snippet path joins what it generated into one make_storage(), and an index is a bare argument
// of it whatever form the generator picked for it, so what tells the two apart is the statement and
// not the text of its code.
TEST_CASE("processMultiSql: the snippet of a batch with an index over an expression compiles") {
    const auto results = processMultiSql("CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);\n"
                                         "CREATE INDEX i_expr ON t(a + 1);\n"
                                         "CREATE UNIQUE INDEX u_expr ON t(b || 'x');");

    REQUIRE(joinGeneratedCode(results) == std::string("struct T {\n"
                                                      "    std::optional<int64_t> a;\n"
                                                      "    std::optional<std::string> b;\n"
                                                      "};\n\n"
                                                      "auto storage = make_storage(\"\",\n"
                                                      "    make_index<T>(\"i_expr\", indexed_column(c(&T::a) + 1)),\n"
                                                      "    make_table(\"t\",\n"
                                                      "        make_column(\"a\", &T::a, primary_key()),\n"
                                                      "        make_column(\"b\", &T::b)));\n"));

    requireCompiles("#include <sqlite_orm/sqlite_orm.h>\n"
                    "#include <cstdint>\n"
                    "#include <optional>\n"
                    "#include <string>\n"
                    "#include <vector>\n"
                    "using namespace sqlite_orm;\n" +
                    joinGeneratedCode(results));
}

// A name is rewritten into a C++ identifier character by character, and only a compiler ever says
// whether that worked: `üü` and `ää` take four bytes each, so a rewriting that counted bytes gave
// both of them the same four underscores and the struct declared one member twice. The literal
// above says what the members are called; this says the header is one a compiler takes.
TEST_CASE("processMultiSql: the snippet of a batch with non-ASCII column names compiles") {
    const auto results = processMultiSql("CREATE TABLE t(üü INTEGER PRIMARY KEY, ää TEXT);");

    REQUIRE(joinGeneratedCode(results) == std::string("struct T {\n"
                                                      "    std::optional<int64_t> u00FCu00FC;\n"
                                                      "    std::optional<std::string> u00E4u00E4;\n"
                                                      "};\n\n"
                                                      "auto storage = make_storage(\"\",\n"
                                                      "    make_table(\"t\",\n"
                                                      "        make_column(\"üü\", &T::u00FCu00FC, primary_key()),\n"
                                                      "        make_column(\"ää\", &T::u00E4u00E4)));\n"));

    requireCompiles("#include <sqlite_orm/sqlite_orm.h>\n"
                    "#include <cstdint>\n"
                    "#include <optional>\n"
                    "#include <string>\n"
                    "#include <vector>\n"
                    "using namespace sqlite_orm;\n" +
                    joinGeneratedCode(results));
}

TEST_CASE("processMultiSql: the snippet of a batch with an ungenerated view compiles") {
    const auto results = processMultiSql("CREATE TABLE ok1(a INTEGER PRIMARY KEY);\n"
                                         "CREATE VIEW v1 AS SELECT a + 0x10000000000000000 AS b FROM ok1;\n"
                                         "CREATE VIEW v2 AS SELECT b FROM v1;\n"
                                         "CREATE TRIGGER trv INSTEAD OF INSERT ON v1 BEGIN DELETE FROM ok1; END;");

    requireCompiles("#include <sqlite_orm/sqlite_orm.h>\n"
                    "#include <cstdint>\n"
                    "#include <optional>\n"
                    "#include <string>\n"
                    "#include <vector>\n"
                    "using namespace sqlite_orm;\n" +
                    joinGeneratedCode(results));
}

// A statement the pipeline refuses is left out of the header now instead of taking the header with
// it, so the names it created have to be left out too: the trigger on `bad_v` and the view on
// `bad_t` would otherwise reach a compiler as `.on<BadV>()` and `&BadT::a` with no struct behind
// them, at a header that looks fine as text. SQLite accepts every statement below — it stores a
// view body and a CHECK without compiling them — so this whole schema comes back from sqlite_master.
// `bad_v` selects the one literal SQLite itself refuses to compile, `hex literal too big`, so that
// the view stays ungeneratable: a view that does generate is C++26 reflection code by design
// (`make_view<T>` over a `struct [[= "…"_orm_name]]`, carrying its own codegen warning) and no
// fixture here can be compiled at this project's standard.
TEST_CASE("generateSqliteSchemaHeader: a schema with a statement that did not generate still compiles") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE ok_t (id INTEGER PRIMARY KEY);"
            "CREATE TABLE bad_t (a INTEGER CHECK (a IS NOT 1));"
            "CREATE VIEW bad_v AS SELECT -0x8000000000000000 AS id FROM ok_t;"
            "CREATE VIEW on_bad_t AS SELECT a FROM bad_t;"
            "CREATE TRIGGER on_bad_v INSTEAD OF INSERT ON bad_v BEGIN DELETE FROM ok_t; END;");
    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE_FALSE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    requireCompiles(header.code);
}

// A view SQLite keeps but sqlite_orm has no spelling for is left out of the storage, and a foreign
// key into it would name a struct the header never declares — `foreign_key(&T::r).references(&V1::a)`
// with no `struct V1`, at exit 0. Which views are left out is only known after the tables have been
// generated, so the whole header is generated again once that name is in. SQLite itself takes this
// schema: a foreign key into a view is created without complaint and only `PRAGMA foreign_keys=ON`
// plus an INSERT reports `foreign key mismatch - "t" referencing "v1"`. Checked against sqlite3 3.51.
TEST_CASE("generateSqliteSchemaHeader: a foreign key into a view that is left out goes with it") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE ok1 (a INTEGER PRIMARY KEY);"
            "CREATE VIEW v1 AS SELECT a FROM ok1 GROUP BY a HAVING count(*) > 1;"
            "CREATE TABLE t (id INTEGER PRIMARY KEY, r INTEGER REFERENCES v1(a));");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Ok1 {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n\n"
                           "struct T {\n"
                           "    std::optional<int64_t> id;\n"
                           "    std::optional<int64_t> r;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"ok1\",\n"
                           "        make_column(\"a\", &Ok1::a, primary_key())),\n"
                           "        make_table(\"t\",\n"
                           "        make_column(\"id\", &T::id, primary_key()),\n"
                           "        make_column(\"r\", &T::r)));\n"
                           "}\n");
    REQUIRE(header.warnings == std::vector<CodegenWarning>{
                                   {"foreign key on column 'r' references v1, which is not generated, so the generated "
                                    "table has no foreign_key()"},
                                   {"GROUP BY in subquery is not yet mapped to sqlite_orm select(...)"},
                                   {"CREATE VIEW v1: SELECT is not supported for sqlite_orm code generation"},
                                   {"CREATE VIEW `v1` is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// A virtual table is never merged into make_storage() — sqlite_orm spells one `make_virtual_table`,
// which is not a storage argument — so the header declares no struct for it, and a foreign key into
// it or a view over it used to name `Ft` anyway, at exit 0. Its name is marked before the first
// table is generated, because a virtual table is left out whatever it generates.
TEST_CASE("generateSqliteSchemaHeader: nothing that names a virtual table is merged into the storage") {
    ProcessSqliteSchemaResult schema;
    schema.statements.push_back(masterRow("table", "ft", "CREATE VIRTUAL TABLE ft USING fts5(a)"));
    schema.statements.push_back(
        masterRow("table", "tv", "CREATE TABLE tv(id INTEGER PRIMARY KEY, r INTEGER REFERENCES ft(a))"));
    schema.statements.push_back(masterRow("view", "vv", "CREATE VIEW vv AS SELECT a FROM ft"));
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Tv {\n"
                           "    std::optional<int64_t> id;\n"
                           "    std::optional<int64_t> r;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"tv\",\n"
                           "        make_column(\"id\", &Tv::id, primary_key()),\n"
                           "        make_column(\"r\", &Tv::r)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"foreign key on column 'r' references ft, which is not generated, so the generated "
                 "table has no foreign_key()"},
                {"CREATE VIRTUAL TABLE `ft` is not merged into make_storage(); run sqlite2orm on its "
                 "SQL separately"},
                {"`vv` rests on a table that is not generated and is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// FTS5 keeps its index in ordinary tables named `<virtual-table>_data`, `_idx`, `_content`,
// `_docsize` and `_config`, and `sqlite_master` holds a plain `CREATE TABLE` for every one of
// them. They are the module's own storage: writing to one through a storage corrupts the index,
// and sync_schema() would go around the module altogether. SQLite reads the names exactly this way
// — `sqlite3ShadowTableName()` cuts at the LAST underscore and asks the module about the rest —
// and refuses `CREATE TABLE a_b_content(z)` next to `CREATE VIRTUAL TABLE a_b USING fts5(x,
// content='')` with "object name reserved for internal use". The SQL below is what sqlite3 3.51
// stores for the schema of the report.
TEST_CASE("generateSqliteSchemaHeader: the tables FTS5 keeps its index in are not merged into the storage") {
    ProcessSqliteSchemaResult schema;
    schema.statements.push_back(masterRow("table", "docs", "CREATE TABLE docs(id INTEGER PRIMARY KEY, body TEXT)"));
    schema.statements.push_back(
        masterRow("table", "docs_fts", "CREATE VIRTUAL TABLE docs_fts USING fts5(body, content='docs')"));
    schema.statements.push_back(
        masterRow("table", "docs_fts_config", "CREATE TABLE 'docs_fts_config'(k PRIMARY KEY, v) WITHOUT ROWID"));
    schema.statements.push_back(
        masterRow("table", "docs_fts_data", "CREATE TABLE 'docs_fts_data'(id INTEGER PRIMARY KEY, block BLOB)"));
    schema.statements.push_back(
        masterRow("table", "docs_fts_docsize", "CREATE TABLE 'docs_fts_docsize'(id INTEGER PRIMARY KEY, sz BLOB)"));
    schema.statements.push_back(
        masterRow("table",
                  "docs_fts_idx",
                  "CREATE TABLE 'docs_fts_idx'(segid, term, pgno, PRIMARY KEY(segid, term)) WITHOUT ROWID"));
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Docs {\n"
                           "    std::optional<int64_t> id;\n"
                           "    std::optional<std::string> body;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"docs\",\n"
                           "        make_column(\"id\", &Docs::id, primary_key()),\n"
                           "        make_column(\"body\", &Docs::body)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"CREATE TABLE `docs_fts_config` is an internal FTS5 table of virtual table `docs_fts` "
                 "and is not merged into make_storage()"},
                {"CREATE TABLE `docs_fts_data` is an internal FTS5 table of virtual table `docs_fts` "
                 "and is not merged into make_storage()"},
                {"CREATE TABLE `docs_fts_docsize` is an internal FTS5 table of virtual table `docs_fts` "
                 "and is not merged into make_storage()"},
                {"CREATE TABLE `docs_fts_idx` is an internal FTS5 table of virtual table `docs_fts` "
                 "and is not merged into make_storage()"},
                {"CREATE VIRTUAL TABLE `docs_fts` is not merged into make_storage(); run sqlite2orm on "
                 "its SQL separately"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// Only the suffixes FTS5 claims, behind a name that is a virtual table of its own, belong to the
// module. `t5_bogus` is an ordinary table, and so is `t5_extra_data` — SQLite cuts `t5_extra_data`
// at its last underscore, finds no virtual table called `t5_extra`, and creates both without a
// word (checked against sqlite3 3.51).
TEST_CASE("generateSqliteSchemaHeader: a table that only looks like FTS5 storage is merged as it is") {
    ProcessSqliteSchemaResult schema;
    schema.statements.push_back(masterRow("table", "t5", "CREATE VIRTUAL TABLE t5 USING fts5(a)"));
    schema.statements.push_back(masterRow("table", "t5_bogus", "CREATE TABLE t5_bogus(x INTEGER PRIMARY KEY)"));
    schema.statements.push_back(
        masterRow("table", "t5_data", "CREATE TABLE 't5_data'(id INTEGER PRIMARY KEY, block BLOB)"));
    schema.statements.push_back(
        masterRow("table", "t5_extra_data", "CREATE TABLE t5_extra_data(x INTEGER PRIMARY KEY)"));
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct T5Bogus {\n"
                           "    std::optional<int64_t> x;\n"
                           "};\n\n"
                           "struct T5ExtraData {\n"
                           "    std::optional<int64_t> x;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"t5_bogus\",\n"
                           "        make_column(\"x\", &T5Bogus::x, primary_key())),\n"
                           "        make_table(\"t5_extra_data\",\n"
                           "        make_column(\"x\", &T5ExtraData::x, primary_key())));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"CREATE TABLE `t5_data` is an internal FTS5 table of virtual table `t5` and is not "
                 "merged into make_storage()"},
                {"CREATE VIRTUAL TABLE `t5` is not merged into make_storage(); run sqlite2orm on its "
                 "SQL separately"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// The module reads the names without regard to case, so `My_FTS_data` is `My_FTS`'s storage. A view
// over it is a name resting on a table that gets no C++ type, and goes the same way — SQLite stores
// and runs such a view, so it is reachable from an ordinary database.
TEST_CASE("generateSqliteSchemaHeader: a view over an FTS5 table is left out with the table") {
    ProcessSqliteSchemaResult schema;
    schema.statements.push_back(masterRow("table", "My_FTS", "CREATE VIRTUAL TABLE \"My_FTS\" USING fts5(a)"));
    schema.statements.push_back(
        masterRow("table", "My_FTS_data", "CREATE TABLE 'My_FTS_data'(id INTEGER PRIMARY KEY, block BLOB)"));
    schema.statements.push_back(masterRow("view", "blocks", "CREATE VIEW blocks AS SELECT block FROM \"My_FTS_data\""));
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path);\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"CREATE TABLE `My_FTS_data` is an internal FTS5 table of virtual table `My_FTS` and is "
                 "not merged into make_storage()"},
                {"CREATE VIRTUAL TABLE `My_FTS` is not merged into make_storage(); run sqlite2orm on "
                 "its SQL separately"},
                {"`blocks` rests on a table that is not generated and is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// SQLite keeps every name spelled `sqlite_...` for itself: AUTOINCREMENT gives a database
// `sqlite_sequence` and ANALYZE gives it `sqlite_stat1`, both plain `CREATE TABLE` rows of
// `sqlite_master`. A storage holding either could never create it — sqlite3 3.51 answers
// `CREATE TABLE sqlite_sequence(name,seq)` with "object name reserved for internal use", so
// sync_schema() stops on the first attempt — and an INSERT into `sqlite_sequence` through the
// storage rewrites the bookkeeping behind every AUTOINCREMENT key. A view over one is legal SQL
// that SQLite stores and runs, and it rests on a name with no C++ type, so it goes too. The rows
// below are what sqlite3 3.51 stored for the schema of the report.
TEST_CASE("generateSqliteSchemaHeader: the objects SQLite reserves for itself are not merged into the storage") {
    ProcessSqliteSchemaResult schema;
    schema.statements.push_back(
        masterRow("table", "docs", "CREATE TABLE docs(id INTEGER PRIMARY KEY AUTOINCREMENT, body TEXT)"));
    schema.statements.push_back(masterRow("table", "sqlite_sequence", "CREATE TABLE sqlite_sequence(name,seq)"));
    schema.statements.push_back(
        masterRow("table", "docs_fts", "CREATE VIRTUAL TABLE docs_fts USING fts5(body, content='docs')"));
    schema.statements.push_back(
        masterRow("table", "docs_fts_data", "CREATE TABLE 'docs_fts_data'(id INTEGER PRIMARY KEY, block BLOB)"));
    schema.statements.push_back(
        masterRow("table",
                  "docs_fts_idx",
                  "CREATE TABLE 'docs_fts_idx'(segid, term, pgno, PRIMARY KEY(segid, term)) WITHOUT ROWID"));
    schema.statements.push_back(
        masterRow("table", "docs_fts_docsize", "CREATE TABLE 'docs_fts_docsize'(id INTEGER PRIMARY KEY, sz BLOB)"));
    schema.statements.push_back(
        masterRow("table", "docs_fts_config", "CREATE TABLE 'docs_fts_config'(k PRIMARY KEY, v) WITHOUT ROWID"));
    schema.statements.push_back(masterRow("table", "sqlite_stat1", "CREATE TABLE sqlite_stat1(tbl,idx,stat)"));
    schema.statements.push_back(masterRow("view", "seqs", "CREATE VIEW seqs AS SELECT name, seq FROM sqlite_sequence"));
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Docs {\n"
                           "    std::optional<int64_t> id;\n"
                           "    std::optional<std::string> body;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"docs\",\n"
                           "        make_column(\"id\", &Docs::id, primary_key().autoincrement()),\n"
                           "        make_column(\"body\", &Docs::body)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"CREATE TABLE `sqlite_sequence` is reserved for SQLite's own use and is not merged "
                 "into make_storage()"},
                {"CREATE TABLE `docs_fts_data` is an internal FTS5 table of virtual table `docs_fts` "
                 "and is not merged into make_storage()"},
                {"CREATE TABLE `docs_fts_idx` is an internal FTS5 table of virtual table `docs_fts` "
                 "and is not merged into make_storage()"},
                {"CREATE TABLE `docs_fts_docsize` is an internal FTS5 table of virtual table "
                 "`docs_fts` and is not merged into make_storage()"},
                {"CREATE TABLE `docs_fts_config` is an internal FTS5 table of virtual table "
                 "`docs_fts` and is not merged into make_storage()"},
                {"CREATE TABLE `sqlite_stat1` is reserved for SQLite's own use and is not merged "
                 "into make_storage()"},
                {"CREATE VIRTUAL TABLE `docs_fts` is not merged into make_storage(); run sqlite2orm "
                 "on its SQL separately"},
                {"`seqs` rests on a table that is not generated and is not merged into "
                 "make_storage()"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// The name of a reserved object is read the way SQLite reads it — without regard to case and
// through the quotes — so `"SQLite_Stat1"` is the same name as `sqlite_stat1`. sqlite3 3.51 refuses
// `CREATE TABLE SQLite_Foo(x)` and `CREATE TABLE "sqlite_foo"(x)` alike, and takes `sqlitefoo` and
// `sqlite` without a word: only the underscore makes the prefix.
TEST_CASE("generateSqliteSchemaHeader: a name that only begins like a reserved one is merged as it is") {
    ProcessSqliteSchemaResult schema;
    schema.statements.push_back(masterRow("table", "sqlitefoo", "CREATE TABLE sqlitefoo(x INTEGER PRIMARY KEY)"));
    schema.statements.push_back(masterRow("table", "SQLite_Stat1", "CREATE TABLE \"SQLite_Stat1\"(tbl,idx,stat)"));
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Sqlitefoo {\n"
                           "    std::optional<int64_t> x;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"sqlitefoo\",\n"
                           "        make_column(\"x\", &Sqlitefoo::x, primary_key())));\n"
                           "}\n");
    REQUIRE(header.warnings == std::vector<CodegenWarning>{
                                   {"CREATE TABLE `SQLite_Stat1` is reserved for SQLite's own use and is not merged "
                                    "into make_storage()"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// Which tables belong to a module is read off the tokens of the `CREATE VIRTUAL TABLE` row, not off
// its AST, because everyday FTS5 DDL never reaches an AST here: `sender UNINDEXED` is a column
// option FTS5 documents, SQLite hands module arguments to the module verbatim rather than parsing
// them, and this parser reads them as expressions and gives up. Reading the module from the AST
// left every shadow table of such a virtual table in the storage — the whole symptom, on a schema
// sqlite3 3.51 creates without a word. The statement itself is left out either way.
TEST_CASE("generateSqliteSchemaHeader: FTS5 tables are left out behind a virtual table that did not parse") {
    ProcessSqliteSchemaResult schema;
    schema.statements.push_back(
        masterRow("table", "mail", "CREATE VIRTUAL TABLE mail USING fts5(subject, body, sender UNINDEXED)"));
    schema.statements.push_back(
        masterRow("table", "mail_data", "CREATE TABLE 'mail_data'(id INTEGER PRIMARY KEY, block BLOB)"));
    schema.statements.push_back(
        masterRow("table",
                  "mail_idx",
                  "CREATE TABLE 'mail_idx'(segid, term, pgno, PRIMARY KEY(segid, term)) WITHOUT ROWID"));
    schema.statements.push_back(
        masterRow("table", "mail_content", "CREATE TABLE 'mail_content'(id INTEGER PRIMARY KEY, c0, c1, c2)"));
    schema.statements.push_back(
        masterRow("table", "mail_docsize", "CREATE TABLE 'mail_docsize'(id INTEGER PRIMARY KEY, sz BLOB)"));
    schema.statements.push_back(
        masterRow("table", "mail_config", "CREATE TABLE 'mail_config'(k PRIMARY KEY, v) WITHOUT ROWID"));
    REQUIRE_FALSE(schema.statements.front().pipeline.ok());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path);\n"
                           "}\n");
    REQUIRE(header.warnings == std::vector<CodegenWarning>{
                                   {"CREATE VIRTUAL TABLE `mail` did not generate and is not merged into "
                                    "make_storage()"},
                                   {"CREATE TABLE `mail_data` is an internal FTS5 table of virtual table `mail` and is "
                                    "not merged into make_storage()"},
                                   {"CREATE TABLE `mail_idx` is an internal FTS5 table of virtual table `mail` and is "
                                    "not merged into make_storage()"},
                                   {"CREATE TABLE `mail_content` is an internal FTS5 table of virtual table `mail` and "
                                    "is not merged into make_storage()"},
                                   {"CREATE TABLE `mail_docsize` is an internal FTS5 table of virtual table `mail` and "
                                    "is not merged into make_storage()"},
                                   {"CREATE TABLE `mail_config` is an internal FTS5 table of virtual table `mail` and "
                                    "is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// `sqlite_master.name` holds the name of the object, which SQLite already took the quotes off when
// it created it, and SQLite reads that name as it stands. A name whose own first and last character
// are quotes is therefore not a reserved name: sqlite3 3.51 creates `CREATE TABLE "'sqlite_foo'"(x
// INTEGER PRIMARY KEY, v TEXT)` without a word, under the name `'sqlite_foo'`, and only
// `sqlite_stat1` beside it belongs to the engine. Taking the quotes off the stored name a second
// time read `'sqlite_foo'` as `sqlite_foo` and threw a table of the user's own out of the storage,
// along with the foreign key pointing at it. The rows below are what sqlite3 3.51 stored.
TEST_CASE("generateSqliteSchemaHeader: a stored name that is quotes around a reserved one is merged as it is") {
    ProcessSqliteSchemaResult schema;
    schema.statements.push_back(
        masterRow("table", "'sqlite_foo'", "CREATE TABLE \"'sqlite_foo'\"(x INTEGER PRIMARY KEY, v TEXT)"));
    schema.statements.push_back(
        masterRow("table",
                  "child",
                  "CREATE TABLE child(id INTEGER PRIMARY KEY, p INTEGER REFERENCES \"'sqlite_foo'\"(x))"));
    schema.statements.push_back(masterRow("table", "sqlite_stat1", "CREATE TABLE sqlite_stat1(tbl,idx,stat)"));
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct SqliteFoo {\n"
                           "    std::optional<int64_t> x;\n"
                           "    std::optional<std::string> v;\n"
                           "};\n\n"
                           "struct Child {\n"
                           "    std::optional<int64_t> id;\n"
                           "    std::optional<int64_t> p;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"'sqlite_foo'\",\n"
                           "        make_column(\"x\", &SqliteFoo::x, primary_key()),\n"
                           "        make_column(\"v\", &SqliteFoo::v)),\n"
                           "        make_table(\"child\",\n"
                           "        make_column(\"id\", &Child::id, primary_key()),\n"
                           "        make_column(\"p\", &Child::p),\n"
                           "        foreign_key(&Child::p).references(&SqliteFoo::x)));\n"
                           "}\n");
    REQUIRE(header.warnings == std::vector<CodegenWarning>{
                                   {"CREATE TABLE `sqlite_stat1` is reserved for SQLite's own use and is not merged "
                                    "into make_storage()"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// The stored name is read as it stands on the other side of the rule too. FTS5 keeps the storage of
// `CREATE VIRTUAL TABLE "[x]" USING fts5(a)` under `[x]_data` and the four names beside it — sqlite3
// 3.51 refuses `DROP TABLE '[x]_data'` with "table [x]_data may not be dropped" — and the brackets
// belong to the name on both sides of the cut. Taking them off the shadow table's name changed
// nothing while taking them off the virtual table's turned `[x]` into `x`, so the two never met and
// all five tables went into the storage. A view over one of them rests on a name that gets no C++
// type and goes with them.
TEST_CASE("generateSqliteSchemaHeader: the tables FTS5 keeps its index in are found behind a bracketed name") {
    ProcessSqliteSchemaResult schema;
    schema.statements.push_back(masterRow("table", "[x]", "CREATE VIRTUAL TABLE \"[x]\" USING fts5(a)"));
    schema.statements.push_back(
        masterRow("table", "[x]_data", "CREATE TABLE '[x]_data'(id INTEGER PRIMARY KEY, block BLOB)"));
    schema.statements.push_back(
        masterRow("table",
                  "[x]_idx",
                  "CREATE TABLE '[x]_idx'(segid, term, pgno, PRIMARY KEY(segid, term)) WITHOUT ROWID"));
    schema.statements.push_back(
        masterRow("table", "[x]_content", "CREATE TABLE '[x]_content'(id INTEGER PRIMARY KEY, c0)"));
    schema.statements.push_back(
        masterRow("table", "[x]_docsize", "CREATE TABLE '[x]_docsize'(id INTEGER PRIMARY KEY, sz BLOB)"));
    schema.statements.push_back(
        masterRow("table", "[x]_config", "CREATE TABLE '[x]_config'(k PRIMARY KEY, v) WITHOUT ROWID"));
    schema.statements.push_back(masterRow("view", "blocks", "CREATE VIEW blocks AS SELECT block FROM \"[x]_data\""));
    schema.statements.push_back(masterRow("table", "notes", "CREATE TABLE notes(id INTEGER PRIMARY KEY, body TEXT)"));
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct Notes {\n"
                           "    std::optional<int64_t> id;\n"
                           "    std::optional<std::string> body;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"notes\",\n"
                           "        make_column(\"id\", &Notes::id, primary_key()),\n"
                           "        make_column(\"body\", &Notes::body)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"CREATE TABLE `[x]_data` is an internal FTS5 table of virtual table `[x]` and is not "
                 "merged into make_storage()"},
                {"CREATE TABLE `[x]_idx` is an internal FTS5 table of virtual table `[x]` and is not "
                 "merged into make_storage()"},
                {"CREATE TABLE `[x]_content` is an internal FTS5 table of virtual table `[x]` and is "
                 "not merged into make_storage()"},
                {"CREATE TABLE `[x]_docsize` is an internal FTS5 table of virtual table `[x]` and is "
                 "not merged into make_storage()"},
                {"CREATE TABLE `[x]_config` is an internal FTS5 table of virtual table `[x]` and is "
                 "not merged into make_storage()"},
                {"CREATE VIRTUAL TABLE `[x]` is not merged into make_storage(); run sqlite2orm on its "
                 "SQL separately"},
                {"`blocks` rests on a table that is not generated and is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// A trigger body statement with no sqlite_orm form is reachable from an ordinary database, not only
// from `-e`: SQLite stores `SELECT *, a FROM t` as a trigger step and runs it, while sqlite_orm's
// `asterisk<T>()` is the form for a result list that is a `*` and nothing else. A placeholder
// standing for the step would be a comment inside `begin(...)`, which is not C++ — the header was
// handed out at exit 0 and did not compile — so the trigger is left out of make_storage() whole and
// the warnings are what name the step. Checked against sqlite3 3.51.
TEST_CASE("generateSqliteSchemaHeader: a trigger step with no sqlite_orm form drops the trigger") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE t (a INTEGER, b TEXT);"
            "CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT b FROM t; SELECT *, a FROM t; END;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct T {\n"
                           "    std::optional<int64_t> a;\n"
                           "    std::optional<std::string> b;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"t\",\n"
                           "        make_column(\"a\", &T::a),\n"
                           "        make_column(\"b\", &T::b)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"a `*` result column next to other result columns is not mapped to sqlite_orm select(...)",
                 SourceLocation{1, 60},
                 18},
                {"a statement in the trigger body is not mapped to sqlite_orm codegen", SourceLocation{1, 60}, 18},
                {"a construct in this statement is not mapped to sqlite_orm, so the statement is not generated"},
                {"CREATE TRIGGER `tr` is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// The card's own schema: a trigger WHEN clause holding a scalar subquery that carries a GROUP BY.
// SQLite stores the trigger and fires it (checked against sqlite3 3.51), sqlite_orm has no form for
// the subquery, and the placeholder standing for it used to be emitted straight into
// `.when(c(new_(&T::a)) == /* (SELECT ...) */)` — a header offered at exit 0 that g++ refuses with
// `expected primary-expression before ')' token`. The trigger is left out of make_storage() instead.
TEST_CASE("generateSqliteSchemaHeader: a trigger WHEN clause with an unmapped subquery drops the trigger") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE t (a INTEGER);"
            "CREATE TABLE t2 (a INTEGER);"
            "CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.a = (SELECT a FROM t2 GROUP BY a) "
            "BEGIN DELETE FROM t; END;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct T {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n\n"
                           "struct T2 {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"t\",\n"
                           "        make_column(\"a\", &T::a)),\n"
                           "        make_table(\"t2\",\n"
                           "        make_column(\"a\", &T2::a)));\n"
                           "}\n");
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{
                {"scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen", SourceLocation{1, 50}, 29},
                {"GROUP BY in subquery is not yet mapped to sqlite_orm select(...)"},
                {"a construct in this statement is not mapped to sqlite_orm, so the statement is not generated"},
                {"CREATE TRIGGER `tr` is not merged into make_storage()"}});
    REQUIRE(header.errors.empty());
    requireCompiles(header.code);
}

// A header is generated from a database the user already has, and the first thing generated code
// does with it is `sync_schema()`. sqlite_orm decides what to do with a mapped table by comparing
// it column by column against `PRAGMA table_info`, and the `notnull` flag is part of that
// comparison: a member that is not an optional makes sqlite_orm emit `NOT NULL`, see the stored
// column as changed and drop the table to rebuild it — every row in it gone. A PRIMARY KEY is
// where the two disagree most easily, because SQLite leaves a PRIMARY KEY column nullable in a
// rowid table (`notnull = 0`, an INTEGER PRIMARY KEY turning an inserted NULL into the next rowid)
// and makes it NOT NULL in a WITHOUT ROWID one. Every spelling of a key is probed here on a live
// database with rows in it, because that is what the user loses.
TEST_CASE("generateSqliteSchemaHeader: sync_schema() over the database the header came from keeps the rows") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE autoinc_pk (id INTEGER PRIMARY KEY AUTOINCREMENT, v TEXT);"
            "CREATE TABLE notnull_pk (id INTEGER PRIMARY KEY NOT NULL, v TEXT NOT NULL);"
            "CREATE TABLE rowid_pk (id INTEGER PRIMARY KEY, v TEXT);"
            "CREATE TABLE table_pk (a INTEGER, b INTEGER, PRIMARY KEY(a, b));"
            "CREATE TABLE text_pk (id TEXT PRIMARY KEY, v TEXT);"
            "CREATE TABLE wr_pk (id TEXT PRIMARY KEY, v TEXT) WITHOUT ROWID;"
            "CREATE TABLE wr_table_pk (a TEXT, b TEXT, PRIMARY KEY(a, b)) WITHOUT ROWID;");
    execSql(file.path,
            "INSERT INTO autoinc_pk VALUES (1, 'keep'), (2, 'me');"
            "INSERT INTO notnull_pk VALUES (1, 'keep'), (2, 'me');"
            "INSERT INTO rowid_pk VALUES (1, 'keep'), (2, 'me');"
            "INSERT INTO table_pk VALUES (1, 1), (2, 2);"
            "INSERT INTO text_pk VALUES ('a', 'keep'), ('b', 'me');"
            "INSERT INTO wr_pk VALUES ('a', 'keep'), ('b', 'me');"
            "INSERT INTO wr_table_pk VALUES ('a', 'x'), ('b', 'y');");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    const CodeGenResult header = generateSqliteSchemaHeader(schema);
    REQUIRE(header.errors.empty());

    // An optional primary key is still a key sqlite_orm fills in, and the insert says so.
    constexpr std::string_view kInsert = R"(    AutoincPk inserted;
    inserted.v = "new";
    std::cout << "inserted autoinc_pk id=" << storage.insert(inserted) << "\n";
)";

    // `sqlite_sequence` is SQLite's own, and AUTOINCREMENT is what makes it exist; it is left out
    // of the storage, so `sync_schema()` says nothing about it either.
    REQUIRE(syncSchemaProbeOutput(header.code, file.path, kInsert) == "autoinc_pk=already_in_sync\n"
                                                                      "notnull_pk=already_in_sync\n"
                                                                      "rowid_pk=already_in_sync\n"
                                                                      "table_pk=already_in_sync\n"
                                                                      "text_pk=already_in_sync\n"
                                                                      "wr_pk=already_in_sync\n"
                                                                      "wr_table_pk=already_in_sync\n"
                                                                      "inserted autoinc_pk id=3\n");

    // The rows the database was seeded with, plus the one the probe inserted through the storage:
    // an optional primary key is still a key sqlite_orm fills in, and nothing was rebuilt.
    REQUIRE(queryText(file.path,
                      "SELECT (SELECT count(*) FROM autoinc_pk) || ',' || (SELECT count(*) FROM notnull_pk) || ',' || "
                      "(SELECT count(*) FROM rowid_pk) || ',' || (SELECT count(*) FROM table_pk) || ',' || (SELECT "
                      "count(*) FROM text_pk) || ',' || (SELECT count(*) FROM wr_pk) || ',' || (SELECT count(*) FROM "
                      "wr_table_pk);") == "3,2,2,2,2,2,2");
}

// A table-level PRIMARY KEY may name the same column twice, and SQLite reads that as one key
// column: it gives the column a single position in the key, and `PRAGMA table_info` — all
// sqlite_orm has to compare a mapped table against — answers `pk = 1` for the `a` of
// `PRIMARY KEY(a, a)` and has no second place to report. sqlite_orm ranks a key column by the last
// place its name takes in `primary_key(...)`, so naming the repeat made it rank the column second,
// see a key that changed and drop the table to rebuild it — every row in it gone. Every spelling
// of a repeat is probed here on a live database with rows in it, because that is what the user
// loses. Checked against sqlite3 3.51.0 and libsqlite3 3.45.1.
TEST_CASE("generateSqliteSchemaHeader: sync_schema() over a key that repeats a column keeps the rows") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE dup_pk (a INTEGER, b INTEGER, c TEXT, PRIMARY KEY(a, a));"
            "CREATE TABLE dup_pk_desc (a INTEGER, b INTEGER, PRIMARY KEY(a, a DESC));"
            "CREATE TABLE dup_pk_mixed (a INTEGER, b INTEGER, PRIMARY KEY(b, a, b));"
            "CREATE TABLE dup_pk_spelled (a INTEGER, b INTEGER, PRIMARY KEY(A, \"a\", [a]));"
            "CREATE TABLE dup_pk_strict (a INTEGER, b INTEGER, PRIMARY KEY(a, a)) STRICT;"
            "CREATE TABLE dup_pk_text (a TEXT, b INTEGER, PRIMARY KEY(a, a));"
            "CREATE TABLE dup_pk_wr (a TEXT, b TEXT, PRIMARY KEY(a, b, a)) WITHOUT ROWID;");
    execSql(file.path,
            "INSERT INTO dup_pk VALUES (1, 1, 'keep'), (2, 2, 'me');"
            "INSERT INTO dup_pk_desc VALUES (1, 1), (2, 2);"
            "INSERT INTO dup_pk_mixed VALUES (1, 1), (2, 2);"
            "INSERT INTO dup_pk_spelled VALUES (1, 1), (2, 2);"
            "INSERT INTO dup_pk_strict VALUES (1, 1), (2, 2);"
            "INSERT INTO dup_pk_text VALUES ('a', 1), ('b', 2);"
            "INSERT INTO dup_pk_wr VALUES ('a', 'x'), ('b', 'y');");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    const CodeGenResult header = generateSqliteSchemaHeader(schema);
    REQUIRE(header.errors.empty());

    REQUIRE(syncSchemaProbeOutput(header.code, file.path, "") == "dup_pk=already_in_sync\n"
                                                                 "dup_pk_desc=already_in_sync\n"
                                                                 "dup_pk_mixed=already_in_sync\n"
                                                                 "dup_pk_spelled=already_in_sync\n"
                                                                 "dup_pk_strict=already_in_sync\n"
                                                                 "dup_pk_text=already_in_sync\n"
                                                                 "dup_pk_wr=already_in_sync\n");

    REQUIRE(queryText(file.path,
                      "SELECT (SELECT count(*) FROM dup_pk) || ',' || (SELECT count(*) FROM dup_pk_desc) || ',' || "
                      "(SELECT count(*) FROM dup_pk_mixed) || ',' || (SELECT count(*) FROM dup_pk_spelled) || ',' || "
                      "(SELECT count(*) FROM dup_pk_strict) || ',' || (SELECT count(*) FROM dup_pk_text) || ',' || "
                      "(SELECT count(*) FROM dup_pk_wr);") == "2,2,2,2,2,2,2");
}

// What naming a repeated key column once costs, on the one shape where it costs anything. SQLite
// aliases the rowid onto a key column when the key is over a SINGLE term of declared type INTEGER,
// so `PRIMARY KEY(a, a)` — two terms — is no alias, while the `primary_key(&T::a)` the generator
// now writes is one, and sqlite_orm has no table-level key of two terms over one column to write
// instead. The database the header was generated from does not move (the case above pins that), but
// a database built from the generated code answers an INSERT that leaves the key column out
// differently: with the next rowid rather than with NULL, and by taking an INSERT that the stored
// schema refuses outright. That is the price of the fix, and it is pinned here rather than left for
// the reader to meet. The report the generator hands out for it is pinned in
// tests/codegen_tests_create_table.cpp. Measured against sqlite3 3.51.0 and the pinned sqlite_orm.
TEST_CASE("generateSqliteSchemaHeader: a key repeating its only column is a rowid alias in a new database") {
    TempDbFile source{makeTempDbPath()};
    execSql(source.path,
            "CREATE TABLE dup_int (a INTEGER, b INTEGER, PRIMARY KEY(a, a));"
            "CREATE TABLE dup_int_notnull (a INTEGER NOT NULL, b INTEGER, PRIMARY KEY(a, a));"
            "CREATE TABLE dup_text (a TEXT, b INTEGER, PRIMARY KEY(a, a));"
            "CREATE TABLE dup_two (a INTEGER, b INTEGER, PRIMARY KEY(b, a, b));");

    SqliteSchemaReader reader(source.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    const CodeGenResult header = generateSqliteSchemaHeader(schema);
    REQUIRE(header.errors.empty());

    // What the schema SQLite itself stores answers: the two-term key leaves the column alone, so a
    // row inserted without it holds NULL — and where the column is NOT NULL, there is no row.
    REQUIRE(insertWithoutKeyColumnOutput(source.path, {"dup_int", "dup_int_notnull", "dup_text", "dup_two"}) ==
            "dup_int=NULL\n"
            "dup_int_notnull=refused\n"
            "dup_text=NULL\n"
            "dup_two=NULL\n");

    // What a database built from the generated header answers instead. The TEXT key and the key
    // left with two columns are no aliases either way and stay as they were — the divergence is the
    // alias and nothing besides it.
    TempDbFile built{makeTempDbPath()};
    REQUIRE(syncSchemaProbeOutput(header.code, built.path, kInsertWithoutKeyColumnProbe) ==
            "dup_int=new_table_created\n"
            "dup_int_notnull=new_table_created\n"
            "dup_text=new_table_created\n"
            "dup_two=new_table_created\n"
            "dup_int=1\n"
            "dup_int_notnull=1\n"
            "dup_text=NULL\n"
            "dup_two=NULL\n");
}

// The shape naming a repeated column once cannot carry over, and what it costs on a live database
// with rows in it. SQLite ranks a key column of a rowid table by the term its name is first spelled
// at and leaves the rank a repeat sits at unused, so a repeat standing before a column the key has
// not named yet takes that column's rank away: sqlite3 3.51.0 ranks the `b` of
// `PRIMARY KEY(a, a, b)` third, with nothing at 2. The collapsed key ranks `b` second and the key
// written as spelled ranks `a` second — sqlite_orm takes the last place a repeated name holds — so
// neither is the stored key, and `sync_schema()` answers a key that changed by dropping the table
// and rebuilding it empty. That is pinned here as the known price rather than left to be met in a
// user's database; the report the generator hands out for it is pinned in
// tests/codegen_tests_create_table.cpp. Beside it stand the shapes that are carried over: the same
// key under WITHOUT ROWID, where SQLite drops the repeat out of the key itself and closes the gap,
// and a repeat standing behind every column the key names first.
TEST_CASE("generateSqliteSchemaHeader: sync_schema() over a key whose repeat takes a rank rebuilds it") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE gap_pk (a INTEGER, b INTEGER, c TEXT, PRIMARY KEY(a, a, b));"
            "CREATE TABLE gap_pk_middle (a INTEGER, b INTEGER, c TEXT, PRIMARY KEY(a, b, b, c));"
            "CREATE TABLE gap_pk_spelled (a INTEGER, b INTEGER, c TEXT, PRIMARY KEY(A, \"a\", [a], b));"
            "CREATE TABLE gap_pk_twice (a INTEGER, b INTEGER, c TEXT, PRIMARY KEY(a, a, a, b));"
            "CREATE TABLE gap_pk_wr (a INTEGER, b INTEGER, c TEXT, PRIMARY KEY(a, a, b)) WITHOUT ROWID;"
            "CREATE TABLE tail_pk (a INTEGER, b INTEGER, c TEXT, PRIMARY KEY(a, b, a));");
    execSql(file.path,
            "INSERT INTO gap_pk VALUES (1, 1, 'keep'), (2, 2, 'me');"
            "INSERT INTO gap_pk_middle VALUES (1, 1, 'keep'), (2, 2, 'me');"
            "INSERT INTO gap_pk_spelled VALUES (1, 1, 'keep'), (2, 2, 'me');"
            "INSERT INTO gap_pk_twice VALUES (1, 1, 'keep'), (2, 2, 'me');"
            "INSERT INTO gap_pk_wr VALUES (1, 1, 'keep'), (2, 2, 'me');"
            "INSERT INTO tail_pk VALUES (1, 1, 'keep'), (2, 2, 'me');");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    const CodeGenResult header = generateSqliteSchemaHeader(schema);
    REQUIRE(header.errors.empty());

    REQUIRE(syncSchemaProbeOutput(header.code, file.path, "") == "gap_pk=dropped_and_recreated\n"
                                                                 "gap_pk_middle=dropped_and_recreated\n"
                                                                 "gap_pk_spelled=dropped_and_recreated\n"
                                                                 "gap_pk_twice=dropped_and_recreated\n"
                                                                 "gap_pk_wr=already_in_sync\n"
                                                                 "tail_pk=already_in_sync\n");

    REQUIRE(queryText(file.path,
                      "SELECT (SELECT count(*) FROM gap_pk) || ',' || (SELECT count(*) FROM gap_pk_middle) || ',' || "
                      "(SELECT count(*) FROM gap_pk_spelled) || ',' || (SELECT count(*) FROM gap_pk_twice) || ',' || "
                      "(SELECT count(*) FROM gap_pk_wr) || ',' || (SELECT count(*) FROM tail_pk);") == "0,0,0,0,2,2");
}

// The other table option SQLite makes a key implicitly NOT NULL for is STRICT — with one exception
// this case is about: the rowid alias, which stays nullable there because the column *is* the
// rowid. Getting the exception wrong in either direction loses rows: ruling the alias NOT NULL
// rebuilds every STRICT table with an `INTEGER PRIMARY KEY`, and ruling the rest nullable rebuilds
// every other STRICT table with a key. Every spelling that decides it is probed here on a live
// database with rows in it. Checked against sqlite3 3.51.0 and libsqlite3 3.45.1.
TEST_CASE("generateSqliteSchemaHeader: sync_schema() over a STRICT database keeps the rows") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE s_alias (id INTEGER PRIMARY KEY, v TEXT) STRICT;"
            "CREATE TABLE s_alias_asc (id INTEGER PRIMARY KEY ASC, v TEXT) STRICT;"
            "CREATE TABLE s_alias_auto (id INTEGER PRIMARY KEY AUTOINCREMENT, v TEXT) STRICT;"
            "CREATE TABLE s_alias_table (id INTEGER, v TEXT, PRIMARY KEY(id)) STRICT;"
            "CREATE TABLE s_any_pk (id ANY PRIMARY KEY, v TEXT) STRICT;"
            "CREATE TABLE s_desc_pk (id INTEGER PRIMARY KEY DESC, v TEXT) STRICT;"
            "CREATE TABLE s_desc_pk_table (id INTEGER, v TEXT, PRIMARY KEY(id DESC)) STRICT;"
            "CREATE TABLE s_int_pk (id INT PRIMARY KEY, v TEXT) STRICT;"
            "CREATE TABLE s_table_pk (a TEXT, b INT, v TEXT, PRIMARY KEY(a, b)) STRICT;"
            "CREATE TABLE s_text_pk (id TEXT PRIMARY KEY, v TEXT) STRICT;"
            "CREATE TABLE s_wr_pk (id INTEGER PRIMARY KEY, v TEXT) STRICT, WITHOUT ROWID;");
    execSql(file.path,
            "INSERT INTO s_alias VALUES (1, 'keep'), (2, 'me');"
            "INSERT INTO s_alias_asc VALUES (1, 'keep'), (2, 'me');"
            "INSERT INTO s_alias_auto VALUES (1, 'keep'), (2, 'me');"
            "INSERT INTO s_alias_table VALUES (1, 'keep'), (2, 'me');"
            "INSERT INTO s_any_pk VALUES ('a', 'keep'), (2, 'me');"
            "INSERT INTO s_desc_pk VALUES (1, 'keep'), (2, 'me');"
            "INSERT INTO s_desc_pk_table VALUES (1, 'keep'), (2, 'me');"
            "INSERT INTO s_int_pk VALUES (1, 'keep'), (2, 'me');"
            "INSERT INTO s_table_pk VALUES ('a', 1, 'keep'), ('b', 2, 'me');"
            "INSERT INTO s_text_pk VALUES ('a', 'keep'), ('b', 'me');"
            "INSERT INTO s_wr_pk VALUES (1, 'keep'), (2, 'me');");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    const CodeGenResult header = generateSqliteSchemaHeader(schema);
    REQUIRE(header.errors.empty());

    // The alias of a STRICT table is an optional member, and inserting through it still fills the
    // key in — the exception costs nothing but the spelling.
    constexpr std::string_view kInsert = R"(    SAliasAuto inserted;
    inserted.v = "new";
    std::cout << "inserted s_alias_auto id=" << storage.insert(inserted) << "\n";
)";

    REQUIRE(syncSchemaProbeOutput(header.code, file.path, kInsert) == "s_alias=already_in_sync\n"
                                                                      "s_alias_asc=already_in_sync\n"
                                                                      "s_alias_auto=already_in_sync\n"
                                                                      "s_alias_table=already_in_sync\n"
                                                                      "s_any_pk=already_in_sync\n"
                                                                      "s_desc_pk=already_in_sync\n"
                                                                      "s_desc_pk_table=already_in_sync\n"
                                                                      "s_int_pk=already_in_sync\n"
                                                                      "s_table_pk=already_in_sync\n"
                                                                      "s_text_pk=already_in_sync\n"
                                                                      "s_wr_pk=already_in_sync\n"
                                                                      "inserted s_alias_auto id=3\n");

    REQUIRE(queryText(file.path,
                      "SELECT (SELECT count(*) FROM s_alias) || ',' || (SELECT count(*) FROM s_alias_asc) || ',' || "
                      "(SELECT count(*) FROM s_alias_auto) || ',' || (SELECT count(*) FROM s_alias_table) || ',' || "
                      "(SELECT count(*) FROM s_any_pk) || ',' || (SELECT count(*) FROM s_desc_pk) || ',' || (SELECT "
                      "count(*) FROM s_desc_pk_table) || ',' || (SELECT count(*) FROM s_int_pk) || ',' || (SELECT "
                      "count(*) FROM s_table_pk) || ',' || (SELECT count(*) FROM s_text_pk) || ',' || (SELECT "
                      "count(*) FROM s_wr_pk);") == "2,2,3,2,2,2,2,2,2,2,2");
}

// ANY is the one type name a STRICT table reads as "whatever the value is": SQLite stores an
// INTEGER, a REAL, a TEXT, a BLOB or a NULL in such a column exactly as it came. The affinity rule
// has no case for the name and falls through to NUMERIC, so the column used to be mapped to a
// `double` member, and everything in it that was not a number read back as 0 — with no error, and
// with `sync_schema()` saying `already_in_sync`, because sqlite_orm compares a mapped column by
// name, notnull, default, pk and hidden and never by type. A literal cannot catch that: the loss
// happens between SQLite and the member, so the values are put in with SQLite and read back out
// through the generated header. Checked against sqlite3 3.51.0 and libsqlite3 3.45.1.
TEST_CASE("generateSqliteSchemaHeader: an ANY column of a STRICT table reads every value back") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE s_any (id INTEGER PRIMARY KEY, v ANY) STRICT;");
    execSql(file.path,
            "INSERT INTO s_any VALUES (1, 'text'), (2, 42), (3, 2.5), (4, 1.0/3), (5, x'004100'), (6, NULL);");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    const CodeGenResult header = generateSqliteSchemaHeader(schema);
    REQUIRE(header.errors.empty());
    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct SAny {\n"
                           "    std::optional<int64_t> id;\n"
                           "    std::optional<std::vector<char>> v;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_table(\"s_any\",\n"
                           "        make_column(\"id\", &SAny::id, primary_key()),\n"
                           "        make_column(\"v\", &SAny::v)));\n"
                           "}\n");

    // Every byte of the stored value reaches the caller — where a `double` member answered 0 for
    // the text, the blob and the NULL alike. What the mapping does not carry is the storage class
    // the value had and, for a REAL, the digits past the 15 SQLite renders: 1.0/3 is stored as
    // 0.3333333333333331 and reads back as `0.333333333333333`. Both are the warning's to say.
    constexpr std::string_view kReadBack = R"(    auto text = [](const std::vector<char>& bytes) {
        static const char digits[] = "0123456789abcdef";
        std::string out;
        for (char byte: bytes) {
            const auto value = static_cast<unsigned char>(byte);
            if (value >= 0x20 && value < 0x7f) {
                out += byte;
            } else {
                out += "\\x";
                out += digits[(value >> 4) & 0xf];
                out += digits[value & 0xf];
            }
        }
        return out;
    };
    for (const auto& row: storage.get_all<SAny>()) {
        std::cout << "v[" << row.id.value() << "]=" << (row.v ? text(*row.v) : std::string("<null>")) << "\n";
    }
    SAny written;
    written.v = std::vector<char>{'n', 'e', 'w'};
    std::cout << "inserted s_any id=" << storage.insert(written) << "\n";
)";

    REQUIRE(syncSchemaProbeOutput(header.code, file.path, kReadBack) == "s_any=already_in_sync\n"
                                                                        "v[1]=text\n"
                                                                        "v[2]=42\n"
                                                                        "v[3]=2.5\n"
                                                                        "v[4]=0.333333333333333\n"
                                                                        "v[5]=\\x00A\\x00\n"
                                                                        "v[6]=<null>\n"
                                                                        "inserted s_any id=7\n");

    // Nothing was rebuilt — the mapped type is not what `sync_schema()` compares — and the row the
    // probe wrote through the member went in as a BLOB, which is the other half of the warning:
    // the member has one storage class to bind, whatever the column would have taken.
    REQUIRE(queryText(file.path, "SELECT count(*) || ',' || (SELECT typeof(v) FROM s_any WHERE id = 7) FROM s_any;") ==
            "7,blob");
}

// sqlite_orm syncs the database objects of a storage in declaration order only in the revisions
// after the v1.9.1 release: the release itself walks them backwards, so an index or a trigger
// written after the table it is made for reaches SQLite before that table exists and
// `sync_schema()` throws `no such table: main.t` on the very first call, leaving the database
// empty. The order written here is the one both take — every index and trigger first, the tables
// they are made for after them. The literal of the first case is what pins that order, over two
// tables so that "last argument" cannot pass for it, and it is the only guard of the order there
// is: these tests build against the pinned revision, which takes either order, so no case here
// can be run against the release the order is written for.
TEST_CASE("generateSqliteSchemaHeader: an index and a trigger stand before the table they are made for") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE t (id INTEGER PRIMARY KEY, a TEXT);"
            "CREATE TABLE u (id INTEGER PRIMARY KEY);"
            "CREATE INDEX t_a_idx ON t (a);"
            "CREATE TRIGGER t_trg AFTER INSERT ON t BEGIN DELETE FROM u; END;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    REQUIRE(schema.allOk());
    const CodeGenResult header = generateSqliteSchemaHeader(schema);

    REQUIRE(header.code == "#pragma once\n\n"
                           "#include <sqlite_orm/sqlite_orm.h>\n"
                           "#include <cstdint>\n"
                           "#include <optional>\n"
                           "#include <string>\n"
                           "#include <vector>\n\n"
                           "struct T {\n"
                           "    std::optional<int64_t> id;\n"
                           "    std::optional<std::string> a;\n"
                           "};\n\n"
                           "struct U {\n"
                           "    std::optional<int64_t> id;\n"
                           "};\n\n\n"
                           "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                           "    using namespace sqlite_orm;\n"
                           "    return make_storage(db_path,\n"
                           "        make_index(\"t_a_idx\", indexed_column(&T::a)),\n"
                           "        make_trigger(\"t_trg\", after().insert().on<T>().begin(remove_all<U>())),\n"
                           "        make_table(\"t\",\n"
                           "        make_column(\"id\", &T::id, primary_key()),\n"
                           "        make_column(\"a\", &T::a)),\n"
                           "        make_table(\"u\",\n"
                           "        make_column(\"id\", &U::id, primary_key())));\n"
                           "}\n");
    REQUIRE(header.errors.empty());
}

TEST_CASE("generateSqliteSchemaHeader: sync_schema() creates the index and the trigger over an empty database") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE t (id INTEGER PRIMARY KEY, a TEXT);"
            "CREATE TABLE u (id INTEGER PRIMARY KEY);"
            "CREATE INDEX t_a_idx ON t (a);"
            "CREATE TRIGGER t_trg AFTER INSERT ON t BEGIN DELETE FROM u; END;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    const CodeGenResult header = generateSqliteSchemaHeader(schema);
    REQUIRE(header.errors.empty());

    // The order cannot fail this case: it is built against the pinned revision, which sorts the
    // database objects by dependency and syncs a database that holds nothing at all just as fully
    // in either order (checked, both ways round). What it guards is that a header carrying an
    // index and a trigger compiles and syncs at all, and that every object the schema named
    // reaches `sqlite_master`.
    TempDbFile empty{makeTempDbPath()};
    REQUIRE(syncSchemaProbeOutput(header.code, empty.path, "") == "t=new_table_created\n"
                                                                  "t_a_idx=new_table_created\n"
                                                                  "t_trg=new_table_created\n"
                                                                  "u=new_table_created\n");

    REQUIRE(queryText(empty.path,
                      "SELECT group_concat(type || ':' || name, ',') FROM (SELECT type, name FROM sqlite_master "
                      "ORDER BY name);") == "table:t,index:t_a_idx,trigger:t_trg,table:u");
}

// The row id is the one name a CREATE TABLE resolves that the table declares no column of, and a
// CHECK of a rowid table resolves it in every spelling — so the double quotes that make an unknown
// name a string literal elsewhere do not reach it. Written as a string, the CHECK below came out
// `check(c("rowid") == 1)`, and this is what that costs past the compiler, which takes it: the table
// sync_schema() creates carries `CHECK ('rowid' = 1)`, a comparison of two constants that is false
// for every row, so nothing can be written to it — silently, the CLI exiting 0 with no warning,
// where sqlite3 3.51.0 enforces the constraint over the actual row id. There is no member to map the
// row id onto, so the constraint is left out and said so instead. A WITHOUT ROWID table has no row
// id for the name to stand for, and there the string is what SQLite itself reads: that CHECK is kept
// and syncs as the very comparison the database held.
TEST_CASE("generateSqliteSchemaHeader: a CHECK over a double-quoted row id is left out, not made a string") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE t (a INTEGER, CHECK(\"rowid\" = 1));"
            "CREATE TABLE w (a INTEGER PRIMARY KEY, CHECK(\"rowid\" = 1)) WITHOUT ROWID;");

    SqliteSchemaReader reader(file.path.string());
    const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
    const CodeGenResult header = generateSqliteSchemaHeader(schema);
    REQUIRE(header.errors.empty());
    REQUIRE(header.warnings ==
            std::vector<CodegenWarning>{{"the CHECK constraint of table t names column 'rowid', which the table does "
                                         "not declare: sqlite_orm maps no member onto the implicit row id, so the "
                                         "generated table has no check()"}});

    constexpr std::string_view kInsert = R"(    T row;
    row.a = 7;
    storage.insert(row);
    std::cout << "rows=" << storage.count<T>() << "\n";
)";

    TempDbFile empty{makeTempDbPath()};
    REQUIRE(syncSchemaProbeOutput(header.code, empty.path, kInsert) == "t=new_table_created\n"
                                                                       "w=new_table_created\n"
                                                                       "rows=1\n");
    REQUIRE(
        queryText(empty.path, "SELECT group_concat(sql, ' | ') FROM (SELECT sql FROM sqlite_master ORDER BY name);") ==
        "CREATE TABLE \"t\" (\"a\" INTEGER NULL) | "
        "CREATE TABLE \"w\" (\"a\" INTEGER PRIMARY KEY NOT NULL, CHECK ('rowid' = 1)) WITHOUT ROWID");
}
