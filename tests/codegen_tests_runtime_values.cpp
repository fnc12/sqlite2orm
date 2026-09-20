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
                << rowValue << "});\n";
        for (const auto& statement: selectStatements) {
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
            for (std::string line; std::getline(out, line);) {
                values.push_back(line);
            }
        }
        if (exitCode != 0) {
            WARN("building the generated selects failed (exit " << exitCode
                                                                << "); ensure c++, sqlite_orm headers and "
                                                                   "libsqlite3 are usable");
        }
        REQUIRE(exitCode == 0);
        return values;
    }

    /**
     *  Builds a program around the generated INSERT statements for a one-column `t` table whose
     *  field is `std::optional<fieldType>`, or a bare `fieldType` for the NOT NULL column
     *  `nullable` stands for, compiles and links it against sqlite_orm, runs it and returns
     *  `typeof(x)|x` for every row, the way SQLite reports what it stored. An insert that carries
     *  its value through the struct field converts it on the way in, so only running the code
     *  shows which value reached the database.
     */
    std::vector<std::string> insertedColumnRows(const std::vector<std::string>& insertStatements,
                                                std::string_view fieldType,
                                                bool nullable = true) {
        const std::string fieldDeclaration =
            nullable ? "std::optional<" + std::string(fieldType) + "> x;" : std::string(fieldType) + " x{};";
        std::ostringstream program;
        program << "#include <sqlite_orm/sqlite_orm.h>\n"
                   "#include <cstdint>\n"
                   "#include <iostream>\n"
                   "#include <optional>\n"
                   "\n"
                   "struct T {\n"
                   "    "
                << fieldDeclaration
                << "\n"
                   "};\n"
                   "\n"
                   "int main() {\n"
                   "    using namespace sqlite_orm;\n"
                   "    auto storage = make_storage(\"\", make_table(\"t\", make_column(\"x\", &T::x)));\n"
                   "    storage.sync_schema();\n";
        for (const auto& statement: insertStatements) {
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
            for (std::string line; std::getline(out, line);) {
                rows.push_back(line);
            }
        }
        if (exitCode != 0) {
            WARN("building the generated inserts failed (exit " << exitCode
                                                                << "); ensure c++, sqlite_orm headers and "
                                                                   "libsqlite3 are usable");
        }
        REQUIRE(exitCode == 0);
        return rows;
    }

    /**
     *  Builds a program around the generated select statements over a three-row `users` table,
     *  compiles and links it against sqlite_orm, runs it and returns the rows each statement came
     *  back with, comma separated. The row count alone does not pin an OFFSET down — a dropped
     *  `offset(...)` leaves `LIMIT 1, 2` with the two rows it should have, just the wrong two — so
     *  the values are what is compared.
     */
    std::vector<std::string> selectedRowValues(const std::vector<std::string>& selectStatements) {
        std::ostringstream program;
        program << "#include <sqlite_orm/sqlite_orm.h>\n"
                   "#include <iostream>\n"
                   "\n"
                   "struct Users {\n"
                   "    int a = 0;\n"
                   "};\n"
                   "\n"
                   "int main() {\n"
                   "    using namespace sqlite_orm;\n"
                   "    auto storage = make_storage(\"\", make_table(\"users\", make_column(\"a\", &Users::a)));\n"
                   "    storage.sync_schema();\n"
                   "    storage.replace(Users{1});\n"
                   "    storage.replace(Users{2});\n"
                   "    storage.replace(Users{3});\n";
        for (const auto& statement: selectStatements) {
            program << "    {\n        " << statement
                    << "\n        const char* separator = \"\";\n"
                       "        for(const auto& row: rows) {\n"
                       "            std::cout << separator << row;\n"
                       "            separator = \",\";\n"
                       "        }\n"
                       "        std::cout << '\\n';\n    }\n";
        }
        program << "    return 0;\n"
                   "}\n";

        const TempBuildDir dir;
        const std::filesystem::path cpppath = dir.write("limits.cpp", program.str());
        const std::filesystem::path binpath = dir.file("limits");
        const std::filesystem::path outpath = dir.file("limits.out");

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
            for (std::string line; std::getline(out, line);) {
                rows.push_back(line);
            }
        }
        if (exitCode != 0) {
            WARN("building the generated selects failed (exit " << exitCode
                                                                << "); ensure c++, sqlite_orm headers and "
                                                                   "libsqlite3 are usable");
        }
        REQUIRE(exitCode == 0);
        return rows;
    }

    /**
     *  Builds the generated `make_storage` of a single CREATE TABLE, compiles and links it against
     *  sqlite_orm, runs `sync_schema()` and returns one line per probe value saying whether an
     *  insert of it was accepted, followed by the CHECK clause SQLite stored. The generated storage
     *  is given the file database the schema is read back out of. A CHECK constraint is the slot
     *  where the serialized SQL is the whole product — it goes into the schema and SQLite enforces
     *  it from then on, with no C++ term left to hold the grouping — so only what `sqlite_master`
     *  comes back with pins that grouping down.
     */
    std::vector<std::string> checkConstraintBehaviour(const std::string& generatedCode,
                                                      const std::vector<std::string>& probeValues) {
        const TempBuildDir dir;
        const std::filesystem::path databasePath = dir.file("check.sqlite3");

        const std::string inMemory = "make_storage(\"\"";
        const std::size_t storageAt = generatedCode.find(inMemory);
        REQUIRE(storageAt != std::string::npos);
        std::string storageDefinition = generatedCode;
        storageDefinition.replace(storageAt, inMemory.size(), "make_storage(\"" + databasePath.generic_string() + "\"");

        std::ostringstream program;
        program << "#include <sqlite_orm/sqlite_orm.h>\n"
                   "#include <sqlite3.h>\n"
                   "#include <cstdint>\n"
                   "#include <iostream>\n"
                   "#include <optional>\n"
                   "\n"
                   "using namespace sqlite_orm;\n"
                   "\n"
                << storageDefinition
                << "\n"
                   "\n"
                   "int main() {\n"
                   "    storage.sync_schema();\n";
        for (const auto& probeValue: probeValues) {
            program << "    try {\n"
                       "        storage.insert(T{"
                    << probeValue
                    << "});\n"
                       "        std::cout << \""
                    << probeValue
                    << " accepted\" << '\\n';\n"
                       "    } catch(const std::system_error&) {\n"
                       "        std::cout << \""
                    << probeValue
                    << " rejected\" << '\\n';\n"
                       "    }\n";
        }
        // The schema text is read through the C API, so that what is compared is what SQLite stored
        // and not what sqlite_orm would print for it.
        program << "    sqlite3* db = nullptr;\n"
                   "    sqlite3_open(\""
                << databasePath.generic_string()
                << "\", &db);\n"
                   "    sqlite3_stmt* statement = nullptr;\n"
                   "    sqlite3_prepare_v2(db, \"SELECT sql FROM sqlite_master WHERE name = 't'\", -1, &statement, "
                   "nullptr);\n"
                   "    while(sqlite3_step(statement) == SQLITE_ROW) {\n"
                   "        std::cout << reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)) << '\\n';\n"
                   "    }\n"
                   "    sqlite3_finalize(statement);\n"
                   "    sqlite3_close(db);\n"
                   "    return 0;\n"
                   "}\n";

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
        std::vector<std::string> lines;
        {
            std::ifstream out(outpath);
            for (std::string line; std::getline(out, line);) {
                lines.push_back(line);
            }
        }
        if (exitCode != 0) {
            WARN("building the generated CHECK constraint failed (exit "
                 << exitCode << "); ensure c++, sqlite_orm headers and libsqlite3 are usable");
        }
        REQUIRE(exitCode == 0);
        return lines;
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
    REQUIRE(selectedValues(statements) == std::vector<std::string>{"-2", "-50", "1", "3", "-3", "-2.5", "-14"});
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
                              "auto rows = storage.select(as_optional((c(0) - c(&User::a))));",
                              "auto rows = storage.select(as_optional((c(0) - (c(&User::a) + 1))));",
                              "auto rows = storage.select(as_optional((c(0) - (c(0) - c(&User::a)))));",
                          });
    REQUIRE(selectedValues(statements) == std::vector<std::string>{"-5", "3", "-3", "-1", "6", "-7", "-8", "7"});
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
    REQUIRE(selectedValues(statements) == std::vector<std::string>{"3000000000", "-3000000000", "9223372036854775807"});
}

// C++ groups the emitted operators by its own precedence, so a nested operand that would regroup
// there carries parentheses. Without them `1 - (2 - 3)` came out as `c(1) - c(2) - 3` and the
// program printed -4 where SQLite computes 2. Expected values checked against sqlite3 3.51 (the
// single row holds a = 7). The CAST around the `6 | 3 & 5` statement is the int64 widening of
// "runtime: a bitwise result column reads the whole int64 back", which leaves the value alone.
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
                              "auto rows = storage.select(cast<int64_t>((c(6) | 3) & 5));",
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

// A whole number is out of reach of a `bool` field as soon as it is neither 0 nor 1, and the
// NUMERIC affinity of a BOOLEAN column keeps every one of these as SQLite typed it. Expected rows
// checked against sqlite3 3.51 with the same five INSERT statements over `t(x BOOLEAN)`.
TEST_CASE("runtime: an INSERT of a whole number no bool holds keeps the value SQLite stores") {
    const std::vector<std::string> statements{
        generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (5);").code,
        generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (9223372036854775807);").code,
        generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (1_0);").code,
        generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (0xFFFFFFFFFFFFFFFF);").code,
        generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (1);").code,
    };
    REQUIRE(statements ==
            std::vector<std::string>{
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(5)));",
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(9223372036854775807)));",
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(1'0)));",
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(static_cast<int64_t>("
                "0xFFFFFFFFFFFFFFFF))));",
                "storage.insert(T{1});",
            });
    REQUIRE(
        insertedColumnRows(statements, "bool") ==
        std::vector<std::string>{"integer|5", "integer|9223372036854775807", "integer|10", "integer|-1", "integer|1"});
}

// The bare `bool` field of a NOT NULL column refuses the value at compile time rather than
// converting it — "narrowing conversion of '2' from 'int' to 'bool'" — so this one only builds
// through the column list. sqlite3 3.51 stores `integer|2`.
TEST_CASE("runtime: an INSERT of a whole number no bool holds into a NOT NULL BOOLEAN column") {
    const std::vector<std::string> statements{
        generateLastOfBatch("CREATE TABLE t(x BOOLEAN NOT NULL); INSERT INTO t VALUES (2);").code,
    };
    REQUIRE(statements == std::vector<std::string>{
                              "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(2)));",
                          });
    REQUIRE(insertedColumnRows(statements, "bool", false) == std::vector<std::string>{"integer|2"});
}

// A column with no type maps to a `std::vector<char>` field, which only a blob literal initializes:
// `CREATE TABLE ch(x); INSERT INTO ch VALUES (1);` generated `Ch{1}`, and a compiler said
// "could not convert '1' from 'int' to 'std::optional<std::vector<char> >'". SQLite stores a value
// of any storage class in such a column, so the column list is spelled out and the value bound as
// itself. Expected rows checked against sqlite3 3.51 with the same five INSERT statements.
TEST_CASE("runtime: an INSERT into a column with no type stores what SQLite stores") {
    const std::vector<std::string> statements{
        generateLastOfBatch("CREATE TABLE t(x); INSERT INTO t VALUES (1);").code,
        generateLastOfBatch("CREATE TABLE t(x); INSERT INTO t VALUES (1.5);").code,
        generateLastOfBatch("CREATE TABLE t(x); INSERT INTO t VALUES ('a');").code,
        generateLastOfBatch("CREATE TABLE t(x); INSERT INTO t VALUES (X'41');").code,
        generateLastOfBatch("CREATE TABLE t(x); INSERT INTO t VALUES (1+1);").code,
    };
    REQUIRE(statements == std::vector<std::string>{
                              "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(1)));",
                              "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(1.5)));",
                              "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(\"a\")));",
                              "storage.insert(T{std::vector<char>{'\\x41'}});",
                              "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(c(1) + 1)));",
                          });
    REQUIRE(insertedColumnRows(statements, "std::vector<char>") ==
            std::vector<std::string>{"integer|1", "real|1.5", "text|a", "blob|A", "integer|2"});
}

// The same mismatch the other way round: a number and a blob literal reach a `std::string` field
// only through the column list, where SQLite applies the TEXT affinity to the value it typed.
// Expected rows checked against sqlite3 3.51 with the same three INSERT statements.
TEST_CASE("runtime: an INSERT of a number into a TEXT column stores what SQLite stores") {
    const std::vector<std::string> statements{
        generateLastOfBatch("CREATE TABLE t(x TEXT); INSERT INTO t VALUES (1);").code,
        generateLastOfBatch("CREATE TABLE t(x TEXT); INSERT INTO t VALUES (1.5);").code,
        generateLastOfBatch("CREATE TABLE t(x TEXT); INSERT INTO t VALUES (X'41');").code,
    };
    REQUIRE(statements ==
            std::vector<std::string>{
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(1)));",
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(1.5)));",
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(std::vector<char>{'\\x41'})));",
            });
    REQUIRE(insertedColumnRows(statements, "std::string") == std::vector<std::string>{"text|1", "text|1.5", "blob|A"});
}

// A NOT NULL REAL column has a bare `double` field, which the object form brace-initializes, and a
// braced initializer refuses an integer constant the `double` would round: `storage.insert(T{
// 9223372036854775807})` answered "narrowing conversion of '9223372036854775807' from 'long long
// int' to 'double'". Such a value goes through the column list, where SQLite applies the REAL
// affinity to the integer it typed. Expected rows checked against sqlite3 3.51 with the same four
// INSERT statements.
TEST_CASE("runtime: an INSERT of a whole number into a NOT NULL REAL column stores what SQLite stores") {
    const std::vector<std::string> statements{
        generateLastOfBatch("CREATE TABLE t(x REAL NOT NULL); INSERT INTO t VALUES (9223372036854775807);").code,
        generateLastOfBatch("CREATE TABLE t(x REAL NOT NULL); INSERT INTO t VALUES (9007199254740993);").code,
        generateLastOfBatch("CREATE TABLE t(x REAL NOT NULL); INSERT INTO t VALUES (1);").code,
        generateLastOfBatch("CREATE TABLE t(x REAL NOT NULL); INSERT INTO t VALUES (1.5);").code,
    };
    REQUIRE(statements ==
            std::vector<std::string>{
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(9223372036854775807)));",
                "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(9007199254740993)));",
                "storage.insert(T{1});",
                "storage.insert(T{1.5});",
            });
    REQUIRE(insertedColumnRows(statements, "double", false) ==
            std::vector<std::string>{"real|9.22337203685478e+18", "real|9.00719925474099e+15", "real|1.0", "real|1.5"});
}

// A LIMIT value is a whole expression in SQLite, and `LIMIT -1` is how it spells "no limit". The
// generator used to take an unsigned integer literal only, so it reported a parse error on the
// minus sign and emitted the same statement without any `limit(...)` at all. Expected counts
// checked against sqlite3 3.51 over the same three rows.
TEST_CASE("runtime: a generated LIMIT returns the rows SQLite returns") {
    const std::vector<std::string> statements{
        generate("SELECT a FROM users LIMIT -1;"),
        generate("SELECT a FROM users LIMIT 2;"),
        generate("SELECT a FROM users LIMIT 1 OFFSET -1;"),
        generate("SELECT a FROM users LIMIT -1 OFFSET 2;"),
        generate("SELECT a FROM users LIMIT 2 OFFSET 1;"),
        generate("SELECT a FROM users LIMIT 1, 2;"),
        generate("SELECT a FROM users LIMIT 2, 1;"),
        generate("SELECT a FROM users LIMIT 2 * 1;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(&Users::a, limit(-1));",
                              "auto rows = storage.select(&Users::a, limit(2));",
                              "auto rows = storage.select(&Users::a, limit(1, offset(-1)));",
                              "auto rows = storage.select(&Users::a, limit(-1, offset(2)));",
                              "auto rows = storage.select(&Users::a, limit(2, offset(1)));",
                              "auto rows = storage.select(&Users::a, limit(2, offset(1)));",
                              "auto rows = storage.select(&Users::a, limit(1, offset(2)));",
                              "auto rows = storage.select(&Users::a, limit(c(2) * 1));",
                          });
    REQUIRE(selectedRowValues(statements) ==
            std::vector<std::string>{"1,2,3", "1,2", "1", "3", "2,3", "2,3", "3", "1,2"});
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
// arithmetic forms of the same rule are run in
// "runtime: a predicate under an operator keeps the grouping it was written with".)
TEST_CASE("runtime: an operator over a NULL test needs no widening to keep its value") {
    const std::vector<std::string> statements{
        generate("SELECT NOT (a IS NULL);"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(not (is_null(&User::a)));",
                          });
    REQUIRE(selectedValues(statements, "std::optional<int>", "std::nullopt") == std::vector<std::string>{"0"});
    REQUIRE(selectedValues(statements, "std::optional<int>", "7") == std::vector<std::string>{"1"});
}

// sqlite_orm serializes IN, BETWEEN, LIKE, GLOB, MATCH, IS [NOT] NULL and NOT without parentheses,
// and SQLite binds those looser than the operator around them, so `c(1) - is_null(&User::a)` was
// read back as `(1 - a) IS NULL` — one C++ term, another SQL expression, and no complaint from
// either. The CAST the generator now spells out restores the grouping. Both expected rows checked
// against sqlite3 3.51 over `users(a INTEGER)` holding one row, NULL first and 7 second; on master
// the `a = 7` row answers 0, 1, 0, 0, and the arithmetic over a left-hand predicate does not
// compile at all.
TEST_CASE("runtime: a predicate under an operator keeps the grouping it was written with") {
    const std::vector<std::string> statements{
        generate("SELECT 1 - (a IS NULL);"),
        generate("SELECT 1 - (a NOT NULL);"),
        generate("SELECT 1 - (a IN (1,2,3));"),
        generate("SELECT 1 = (a IS NULL);"),
        generate("SELECT (a IS NULL) + 1;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(c(1) - cast<int64_t>(is_null(&User::a)));",
                              "auto rows = storage.select(c(1) - cast<int64_t>(is_not_null(&User::a)));",
                              "auto rows = storage.select(as_optional(c(1) - cast<int64_t>(in(&User::a, {1, 2, 3}))));",
                              "auto rows = storage.select(c(1) == cast<int64_t>(is_null(&User::a)));",
                              "auto rows = storage.select(cast<int64_t>(is_null(&User::a)) + 1);",
                          });
    REQUIRE(selectedValues(statements, "std::optional<int>", "std::nullopt") ==
            std::vector<std::string>{"0", "1", "NULL", "1", "2"});
    REQUIRE(selectedValues(statements, "std::optional<int>", "7") == std::vector<std::string>{"1", "0", "1", "0", "1"});
}

// A CHECK constraint is the slot where the serialized SQL is the whole product: it is stored in the
// schema and SQLite enforces it from then on, with no C++ term left to hold the grouping. The bare
// predicate made the generated table enforce the opposite of what the source said — the stored
// clause read `CHECK (1 - "a" IS NULL)`, that is `(1 - "a") IS NULL`, so it rejected the `a = 7` row
// it should accept and accepted the NULL row it should reject. Checked against sqlite3 3.45.1 and
// 3.51: `CREATE TABLE t(a INTEGER CHECK(1 - (a IS NULL)))` accepts 7 and rejects NULL.
TEST_CASE("runtime: a predicate in a CHECK constraint is enforced the way the source reads") {
    const std::string generatedCode = generate("CREATE TABLE t(a INTEGER CHECK(1 - (a IS NULL)));");
    REQUIRE(generatedCode == "struct T {\n"
                             "    std::optional<int64_t> a;\n"
                             "};\n"
                             "\n"
                             "auto storage = make_storage(\"\",\n"
                             "    make_table(\"t\",\n"
                             "        make_column(\"a\", &T::a, check(c(1) - cast<int64_t>(is_null(&T::a))))));");
    REQUIRE(checkConstraintBehaviour(generatedCode, {"7", "std::nullopt"}) ==
            std::vector<std::string>{
                "7 accepted",
                "std::nullopt rejected",
                "CREATE TABLE \"t\" (\"a\" INTEGER CHECK (1 - CAST (\"a\" IS NULL AS INTEGER)) NULL)",
            });
}

// A dropped COLLATE used to take the `c(…)` wrap of the operand under it with it, and the operand
// then landed in a plain C++ expression: `('a' COLLATE NOCASE) || 'b'` was two `const char*` under
// C++'s own `||`, so `storage.dump` printed `SELECT 1` and the row came back as 1. Values checked
// against sqlite3 3.51, which answers `ab`, 8, 16 and -5 for these four.
TEST_CASE("runtime: an operand under a dropped COLLATE keeps its value") {
    const std::vector<std::string> statements{
        generate("SELECT ('a' COLLATE NOCASE) || 'b';"),
        generate("SELECT (a COLLATE BINARY) + 1;"),
        generate("SELECT 2 * ((a + 1) COLLATE BINARY);"),
        generate("SELECT -(5 COLLATE BINARY);"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(c(\"a\") || \"b\");",
                              "auto rows = storage.select(as_optional(c(&User::a) + 1));",
                              "auto rows = storage.select(as_optional(c(2) * (c(&User::a) + 1)));",
                              "auto rows = storage.select((c(0) - c(5)));",
                          });
    REQUIRE(selectedValues(statements) == std::vector<std::string>{"ab", "8", "16", "-5"});
}

// The sqlite_orm node a result column comes out as is the node under a dropped COLLATE, and so is
// the type the row is read back into: without the widening the NULL row came back as 0 and as "",
// where sqlite3 3.51 answers NULL for all three of these. Checked against it over `users(a INTEGER)`
// holding one row, NULL first and 7 second.
TEST_CASE("runtime: a result column under a dropped COLLATE reads the NULL back") {
    const std::vector<std::string> statements{
        generate("SELECT (a + 1) COLLATE BINARY;"),
        generate("SELECT (a || 'x') COLLATE NOCASE;"),
        generate("SELECT (-a) COLLATE BINARY;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(as_optional(c(&User::a) + 1));",
                              "auto rows = storage.select(as_optional(c(&User::a) || \"x\"));",
                              "auto rows = storage.select(as_optional((c(0) - c(&User::a))));",
                          });
    REQUIRE(selectedValues(statements, "std::optional<int>", "std::nullopt") ==
            std::vector<std::string>{"NULL", "NULL", "NULL"});
    REQUIRE(selectedValues(statements, "std::optional<int>", "7") == std::vector<std::string>{"8", "7x", "-7"});
}

// `operator!` is the one sqlite_orm operator that keeps the `c(...)` its operand carries instead of
// unwrapping it, and the walker that collects the tables a statement reads stops at such a wrapper.
// A column a NOT is the only mention of was invisible to it: `select(not c(&User::a))` serialized to
// `SELECT NOT "users"."a"` with no FROM clause at all and threw `SQL logic error` before any value
// reached the caller. The column-pointer form names the same column and the walker reads it. A
// second NOT did not even compile — `negated_condition_t` is neither negatable nor an operator
// argument — which the CAST that delimits it fixes. The same wrapper hides a value from the walk
// that binds one, so `select(not c(0))` ran with an empty parameter and answered NULL where SQLite
// answers 1; `0 + x` is the numeric coercion SQLite applies in a boolean context anyway. Values
// checked against sqlite3 3.51 over `users(a INTEGER)` holding one row, NULL first and 7 second.
TEST_CASE("runtime: a NOT returns the value SQLite computes") {
    const std::vector<std::string> statements{
        generate("SELECT NOT a;"),
        generate("SELECT NOT NOT a;"),
        generate("SELECT NOT (a NOT BETWEEN 1 AND 9);"),
        generate("SELECT NOT 0;"),
        generate("SELECT NOT 0.5;"),
        generate("SELECT NOT 'abc';"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(as_optional(not column<User>(&User::a)));",
                              "auto rows = storage.select(as_optional(not cast<int64_t>(not column<User>(&User::a))));",
                              "auto rows = storage.select(as_optional(not cast<int64_t>(!between(&User::a, 1, 9))));",
                              "auto rows = storage.select(not (c(0) + 0));",
                              "auto rows = storage.select(not (c(0) + 0.5));",
                              "auto rows = storage.select(not (c(0) + \"abc\"));",
                          });
    REQUIRE(selectedValues(statements, "std::optional<int>", "std::nullopt") ==
            std::vector<std::string>{"NULL", "NULL", "NULL", "1", "0", "1"});
    REQUIRE(selectedValues(statements, "std::optional<int>", "7") ==
            std::vector<std::string>{"0", "1", "1", "1", "0", "1"});
}

// The same column-pointer form has to reach a NOT wherever it stands, and a CHECK constraint is
// where the serialized SQL is the whole product: it goes into the schema and SQLite enforces it
// from then on. What the clause has to read is `CHECK (NOT "a")` — checked against sqlite3 3.51,
// which accepts 0 and NULL for it and rejects 7.
TEST_CASE("runtime: a NOT over a column in a CHECK constraint is enforced the way the source reads") {
    const std::string generatedCode = generate("CREATE TABLE t(a INTEGER CHECK(NOT a));");
    REQUIRE(generatedCode == "struct T {\n"
                             "    std::optional<int64_t> a;\n"
                             "};\n"
                             "\n"
                             "auto storage = make_storage(\"\",\n"
                             "    make_table(\"t\",\n"
                             "        make_column(\"a\", &T::a, check(not column<T>(&T::a)))));");
    REQUIRE(checkConstraintBehaviour(generatedCode, {"0", "7", "std::nullopt"}) ==
            std::vector<std::string>{
                "0 accepted",
                "7 rejected",
                "std::nullopt accepted",
                "CREATE TABLE \"t\" (\"a\" INTEGER CHECK (NOT \"a\") NULL)",
            });
}

// A column that names a SELECT alias is generated as `get<Alias>()`, not as a column pointer, and
// the statement it stands in already names the table through the aliased result column — so it
// keeps the `c(...)` wrapper it always had and still runs. Checked against sqlite3 3.51: the row
// holding a = 0 is the one `WHERE NOT al` returns.
TEST_CASE("runtime: a SELECT alias under a NOT returns the row SQLite returns") {
    const std::vector<std::string> statements{
        generate("SELECT a AS al FROM user WHERE NOT al;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "struct AlAlias : sqlite_orm::alias_tag {\n"
                              "    static const std::string& get() {\n"
                              "        static const std::string res = \"al\";\n"
                              "        return res;\n"
                              "    }\n"
                              "};\n"
                              "auto rows = storage.select(as<AlAlias>(&User::a), where(not c(get<AlAlias>())));",
                          });
    REQUIRE(selectedValues(statements, "std::optional<int>", "0") == std::vector<std::string>{"0"});
}

// sqlite_orm types `&`, `|`, `<<`, `>>` and `~` as `int`, so the int64 SQLite computes reached the
// caller through a 32-bit truncation: `9223372036854775807 & -1` printed -1. The CAST widens the
// C++ type without moving the value — a bitwise result is an INTEGER or a NULL, and a CAST to
// INTEGER keeps both. Expected values checked against sqlite3 3.51 over `users(a INTEGER)` holding
// one row with a = 9223372036854775807: 9223372036854775807 for the four binary operators and
// -9223372036854775808 for `~a`.
TEST_CASE("runtime: a bitwise result column reads the whole int64 back") {
    const std::vector<std::string> statements{
        generate("SELECT a & -1;"),
        generate("SELECT a | 0;"),
        generate("SELECT a << 0;"),
        generate("SELECT a >> 0;"),
        generate("SELECT ~a;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(as_optional(cast<int64_t>(c(&User::a) & -1)));",
                              "auto rows = storage.select(as_optional(cast<int64_t>(c(&User::a) | 0)));",
                              "auto rows = storage.select(as_optional(cast<int64_t>(c(&User::a) << 0)));",
                              "auto rows = storage.select(as_optional(cast<int64_t>(c(&User::a) >> 0)));",
                              "auto rows = storage.select(as_optional(cast<int64_t>(~c(&User::a))));",
                          });
    REQUIRE(selectedValues(statements, "int64_t", "9223372036854775807") ==
            std::vector<std::string>{"9223372036854775807",
                                     "9223372036854775807",
                                     "9223372036854775807",
                                     "9223372036854775807",
                                     "-9223372036854775808"});
}

// What the arithmetic operators still do, and why the generated column carries a warning instead of
// a CAST of its own: sqlite_orm types `+`, `-`, `*`, `/` and `%` as `double`, which rounds the
// integers past 2^53, and a CAST to INTEGER would truncate the REAL the very same operators answer
// with as soon as an operand is one. sqlite3 3.51 answers the same two rows with the INTEGERs
// 9223372036854775807 and 9223372036854775806, and the third with the REAL 1.8446744073709552e+19.
TEST_CASE("runtime: an arithmetic result column rounds the int64 it reads back") {
    const std::vector<std::string> statements{
        generate("SELECT a + 0;"),
        generate("SELECT a - 1;"),
        generate("SELECT a * 2;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(as_optional(c(&User::a) + 0));",
                              "auto rows = storage.select(as_optional(c(&User::a) - 1));",
                              "auto rows = storage.select(as_optional(c(&User::a) * 2));",
                          });
    REQUIRE(selectedValues(statements, "int64_t", "9223372036854775807") ==
            std::vector<std::string>{"9.22337e+18", "9.22337e+18", "1.84467e+19"});
}

// Prefix NOT is weaker than every binary operator in SQLite, AND and OR aside, so `NOT a + 1` is
// `NOT (a + 1)` and `NOT a IN (1, 2)` is `NOT (a IN (1, 2))`. The parser used to give NOT a primary
// as its operand, which regrouped all four of these. Expected values checked against sqlite3 3.51
// over `users(a INTEGER)`: the row a = 7 answers 0, 1, 1, 0 and the row a = NULL answers
// NULL, NULL, NULL, 0.
TEST_CASE("runtime: a prefix NOT groups the way SQLite groups it") {
    const std::vector<std::string> statements{
        generate("SELECT NOT a + 1;"),
        generate("SELECT NOT a IN (1, 2);"),
        generate("SELECT NOT a BETWEEN 8 AND 9;"),
        generate("SELECT NOT (a IS NULL) + 1;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(as_optional(not (c(&User::a) + 1)));",
                              "auto rows = storage.select(as_optional(not (in(&User::a, {1, 2}))));",
                              "auto rows = storage.select(as_optional(not (between(&User::a, 8, 9))));",
                              "auto rows = storage.select(not (cast<int64_t>(is_null(&User::a)) + 1));",
                          });
    REQUIRE(selectedValues(statements) == std::vector<std::string>{"0", "1", "1", "0"});
    REQUIRE(selectedValues(statements, "std::optional<int>", "std::nullopt") ==
            std::vector<std::string>{"NULL", "NULL", "NULL", "0"});
}

// C++ spells a logical OR and a concatenation with the same token, `||`, and sqlite_orm picks
// between its `or_condition_t` and its `conc_t` by the operands: `operator||` builds the OR only
// when one of them is a condition. So `SELECT 1 OR 0` spelled `c(1) or 0` ran as `SELECT 1 || 0`
// and printed '10', and `SELECT (a = 7) || 'x'` spelled `c(&User::a) == 7 || "x"` ran as
// `SELECT (a = 7) OR 'x'` and printed 1. `or_()` and `conc()` name the node they build. Expected
// values checked against sqlite3 3.51 over `users(a INTEGER)` holding one row with a = 7.
TEST_CASE("runtime: an OR and a concatenation return the values SQLite computes") {
    const std::vector<std::string> statements{
        generate("SELECT 1 OR 0;"),
        generate("SELECT 0 OR 0;"),
        generate("SELECT a OR 0;"),
        generate("SELECT NULL OR 0;"),
        generate("SELECT NULL OR 1;"),
        generate("SELECT 'a' OR 0;"),
        generate("SELECT a OR 0 OR 0;"),
        generate("SELECT -(1 OR 0);"),
        generate("SELECT a = 7 OR 0;"),
        generate("SELECT (a = 7) || 'x';"),
        generate("SELECT (1 OR 0) || 'x';"),
        generate("SELECT (a IS NULL) || 'x';"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(or_(1, 0));",
                              "auto rows = storage.select(or_(0, 0));",
                              "auto rows = storage.select(as_optional(or_(&User::a, 0)));",
                              "auto rows = storage.select(as_optional(or_(nullptr, 0)));",
                              "auto rows = storage.select(as_optional(or_(nullptr, 1)));",
                              "auto rows = storage.select(or_(\"a\", 0));",
                              "auto rows = storage.select(as_optional(or_(or_(&User::a, 0), 0)));",
                              "auto rows = storage.select((c(0) - (or_(1, 0))));",
                              "auto rows = storage.select(as_optional(c(&User::a) == 7 or 0));",
                              "auto rows = storage.select(as_optional(conc(c(&User::a) == 7, \"x\")));",
                              "auto rows = storage.select(conc(or_(1, 0), \"x\"));",
                              "auto rows = storage.select(cast<int64_t>(is_null(&User::a)) || \"x\");",
                          });
    REQUIRE(selectedValues(statements, "int", "7") ==
            std::vector<std::string>{"1", "0", "1", "NULL", "1", "0", "1", "-1", "1", "1x", "1x", "0x"});
}

// A predicate serializer parenthesizes none of its arguments, and SQLite binds AND and OR looser
// than every predicate, so an AND or an OR standing there bare escapes into the predicate:
// `is_null(or_(1, 0))` runs as `SELECT 1 OR 0 IS NULL`, which is `1 OR (0 IS NULL)` and answers 1
// where `(1 OR 0) IS NULL` answers 0, and `between(1, or_(1, 0), 3)` runs as
// `SELECT 1 BETWEEN 1 OR 0 AND 3`, which SQLite refuses as a syntax error. The `cast<int64_t>`
// wrapper delimits the argument. A bound of a BETWEEN is not run here: `between(A, T, T)` deduces
// one type for both bounds, so an AND or an OR in one of them does not compile whatever it is
// wrapped in — it did not on master either, where the bound was a `conc_t`. Expected values
// checked against sqlite3 3.51 over `users(a INTEGER)` holding one row with a = 7.
TEST_CASE("runtime: an AND or an OR in a predicate argument returns the value SQLite computes") {
    const std::vector<std::string> statements{
        generate("SELECT (1 OR 0) IS NULL;"),
        generate("SELECT (NULL OR 0) IS NULL;"),
        generate("SELECT (a OR 0) IS NOT NULL;"),
        generate("SELECT (1 OR 0) LIKE 'x';"),
        generate("SELECT (a OR 0) NOT LIKE 'x';"),
        generate("SELECT (1 OR 0) GLOB 'x';"),
        generate("SELECT (1 OR 0) IN (0, 1);"),
        generate("SELECT (1 AND 0) IN (0, 1);"),
        generate("SELECT (a OR 0) NOT IN (0, 1);"),
        generate("SELECT (a OR 0) BETWEEN 0 AND 1;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(is_null(cast<int64_t>(or_(1, 0))));",
                              "auto rows = storage.select(is_null(cast<int64_t>(or_(nullptr, 0))));",
                              "auto rows = storage.select(is_not_null(cast<int64_t>(or_(&User::a, 0))));",
                              "auto rows = storage.select(like(cast<int64_t>(or_(1, 0)), \"x\"));",
                              "auto rows = storage.select(as_optional(!like(cast<int64_t>(or_(&User::a, 0)), \"x\")));",
                              "auto rows = storage.select(glob(cast<int64_t>(or_(1, 0)), \"x\"));",
                              "auto rows = storage.select(in(cast<int64_t>(or_(1, 0)), {0, 1}));",
                              "auto rows = storage.select(in(cast<int64_t>(c(1) and 0), {0, 1}));",
                              "auto rows = storage.select(as_optional(not_in(cast<int64_t>(or_(&User::a, 0)), "
                              "{0, 1})));",
                              "auto rows = storage.select(as_optional(between(cast<int64_t>(or_(&User::a, 0)), "
                              "0, 1)));",
                          });
    REQUIRE(selectedValues(statements) == std::vector<std::string>{"0", "1", "1", "0", "1", "0", "1", "1", "0", "1"});
}

// sqlite_orm spells `or` and the concatenation with the same `operator||`, and picks between them
// by the operands, so an OR over operands that are no conditions is generated as the `or_(…)` call
// — an `or_condition_t` either way, which sqlite_orm negates. The `conc_t` the operator spelling
// used to build is not negatable, so a NOT over an OR did not compile at all. A NOT over one still
// carries the CAST every `negated_condition_t` needs under a second NOT. Expected values checked
// against sqlite3 3.51 over `users(a INTEGER)` holding one row with a = 7.
TEST_CASE("runtime: a NOT over an OR returns the value SQLite computes") {
    const std::vector<std::string> statements{
        generate("SELECT NOT (a OR 0);"),
        generate("SELECT NOT (0 OR 0);"),
        generate("SELECT NOT (NULL OR 0);"),
        generate("SELECT NOT (NULL OR 1);"),
        generate("SELECT NOT ('a' OR 0);"),
        generate("SELECT NOT (a = 7 OR 0);"),
        generate("SELECT NOT NOT (a OR 0);"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(as_optional(not (or_(&User::a, 0))));",
                              "auto rows = storage.select(not (or_(0, 0)));",
                              "auto rows = storage.select(as_optional(not (or_(nullptr, 0))));",
                              "auto rows = storage.select(as_optional(not (or_(nullptr, 1))));",
                              "auto rows = storage.select(not (or_(\"a\", 0)));",
                              "auto rows = storage.select(as_optional(not (c(&User::a) == 7 or 0)));",
                              "auto rows = storage.select(as_optional(not cast<int64_t>(not (or_(&User::a, 0)))));",
                          });
    REQUIRE(selectedValues(statements) == std::vector<std::string>{"0", "1", "NULL", "0", "1", "0", "1"});
}

// A concatenation under a NOT is delimited with a CAST to REAL, and REAL is the only type that
// reproduces what SQLite answers: a concatenation gives TEXT (or NULL), and SQLite reads the truth
// of a text value through its real value, so `NOT ('0' || '.5')` is 0 where
// `NOT CAST('0' || '.5' AS INTEGER)` is 1. Expected values checked against sqlite3 3.51 and the
// linked libsqlite3 3.45.1 over `users(a TEXT)` holding one row with a = '0'; the two agree on
// every one of 2032 text values for `NOT x` against `NOT CAST(x AS REAL)`.
TEST_CASE("runtime: a NOT over a concatenation returns the value SQLite computes") {
    const std::vector<std::string> statements{
        generate("SELECT NOT (a || '.5');"),
        generate("SELECT NOT (a || '');"),
        generate("SELECT NOT (a || '1');"),
        generate("SELECT NOT ('abc' || 'd');"),
        generate("SELECT NOT ('1' || '2');"),
        generate("SELECT NOT (NULL || 'x');"),
        generate("SELECT NOT (a || 'e-400');"),
        generate("SELECT NOT ((a IS NULL) || '5');"),
        generate("SELECT NOT NOT (a || '.5');"),
    };
    REQUIRE(statements ==
            std::vector<std::string>{
                "auto rows = storage.select(as_optional(not cast<double>(c(&User::a) || \".5\")));",
                "auto rows = storage.select(as_optional(not cast<double>(c(&User::a) || \"\")));",
                "auto rows = storage.select(as_optional(not cast<double>(c(&User::a) || \"1\")));",
                "auto rows = storage.select(not cast<double>(c(\"abc\") || \"d\"));",
                "auto rows = storage.select(not cast<double>(c(\"1\") || \"2\"));",
                "auto rows = storage.select(as_optional(not cast<double>(c(nullptr) || \"x\")));",
                "auto rows = storage.select(as_optional(not cast<double>(c(&User::a) || \"e-400\")));",
                "auto rows = storage.select(not cast<double>(cast<int64_t>(is_null(&User::a)) || \"5\"));",
                "auto rows = storage.select(as_optional(not cast<int64_t>(not cast<double>(c(&User::a) || "
                "\".5\"))));",
            });
    REQUIRE(selectedValues(statements, "std::string", "\"0\"") ==
            std::vector<std::string>{"0", "1", "0", "1", "0", "NULL", "1", "0", "1"});
}

// sqlite_orm types a BETWEEN, an IN, a LIKE and a GLOB `bool`, a CAST the type the CAST asks for,
// and a call of a built-in function the return type that function declares, so a NULL row reached
// the caller as false / "" / 0 — silently, with the serialized SQL right. `as_optional` keeps the
// SQL and widens the type. Both expected rows checked against sqlite3 3.51 over `users(a INTEGER)`
// holding one row, NULL first and 7 second; on master the NULL row reads back as 0, 0, 0, 0, 0,
// "", 0, "", 0. The `hex` column is the counter-check: SQLite answers `hex(NULL)` with the empty
// text rather than NULL, so it is not widened and reads back empty either way.
TEST_CASE("runtime: a result column typed by a predicate, a CAST or a function call reads the NULL back") {
    const std::vector<std::string> statements{
        generate("SELECT a BETWEEN 1 AND 9;"),
        generate("SELECT a NOT BETWEEN 1 AND 9;"),
        generate("SELECT a IN (1, 7);"),
        generate("SELECT a NOT IN (1, 7);"),
        generate("SELECT a LIKE 'x';"),
        generate("SELECT CAST(a AS TEXT);"),
        generate("SELECT length(a);"),
        generate("SELECT upper(a);"),
        generate("SELECT avg(a);"),
        generate("SELECT hex(a);"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(as_optional(between(&User::a, 1, 9)));",
                              "auto rows = storage.select(as_optional(!between(&User::a, 1, 9)));",
                              "auto rows = storage.select(as_optional(in(&User::a, {1, 7})));",
                              "auto rows = storage.select(as_optional(not_in(&User::a, {1, 7})));",
                              "auto rows = storage.select(as_optional(like(&User::a, \"x\")));",
                              "auto rows = storage.select(as_optional(cast<std::string>(&User::a)));",
                              "auto rows = storage.select(as_optional(length(&User::a)));",
                              "auto rows = storage.select(as_optional(upper(&User::a)));",
                              "auto rows = storage.select(as_optional(avg(&User::a)));",
                              "auto rows = storage.select(hex(&User::a));",
                          });
    REQUIRE(selectedValues(statements, "std::optional<int>", "std::nullopt") ==
            std::vector<std::string>{"NULL", "NULL", "NULL", "NULL", "NULL", "NULL", "NULL", "NULL", "NULL", ""});
    REQUIRE(selectedValues(statements, "std::optional<int>", "7") ==
            std::vector<std::string>{"1", "0", "1", "0", "0", "7", "1", "7", "7", "37"});
}

// Most built-ins answer NULL over arguments that hold none, and sqlite_orm types the call by the
// return type the function declares, so the row reached the caller as 0 / "" — and an operator over
// such a call is typed by the operator alone and lost the NULL the same way. Every value here is
// what libsqlite3 3.45.1 answers, the version this project links; before the widening the NULL rows
// read back as 0, "", 0, "", 0, 0. The last two columns are the counter-check: `upper` and `length`
// answer NULL for no reason other than a NULL argument, so a spelled-out argument leaves them plain
// and they read back as they always did.
// A `||` over a call is left to the codegen cases: sqlite_orm hands a built-in call back as a
// `builtin_function_t` rather than a `builtin_function_call` on a compiler its C++20 built-in path
// is off for, and that type is not an operator argument, so `date("bogus") || "x"` does not compile
// with Apple clang whether it is widened or not. That gap is master's and has its own card; the
// widening it would exercise is the same one `nullif(1, 1) + 1` and `unicode('') + 1` exercise here.
TEST_CASE("runtime: a built-in that answers NULL over spelled-out arguments reads the NULL back") {
    const std::vector<std::string> statements{
        generate("SELECT nullif(1, 1) + 1;"),
        generate("SELECT date('bogus');"),
        generate("SELECT julianday('bogus');"),
        generate("SELECT strftime('%Y', 'bogus');"),
        generate("SELECT unicode('');"),
        generate("SELECT unicode('') + 1;"),
        generate("SELECT upper('a');"),
        generate("SELECT length('x');"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(as_optional(nullif(1, 1) + 1));",
                              "auto rows = storage.select(as_optional(date(\"bogus\")));",
                              "auto rows = storage.select(as_optional(julianday(\"bogus\")));",
                              "auto rows = storage.select(as_optional(strftime(\"%Y\", \"bogus\")));",
                              "auto rows = storage.select(as_optional(unicode(\"\")));",
                              "auto rows = storage.select(as_optional(unicode(\"\") + 1));",
                              "auto rows = storage.select(upper(\"a\"));",
                              "auto rows = storage.select(length(\"x\"));",
                          });
    REQUIRE(selectedValues(statements) ==
            std::vector<std::string>{"NULL", "NULL", "NULL", "NULL", "NULL", "NULL", "A", "1"});
}

// A unary plus is dropped, so a result column under one is read back exactly the way the bare
// operand is: the same code, and so the same value and the same NULL. sqlite3 3.51 over
// `users(a INTEGER)` answers `+a` and `+upper(a)` with 7 and '7' on the row a = 7, and with NULL
// on the row a = NULL.
TEST_CASE("runtime: a result column under a unary plus reads back as the bare operand does") {
    const std::vector<std::string> statements{
        generate("SELECT +a;"),
        generate("SELECT a;"),
        generate("SELECT +upper(a);"),
        generate("SELECT upper(a);"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(&User::a);",
                              "auto rows = storage.select(&User::a);",
                              "auto rows = storage.select(as_optional(upper(&User::a)));",
                              "auto rows = storage.select(as_optional(upper(&User::a)));",
                          });
    REQUIRE(selectedValues(statements) == std::vector<std::string>{"7", "7", "7", "7"});
    REQUIRE(selectedValues(statements, "std::optional<int>", "std::nullopt") ==
            std::vector<std::string>{"NULL", "NULL", "NULL", "NULL"});
}

// `operator!` keeps the `c(...)` its operand carries, and the walk that collects the tables a
// statement reads stops at that wrapper, so `storage.select(not c(&User::a))` runs as
// `SELECT NOT "users"."a"` with no FROM clause at all and throws. A column under a NOT is
// generated as `column<T>(&T::a)` for that reason, and a unary plus between the two has to pass
// the request on rather than reset it. sqlite3 3.51 over `users(a INTEGER)` holding 7 answers both
// `NOT a` and `NOT +a` with 0.
TEST_CASE("runtime: a unary plus under a NOT keeps the column form the table walk reads") {
    const std::vector<std::string> statements{
        generate("SELECT NOT a;"),
        generate("SELECT NOT +a;"),
        generate("SELECT NOT + +a;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(as_optional(not column<User>(&User::a)));",
                              "auto rows = storage.select(as_optional(not column<User>(&User::a)));",
                              "auto rows = storage.select(as_optional(not column<User>(&User::a)));",
                          });
    REQUIRE(selectedValues(statements) == std::vector<std::string>{"0", "0", "0"});
}

// SQLite's parser reads the sign of a minus through a unary plus, so `-+9223372036854775807` is
// the INTEGER `-9223372036854775807` rather than a subtraction computed in a double, which would
// hand the caller -9223372036854775808. sqlite3 3.51 answers `-+1` with -1 and
// `-+9223372036854775807` with -9223372036854775807.
TEST_CASE("runtime: a sign folded through a unary plus keeps the whole int64") {
    const std::vector<std::string> statements{
        generate("SELECT -+1;"),
        generate("SELECT -+9223372036854775807;"),
        generate("SELECT -9223372036854775807;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(-1);",
                              "auto rows = storage.select(-9223372036854775807);",
                              "auto rows = storage.select(-9223372036854775807);",
                          });
    REQUIRE(selectedValues(statements) ==
            std::vector<std::string>{"-1", "-9223372036854775807", "-9223372036854775807"});
}

// A NaN is the one value SQLite has no storage class for, so a computation that runs into one is
// stored as NULL, and `+`, `-` and `*` answer NULL over operands that are none. sqlite_orm types
// them `double`, so the first four rows reached the caller as 0. Every value here is what
// libsqlite3 3.45.1 answers, the version this project links. `9e999` is an infinity the literal's
// own text carries, the shape the magnitude bound above it cannot answer for. The last four are the
// counter-check: an infinity of its own is a REAL SQLite carries back as it is, and arithmetic that
// cannot reach one is left plain and reads back as it always did.
TEST_CASE("runtime: an arithmetic result column that overflows into a NaN reads the NULL back") {
    const std::vector<std::string> statements{
        generate("SELECT 0 * (1e300 * 1e300);"),
        generate("SELECT 0.0 * (1e300 * 1e300);"),
        generate("SELECT 1e300 * 1e300 - 1e300 * 1e300;"),
        generate("SELECT (1e300 * 1e300) / (1e300 * 1e300);"),
        generate("SELECT 9e999 - 9e999;"),
        generate("SELECT 0 * 9e999;"),
        generate("SELECT 1e300 * 1e300;"),
        generate("SELECT 9e999;"),
        generate("SELECT 0 * 1e300;"),
        generate("SELECT 1.5 + 2.5;"),
    };
    REQUIRE(statements == std::vector<std::string>{
                              "auto rows = storage.select(as_optional(c(0) * (c(1e300) * 1e300)));",
                              "auto rows = storage.select(as_optional(c(0.0) * (c(1e300) * 1e300)));",
                              "auto rows = storage.select(as_optional(c(1e300) * 1e300 - c(1e300) * 1e300));",
                              "auto rows = storage.select(as_optional(c(1e300) * 1e300 / (c(1e300) * 1e300)));",
                              "auto rows = storage.select(as_optional(c(9e999) - 9e999));",
                              "auto rows = storage.select(as_optional(c(0) * 9e999));",
                              "auto rows = storage.select(c(1e300) * 1e300);",
                              "auto rows = storage.select(9e999);",
                              "auto rows = storage.select(c(0) * 1e300);",
                              "auto rows = storage.select(c(1.5) + 2.5);",
                          });
    REQUIRE(selectedValues(statements) ==
            std::vector<std::string>{"NULL", "NULL", "NULL", "NULL", "NULL", "NULL", "inf", "inf", "0", "4"});
}

// sqlite_orm's `between(A, T, T)` deduces one C++ type from both bounds, so an `int` bound next to
// a 64-bit one did not compile at all — `between(&User::a, 1, 3000000000)` is the `no matching
// function` this test would have failed to build on. The `int64_t` cast that gives them one type
// has to leave the values alone, and a TEXT column is where that is visible: SQLite applies the
// column's affinity to a bound with none of its own, and '1' compares against the text '1' an
// INTEGER bound becomes rather than against the '1.0' a REAL one would. Every value here is what
// sqlite3 3.45.1 and 3.51 answer for the same SQL.
TEST_CASE("runtime: BETWEEN bounds of two integer widths read back as SQLite computes them") {
    const std::vector<std::string> statements{
        generate("SELECT a BETWEEN 1 AND 3000000000;"),
        generate("SELECT a BETWEEN 3000000000 AND 4000000000;"),
        generate("SELECT a BETWEEN 1 AND TRUE;"),
        generate("SELECT a BETWEEN 1 AND 0xFFFFFFFF;"),
        generate("SELECT a BETWEEN -1 AND 3000000000;"),
    };
    REQUIRE(statements ==
            std::vector<std::string>{
                "auto rows = storage.select(as_optional(between(&User::a, static_cast<int64_t>(1), 3000000000)));",
                "auto rows = storage.select(as_optional(between(&User::a, 3000000000, 4000000000)));",
                "auto rows = storage.select(as_optional(between(&User::a, static_cast<int64_t>(1), "
                "static_cast<int64_t>(true))));",
                "auto rows = storage.select(as_optional(between(&User::a, static_cast<int64_t>(1), "
                "static_cast<int64_t>(0xFFFFFFFF))));",
                "auto rows = storage.select(as_optional(between(&User::a, static_cast<int64_t>(-1), 3000000000)));",
            });
    REQUIRE(selectedValues(statements, "int64_t", "2147483648") == std::vector<std::string>{"1", "0", "0", "1", "1"});
    REQUIRE(selectedValues(statements, "std::string", "\"1\"") == std::vector<std::string>{"1", "0", "1", "1", "1"});
}
