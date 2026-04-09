#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    std::string toLowerAscii(std::string_view input) {
        std::string result(input);
        for(auto& c : result) {
            if(c >= 'A' && c <= 'Z') {
                c = c - 'A' + 'a';
            }
        }
        return result;
    }

    std::string stripSqlQuotes(std::string_view identifier) {
        if(identifier.size() >= 2) {
            char first = identifier.front();
            char last = identifier.back();
            if((first == '"' && last == '"') || (first == '\'' && last == '\'') || (first == '`' && last == '`') ||
               (first == '[' && last == ']')) {
                return std::string(identifier.substr(1, identifier.size() - 2));
            }
        }
        return std::string(identifier);
    }

    std::string normalizeSqlName(std::string_view identifier) {
        return toLowerAscii(stripSqlQuotes(identifier));
    }

}  // namespace sqlite2orm
