#include <sqlite2orm/schema_reader.h>

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
               ("sqlite2orm_schema_" + std::to_string(dist(gen)) + ".db");
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

}  // namespace

TEST_CASE("SqliteSchemaReader: open missing file throws") {
    const auto path = makeTempDbPath();
    REQUIRE_THROWS_AS(SqliteSchemaReader(path.string()), SchemaReadError);
}

TEST_CASE("SqliteSchemaReader: master, table_xinfo, foreign keys, indexes") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path,
            "CREATE TABLE parent (id INTEGER PRIMARY KEY, name TEXT);"
            "CREATE TABLE child (id INTEGER PRIMARY KEY, parent_id INTEGER NOT NULL, "
            "  FOREIGN KEY (parent_id) REFERENCES parent(id));"
            "CREATE INDEX idx_child_parent ON child(parent_id);");

    SqliteSchemaReader reader(file.path.string());
    const auto master = reader.masterEntries();
    REQUIRE_FALSE(master.empty());

    bool sawParent = false;
    bool sawChild = false;
    bool sawIndex = false;
    for(const auto& row: master) {
        if(row.type == "table" && row.name == "parent") {
            sawParent = true;
            REQUIRE(row.tableName == "parent");
            REQUIRE_FALSE(row.sql.empty());
        }
        if(row.type == "table" && row.name == "child") {
            sawChild = true;
            REQUIRE(row.tableName == "child");
        }
        if(row.type == "index" && row.name == "idx_child_parent") {
            sawIndex = true;
            REQUIRE(row.tableName == "child");
        }
    }
    REQUIRE(sawParent);
    REQUIRE(sawChild);
    REQUIRE(sawIndex);

    const auto childCols = reader.tableXInfo("child");
    REQUIRE(childCols.size() == 2);
    REQUIRE(childCols[0].name == "id");
    REQUIRE(childCols[0].pk == 1);
    REQUIRE(childCols[1].name == "parent_id");
    REQUIRE(childCols[1].notNull);

    const auto fks = reader.foreignKeyList("child");
    REQUIRE(fks.size() == 1);
    REQUIRE(fks[0].table == "parent");
    REQUIRE(fks[0].from == "parent_id");
    REQUIRE(fks[0].to == "id");

    const auto indexes = reader.indexList("child");
    bool sawNamedIndex = false;
    for(const auto& ix: indexes) {
        if(ix.name == "idx_child_parent") {
            sawNamedIndex = true;
            REQUIRE_FALSE(ix.unique);
        }
    }
    REQUIRE(sawNamedIndex);

    const auto info = reader.indexInfo("idx_child_parent");
    REQUIRE_FALSE(info.empty());
    REQUIRE(info[0].name == "parent_id");
    REQUIRE(info[0].cid == 1);
}

TEST_CASE("SqliteSchemaReader: quoted table name in PRAGMA") {
    TempDbFile file{makeTempDbPath()};
    execSql(file.path, "CREATE TABLE \"weird'name\" (x INTEGER);");

    SqliteSchemaReader reader(file.path.string());
    const auto cols = reader.tableXInfo("weird'name");
    REQUIRE(cols.size() == 1);
    REQUIRE(cols[0].name == "x");
}
