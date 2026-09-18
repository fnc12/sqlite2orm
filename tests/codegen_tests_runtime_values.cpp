#include "codegen_tests_common.hpp"
#include "temp_build_dir.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
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

    /**
     *  Builds a program around the generated INSERT statements for a one-column `t` table whose
     *  field is `std::optional<fieldType>`, compiles and links it against sqlite_orm, runs it and
     *  returns `typeof(x)|x` for every row, the way SQLite reports what it stored. An insert that
     *  carries its value through the struct field converts it on the way in, so only running the
     *  code shows which value reached the database.
     */
    std::vector<std::string> insertedColumnRows(const std::vector<std::string>& insertStatements,
                                                std::string_view fieldType) {
        std::ostringstream program;
        program << "#include <sqlite_orm/sqlite_orm.h>\n"
                   "#include <cstdint>\n"
                   "#include <iostream>\n"
                   "#include <optional>\n"
                   "\n"
                   "struct T {\n"
                   "    std::optional<"
                << fieldType
                << "> x;\n"
                   "};\n"
                   "\n"
                   "int main() {\n"
                   "    using namespace sqlite_orm;\n"
                   "    auto storage = make_storage(\"\", make_table(\"t\", make_column(\"x\", &T::x)));\n"
                   "    storage.sync_schema();\n";
        for(const auto& statement: insertStatements) {
            program << "    " << statement << "\n";
        }
        program << "    for(auto& row: storage.select(columns(typeof_(&T::x), cast<std::string>(&T::x)))) {\n"
                   "        std::cout << std::get<0>(row) << '|' << std::get<1>(row) << '\\n';\n"
                   "    }\n"
                   "    return 0;\n"
                   "}\n";

        const TempBuildDir dir;
        const std::filesystem::path cpppath = dir.write("insert.cpp", program.str());
        const std::filesystem::path binpath = dir.file("insert");
        const std::filesystem::path outpath = dir.file("insert.out");

        std::ostringstream cmd;
        cmd << TempBuildDir::compilerCommand();
        cmd << ' ' << cpppath.string();
        cmd << ' ' << TempBuildDir::sqlite3LinkFlags() << " -o " << binpath.string();
        cmd << " && " << binpath.string() << " > " << outpath.string();
        cmd << " 2>&1";

        const int exitCode = TempBuildDir::run(cmd.str());
        std::vector<std::string> rows;
        {
            std::ifstream out(outpath);
            for(std::string line; std::getline(out, line);) {
                rows.push_back(line);
            }
        }
        if(exitCode != 0) {
            WARN("building the generated inserts failed (exit " << exitCode
                                                                << "); ensure c++, sqlite_orm headers and "
                                                                   "libsqlite3 are usable");
        }
        REQUIRE(exitCode == 0);
        return rows;
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

// The object form of an insert sends every value through a struct field, so a literal SQLite keeps
// a REAL reached an INTEGER column as garbage: `T{99999999999999999999.0}` stored
// `integer|-9223372036854775808` (converting an out-of-range double to an int64_t is undefined),
// and before the literal itself was widened it stored `integer|7766279631452241919`. Spelling the
// column list out binds the value and leaves the affinity to SQLite. Expected rows checked against
// sqlite3 3.51 with the same three INSERT statements.
TEST_CASE("runtime: an INSERT value out of reach of its column field keeps the value SQLite stores") {
    const std::vector<std::string> statements{
        generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (99999999999999999999);").code,
        generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (1.5);").code,
        generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (-9223372036854775808);").code,
    };
    REQUIRE(statements ==
            std::vector<std::string>{
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(99999999999999999999.0)));",
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(1.5)));",
                "storage.insert(T{-9223372036854775808.0});",
            });
    REQUIRE(insertedColumnRows(statements, "int64_t") ==
            std::vector<std::string>{"real|1.0e+20", "real|1.5", "integer|-9223372036854775808"});
}

// The `bool` field of a BOOLEAN column reaches even less far: the object form made every one of
// these values 1. sqlite3 3.51 stores `real|1.0e+20`, `real|1.5` and `integer|2` for the same
// statements over `t(x BOOLEAN)`, and the same three rows for `t(x INTEGER)` — both affinities
// leave a value they cannot hold losslessly a REAL — which is what sqlite_orm declares a `bool`
// field as.
TEST_CASE("runtime: an INSERT value out of reach of a bool field keeps the value SQLite stores") {
    const std::vector<std::string> statements{
        generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (99999999999999999999);").code,
        generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (1.5);").code,
        generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (2.0);").code,
    };
    REQUIRE(statements ==
            std::vector<std::string>{
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(99999999999999999999.0)));",
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(1.5)));",
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(2.0)));",
            });
    REQUIRE(insertedColumnRows(statements, "bool") ==
            std::vector<std::string>{"real|1.0e+20", "real|1.5", "integer|2"});
}
