#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace sqlite2orm {

    struct SchemaReadError : std::runtime_error {
        int sqliteResultCode = 0;

        SchemaReadError(std::string message, int sqliteResultCode);
    };

    /** One row from `sqlite_master` (type, name, tbl_name, rootpage, sql). */
    struct SqliteMasterRow {
        std::string type;
        std::string name;
        std::string tableName;
        std::int64_t rootpage = 0;
        std::string sql;
    };

    /** One row from `PRAGMA table_xinfo(table-name)`. */
    struct TableXInfoRow {
        int cid = 0;
        std::string name;
        std::string declType;
        bool notNull = false;
        std::string defaultValue;
        /** 0 = not part of PK; 1,2,… = position in ROWID/PK (SQLite rules). */
        int pk = 0;
        int hidden = 0;
    };

    /** One row from `PRAGMA foreign_key_list(table-name)`. */
    struct ForeignKeyListRow {
        int id = 0;
        int seq = 0;
        std::string table;
        std::string from;
        std::string to;
        std::string onUpdate;
        std::string onDelete;
        std::string match;
    };

    /** One row from `PRAGMA index_list(table-name)`. */
    struct IndexListRow {
        int seq = 0;
        std::string name;
        bool unique = false;
        std::string origin;
        int partial = 0;
    };

    /** One row from `PRAGMA index_info(index-name)`. */
    struct IndexInfoRow {
        int seqno = 0;
        int cid = 0;
        std::string name;
    };

    /**
     *  Read-only connection to a SQLite database file for schema introspection
     *  (sqlite_master + PRAGMA helpers from phase 21.2).
     */
    class SqliteSchemaReader {
      public:
        explicit SqliteSchemaReader(const std::string& databasePath);
        ~SqliteSchemaReader();

        SqliteSchemaReader(const SqliteSchemaReader&) = delete;
        SqliteSchemaReader& operator=(const SqliteSchemaReader&) = delete;
        SqliteSchemaReader(SqliteSchemaReader&&) noexcept;
        SqliteSchemaReader& operator=(SqliteSchemaReader&&) noexcept;

        [[nodiscard]] std::vector<SqliteMasterRow> masterEntries() const;
        [[nodiscard]] std::vector<TableXInfoRow> tableXInfo(std::string_view tableName) const;
        [[nodiscard]] std::vector<ForeignKeyListRow> foreignKeyList(std::string_view tableName) const;
        [[nodiscard]] std::vector<IndexListRow> indexList(std::string_view tableName) const;
        [[nodiscard]] std::vector<IndexInfoRow> indexInfo(std::string_view indexName) const;

      private:
        struct DbDeleter {
            void operator()(sqlite3* p) const;
        };
        std::unique_ptr<sqlite3, DbDeleter> db;

        static std::string quoteSqlStringLiteral(std::string_view identifier);
    };

}  // namespace sqlite2orm
