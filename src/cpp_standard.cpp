#include <sqlite2orm/cpp_standard.h>

namespace sqlite2orm {

    std::optional<int> parseCppStandard(std::string_view text) {
        if (text == "14") {
            return 14;
        }
        if (text == "17") {
            return 17;
        }
        if (text == "20") {
            return 20;
        }
        if (text == "26") {
            return 26;
        }
        return std::nullopt;
    }

}  // namespace sqlite2orm
