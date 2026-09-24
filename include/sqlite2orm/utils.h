#pragma once

#include <cstddef>
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

    /** One character read off the front of some text, as `decodeUtf8Character` reads it. */
    struct Utf8Character {
        /**
         *  The code point the bytes stand for; the byte itself when they stand for none, i.e.
         *  when `valid` is false.
         */
        char32_t codePoint = 0;
        /** How many bytes the character takes, never 0 for a non-empty text and never past its end. */
        std::size_t length = 1;
        /** Whether the bytes are a well-formed UTF-8 character rather than a byte standing alone. */
        bool valid = false;
    };

    /**
     *  The character `text` begins with. Well-formed UTF-8 is decoded as one character however
     *  many bytes it takes; anything else — a truncated sequence, an overlong one, a surrogate, a
     *  byte past the U+10FFFF range, a continuation byte standing alone — is answered as that one
     *  byte with `valid` false, because SQLite takes any byte from 0x80 up in an identifier
     *  whether or not the text around it is valid UTF-8, and so does this tokenizer. `text` must
     *  not be empty.
     */
    Utf8Character decodeUtf8Character(std::string_view text);

}  // namespace sqlite2orm
