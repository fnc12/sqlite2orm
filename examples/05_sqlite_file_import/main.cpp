#include <sqlite2orm/schema_header.h>
#include <sqlite2orm/schema_process.h>
#include <sqlite2orm/schema_reader.h>

#include <fmt/format.h>

int main(int argc, char* argv[]) {
    if(argc < 2) {
        fmt::print(stderr, "usage: {} <database.sqlite>\n", argv[0]);
        return 1;
    }

    try {
        sqlite2orm::SqliteSchemaReader reader(argv[1]);
        auto schema = sqlite2orm::processSqliteSchema(reader);

        if(!schema.allOk()) {
            for(const auto& stmt : schema.statements) {
                if(!stmt.pipeline.ok()) {
                    fmt::print(stderr, "problem with {} {}:\n", stmt.meta.type, stmt.meta.name);
                    for(const auto& error : stmt.pipeline.validationErrors) {
                        fmt::print(stderr, "  validation: {}\n", error.message);
                    }
                }
            }
        }

        auto header = sqlite2orm::generateSqliteSchemaHeader(schema);
        fmt::print("{}\n", header.code);

    } catch(const sqlite2orm::SchemaReadError& e) {
        fmt::print(stderr, "schema read error: {}\n", e.what());
        return 1;
    }

    return 0;
}
