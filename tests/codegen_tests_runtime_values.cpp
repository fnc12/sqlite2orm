#include "codegen_tests_common.hpp"
#include "temp_build_dir.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

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

        const TempBuildDir dir;
        const std::filesystem::path cpppath = dir.write("check.cpp", program.str());
        const std::filesystem::path binpath = dir.file("check");
        const std::filesystem::path outpath = dir.file("check.out");

        std::ostringstream cmd;
        cmd << TempBuildDir::compilerCommand();
        cmd << ' ' << cpppath.string();
        cmd << ' ' << TempBuildDir::sqlite3LinkFlags() << " -o " << binpath.string();
        cmd << " && " << binpath.string() << " > " << outpath.string();
        cmd << " 2>&1";

        const int exitCode = TempBuildDir::run(cmd.str());
        std::vector<std::string> values;
        {
            std::ifstream out(outpath);
            for(std::string line; std::getline(out, line);) {
                values.push_back(line);
            }
        }
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
        generate("SELECT - - -3;"),
        generate("SELECT -2.5;"),
        generate("SELECT a * -2;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(-2);",
                              "auto rows = storage.select(c(100) / -2);",
                              "auto rows = storage.select(c(-2) + 3);",
                              "auto rows = storage.select(-(-3));",
                              "auto rows = storage.select(-(-(-3)));",
                              "auto rows = storage.select(-2.5);",
                              "auto rows = storage.select(c(&User::a) * -2);",
                          });
    REQUIRE(selectedValues(statements) ==
            std::vector<std::string>{"-2", "-50", "1", "3", "-3", "-2.5", "-14"});
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

// `case_<int>()` truncates the branch value the way the field inferred for it would: the result
// type the CASE is generated with comes from the same inference. Expected values checked against
// sqlite3 3.51 (the single row holds a = 7).
TEST_CASE("runtime: a CASE branch beyond int32 keeps its value") {
    const std::vector<std::string> statements{
        generate("SELECT CASE WHEN a > 0 THEN 3000000000 ELSE 0 END;"),
        generate("SELECT CASE WHEN a > 0 THEN -3000000000 ELSE 0 END;"),
        generate("SELECT CASE WHEN a > 0 THEN 9223372036854775807 ELSE 0 END;"),
    };
    REQUIRE(statements ==
            std::vector<std::string>{
                "auto rows = storage.select(case_<int64_t>().when(c(&User::a) > 0, then(3000000000)).else_(0).end());",
                "auto rows = storage.select(case_<int64_t>().when(c(&User::a) > 0, then(-3000000000)).else_(0).end());",
                "auto rows = storage.select(case_<int64_t>().when(c(&User::a) > 0, "
                "then(9223372036854775807)).else_(0).end());",
            });
    REQUIRE(selectedValues(statements) ==
            std::vector<std::string>{"3000000000", "-3000000000", "9223372036854775807"});
}
