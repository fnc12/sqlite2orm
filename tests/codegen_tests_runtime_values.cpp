#include "codegen_tests_common.hpp"

#include <cstdlib>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#endif
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

    namespace fs = std::filesystem;

    /**
     *  Builds a program around the generated select statements, compiles and links it against
     *  sqlite_orm, runs it and returns one line per statement with the value of its first row.
     *  Generated code that compiles can still hand the caller a wrong value — sqlite_orm reports the
     *  result type of an expression on its own — so only running it pins down what a user sees.
     */
    std::vector<std::string> selectedValues(const std::vector<std::string>& selectStatements) {
        std::ostringstream program;
        program << "#include <sqlite_orm/sqlite_orm.h>\n"
                   "#include <iostream>\n"
                   "\n"
                   "struct User {\n"
                   "    int a = 0;\n"
                   "};\n"
                   "\n"
                   "int main() {\n"
                   "    using namespace sqlite_orm;\n"
                   "    auto storage = make_storage(\"\", make_table(\"users\", make_column(\"a\", &User::a)));\n"
                   "    storage.sync_schema();\n"
                   "    storage.replace(User{7});\n";
        for(const auto& statement: selectStatements) {
            program << "    {\n        " << statement << "\n        std::cout << rows.at(0) << '\\n';\n    }\n";
        }
        program << "    return 0;\n"
                   "}\n";

        static thread_local std::mt19937 gen{std::random_device{}()};
        std::uniform_int_distribution<std::uint64_t> dist{};
        const fs::path dir = fs::temp_directory_path() / ("sqlite2orm_values_" + std::to_string(dist(gen)));
        std::error_code ec;
        fs::create_directories(dir, ec);
        REQUIRE_FALSE(ec);

        const fs::path cpppath = dir / "check.cpp";
        const fs::path binpath = dir / "check";
        const fs::path outpath = dir / "check.out";
        {
            std::ofstream c(cpppath);
            REQUIRE(c);
            c << program.str();
        }

        std::ostringstream cmd;
        cmd << "c++ -std=c++20";
#if defined(__APPLE__)
        cmd << " -stdlib=libc++";
#endif
        cmd << " -I" << SQLITE2ORM_TEST_SQLITE_ORM_INCLUDE;
        cmd << ' ' << cpppath.string();
        cmd << " -lsqlite3 -o " << binpath.string();
        cmd << " && " << binpath.string() << " > " << outpath.string();
        cmd << " 2>&1";

        const int rawStatus = std::system(cmd.str().c_str());
        int exitCode = rawStatus;
#if defined(__unix__) || defined(__APPLE__)
        if(rawStatus != -1) {
            exitCode = WEXITSTATUS(rawStatus);
        }
#endif
        std::vector<std::string> values;
        {
            std::ifstream out(outpath);
            for(std::string line; std::getline(out, line);) {
                values.push_back(line);
            }
        }
        fs::remove_all(dir, ec);
        if(exitCode != 0) {
            WARN("building the generated selects failed (exit " << exitCode
                                                                << "); ensure c++, sqlite_orm headers and "
                                                                   "libsqlite3 are usable");
        }
        REQUIRE(exitCode == 0);
        return values;
    }

}  // namespace

// The generated code used to read every negative numeric literal back as 0: the SQL was right, but
// sqlite_orm's unary_minus_t reported a wrong result type for it. Expected values checked against
// sqlite3 3.51 (the single row holds a = 7).
TEST_CASE("runtime: generated negative literals keep their value") {
    const std::vector<std::string> statements{
        generate("SELECT -2;"),
        generate("SELECT 100 / -2;"),
        generate("SELECT -2 + 3;"),
        generate("SELECT - -3;"),
        generate("SELECT -2.5;"),
        generate("SELECT a * -2;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(-2);",
                              "auto rows = storage.select(c(100) / -2);",
                              "auto rows = storage.select(c(-2) + 3);",
                              "auto rows = storage.select(-(-3));",
                              "auto rows = storage.select(-2.5);",
                              "auto rows = storage.select(c(&User::a) * -2);",
                          });
    REQUIRE(selectedValues(statements) ==
            std::vector<std::string>{"-2", "-50", "1", "3", "-2.5", "-14"});
}

// A negation over anything but a numeric constant is generated as a subtraction from zero, which
// SQLite computes exactly like the negation. Expected values checked against sqlite3 3.51 (the
// single row holds a = 7), where the unary form these replace reads every one of them back as 0
// and throws outright over a column.
TEST_CASE("runtime: a negation over a general operand keeps its value") {
    const std::vector<std::string> statements{
        generate("SELECT -(2+3);"),
        generate("SELECT - ~2;"),
        generate("SELECT -length('abc');"),
        generate("SELECT -CAST(1 AS INTEGER);"),
        generate("SELECT 1 - -(2+3);"),
        generate("SELECT -a;"),
        generate("SELECT -(a+1);"),
        generate("SELECT - -a;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select((c(0) - (c(2) + 3)));",
                              "auto rows = storage.select((c(0) - (~c(2))));",
                              "auto rows = storage.select((c(0) - (length(\"abc\"))));",
                              "auto rows = storage.select((c(0) - (cast<int64_t>(1))));",
                              "auto rows = storage.select(c(1) - (c(0) - (c(2) + 3)));",
                              "auto rows = storage.select((c(0) - c(&User::a)));",
                              "auto rows = storage.select((c(0) - (c(&User::a) + 1)));",
                              "auto rows = storage.select((c(0) - (c(0) - c(&User::a))));",
                          });
    REQUIRE(selectedValues(statements) ==
            std::vector<std::string>{"-5", "3", "-3", "-1", "6", "-7", "-8", "7"});
}
