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

    // What to do about headers that are not the pinned revision depends on where they came from.
    // A populated dependency moves on the next reconfigure; a directory named by
    // FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS never does, however many times cmake runs, so
    // telling its reader to re-run cmake sends them off in the one direction that cannot help.
    [[nodiscard]] std::string staleHeadersMessage(std::string_view sourceDirectory,
                                                  std::string_view sourceOverride,
                                                  std::string_view configuredRevision) {
        if (!sourceOverride.empty()) {
            return "these headers come from " + std::string{sourceOverride} +
                   ", where FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS points; check that "
                   "directory out at " +
                   std::string{configuredRevision} +
                   " or configure without the override, because re-running cmake does not move a "
                   "source override";
        }
        return "the headers in " + std::string{sourceDirectory} + " are stale; re-run cmake to move them to " +
               std::string{configuredRevision};
    }
}

// sqlite_orm's `dev` branch moves, and the runtime tests compile the generated code against it, so
// a branch name would let the same commit of this repository pass today and fail tomorrow.
TEST_CASE("CMakeLists pins sqlite_orm to a revision") {
    const std::string expected =
        "    set(SQLITE2ORM_SQLITE_ORM_PINNED_REVISION "
        "\"eb77998ef5e27350b25977b061e46e202742ecc8\")\n"
        "    set(SQLITE2ORM_SQLITE_ORM_REVISION \"${SQLITE2ORM_SQLITE_ORM_PINNED_REVISION}\"\n"
        "        CACHE STRING \"Revision of fnc12/sqlite_orm the runtime tests compile against\")\n";
    REQUIRE(countOccurrences(readSourceFile("CMakeLists.txt"), expected) == 1);
}

TEST_CASE("CMakeLists fetches the configured sqlite_orm revision") {
    const std::string expected = "    message(STATUS \"[sqlite2orm] Fetching sqlite_orm headers "
                                 "(${SQLITE2ORM_SQLITE_ORM_REVISION})...\")\n"
                                 "    FetchContent_Declare(\n"
                                 "        sqlite_orm_headers\n"
                                 "        GIT_REPOSITORY https://github.com/fnc12/sqlite_orm.git\n"
                                 "        GIT_TAG ${SQLITE2ORM_SQLITE_ORM_REVISION}\n"
                                 "    )\n";
    REQUIRE(countOccurrences(readSourceFile("CMakeLists.txt"), expected) == 1);
}

// SQLITE2ORM_SQLITE_ORM_REVISION is a cache entry: a tree configured once against `dev` keeps it,
// and from then on the guard below can only skip. Such a tree has to say at configure time that
// its guard is off, or it stays green while checking nothing.
TEST_CASE("CMakeLists reports a tree configured off the pin") {
    const std::string expected =
        "    if(NOT SQLITE2ORM_SQLITE_ORM_REVISION STREQUAL "
        "\"${SQLITE2ORM_SQLITE_ORM_PINNED_REVISION}\")\n"
        "        message(STATUS \"[sqlite2orm] This tree is configured against sqlite_orm \"\n"
        "                       \"${SQLITE2ORM_SQLITE_ORM_REVISION}, not the pinned \"\n"
        "                       \"${SQLITE2ORM_SQLITE_ORM_PINNED_REVISION}: the revision guard is "
        "off here.\")\n"
        "    endif()\n";
    REQUIRE(countOccurrences(readSourceFile("CMakeLists.txt"), expected) == 1);
}

TEST_CASE("README quotes the pinned sqlite_orm revision") {
    const std::string expected =
        "| `SQLITE2ORM_SQLITE_ORM_REVISION` | `eb77998ef5e27350b25977b061e46e202742ecc8` | Revision "
        "of `fnc12/sqlite_orm` the runtime tests compile against |\n";
    REQUIRE(countOccurrences(readSourceFile("README.md"), expected) == 1);
}

TEST_CASE("a mismatch under a source override names the override") {
    REQUIRE(staleHeadersMessage("/elsewhere/sqlite_orm",
                                "/elsewhere/sqlite_orm",
                                "eb77998ef5e27350b25977b061e46e202742ecc8") ==
            "these headers come from /elsewhere/sqlite_orm, where "
            "FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS points; check that directory out at "
            "eb77998ef5e27350b25977b061e46e202742ecc8 or configure without the override, because "
            "re-running cmake does not move a source override");
}

TEST_CASE("a mismatch in a populated dependency asks for a reconfigure") {
    REQUIRE(staleHeadersMessage("/work/build/_deps/sqlite_orm_headers-src",
                                "",
                                "eb77998ef5e27350b25977b061e46e202742ecc8") ==
            "the headers in /work/build/_deps/sqlite_orm_headers-src are stale; re-run cmake to "
            "move them to eb77998ef5e27350b25977b061e46e202742ecc8");
}

// A build tree keeps the headers it populated, and one pointed at a checkout of its own
// (FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS) keeps whatever sits there. Saying so here costs one
// assertion; leaving it unsaid costs a round of blaming the change under test for a compile error
// inside a generated program.
TEST_CASE("the sqlite_orm headers under test are the pinned revision") {
    const std::string_view configuredRevision = SQLITE2ORM_TEST_SQLITE_ORM_REVISION;
    const std::string_view populatedRevision = SQLITE2ORM_TEST_SQLITE_ORM_HEAD;
    const std::string_view sourceDirectory = SQLITE2ORM_TEST_SQLITE_ORM_SOURCE_DIR;
    const std::string_view sourceOverride = SQLITE2ORM_TEST_SQLITE_ORM_SOURCE_OVERRIDE;
    if (configuredRevision != pinnedRevision) {
        SKIP("this build was configured against sqlite_orm " + std::string{configuredRevision});
    }
    // Only a checkout can be asked what revision it holds, and a directory that is not one is not
    // evidence of anything: reporting the commit of the repository above it would blame sqlite_orm
    // for a sqlite2orm sha.
    if (populatedRevision.empty()) {
        SKIP("the headers in " + std::string{sourceDirectory} +
             " are not a git checkout, so their revision is unknown");
    }
    INFO(staleHeadersMessage(sourceDirectory, sourceOverride, configuredRevision));
    REQUIRE(populatedRevision == configuredRevision);
}
