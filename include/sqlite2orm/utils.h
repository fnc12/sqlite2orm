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

}  // namespace sqlite2orm
