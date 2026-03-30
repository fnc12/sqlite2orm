#include <sqlite2orm/schema_reader.h>

#include <sqlite3.h>

#include <sstream>
#include <string_view>
#include <utility>

namespace sqlite2orm {

    SchemaReadError::SchemaReadError(std::string message, int sqliteResultCode) :
        std::runtime_error(std::move(message)), sqliteResultCode(sqliteResultCode) {}

    void SqliteSchemaReader::DbDeleter::operator()(sqlite3* p) const {
        if (p) {
            sqlite3_close(p);
        }
    }

    SqliteSchemaReader::SqliteSchemaReader(const std::string& databasePath) {
        sqlite3* raw = nullptr;
        const int rc = sqlite3_open_v2(databasePath.c_str(),
                                       &raw,
                                       SQLITE_OPEN_READONLY,
                                       nullptr);
        if (rc != SQLITE_OK) {
            const char* err = raw ? sqlite3_errmsg(raw) : sqlite3_errstr(rc);
            if (raw) {
                sqlite3_close(raw);
            }
            throw SchemaReadError(err ? std::string{err} : "sqlite3_open_v2 failed", rc);
        }
        db.reset(raw);
    }

    SqliteSchemaReader::~SqliteSchemaReader() = default;

    SqliteSchemaReader::SqliteSchemaReader(SqliteSchemaReader&&) noexcept = default;

    SqliteSchemaReader& SqliteSchemaReader::operator=(SqliteSchemaReader&&) noexcept = default;

    namespace {

        [[nodiscard]] std::string textOrEmpty(sqlite3_stmt* st, int iCol) {
            if (sqlite3_column_type(st, iCol) == SQLITE_NULL) {
                return {};
            }
            const unsigned char* t = sqlite3_column_text(st, iCol);
            return t ? std::string{reinterpret_cast<const char*>(t)} : std::string{};
        }

        void checkPrepare(sqlite3* rawDb, int rc, std::string_view context) {
            if (rc == SQLITE_OK) {
                return;
            }
            const char* err = rawDb ? sqlite3_errmsg(rawDb) : sqlite3_errstr(rc);
            std::ostringstream oss;
            oss << context;
            if (err) {
                oss << ": " << err;
            }
            throw SchemaReadError(oss.str(), rc);
        }

    }  // namespace

    std::string SqliteSchemaReader::quoteSqlStringLiteral(std::string_view identifier) {
        std::string out;
        out.reserve(identifier.size() + 2);
        out.push_back('\'');
        for (const char c: identifier) {
            if (c == '\'') {
                out.append("''");
            } else {
                out.push_back(c);
            }
        }
        out.push_back('\'');
        return out;
    }

    std::vector<SqliteMasterRow> SqliteSchemaReader::masterEntries() const {
        sqlite3* rawDb = db.get();
        sqlite3_stmt* st = nullptr;
        static constexpr const char kSql[] =
            "SELECT type, name, tbl_name, rootpage, sql FROM sqlite_master ORDER BY type, name;";
        int rc = sqlite3_prepare_v2(rawDb, kSql, -1, &st, nullptr);
        checkPrepare(rawDb, rc, "sqlite_master prepare");
        std::vector<SqliteMasterRow> out;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            SqliteMasterRow row;
            row.type = textOrEmpty(st, 0);
            row.name = textOrEmpty(st, 1);
            row.tableName = textOrEmpty(st, 2);
            row.rootpage = sqlite3_column_int64(st, 3);
            row.sql = textOrEmpty(st, 4);
            out.push_back(std::move(row));
        }
        const int stepRc = rc;
        sqlite3_finalize(st);
        if (stepRc != SQLITE_DONE) {
            checkPrepare(rawDb, stepRc, "sqlite_master step");
        }
        return out;
    }

    std::vector<TableXInfoRow> SqliteSchemaReader::tableXInfo(std::string_view tableName) const {
        const std::string sql = std::string{"PRAGMA table_xinfo("} + quoteSqlStringLiteral(tableName) + ");";
        sqlite3* rawDb = db.get();
        sqlite3_stmt* st = nullptr;
        int rc = sqlite3_prepare_v2(rawDb, sql.c_str(), static_cast<int>(sql.size()), &st, nullptr);
        checkPrepare(rawDb, rc, "PRAGMA table_xinfo prepare");
        std::vector<TableXInfoRow> out;
        const int nCol = sqlite3_column_count(st);
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            TableXInfoRow row;
            row.cid = sqlite3_column_int(st, 0);
            row.name = textOrEmpty(st, 1);
            row.declType = textOrEmpty(st, 2);
            row.notNull = sqlite3_column_int(st, 3) != 0;
            row.defaultValue = textOrEmpty(st, 4);
            row.pk = sqlite3_column_int(st, 5);
            if (nCol >= 7) {
                row.hidden = sqlite3_column_int(st, 6);
            }
            out.push_back(std::move(row));
        }
        const int stepRc = rc;
        sqlite3_finalize(st);
        if (stepRc != SQLITE_DONE) {
            checkPrepare(rawDb, stepRc, "PRAGMA table_xinfo step");
        }
        return out;
    }

    std::vector<ForeignKeyListRow> SqliteSchemaReader::foreignKeyList(std::string_view tableName) const {
        const std::string sql =
            std::string{"PRAGMA foreign_key_list("} + quoteSqlStringLiteral(tableName) + ");";
        sqlite3* rawDb = db.get();
        sqlite3_stmt* st = nullptr;
        int rc = sqlite3_prepare_v2(rawDb, sql.c_str(), static_cast<int>(sql.size()), &st, nullptr);
        checkPrepare(rawDb, rc, "PRAGMA foreign_key_list prepare");
        std::vector<ForeignKeyListRow> out;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            ForeignKeyListRow row;
            row.id = sqlite3_column_int(st, 0);
            row.seq = sqlite3_column_int(st, 1);
            row.table = textOrEmpty(st, 2);
            row.from = textOrEmpty(st, 3);
            row.to = textOrEmpty(st, 4);
            row.onUpdate = textOrEmpty(st, 5);
            row.onDelete = textOrEmpty(st, 6);
            row.match = textOrEmpty(st, 7);
            out.push_back(std::move(row));
        }
        const int stepRc = rc;
        sqlite3_finalize(st);
        if (stepRc != SQLITE_DONE) {
            checkPrepare(rawDb, stepRc, "PRAGMA foreign_key_list step");
        }
        return out;
    }

    std::vector<IndexListRow> SqliteSchemaReader::indexList(std::string_view tableName) const {
        const std::string sql = std::string{"PRAGMA index_list("} + quoteSqlStringLiteral(tableName) + ");";
        sqlite3* rawDb = db.get();
        sqlite3_stmt* st = nullptr;
        int rc = sqlite3_prepare_v2(rawDb, sql.c_str(), static_cast<int>(sql.size()), &st, nullptr);
        checkPrepare(rawDb, rc, "PRAGMA index_list prepare");
        std::vector<IndexListRow> out;
        const int nCol = sqlite3_column_count(st);
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            IndexListRow row;
            row.seq = sqlite3_column_int(st, 0);
            row.name = textOrEmpty(st, 1);
            row.unique = sqlite3_column_int(st, 2) != 0;
            row.origin = textOrEmpty(st, 3);
            if (nCol >= 5) {
                row.partial = sqlite3_column_int(st, 4);
            }
            out.push_back(std::move(row));
        }
        const int stepRc = rc;
        sqlite3_finalize(st);
        if (stepRc != SQLITE_DONE) {
            checkPrepare(rawDb, stepRc, "PRAGMA index_list step");
        }
        return out;
    }

    std::vector<IndexInfoRow> SqliteSchemaReader::indexInfo(std::string_view indexName) const {
        const std::string sql = std::string{"PRAGMA index_info("} + quoteSqlStringLiteral(indexName) + ");";
        sqlite3* rawDb = db.get();
        sqlite3_stmt* st = nullptr;
        int rc = sqlite3_prepare_v2(rawDb, sql.c_str(), static_cast<int>(sql.size()), &st, nullptr);
        checkPrepare(rawDb, rc, "PRAGMA index_info prepare");
        std::vector<IndexInfoRow> out;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            IndexInfoRow row;
            row.seqno = sqlite3_column_int(st, 0);
            row.cid = sqlite3_column_int(st, 1);
            row.name = textOrEmpty(st, 2);
            out.push_back(std::move(row));
        }
        const int stepRc = rc;
        sqlite3_finalize(st);
        if (stepRc != SQLITE_DONE) {
            checkPrepare(rawDb, stepRc, "PRAGMA index_info step");
        }
        return out;
    }

}  // namespace sqlite2orm
