#include <sqlite2orm/schema_process.h>

#include <algorithm>
#include <cctype>

namespace sqlite2orm {

    namespace {

        int masterTypeOrder(std::string_view type) {
            if(type == "table") {
                return 0;
            }
            if(type == "view") {
                return 1;
            }
            if(type == "index") {
                return 2;
            }
            if(type == "trigger") {
                return 3;
            }
            return 4;
        }

    }  // namespace

    bool ProcessSqliteSchemaResult::allOk() const {
        for(const SchemaStatementResult& statement : statements) {
            if(!statement.pipeline.ok()) {
                return false;
            }
        }
        return true;
    }

    ProcessSqliteSchemaResult processSqliteSchema(const SqliteSchemaReader& reader) {
        std::vector<SqliteMasterRow> rows = reader.masterEntries();
        std::stable_sort(rows.begin(), rows.end(), [](const SqliteMasterRow& leftRow, const SqliteMasterRow& rightRow) {
            const int orderLeft = masterTypeOrder(leftRow.type);
            const int orderRight = masterTypeOrder(rightRow.type);
            if(orderLeft != orderRight) {
                return orderLeft < orderRight;
            }
            const auto compareNamesCaseInsensitive = [](const std::string& left, const std::string& right) {
                return std::lexicographical_compare(
                    left.begin(), left.end(), right.begin(), right.end(), [](char leftChar, char rightChar) {
                        return std::tolower(static_cast<unsigned char>(leftChar)) <
                               std::tolower(static_cast<unsigned char>(rightChar));
                    });
            };
            return compareNamesCaseInsensitive(leftRow.name, rightRow.name);
        });

        ProcessSqliteSchemaResult out;
        for(const auto& row: rows) {
            if(row.sql.empty()) {
                continue;
            }
            SchemaStatementMeta meta;
            meta.type = row.type;
            meta.name = row.name;
            meta.tableName = row.tableName;
            meta.sql = row.sql;
            SchemaStatementResult one;
            one.meta = std::move(meta);
            one.pipeline = processSql(row.sql);
            out.statements.push_back(std::move(one));
        }
        return out;
    }

}  // namespace sqlite2orm
