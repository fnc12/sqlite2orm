#include "source_file_text.hpp"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace source_file_test_helpers {

    std::string readSourceFile(std::string_view relativePath) {
        const std::filesystem::path path = std::filesystem::path{SQLITE2ORM_TEST_SOURCE_DIR} / relativePath;
        std::ifstream stream{path, std::ios::binary};
        REQUIRE(stream.is_open());
        return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }

    std::size_t countOccurrences(std::string_view haystack, std::string_view needle) {
        std::size_t count = 0;
        for (std::size_t pos = haystack.find(needle); pos != std::string_view::npos;
             pos = haystack.find(needle, pos + needle.size())) {
            ++count;
        }
        return count;
    }

}  // namespace source_file_test_helpers
