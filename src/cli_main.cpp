#include <sqlite2orm/json_emit.h>
#include <sqlite2orm/process.h>
#include <sqlite2orm/schema_header.h>
#include <sqlite2orm/schema_process.h>
#include <sqlite2orm/schema_reader.h>

#include <fmt/format.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

    void printUsage(FILE* out) {
        fmt::print(out,
                   "sqlite2orm — sqlite2orm codegen (single SQL statement or .sqlite3 schema)\n\n"
                   "Usage:\n"
                   "  sqlite2orm -e <sql>           Codegen from a SQL string\n"
                   "  sqlite2orm --db <file.sqlite3> [--json]\n"
                   "                                Full header from DB schema (phase 21)\n"
                   "  sqlite2orm <file.sql>         Read one statement from file\n"
                   "  sqlite2orm                    Read one statement from stdin\n"
                   "\n"
                   "Options:\n"
                   "  --json                       With --db: print JSON decision points (stderr: warnings)\n"
                   "  -h, --help                   Show this help\n");
    }

    std::string readStream(std::istream& in) {
        std::ostringstream oss;
        oss << in.rdbuf();
        return oss.str();
    }

    std::string readFile(const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        if(!stream) {
            throw std::runtime_error("cannot open file: " + path);
        }
        return readStream(stream);
    }

    int indexOfArg(int argc, char** argv, std::string_view flag) {
        for(int i = 1; i < argc; ++i) {
            if(std::string_view(argv[i]) == flag) {
                return i;
            }
        }
        return -1;
    }

    int runDbMode(const std::string& dbPath, bool jsonOnly) {
        using namespace sqlite2orm;
        try {
            SqliteSchemaReader reader(dbPath);
            const ProcessSqliteSchemaResult schema = processSqliteSchema(reader);
            if(jsonOnly) {
                fmt::print("{}\n", sqliteSchemaResultToJson(schema));
            }
            for(const auto& st: schema.statements) {
                for(const auto& w: st.pipeline.codegen.warnings) {
                    fmt::print(stderr, "warning: {}\n", w);
                }
            }
            if(!jsonOnly) {
                const CodeGenResult header = generateSqliteSchemaHeader(schema);
                for(const auto& w: header.warnings) {
                    fmt::print(stderr, "warning: {}\n", w);
                }
                if(!schema.allOk()) {
                    for(const auto& st: schema.statements) {
                        if(st.pipeline.ok()) {
                            continue;
                        }
                        for(const auto& err: st.pipeline.parseResult.errors) {
                            fmt::print(stderr, "parse error [{} {}]: {} at {}:{}\n", st.meta.type, st.meta.name,
                                       err.message, err.location.line, err.location.column);
                        }
                        for(const auto& err: st.pipeline.validationErrors) {
                            fmt::print(stderr, "validation [{} {}]: {} ({})\n", st.meta.type, st.meta.name,
                                       err.message, err.nodeType);
                        }
                    }
                    return 1;
                }
                fmt::print("{}", header.code);
                if(!header.code.empty() && header.code.back() != '\n') {
                    fmt::print("\n");
                }
            }
        } catch(const SchemaReadError& e) {
            fmt::print(stderr, "sqlite2orm: cannot open database: {}\n", e.what());
            return 2;
        } catch(const std::exception& ex) {
            fmt::print(stderr, "sqlite2orm: {}\n", ex.what());
            return 2;
        }
        return EXIT_SUCCESS;
    }

}  // namespace

int main(int argc, char** argv) {
    using namespace sqlite2orm;

    const int dbFlag = indexOfArg(argc, argv, "--db");
    if(dbFlag >= 0) {
        if(dbFlag + 1 >= argc) {
            fmt::print(stderr, "sqlite2orm: --db requires a path\n");
            printUsage(stderr);
            return 2;
        }
        const bool jsonOnly = indexOfArg(argc, argv, "--json") >= 0;
        return runDbMode(argv[dbFlag + 1], jsonOnly);
    }

    std::string sql;
    try {
        if(argc >= 2) {
            const std::string_view arg1 = argv[1];
            if(arg1 == "-h" || arg1 == "--help") {
                printUsage(stdout);
                return EXIT_SUCCESS;
            }
            if(arg1 == "-e") {
                if(argc < 3) {
                    fmt::print(stderr, "sqlite2orm: -e requires a SQL argument\n");
                    printUsage(stderr);
                    return 2;
                }
                sql = argv[2];
            } else {
                sql = readFile(argv[1]);
            }
        } else {
            sql = readStream(std::cin);
        }
    } catch(const std::exception& ex) {
        fmt::print(stderr, "sqlite2orm: {}\n", ex.what());
        return 2;
    }

    if(sql.empty()) {
        fmt::print(stderr, "sqlite2orm: empty SQL input\n");
        return 2;
    }

    const auto results = processMultiSql(sql);
    int exitCode = EXIT_SUCCESS;
    for(const ProcessSqlResult& result : results) {
        for(const auto& warning : result.codegen.warnings) {
            fmt::print(stderr, "warning: {}\n", warning);
        }
        if(!result.parseResult.errors.empty()) {
            for(const auto& err : result.parseResult.errors) {
                fmt::print(stderr, "parse error: {} at {}:{}\n", err.message, err.location.line, err.location.column);
            }
            exitCode = 1;
        }
        if(!result.validationErrors.empty()) {
            for(const auto& err : result.validationErrors) {
                fmt::print(stderr, "validation: {} ({})\n", err.message, err.nodeType);
            }
            exitCode = 1;
        }
    }
    const auto code = joinGeneratedCode(results);
    if(!code.empty()) {
        fmt::print("{}", code);
    }
    return exitCode;
}
