#include "source_file_text.hpp"

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

// A source file is read whole whenever it is edited, so one that grows past a size that fits
// comfortably in a read gets split by responsibility rather than left to grow. The files still
// listed here are the ones waiting for their split; each split takes its file off the list, and
// a file that newly grows past the limit shows up here as an extra name.
TEST_CASE("source layout: no file under src/ grows past 1500 lines") {
    constexpr std::size_t kLineLimit = 1500;
    const std::vector<std::string> expected{
        "codegen_ddl.cpp",
        "codegen_expression.cpp",
        "codegen_select.cpp",
    };

    std::vector<std::string> found;
    const std::filesystem::path sourceDirectory = std::filesystem::path{SQLITE2ORM_TEST_SOURCE_DIR} / "src";
    for (const auto& entry: std::filesystem::directory_iterator{sourceDirectory}) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string fileName = entry.path().filename().string();
        const std::string text = source_file_test_helpers::readSourceFile("src/" + fileName);
        if (source_file_test_helpers::countOccurrences(text, "\n") > kLineLimit) {
            found.push_back(fileName);
        }
    }
    std::sort(found.begin(), found.end());
    REQUIRE(found == expected);
}
