#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    std::string toLowerAscii(std::string_view input) {
        std::string result(input);
        for (auto& c: result) {
            if (c >= 'A' && c <= 'Z') {
                c = c - 'A' + 'a';
            }
        }
        return result;
    }

    std::string stripSqlQuotes(std::string_view identifier) {
        if (identifier.size() >= 2) {
            char first = identifier.front();
            char last = identifier.back();
            if ((first == '"' && last == '"') || (first == '\'' && last == '\'') || (first == '`' && last == '`') ||
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
            {"primary_key", "PRIMARY KEY"},
            {"primarykey", "PRIMARY KEY"},
            {"foreign_key", "FOREIGN KEY"},
            {"foreignkey", "FOREIGN KEY"},
            {"not_null", "NOT NULL"},
            {"notnull", "NOT NULL"},
            {"unique_key", "UNIQUE"},
            {"auto_increment", "AUTOINCREMENT"},
            {"auto-increment", "AUTOINCREMENT"},
            {"exist", "EXISTS"},
            {"defualt", "DEFAULT"},
            {"referances", "REFERENCES"},
        };
        for (const auto& [typo, fix]: kTypos) {
            if (normalized == typo) {
                return std::string(fix);
            }
        }
        return {};
    }

    std::string withoutDigitSeparators(std::string_view numericLiteral) {
        std::string result;
        result.reserve(numericLiteral.size());
        for (char character: numericLiteral) {
            if (character != '_') {
                result += character;
            }
        }
        return result;
    }

    bool hexLiteralIsInt64Min(std::string_view integerLiteral) {
        if (integerLiteral.size() < 3 || integerLiteral.front() != '0' ||
            (integerLiteral[1] != 'x' && integerLiteral[1] != 'X')) {
            return false;
        }
        // Neither the separators nor the leading zeros carry any value, so `0x0_8000_0000_0000_0000`
        // names INT64_MIN just as `0x8000000000000000` does.
        std::string digits;
        for (char character: integerLiteral.substr(2)) {
            if (character != '_' && (character != '0' || !digits.empty())) {
                digits += character;
            }
        }
        return digits == "8000000000000000";
    }

    bool isUtf8ContinuationByte(char byte) {
        return (static_cast<unsigned char>(byte) & 0xC0u) == 0x80u;
    }

    std::size_t utf8CharacterCount(std::string_view text) {
        std::size_t count = 0;
        for (const char byte: text) {
            if (!isUtf8ContinuationByte(byte)) {
                ++count;
            }
        }
        return count;
    }

}  // namespace sqlite2orm
