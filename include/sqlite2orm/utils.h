#pragma once

#include <string>
#include <string_view>

namespace sqlite2orm {

    inline std::string toLowerAscii(std::string_view input) {
        std::string result(input);
        for(auto& c : result) {
            if(c >= 'A' && c <= 'Z') {
                c = c - 'A' + 'a';
            }
        }
        return result;
    }

}  // namespace sqlite2orm
