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
     *  sqlite_orm, runs it and returns one line per statement with the value of its first row —
     *  `NULL` for a row the result type can hold a NULL in and does. The single row of `users`
     *  holds `a = rowValue`, and `a` is declared `fieldType`.
     *  Generated code that compiles can still hand the caller a wrong value — sqlite_orm reports the
     *  result type of an expression on its own — so only running it pins down what a user sees.
     */
    std::vector<std::string> selectedValues(const std::vector<std::string>& selectStatements,
                                            std::string_view fieldType = "int",
                                            std::string_view rowValue = "7") {
        std::ostringstream program;
        program << "#include <sqlite_orm/sqlite_orm.h>\n"
                   "#include <iostream>\n"
                   "#include <memory>\n"
                   "#include <optional>\n"
                   "\n"
                   "struct User {\n"
                   "    "
                << fieldType
                << " a{};\n"
                   "};\n"
                   "\n"
                   "void printValue(std::nullptr_t) {\n"
                   "    std::cout << \"NULL\" << '\\n';\n"
                   "}\n"
                   "\n"
                   "template<class T>\n"
                   "void printValue(const T& value) {\n"
                   "    std::cout << value << '\\n';\n"
                   "}\n"
                   "\n"
                   "template<class T>\n"
                   "void printValue(const std::optional<T>& value) {\n"
                   "    value ? printValue(*value) : printValue(nullptr);\n"
                   "}\n"
                   "\n"
                   "template<class T>\n"
                   "void printValue(const std::unique_ptr<T>& value) {\n"
                   "    value ? printValue(*value) : printValue(nullptr);\n"
                   "}\n"
                   "\n"
                   "int main() {\n"
                   "    using namespace sqlite_orm;\n"
                   "    auto storage = make_storage(\"\", make_table(\"users\", make_column(\"a\", &User::a)));\n"
                   "    storage.sync_schema();\n"
                   "    storage.replace(User{"
                << rowValue
                << "});\n";
        for(const auto& statement: selectStatements) {
            program << "    {\n        " << statement << "\n        printValue(rows.at(0));\n    }\n";
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
                              "auto rows = storage.select(as_optional(c(100) / -2));",
                              "auto rows = storage.select(c(-2) + 3);",
                              "auto rows = storage.select(-(-3));",
                              "auto rows = storage.select(-(-(-3)));",
                              "auto rows = storage.select(-2.5);",
                              "auto rows = storage.select(as_optional(c(&User::a) * -2));",
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
                              "auto rows = storage.select(as_optional((c(0) - (length(\"abc\")))));",
                              "auto rows = storage.select(as_optional((c(0) - (cast<int64_t>(1)))));",
                              "auto rows = storage.select(c(1) - (c(0) - (c(2) + 3)));",
                              "auto rows = storage.select(as_optional((c(0) - c(&User::a))));",
                              "auto rows = storage.select(as_optional((c(0) - (c(&User::a) + 1))));",
                              "auto rows = storage.select(as_optional((c(0) - (c(0) - c(&User::a)))));",
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

// C++ groups the emitted operators by its own precedence, so a nested operand that would regroup
// there carries parentheses. Without them `1 - (2 - 3)` came out as `c(1) - c(2) - 3` and the
// program printed -4 where SQLite computes 2. Expected values checked against sqlite3 3.51 (the
// single row holds a = 7).
TEST_CASE("runtime: a nested operand keeps the value its SQL grouping has") {
    const std::vector<std::string> statements{
        generate("SELECT 1 - (2 - 3);"),
        generate("SELECT 20 / (4 / 2);"),
        generate("SELECT 10 % (7 % 4);"),
        generate("SELECT (1 + 2) * 3;"),
        generate("SELECT 4 & 2 < 3;"),
        generate("SELECT 6 | 3 & 5;"),
        generate("SELECT a - (a - 1);"),
        generate("SELECT 1 - 2 - 3;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(c(1) - (c(2) - 3));",
                              "auto rows = storage.select(as_optional(c(20) / (c(4) / 2)));",
                              "auto rows = storage.select(as_optional(c(10) % (c(7) % 4)));",
                              "auto rows = storage.select((c(1) + 2) * 3);",
                              "auto rows = storage.select((c(4) & 2) < 3);",
                              "auto rows = storage.select((c(6) | 3) & 5);",
                              "auto rows = storage.select(as_optional(c(&User::a) - (c(&User::a) - 1)));",
                              "auto rows = storage.select(c(1) - 2 - 3);",
                          });
    REQUIRE(selectedValues(statements) == std::vector<std::string>{"2", "10", "1", "9", "1", "5", "1", "-4"});
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

// sqlite_orm reports the result type of a binary operator from the operator alone — `double` for
// the arithmetic ones, `bool` for a comparison, `std::string` for `||` — so a NULL row reached the
// caller as 0 / false / "" whatever the SQL said. `as_optional` keeps the SQL and widens the type.
// Both expected rows checked against sqlite3 3.51 over `users(a INTEGER)` holding one row, NULL
// first and 7 second; on master every value of the NULL row reads back as 0.
TEST_CASE("runtime: a result column that can be NULL reads the NULL back") {
    const std::vector<std::string> statements{
        generate("SELECT a + 1;"),
        generate("SELECT a * 2;"),
        generate("SELECT a % 2;"),
        generate("SELECT 0 - a;"),
        generate("SELECT -a;"),
        generate("SELECT a > 0;"),
        generate("SELECT NULL + 1;"),
        generate("SELECT 1 / 0;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(as_optional(c(&User::a) + 1));",
                              "auto rows = storage.select(as_optional(c(&User::a) * 2));",
                              "auto rows = storage.select(as_optional(c(&User::a) % 2));",
                              "auto rows = storage.select(as_optional(c(0) - &User::a));",
                              "auto rows = storage.select(as_optional((c(0) - c(&User::a))));",
                              "auto rows = storage.select(as_optional(c(&User::a) > 0));",
                              "auto rows = storage.select(as_optional(c(nullptr) + 1));",
                              "auto rows = storage.select(as_optional(c(1) / 0));",
                          });
    REQUIRE(selectedValues(statements, "std::optional<int>", "std::nullopt") ==
            std::vector<std::string>{"NULL", "NULL", "NULL", "NULL", "NULL", "NULL", "NULL", "NULL"});
    REQUIRE(selectedValues(statements, "std::optional<int>", "7") ==
            std::vector<std::string>{"8", "14", "1", "-7", "-7", "1", "NULL", "NULL"});
}

// The widening rule leaves an operator over a NULL test alone, and it is right to: sqlite3 3.45.1
// answers `NOT (a IS NULL)` with 0 over a NULL row and 1 over `a = 7`, never with a NULL. (The
// arithmetic forms of the same rule — `(a IS NULL) + 1` — have no sqlite_orm overload to run.)
TEST_CASE("runtime: an operator over a NULL test needs no widening to keep its value") {
    const std::vector<std::string> statements{
        generate("SELECT NOT (a IS NULL);"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(not (is_null(&User::a)));",
                          });
    REQUIRE(selectedValues(statements, "std::optional<int>", "std::nullopt") ==
            std::vector<std::string>{"0"});
    REQUIRE(selectedValues(statements, "std::optional<int>", "7") == std::vector<std::string>{"1"});
}
