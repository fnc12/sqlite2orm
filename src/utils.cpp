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

    std::string keywordTypoSuggestion(std::string_view token) {
        // Glued/hyphenated/partial keyword typos → the correct SQLite spelling. Compared
        // case-insensitively; the returned form is what a "did you mean …?" hint should show.
        const std::string normalized = toLowerAscii(token);
        static const std::pair<std::string_view, std::string_view> kTypos[] = {
            {"primary_key", "PRIMARY KEY"},   {"primarykey", "PRIMARY KEY"},
            {"foreign_key", "FOREIGN KEY"},   {"foreignkey", "FOREIGN KEY"},
            {"not_null", "NOT NULL"},         {"notnull", "NOT NULL"},
            {"unique_key", "UNIQUE"},
            {"auto_increment", "AUTOINCREMENT"}, {"auto-increment", "AUTOINCREMENT"},
            {"exist", "EXISTS"},
            {"defualt", "DEFAULT"},           {"referances", "REFERENCES"},
        };
        for(const auto& [typo, fix] : kTypos) {
            if(normalized == typo) {
                return std::string(fix);
            }
        }
        return {};
    }

}  // namespace sqlite2orm
