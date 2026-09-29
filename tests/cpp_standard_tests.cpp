#include <sqlite2orm/cpp_standard.h>

#include <catch2/catch_all.hpp>

#include <optional>
#include <string_view>

using namespace sqlite2orm;

TEST_CASE("parseCppStandard: each standard --std accepts") {
    CHECK(parseCppStandard("14") == std::optional<int>{14});
    CHECK(parseCppStandard("17") == std::optional<int>{17});
    CHECK(parseCppStandard("20") == std::optional<int>{20});
    CHECK(parseCppStandard("26") == std::optional<int>{26});
}

// A value the CLI does not know must not fall back to the default standard: the user would get code
// for a standard other than the one asked for without being told.
TEST_CASE("parseCppStandard: anything else is refused") {
    CHECK(parseCppStandard("11") == std::nullopt);
    CHECK(parseCppStandard("23") == std::nullopt);
    CHECK(parseCppStandard("abc") == std::nullopt);
    CHECK(parseCppStandard("") == std::nullopt);
    CHECK(parseCppStandard("c++20") == std::nullopt);
    CHECK(parseCppStandard("20 ") == std::nullopt);
    CHECK(parseCppStandard("020") == std::nullopt);
    CHECK(parseCppStandard("+20") == std::nullopt);
}

TEST_CASE("parseCppStandard: the choices named in the CLI error") {
    CHECK(kCppStandardChoices == std::string_view{"14, 17, 20, 26"});
}
