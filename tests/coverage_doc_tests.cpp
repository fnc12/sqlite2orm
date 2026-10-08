#include "codegen_tests_common.hpp"
#include "source_file_text.hpp"
#include "temp_build_dir.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

    using source_file_test_helpers::countOccurrences;

    [[nodiscard]] std::string readCoverage() {
        return source_file_test_helpers::readSourceFile("COVERAGE.md");
    }

    /**
     *  Builds a program that runs `statement` over a database holding a table called `tableName`,
     *  links it against sqlite_orm, runs it and answers what the call told the caller: `ok` for a
     *  call that returned, or the `what()` of the `std::system_error` it threw. What a failing
     *  pragma says is the library's to decide, not SQLite's — a row that quotes the message a user
     *  reads has to read it off the pinned revision rather than off the sqlite3 shell.
     */
    [[nodiscard]] std::string pragmaOutcomeText(std::string_view statement, std::string_view tableName) {
        std::ostringstream program;
        program << "#include <sqlite_orm/sqlite_orm.h>\n"
                   "#include <iostream>\n"
                   "#include <system_error>\n"
                   "\n"
                   "struct Row {\n"
                   "    int id = 0;\n"
                   "};\n"
                   "\n"
                   "int main() {\n"
                   "    auto storage = sqlite_orm::make_storage(\n"
                   "        \"\", sqlite_orm::make_table(\""
                << tableName
                << "\", sqlite_orm::make_column(\"id\", &Row::id)));\n"
                   "    storage.sync_schema();\n"
                   "    try {\n"
                   "        const auto rows = "
                << statement
                << "\n"
                   "        (void)rows;\n"
                   "        std::cout << \"ok\" << '\\n';\n"
                   "    } catch(const std::system_error& e) {\n"
                   "        std::cout << e.what() << '\\n';\n"
                   "    }\n"
                   "    return 0;\n"
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
        std::string outcome;
        {
            std::ifstream out(outpath);
            std::getline(out, outcome);
        }
        if (exitCode != 0) {
            WARN("building the generated PRAGMA call failed (exit "
                 << exitCode << "); ensure c++, sqlite_orm headers and libsqlite3 are usable");
        }
        REQUIRE(exitCode == 0);
        return outcome;
    }
}  // namespace

// The `OR` row quotes the code the generator emits for a MATCH under an OR, and a quote of
// generated code goes stale the moment the generator changes: the row went on saying that output
// did not compile long after the `c()` wrap made it compile, which is the worst shape for a row
// that carries a public link. The two literals below are the same expression — the fixture the
// codegen tests generate against names the struct `User`, the coverage row names it `T` — so a
// change to either side fails here and asks for the other.
TEST_CASE("COVERAGE.md quotes the code the generator emits for a MATCH under an OR") {
    REQUIRE(generate("a MATCH 'x' OR b") == R"(or_(c(match(&User::a, "x")), &User::b))");
    REQUIRE(countOccurrences(readCoverage(), R"(`or_(c(match(&T::a, "x")), &T::b)`)") == 1);
}

// The same for the spelling a condition beside the MATCH carries, which takes no quote.
TEST_CASE("COVERAGE.md quotes the code the generator emits for a MATCH beside a condition") {
    REQUIRE(generate("a MATCH 'x' OR b = 1") == R"(match(&User::a, "x") or c(&User::b) == 1)");
    REQUIRE(countOccurrences(readCoverage(), R"(`match(&T::a, "x") or c(&T::b) == 1`)") == 1);
}

// The same for the constant the OR keeps in the SQL beside a MATCH.
TEST_CASE("COVERAGE.md quotes the code the generator emits for a constant beside a MATCH") {
    REQUIRE(generate("a MATCH 'x' OR 1") == R"(or_(c(match(&User::a, "x")), c(internal::literal_holder<int>{1})))");
    REQUIRE(countOccurrences(readCoverage(), R"(`or_(c(match(&T::a, "x")), c(internal::literal_holder<int>{1}))`)") ==
            1);
}

// The CASE result-type row quotes the code the generator emits for the one branch pair with no
// number over it — an `int64_t` branch beside a REAL one — and the row is the reason the pair is
// read as text rather than through a `double` that drops every integer past 2^53. Both literals
// below are the same expression, the fixture naming the struct `User` and the row naming it `T`,
// so a change to either side fails here and asks for the other.
TEST_CASE("COVERAGE.md quotes the code the generator emits for a CASE over an integer and a REAL") {
    REQUIRE(generate("CASE WHEN a > 0 THEN a * 1 ELSE 1.5 END") ==
            "case_<std::string>().when(c(&User::a) > 0, then(c(&User::a) * 1)).else_(1.5).end()");
    REQUIRE(countOccurrences(readCoverage(),
                             "`case_<std::string>().when(c(&T::a) > 0, then(c(&T::a) * 1)).else_(1.5).end()`") == 1);
}

// The same row names what the widening still cannot reach: a branch whose type the operation
// knows rather than the literal under it is read through the default `int`, so the row quotes a
// CASE that answers a number where SQLite answers text. The value it comes back as is pinned by
// running the generated select, in codegen_tests_runtime_values.cpp.
TEST_CASE("COVERAGE.md quotes the CASE branch the type inference still reads through an int") {
    REQUIRE(generate("CASE WHEN a THEN a || 'x' ELSE 1 END") ==
            R"(case_<int>().when(&User::a, then(c(&User::a) || "x")).else_(1).end())");
    REQUIRE(countOccurrences(readCoverage(),
                             "`CASE WHEN a THEN a || 'x' ELSE 1 END` comes back as 7 where "
                             "SQLite answers `7x`") == 1);
}

// The IS NULL row quotes what a NULL under an operator on the right of an IS is generated as: a
// binary IS, not `is_null()`. The fixture names the struct `User`, the row names it `T`.
TEST_CASE("COVERAGE.md quotes the code the generator emits for an IS over NULL - 1") {
    REQUIRE(generate("a IS NULL - 1") == "is(&User::a, c(nullptr) - 1)");
    REQUIRE(countOccurrences(readCoverage(), "`is(&T::a, c(nullptr) - 1)`") == 1);
}

// The `IS expr` row quotes what an IS against TRUE or FALSE is generated as: a truth test of the left
// operand, not a comparison with the 1 or 0 the keyword binds as. The fixture names the struct
// `User`, the row names it `T`.
TEST_CASE("COVERAGE.md quotes the code the generator emits for an IS truth test") {
    REQUIRE(generate("a IS TRUE") == "is(and_(&User::a, true), true)");
    REQUIRE(generate("a IS NOT FALSE") == "is_not(and_(&User::a, true), false)");
    REQUIRE(countOccurrences(readCoverage(), "`is(and_(&T::a, true), true)`") == 1);
    REQUIRE(countOccurrences(readCoverage(), "`is_not(and_(&T::a, true), false)`") == 1);
}

// Every row that describes one of the four gaps reported in fnc12/sqlite_orm#1543 links it, so a
// reader who hits the gap reaches the upstream report from wherever this file mentions it: the
// `OR` row, the UNIQUE index over an expression, `json_extract`, `json_quote` and the PRAGMA row
// that owns the unquoted `integrity_check` argument.
TEST_CASE("COVERAGE.md links the upstream report from every row that describes one of its gaps") {
    REQUIRE(countOccurrences(readCoverage(), "https://github.com/fnc12/sqlite_orm/issues/1543") == 5);
}

// The PRAGMA row quotes the `integrity_check` call generated for a table name that needs quoting.
// sqlite_orm streams that argument into the pragma text raw, so the generated literal carries the
// name already quoted; running it over a database holding the table is what says the row's `ok`
// is true, rather than the `SQL logic error` the unquoted call threw before.
TEST_CASE("COVERAGE.md quotes the integrity_check call generated for a name that needs quoting") {
    const std::string statement = generate(R"(PRAGMA integrity_check("my table");)");
    REQUIRE(statement == R"(storage.pragma.integrity_check("\"my table\"");)");
    REQUIRE(pragmaOutcomeText(statement, "my table") == "ok");
    REQUIRE(countOccurrences(readCoverage(), R"(`storage.pragma.integrity_check("\"my table\"")`)") == 1);
}
