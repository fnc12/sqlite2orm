#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace source_file_test_helpers {

    /**
     *  Contents of a file of this repository, named by its path relative to the source tree. The
     *  tests that pin what README and the build files say about each other read them with this.
     */
    [[nodiscard]] std::string readSourceFile(std::string_view relativePath);

    /** How many non-overlapping times `needle` occurs in `haystack`. */
    [[nodiscard]] std::size_t countOccurrences(std::string_view haystack, std::string_view needle);

}  // namespace source_file_test_helpers
