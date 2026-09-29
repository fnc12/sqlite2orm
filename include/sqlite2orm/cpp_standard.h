#pragma once

#include <optional>
#include <string_view>

namespace sqlite2orm {

    /** The spellings `--std` accepts, in the order the CLI lists them. */
    inline constexpr std::string_view kCppStandardChoices = "14, 17, 20, 26";

    /**
     *  The C++ standard `text` names as a `CodeGenPolicy::targetCppStandard` value, or nothing when it
     *  is not one of `kCppStandardChoices` spelled exactly: a typo such as `--std 11` or `--std c++20`
     *  is refused rather than quietly generating for the default standard.
     */
    std::optional<int> parseCppStandard(std::string_view text);

}  // namespace sqlite2orm
