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
        for(const auto& statement: selectStatements) {
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
            for(std::string line; std::getline(out, line);) {
                rows.push_back(line);
            }
        }
        if(exitCode != 0) {
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
        storageDefinition.replace(storageAt, inMemory.size(),
                                  "make_storage(\"" + databasePath.generic_string() + "\"");

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
        for(const auto& probeValue: probeValues) {
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
            for(std::string line; std::getline(out, line);) {
                lines.push_back(line);
            }
        }
        if(exitCode != 0) {
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
    REQUIRE(statements ==
            std::vector<std::string>{
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
    REQUIRE(insertedColumnRows(statements, "std::string") ==
            std::vector<std::string>{"text|1", "text|1.5", "blob|A"});
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
    REQUIRE(selectedValues(statements, "std::optional<int>", "std::nullopt") ==
            std::vector<std::string>{"0"});
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
    REQUIRE(selectedValues(statements, "std::optional<int>", "7") ==
            std::vector<std::string>{"1", "0", "1", "0", "1"});
}

// A CHECK constraint is the slot where the serialized SQL is the whole product: it is stored in the
// schema and SQLite enforces it from then on, with no C++ term left to hold the grouping. The bare
// predicate made the generated table enforce the opposite of what the source said — the stored
// clause read `CHECK (1 - "a" IS NULL)`, that is `(1 - "a") IS NULL`, so it rejected the `a = 7` row
// it should accept and accepted the NULL row it should reject. Checked against sqlite3 3.45.1 and
// 3.51: `CREATE TABLE t(a INTEGER CHECK(1 - (a IS NULL)))` accepts 7 and rejects NULL.
TEST_CASE("runtime: a predicate in a CHECK constraint is enforced the way the source reads") {
    const std::string generatedCode = generate("CREATE TABLE t(a INTEGER CHECK(1 - (a IS NULL)));");
    REQUIRE(generatedCode ==
            "struct T {\n"
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
    REQUIRE(selectedValues(statements, "std::optional<int>", "7") ==
            std::vector<std::string>{"8", "7x", "-7"});
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
            std::vector<std::string>{"9223372036854775807", "9223372036854775807", "9223372036854775807",
                                     "9223372036854775807", "-9223372036854775808"});
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
