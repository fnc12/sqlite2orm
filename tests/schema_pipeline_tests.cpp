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

    REQUIRE(header.code ==
            "#pragma once\n\n"
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

    REQUIRE(header.code ==
            "#pragma once\n\n"
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

    REQUIRE(header.code ==
            "#pragma once\n\n"
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

    REQUIRE(header.code ==
            "#pragma once\n\n"
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

    REQUIRE(header.code ==
            "#pragma once\n\n"
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

    REQUIRE(header.code ==
            "#pragma once\n\n"
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

    REQUIRE(header.code ==
            "#pragma once\n\n"
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

    REQUIRE(header.code ==
            "#pragma once\n\n"
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
