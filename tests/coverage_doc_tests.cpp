#include "codegen_tests_common.hpp"
#include "source_file_text.hpp"

#include <string>

namespace {

    using source_file_test_helpers::countOccurrences;

    [[nodiscard]] std::string readCoverage() {
        return source_file_test_helpers::readSourceFile("COVERAGE.md");
    }
}

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

// Every row that describes one of the four gaps reported in fnc12/sqlite_orm#1543 links it, so a
// reader who hits the gap reaches the upstream report from wherever this file mentions it: the
// `OR` row, the UNIQUE index over an expression, `json_extract`, `json_quote` and the PRAGMA row
// that owns the unquoted `integrity_check` argument.
TEST_CASE("COVERAGE.md links the upstream report from every row that describes one of its gaps") {
    REQUIRE(countOccurrences(readCoverage(), "https://github.com/fnc12/sqlite_orm/issues/1543") == 5);
}
