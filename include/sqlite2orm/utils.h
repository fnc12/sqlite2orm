#pragma once

#include <string>
#include <string_view>

namespace sqlite2orm {

    std::string toLowerAscii(std::string_view input);
    std::string stripSqlQuotes(std::string_view identifier);
    std::string normalizeSqlName(std::string_view identifier);

    /**
     *  Correct spelling for a recognized glued/hyphenated/partial keyword typo
     *  (e.g. `PRIMARY_KEY` → `PRIMARY KEY`, `EXIST` → `EXISTS`), or empty if the token
     *  is not a known typo. Used to attach a "did you mean …?" suggestion to an error.
     */
    std::string keywordTypoSuggestion(std::string_view token);

    /**
     *  The literal with SQLite's `_` digit separators taken out, which is how SQLite spells a
     *  numeric literal back in its error messages.
     */
    std::string withoutDigitSeparators(std::string_view numericLiteral);

    /**
     *  True for a hexadecimal integer literal standing for INT64_MIN (`0x8000000000000000`), the
     *  one value whose negation SQLite rejects with `hex literal too big`.
     */
    bool hexLiteralIsInt64Min(std::string_view integerLiteral);

}  // namespace sqlite2orm
