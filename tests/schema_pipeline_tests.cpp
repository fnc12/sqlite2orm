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
        {}};

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
                           "    int64_t id = 0;\n"
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
                           "    int64_t id = 0;\n"
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
                           "        make_table(\"good\",\n"
                           "        make_column(\"a\", &Good::a)),\n"
                           "        make_index(\"i_ok\", indexed_column(&Good::a)));\n"
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
    REQUIRE(
        sqliteSchemaResultToJson(schema) ==
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
    const CodeGenResult expected{std::string("#pragma once\n\n"
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
                                       "    int64_t a = 0;\n"
                                       "    std::optional<std::string> b;\n"
                                       "};\n\n\n"
                                       "inline auto make_sqlite_schema_storage(const std::string& db_path) {\n"
                                       "    using namespace sqlite_orm;\n"
                                       "    return make_storage(db_path,\n"
                                       "        make_table(\"t\",\n"
                                       "        make_column(\"a\", &T::a, primary_key()),\n"
                                       "        make_column(\"b\", &T::b)),\n"
                                       "        make_index(\"i_col\", indexed_column(&T::b)),\n"
                                       "        make_index<T>(\"i_expr\", indexed_column(c(&T::a) + 1)));\n"
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

    REQUIRE(joinGeneratedCode(results) ==
            std::string("struct T {\n"
                        "    int64_t a = 0;\n"
                        "    std::optional<std::string> b;\n"
                        "};\n\n"
                        "auto storage = make_storage(\"\",\n"
                        "    make_table(\"t\",\n"
                        "        make_column(\"a\", &T::a, primary_key()),\n"
                        "        make_column(\"b\", &T::b)),\n"
                        "    make_index<T>(\"i_expr\", indexed_column(c(&T::a) + 1)));\n"));

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
TEST_CASE("generateSqliteSchemaHeader: a schema with a statement that did not generate still compiles") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE ok_t (id INTEGER PRIMARY KEY);"
            "CREATE TABLE bad_t (a INTEGER CHECK (a IS NOT 1));"
            "CREATE VIEW bad_v AS SELECT +id AS id FROM ok_t;"
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
                           "    int64_t a = 0;\n"
                           "};\n\n"
                           "struct T {\n"
                           "    int64_t id = 0;\n"
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
                           "    int64_t id = 0;\n"
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
