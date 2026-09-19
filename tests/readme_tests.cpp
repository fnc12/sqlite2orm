#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

    [[nodiscard]] std::string readReadme() {
        const std::filesystem::path path = std::filesystem::path{SQLITE2ORM_TEST_SOURCE_DIR} / "README.md";
        std::ifstream stream{path, std::ios::binary};
        REQUIRE(stream.is_open());
        return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }

    [[nodiscard]] std::size_t countOccurrences(std::string_view haystack, std::string_view needle) {
        std::size_t count = 0;
        for (std::size_t pos = haystack.find(needle); pos != std::string_view::npos;
             pos = haystack.find(needle, pos + needle.size())) {
            ++count;
        }
        return count;
    }
}

// The playground is the cheapest way to try the generator, so README must point at it before it
// starts talking about features and building from source.
TEST_CASE("README opens with the playground link") {
    const std::string readme = readReadme();
    const std::string expected =
        "# sqlite2orm\n"
        "\n"
        "Converts SQLite SQL into C++ code for [sqlite_orm](https://github.com/fnc12/sqlite_orm).\n"
        "\n"
        "Paste any SQLite statement — `CREATE TABLE`, `SELECT`, `INSERT`, triggers, indexes, window "
        "functions — and get ready-to-compile `sqlite_orm` API calls with struct definitions and "
        "`make_storage()`.\n"
        "\n"
        "## Try it online\n"
        "\n"
        "No build required — paste SQL and get sqlite_orm code at "
        "[sqliteorm.com/playground](https://sqliteorm.com/playground).\n"
        "\n"
        "## Features\n";
    REQUIRE(readme.substr(0, expected.size()) == expected);
}

TEST_CASE("README links the playground exactly once") {
    REQUIRE(countOccurrences(readReadme(), "https://sqliteorm.com/playground") == 1);
}
