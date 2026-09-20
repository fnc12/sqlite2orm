#include "source_file_text.hpp"

#include <catch2/catch_all.hpp>

#include <string>
#include <string_view>

namespace {

    using source_file_test_helpers::countOccurrences;
    using source_file_test_helpers::readSourceFile;

    // The revision cmake/SqliteOrmPinnedRevision.cmake pins. Spelled out here so that bumping it
    // takes updating every place that promises it: the build, the README table and this test.
    constexpr std::string_view pinnedRevision = "eb77998ef5e27350b25977b061e46e202742ecc8";

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

    // An empty revision is the answer for headers that are not a checkout and for a machine that
    // has no git to ask, and the two want different things done about them. Reporting the first
    // one that fits tells a reader whose configure simply had no git that their checkout is not
    // one, which is the kind of wrong turn this whole guard exists to spare them.
    [[nodiscard]] std::string unknownRevisionReason(std::string_view sourceDirectory, std::string_view gitExecutable) {
        if (gitExecutable.empty()) {
            return "no git executable was found when this build was configured, so nothing could read the revision "
                   "of the headers in " +
                   std::string{sourceDirectory};
        }
        return "the headers in " + std::string{sourceDirectory} +
               " are not a git checkout, so their revision is unknown";
    }
}

// sqlite_orm's `dev` branch moves, and the runtime tests compile the generated code against it, so
// a branch name would let the same commit of this repository pass today and fail tomorrow. The
// revision defaults as a plain variable: a `CACHE STRING` default is written on the first configure
// and outranks the file afterwards, so a bump would move CI and leave every tree that had already
// configured on the old headers, with the guard below skipping instead of catching it.
TEST_CASE("the pinned revision module pins sqlite_orm to a revision") {
    const std::string expected =
        "set(SQLITE2ORM_SQLITE_ORM_PINNED_REVISION "
        "\"eb77998ef5e27350b25977b061e46e202742ecc8\")\n"
        "if(NOT DEFINED SQLITE2ORM_SQLITE_ORM_REVISION)\n"
        "    set(SQLITE2ORM_SQLITE_ORM_REVISION \"${SQLITE2ORM_SQLITE_ORM_PINNED_REVISION}\")\n"
        "elseif(NOT SQLITE2ORM_SQLITE_ORM_REVISION STREQUAL "
        "SQLITE2ORM_SQLITE_ORM_PINNED_REVISION)\n";
    REQUIRE(countOccurrences(readSourceFile("cmake/SqliteOrmPinnedRevision.cmake"), expected) == 1);
}

TEST_CASE("CMakeLists takes the revision from the pinned revision module") {
    const std::string expected = "    include(\"${CMAKE_CURRENT_LIST_DIR}/cmake/SqliteOrmPinnedRevision.cmake\")\n";
    REQUIRE(countOccurrences(readSourceFile("CMakeLists.txt"), expected) == 1);
}

// The ref handed to FetchContent is the revision as resolved, not as written: a branch name goes
// to `git checkout` bare out of a tree populated at the pinned hash, and sqlite_orm has a top-level
// directory named like the branch README tells its reader to try.
TEST_CASE("CMakeLists fetches the configured sqlite_orm revision") {
    const std::string expected = "    sqlite2orm_headers_checkout_ref(\n"
                                 "        \"${SQLITE2ORM_SQLITE_ORM_REVISION}\" "
                                 "\"${sqlite2orm_sqlite_orm_repository}\"\n"
                                 "        \"${sqlite2orm_git_executable}\" "
                                 "sqlite2orm_sqlite_orm_checkout_ref)\n"
                                 "    message(STATUS \"[sqlite2orm] Fetching sqlite_orm headers "
                                 "(${sqlite2orm_sqlite_orm_checkout_ref})...\")\n"
                                 "    FetchContent_Declare(\n"
                                 "        sqlite_orm_headers\n"
                                 "        GIT_REPOSITORY ${sqlite2orm_sqlite_orm_repository}\n"
                                 "        GIT_TAG ${sqlite2orm_sqlite_orm_checkout_ref}\n"
                                 "    )\n";
    REQUIRE(countOccurrences(readSourceFile("CMakeLists.txt"), expected) == 1);
}

TEST_CASE("CMakeLists resolves the revision against the repository it fetches") {
    const std::string expected =
        "    set(sqlite2orm_sqlite_orm_repository \"https://github.com/fnc12/sqlite_orm.git\")\n";
    REQUIRE(countOccurrences(readSourceFile("CMakeLists.txt"), expected) == 1);
}

// A tree configured with -DSQLITE2ORM_SQLITE_ORM_REVISION keeps that revision across bumps, and
// from then on the guard below can only skip. Such a tree has to say at configure time that its
// guard is off, or it stays green while checking nothing.
TEST_CASE("the pinned revision module reports a tree configured off the pin") {
    const std::string expected = "    message(STATUS \"[sqlite2orm] This tree is configured against sqlite_orm \"\n"
                                 "                   \"${SQLITE2ORM_SQLITE_ORM_REVISION}, not the pinned \"\n"
                                 "                   \"${SQLITE2ORM_SQLITE_ORM_PINNED_REVISION}: the revision guard is "
                                 "off here. \"\n"
                                 "                   \"Run cmake -U SQLITE2ORM_SQLITE_ORM_REVISION to follow the pin "
                                 "again.\")\n"
                                 "endif()\n";
    REQUIRE(countOccurrences(readSourceFile("cmake/SqliteOrmPinnedRevision.cmake"), expected) == 1);
}

TEST_CASE("README quotes the pinned sqlite_orm revision") {
    const std::string expected =
        "| `SQLITE2ORM_SQLITE_ORM_REVISION` | `eb77998ef5e27350b25977b061e46e202742ecc8` | Revision "
        "of `fnc12/sqlite_orm` the runtime tests compile against |\n";
    REQUIRE(countOccurrences(readSourceFile("README.md"), expected) == 1);
}

// The one thing README offers other than following the pin is aiming a tree at a branch, and that
// is the command the resolution above exists for: a tree already populated at the pinned hash used
// to answer it with a git error naming neither this project nor its option.
TEST_CASE("README says how a branch is checked out") {
    const std::string expected =
        "cmake -S . -B build -DSQLITE2ORM_SQLITE_ORM_REVISION=dev\n"
        "```\n"
        "\n"
        "A branch is checked out as `origin/<branch>`, which the build works out by asking the "
        "remote: a\n"
        "tree populated at the pinned hash holds no local branch of its own, and `git checkout "
        "dev` inside a\n"
        "sqlite_orm checkout — which keeps a top-level `dev/` directory — is ambiguous "
        "between the two.\n"
        "Tags and commit hashes are taken as they are written.\n";
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
    REQUIRE(staleHeadersMessage("/tmp/build/_deps/sqlite_orm_headers-src",
                                "",
                                "eb77998ef5e27350b25977b061e46e202742ecc8") ==
            "the headers in /tmp/build/_deps/sqlite_orm_headers-src are stale; re-run cmake to "
            "move them to eb77998ef5e27350b25977b061e46e202742ecc8");
}

TEST_CASE("headers that are not a checkout are reported as such") {
    REQUIRE(unknownRevisionReason("/tmp/build/_deps/sqlite_orm_headers-src", "/usr/bin/git") ==
            "the headers in /tmp/build/_deps/sqlite_orm_headers-src are not a git checkout, so "
            "their revision is unknown");
}

TEST_CASE("a configure without git says so instead of blaming the headers") {
    REQUIRE(unknownRevisionReason("/tmp/build/_deps/sqlite_orm_headers-src", "") ==
            "no git executable was found when this build was configured, so nothing could read "
            "the revision of the headers in /tmp/build/_deps/sqlite_orm_headers-src");
}

// A build tree keeps the headers it populated, and one pointed at a checkout of its own
// (FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS) keeps whatever sits there. Saying so here costs one
// assertion; leaving it unsaid costs a round of blaming the change under test for a compile error
// inside a generated program.
//
// The revision compared is the one cmake read after populating the headers, so this is what the
// tree was configured with and not necessarily what it holds right now: a checkout moved by hand
// after that configure is invisible here until the next one. Everything that moves the headers as
// part of a build goes through a configure, which is what makes the snapshot worth having.
TEST_CASE("the sqlite_orm headers this build was configured with were the pinned revision") {
    const std::string_view configuredRevision = SQLITE2ORM_TEST_SQLITE_ORM_REVISION;
    const std::string_view populatedRevision = SQLITE2ORM_TEST_SQLITE_ORM_HEAD;
    const std::string_view sourceDirectory = SQLITE2ORM_TEST_SQLITE_ORM_SOURCE_DIR;
    const std::string_view sourceOverride = SQLITE2ORM_TEST_SQLITE_ORM_SOURCE_OVERRIDE;
    if (configuredRevision != pinnedRevision) {
        SKIP("this build was configured against sqlite_orm " + std::string{configuredRevision});
    }
    // Only a checkout can be asked what revision it holds, and only where there is a git to ask
    // with; a directory that is not one is not evidence of anything, and reporting the commit of
    // the repository above it would blame sqlite_orm for a sqlite2orm sha. Which of the two left
    // the answer empty decides what the reader should do, so the skip says which one it was.
    if (populatedRevision.empty()) {
        SKIP(unknownRevisionReason(sourceDirectory, SQLITE2ORM_TEST_GIT_EXECUTABLE));
    }
    INFO(staleHeadersMessage(sourceDirectory, sourceOverride, configuredRevision));
    REQUIRE(populatedRevision == configuredRevision);
}
