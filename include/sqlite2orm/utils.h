#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace sqlite2orm {

    std::string toLowerAscii(std::string_view input);
    std::string stripSqlQuotes(std::string_view identifier);
    std::string normalizeSqlName(std::string_view identifier);

    /**
     *  The SQL string literal for what an identifier stands for where SQLite reads no name: the
     *  quotes come off, a quote doubled inside a quoted identifier collapses, and the text is
     *  written back between single quotes. `abc`, `"abc"`, `` `abc` `` and `[abc]` all give
     *  `'abc'`, and `"a""b"` gives `'a"b'`.
     */
    std::string identifierAsStringLiteral(std::string_view identifier);

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

    /** True for a byte that continues a multi-byte UTF-8 character rather than starting one. */
    bool isUtf8ContinuationByte(char byte);

    /**
     *  Characters `text` is written with: one Unicode code point counts once however many UTF-8
     *  bytes it takes, so the count is what a consumer holding the same text as characters
     *  measures — `CodegenWarning::length` is in this unit. Input that is not valid UTF-8 is
     *  counted byte by byte except for the bytes that continue a character, which never makes the
     *  count exceed `text.size()`.
     */
    std::size_t utf8CharacterCount(std::string_view text);

}  // namespace sqlite2orm
