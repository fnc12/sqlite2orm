#include <sqlite2orm/schema_process.h>
#include <sqlite2orm/tokenizer.h>

#include "codegen_utils.h"
#include "process_internal.h"

#include <algorithm>
#include <cctype>

namespace sqlite2orm {

    namespace {

        int masterTypeOrder(std::string_view type) {
            if (type == "table") {
                return 0;
            }
            if (type == "view") {
                return 1;
            }
            if (type == "index") {
                return 2;
            }
            if (type == "trigger") {
                return 3;
            }
            return 4;
        }

    }  // namespace

    bool ProcessSqliteSchemaResult::allOk() const {
        for (const SchemaStatementResult& statement: statements) {
            if (!statement.pipeline.ok()) {
                return false;
            }
        }
        return true;
    }

    ProcessSqliteSchemaResult processSqliteSchema(const SqliteSchemaReader& reader, const CodeGenPolicy* policy) {
        std::vector<SqliteMasterRow> rows = reader.masterEntries();
        std::stable_sort(rows.begin(), rows.end(), [](const SqliteMasterRow& leftRow, const SqliteMasterRow& rightRow) {
            const int orderLeft = masterTypeOrder(leftRow.type);
            const int orderRight = masterTypeOrder(rightRow.type);
            if (orderLeft != orderRight) {
                return orderLeft < orderRight;
            }
            const auto compareNamesCaseInsensitive = [](const std::string& left, const std::string& right) {
                return std::lexicographical_compare(left.begin(),
                                                    left.end(),
                                                    right.begin(),
                                                    right.end(),
                                                    [](char leftChar, char rightChar) {
                                                        return std::tolower(static_cast<unsigned char>(leftChar)) <
                                                               std::tolower(static_cast<unsigned char>(rightChar));
                                                    });
            };
            return compareNamesCaseInsensitive(leftRow.name, rightRow.name);
        });

        // Every table's columns are known before any of them is generated: a foreign key may name a
        // parent sorted after its child, and one naming no column of the parent stands for that
        // parent's PRIMARY KEY, which only its columns tell.
        std::map<std::string, std::vector<SourceTableColumn>> sourceTables;
        for (const auto& row: rows) {
            if (row.type != "table" || row.sql.empty()) {
                continue;
            }
            try {
                Tokenizer tokenizer;
                Parser parser;
                const ParseResult parseResult = parser.parse(tokenizer.tokenize(row.sql));
                if (const auto* createTable = dynamic_cast<const CreateTableNode*>(parseResult.astNodePointer.get())) {
                    sourceTables[normalizeSqlIdentifier(stripIdentifierQuotes(createTable->tableName))] =
                        sourceTableColumnsFromCreateTable(*createTable);
                }
            } catch (const TokenizeError&) {
                // The statement reports the error when it is generated below.
            }
        }

        ProcessSqliteSchemaResult out;
        for (const auto& row: rows) {
            if (row.sql.empty()) {
                continue;
            }
            SchemaStatementResult one;
            one.meta.type = row.type;
            one.meta.name = row.name;
            one.meta.tableName = row.tableName;
            one.meta.sql = row.sql;
            one.pipeline = processSqlWithSourceTables(one.meta.sql, policy, sourceTables);
            out.statements.push_back(std::move(one));
        }
        return out;
    }

}  // namespace sqlite2orm
