#include <sqlite2orm/cpp_standard.h>
#include <sqlite2orm/process.h>
#include <sqlite2orm/schema_process.h>
#include <sqlite2orm/schema_reader.h>
#include <sqlite2orm/schema_report.h>

#include <fmt/format.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

    void printUsage(FILE* out) {
        fmt::print(out,
                   "sqlite2orm — sqlite2orm codegen (single SQL statement or .sqlite3 schema)\n\n"
                   "Usage:\n"
                   "  sqlite2orm -e <sql>           Codegen from a SQL string\n"
                   "  sqlite2orm --db <file.sqlite3> [--json] [--strict]\n"
                   "                                Full header from DB schema (phase 21)\n"
                   "  sqlite2orm <file.sql>         Read one statement from file\n"
                   "  sqlite2orm                    Read one statement from stdin\n"
                   "\n"
                   "Options:\n"
                   "  --json                       With --db: print JSON decision points (stderr: diagnostics)\n"
                   "  --strict                     With --db: exit 1 unless every schema statement is in the\n"
                   "                               generated header (the coverage --json reports)\n"
                   "  --std <14|17|20|26>          Target C++ standard of the generated code (default: 20)\n"
                   "  -h, --help                   Show this help\n");
    }

    std::string readStream(std::istream& in) {
        std::ostringstream oss;
        oss << in.rdbuf();
        return oss.str();
    }

    std::string readFile(const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            throw std::runtime_error("cannot open file: " + path);
        }
        return readStream(stream);
    }

    int indexOfArg(const std::vector<std::string_view>& args, std::string_view flag) {
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == flag) {
                return int(i);
            }
        }
        return -1;
    }

    int runDbMode(const std::string& dbPath, bool jsonOnly, bool strict, const sqlite2orm::CodeGenPolicy* policy) {
        using namespace sqlite2orm;
        try {
            SqliteSchemaReader reader(dbPath);
            const ProcessSqliteSchemaResult schema = processSqliteSchema(reader, policy);
            const SchemaReport report = reportSqliteSchema(schema, jsonOnly, policy, strict);
            fmt::print(stderr, "{}", report.err);
            fmt::print("{}", report.out);
            return report.exitCode;
        } catch (const SchemaReadError& e) {
            fmt::print(stderr, "sqlite2orm: cannot open database: {}\n", e.what());
            return 2;
        } catch (const std::exception& ex) {
            fmt::print(stderr, "sqlite2orm: {}\n", ex.what());
            return 2;
        }
    }

}  // namespace

int main(int argc, char** argv) {
    using namespace sqlite2orm;

    std::vector<std::string_view> args(argv + 1, argv + argc);

    // Without `--std` no policy is passed at all, so the default run is exactly what it was before
    // the flag existed.
    std::optional<CodeGenPolicy> policy;
    const int stdFlag = indexOfArg(args, "--std");
    if (stdFlag >= 0) {
        if (size_t(stdFlag) + 1 >= args.size()) {
            fmt::print(stderr, "sqlite2orm: --std requires a value (one of {})\n", kCppStandardChoices);
            printUsage(stderr);
            return 2;
        }
        const std::string_view value = args[stdFlag + 1];
        const std::optional<int> standard = parseCppStandard(value);
        if (!standard) {
            fmt::print(stderr,
                       "sqlite2orm: unsupported --std value '{}' (expected one of {})\n",
                       value,
                       kCppStandardChoices);
            printUsage(stderr);
            return 2;
        }
        policy.emplace();
        policy->targetCppStandard = *standard;
        args.erase(args.begin() + stdFlag, args.begin() + stdFlag + 2);
    }
    const CodeGenPolicy* policyPointer = policy ? &*policy : nullptr;

    const int dbFlag = indexOfArg(args, "--db");
    if (dbFlag >= 0) {
        if (size_t(dbFlag) + 1 >= args.size()) {
            fmt::print(stderr, "sqlite2orm: --db requires a path\n");
            printUsage(stderr);
            return 2;
        }
        const bool jsonOnly = indexOfArg(args, "--json") >= 0;
        const bool strict = indexOfArg(args, "--strict") >= 0;
        return runDbMode(std::string(args[dbFlag + 1]), jsonOnly, strict, policyPointer);
    }

    std::string sql;
    try {
        if (!args.empty()) {
            const std::string_view arg1 = args[0];
            if (arg1 == "-h" || arg1 == "--help") {
                printUsage(stdout);
                return EXIT_SUCCESS;
            }
            if (arg1 == "-e") {
                if (args.size() < 2) {
                    fmt::print(stderr, "sqlite2orm: -e requires a SQL argument\n");
                    printUsage(stderr);
                    return 2;
                }
                sql = args[1];
            } else {
                sql = readFile(std::string(arg1));
            }
        } else {
            sql = readStream(std::cin);
        }
    } catch (const std::exception& ex) {
        fmt::print(stderr, "sqlite2orm: {}\n", ex.what());
        return 2;
    }

    if (sql.empty()) {
        fmt::print(stderr, "sqlite2orm: empty SQL input\n");
        return 2;
    }

    const auto results = processMultiSql(sql, policyPointer);
    int exitCode = EXIT_SUCCESS;
    for (const ProcessSqlResult& result: results) {
        for (const auto& warning: result.codegen.warnings) {
            fmt::print(stderr, "warning: {}\n", warning.message);
        }
        if (!result.parseResult.errors.empty()) {
            for (const auto& err: result.parseResult.errors) {
                fmt::print(stderr, "parse error: {} at {}:{}\n", err.message, err.location.line, err.location.column);
            }
            exitCode = 1;
        }
        if (!result.validationErrors.empty()) {
            for (const auto& err: result.validationErrors) {
                fmt::print(stderr, "validation: {} ({})\n", err.message, err.nodeType);
            }
            exitCode = 1;
        }
        if (!result.codegen.errors.empty()) {
            for (const auto& err: result.codegen.errors) {
                fmt::print(stderr, "codegen error: {}\n", err);
            }
            exitCode = 1;
        }
    }
    const auto code = joinGeneratedCode(results);
    if (!code.empty()) {
        fmt::print("{}", code);
    }
    return exitCode;
}
