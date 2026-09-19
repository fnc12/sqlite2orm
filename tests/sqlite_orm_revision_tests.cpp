#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

    // The revision CMakeLists.txt pins. Spelled out here so that bumping it takes updating every
    // place that promises it: the build, the README table and this test.
    constexpr std::string_view pinnedRevision = "eb77998ef5e27350b25977b061e46e202742ecc8";

    [[nodiscard]] std::string readSourceFile(std::string_view name) {
        const std::filesystem::path path = std::filesystem::path{SQLITE2ORM_TEST_SOURCE_DIR} / name;
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

// sqlite_orm's `dev` branch moves, and the runtime tests compile the generated code against it, so
// a branch name would let the same commit of this repository pass today and fail tomorrow.
TEST_CASE("CMakeLists pins sqlite_orm to a revision") {
    const std::string expected =
        "    set(SQLITE2ORM_SQLITE_ORM_REVISION \"eb77998ef5e27350b25977b061e46e202742ecc8\"\n"
        "        CACHE STRING \"Revision of fnc12/sqlite_orm the runtime tests compile against\")\n"
        "    message(STATUS \"[sqlite2orm] Fetching sqlite_orm headers "
        "(${SQLITE2ORM_SQLITE_ORM_REVISION})...\")\n"
        "    FetchContent_Declare(\n"
        "        sqlite_orm_headers\n"
        "        GIT_REPOSITORY https://github.com/fnc12/sqlite_orm.git\n"
        "        GIT_TAG ${SQLITE2ORM_SQLITE_ORM_REVISION}\n"
        "    )\n";
    REQUIRE(countOccurrences(readSourceFile("CMakeLists.txt"), expected) == 1);
}

TEST_CASE("README quotes the pinned sqlite_orm revision") {
    const std::string expected =
        "| `SQLITE2ORM_SQLITE_ORM_REVISION` | `eb77998ef5e27350b25977b061e46e202742ecc8` | Revision "
        "of `fnc12/sqlite_orm` the runtime tests compile against |\n";
    REQUIRE(countOccurrences(readSourceFile("README.md"), expected) == 1);
}

// A build tree keeps the headers it populated, and one pointed at a checkout of its own
// (FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS) keeps whatever sits there. Saying so here costs one
// assertion; leaving it unsaid costs a round of blaming the change under test for a compile error
// inside a generated program.
TEST_CASE("the sqlite_orm headers under test are the pinned revision") {
    const std::string_view configuredRevision = SQLITE2ORM_TEST_SQLITE_ORM_REVISION;
    const std::string_view populatedRevision = SQLITE2ORM_TEST_SQLITE_ORM_HEAD;
    if (configuredRevision != pinnedRevision) {
        SKIP("this build was configured against sqlite_orm " + std::string{configuredRevision});
    }
    if (populatedRevision.empty()) {
        SKIP("the revision of the populated sqlite_orm headers is unknown");
    }
    INFO("the headers in _deps are stale; re-run cmake to move them to " << configuredRevision);
    REQUIRE(populatedRevision == configuredRevision);
}
