#pragma once

#include <sqlite2orm/process.h>
#include <sqlite2orm/schema_reader.h>

#include <string>
#include <vector>

namespace sqlite2orm {

    struct SchemaStatementMeta {
        std::string type;
        std::string name;
        std::string tableName;
        std::string sql;

        bool operator==(const SchemaStatementMeta& other) const = default;
    };

    struct SchemaStatementResult {
        SchemaStatementMeta meta;
        ProcessSqlResult pipeline;

        bool operator==(const SchemaStatementResult& other) const = default;
    };

    /** Outcome of running the SQL pipeline on each `sqlite_master` row that carries DDL (phase 21.3). */
    struct ProcessSqliteSchemaResult {
        std::vector<SchemaStatementResult> statements;

        [[nodiscard]] bool allOk() const;

        bool operator==(const ProcessSqliteSchemaResult& other) const = default;
    };

    /**
     *  Read `sqlite_master`, order DDL (tables → views → indexes → triggers), run `processSql` per `sql`.
     */
    ProcessSqliteSchemaResult processSqliteSchema(const SqliteSchemaReader& reader);

}  // namespace sqlite2orm
