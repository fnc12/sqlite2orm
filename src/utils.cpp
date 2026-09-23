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

    std::string identifierAsStringLiteral(std::string_view identifier) {
        const std::string body = stripSqlQuotes(identifier);
        // The quote an identifier is written with stands for itself inside it when it is doubled;
        // a bracketed identifier has no escape at all, the first `]` ending it.
        const bool quoted = body.size() + 2 == identifier.size();
        const char quote = quoted && identifier.front() != '[' ? identifier.front() : '\0';
        std::string literal = "'";
        for (size_t index = 0; index < body.size(); ++index) {
            if (body[index] == quote && index + 1 < body.size() && body[index + 1] == quote) {
                ++index;
            }
            literal += body[index];
            if (body[index] == '\'') {
                literal += '\'';
            }
        }
        literal += "'";
        return literal;
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

    Utf8Character decodeUtf8Character(std::string_view text) {
        const auto byteAt = [&text](std::size_t index) {
            return static_cast<unsigned char>(text[index]);
        };
        const unsigned char lead = byteAt(0);
        if (lead < 0x80u) {
            return Utf8Character{lead, 1, true};
        }
        // The ranges the second byte is checked against are what rules out the sequences UTF-8
        // has more than one spelling for (an overlong `0xC0 0x80`), the surrogate halves, which
        // are code points no character is written with, and everything past U+10FFFF.
        std::size_t length = 0;
        char32_t codePoint = 0;
        unsigned char secondByteFirst = 0x80u;
        unsigned char secondByteLast = 0xBFu;
        if (lead >= 0xC2u && lead <= 0xDFu) {
            length = 2;
            codePoint = lead & 0x1Fu;
        } else if (lead >= 0xE0u && lead <= 0xEFu) {
            length = 3;
            codePoint = lead & 0x0Fu;
            if (lead == 0xE0u) {
                secondByteFirst = 0xA0u;
            } else if (lead == 0xEDu) {
                secondByteLast = 0x9Fu;
            }
        } else if (lead >= 0xF0u && lead <= 0xF4u) {
            length = 4;
            codePoint = lead & 0x07u;
            if (lead == 0xF0u) {
                secondByteFirst = 0x90u;
            } else if (lead == 0xF4u) {
                secondByteLast = 0x8Fu;
            }
        } else {
            return Utf8Character{lead, 1, false};
        }
        if (text.size() < length) {
            return Utf8Character{lead, 1, false};
        }
        for (std::size_t index = 1; index < length; ++index) {
            const unsigned char byte = byteAt(index);
            const unsigned char first = index == 1 ? secondByteFirst : 0x80u;
            const unsigned char last = index == 1 ? secondByteLast : 0xBFu;
            if (byte < first || byte > last) {
                return Utf8Character{lead, 1, false};
            }
            codePoint = (codePoint << 6) | (byte & 0x3Fu);
        }
        return Utf8Character{codePoint, length, true};
    }

}  // namespace sqlite2orm
