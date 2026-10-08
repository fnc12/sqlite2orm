#include "codegen_utils.h"
#include "codegen_literal_digits.h"

#include <sqlite2orm/utils.h>

#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace sqlite2orm {

    std::optional<std::string> journalModeSqlTokenToCppEnum(std::string_view token) {
        const std::string lower = toLowerAscii(stripIdentifierQuotes(token));
        // `DELETE_` rather than `DELETE`: the Windows SDK defines `DELETE` as a macro, and
        // sqlite_orm's journal_mode header only hides it while the enum is being declared, so in a
        // user translation unit that included <windows.h> the name is a macro again. sqlite_orm
        // declares `DELETE_ = DELETE` for exactly that, and it spells the same value everywhere.
        if (lower == "delete")
            return std::string{"sqlite_orm::journal_mode::DELETE_"};
        if (lower == "truncate")
            return std::string{"sqlite_orm::journal_mode::TRUNCATE"};
        if (lower == "persist")
            return std::string{"sqlite_orm::journal_mode::PERSIST"};
        if (lower == "memory")
            return std::string{"sqlite_orm::journal_mode::MEMORY"};
        if (lower == "wal")
            return std::string{"sqlite_orm::journal_mode::WAL"};
        if (lower == "off")
            return std::string{"sqlite_orm::journal_mode::OFF"};
        return std::nullopt;
    }

    std::optional<std::string> lockingModeSqlTokenToCppEnum(std::string_view token) {
        const std::string lower = toLowerAscii(stripIdentifierQuotes(token));
        if (lower == "normal")
            return std::string{"sqlite_orm::locking_mode::NORMAL"};
        if (lower == "exclusive")
            return std::string{"sqlite_orm::locking_mode::EXCLUSIVE"};
        return std::nullopt;
    }

    namespace {

        /**
         *  The name a keyword PRAGMA value reads as. SQLite's `nmnum` rule falls `ON`, `TRUE`,
         *  `FALSE` and `CURRENT_DATE` and its siblings back to a bare name, and the PRAGMA's reader
         *  gets those very letters — so `PRAGMA integrity_check(on)` looks a table called `on` up,
         *  it does not pass a boolean on.
         */
        std::optional<std::string> pragmaKeywordValueName(const AstNode& valueNode) {
            if (const auto* boolLiteral = dynamic_cast<const BoolLiteralNode*>(&valueNode)) {
                return std::string(boolLiteral->spelling);
            }
            if (const auto* currentDatetime = dynamic_cast<const CurrentDatetimeLiteralNode*>(&valueNode)) {
                return currentDatetime->spelling;
            }
            return std::nullopt;
        }

    }  // namespace

    std::optional<std::string> pragmaTableNameLiteral(const AstNode& valueNode) {
        if (const auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(&valueNode)) {
            return sqlStringToCpp(stringLiteral->value);
        }
        if (const auto* columnRef = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
            return identifierToCppStringLiteral(columnRef->columnName);
        }
        if (auto keywordName = pragmaKeywordValueName(valueNode)) {
            return identifierToCppStringLiteral(*keywordName);
        }
        return std::nullopt;
    }

    std::optional<std::string> pragmaJournalOrLockingValueToken(const AstNode& valueNode) {
        if (const auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(&valueNode)) {
            if (stringLiteral->value.size() >= 2 && stringLiteral->value.front() == '\'' &&
                stringLiteral->value.back() == '\'') {
                return std::string(stringLiteral->value.substr(1, stringLiteral->value.size() - 2));
            }
        }
        if (const auto* columnRef = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
            return std::string(columnRef->columnName);
        }
        return std::nullopt;
    }

    std::optional<std::int32_t> sqlitePragmaInt32(std::string_view valueText) {
        // `sqlite3GetInt32()` takes the sign off first and only then looks for a `0x` prefix, so a
        // signed value never reaches its hexadecimal branch: `-0x10` falls through to the decimal
        // one, which stops at the `x` and answers 0 rather than -16.
        bool negated = false;
        bool hexadecimal = false;
        if (!valueText.empty() && (valueText.front() == '-' || valueText.front() == '+')) {
            negated = valueText.front() == '-';
            valueText.remove_prefix(1);
        } else {
            hexadecimal = valueText.size() > 2 && valueText[0] == '0' && (valueText[1] == 'x' || valueText[1] == 'X') &&
                          isHexDigit(valueText[2]);
        }
        if (hexadecimal) {
            size_t index = 2;
            while (index < valueText.size() && valueText[index] == '0') {
                ++index;
            }
            std::uint32_t value = 0;
            size_t digitCount = 0;
            for (; index < valueText.size() && digitCount < 8 && isHexDigit(valueText[index]); ++index, ++digitCount) {
                value = value * 16 + static_cast<std::uint32_t>(hexDigitValue(valueText[index]));
            }
            // SQLite reads eight hexadecimal digits at most and refuses the value outright once the
            // sign bit is set or a ninth digit follows, rather than wrapping or truncating it.
            if ((value & 0x80000000u) != 0 || (index < valueText.size() && isHexDigit(valueText[index]))) {
                return std::nullopt;
            }
            return static_cast<std::int32_t>(value);
        }
        if (valueText.empty() || !isDigit(valueText.front())) {
            return std::nullopt;
        }
        size_t index = 0;
        while (index < valueText.size() && valueText[index] == '0') {
            ++index;
        }
        std::int64_t value = 0;
        size_t digitCount = 0;
        for (; index < valueText.size() && digitCount < 11 && isDigit(valueText[index]); ++index, ++digitCount) {
            value = value * 10 + (valueText[index] - '0');
        }
        // The digits stop at the first character that is not one, so `1.5` reads as 1, and the
        // magnitude a negative value may reach is one larger: `-2147483648` is an int32, `2147483648`
        // is not.
        if (digitCount > 10 || value - (negated ? 1 : 0) > 2147483647) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(negated ? -value : value);
    }

    bool sqlitePragmaBoolean(std::string_view valueText) {
        if (!valueText.empty() && isDigit(valueText.front())) {
            // SQLite's `getSafetyLevel()` returns a u8, so the int32 loses everything above its low
            // byte before the `!= 0` test: `256` and `65536` are false while `255` and `257` are true.
            return (sqlitePragmaInt32(valueText).value_or(0) & 0xFF) != 0;
        }
        const std::string lower = toLowerAscii(valueText);
        return lower == "on" || lower == "yes" || lower == "true";
    }

    namespace {

        /** SQLite's `sqlite3Isspace()`, the characters `sqlite3Atoi64()` skips past. */
        bool isSqliteSpace(char character) {
            return character == ' ' || character == '\t' || character == '\n' || character == '\v' ||
                   character == '\f' || character == '\r';
        }

        /**
         *  The prefix `sqlite3DecOrHexToI64()` hands on to `sqlite3Atoi64()`: what
         *  `strspn(z, "+- \n\t0123456789")` spans, plus the one character behind it. So the number
         *  is read from a text that stops at the first character none of those are, and `\v`, `\f`
         *  and `\r` are such characters even though `sqlite3Isspace()` calls them spaces.
         */
        std::string_view sqliteNumberPrefix(std::string_view text) {
            constexpr std::string_view spanned = "+- \n\t0123456789";
            size_t length = 0;
            while (length < text.size() && spanned.find(text[length]) != std::string_view::npos) {
                ++length;
            }
            if (length < text.size()) {
                ++length;
            }
            return text.substr(0, length);
        }

        /**
         *  SQLite's `sqlite3DecOrHexToI64()`: a `0x` prefix reads the rest as hexadecimal, and
         *  everything else goes through `sqlite3Atoi64()` over `sqliteNumberPrefix()`, which skips
         *  leading spaces, takes a sign and then digits, and tolerates only spaces behind them.
         *  Nullopt where SQLite refuses the text — anything but digits behind the number, or a
         *  magnitude past the int64 range. The prefix is what makes the two ends differ: a `\v` in
         *  front of the digits cuts them away and leaves nothing to read, while one behind them is
         *  the trailing space `sqlite3Atoi64()` tolerates and everything past it goes unread, so
         *  `'<VT>12'` is refused and `'12<VT>abc'` reads as 12 where `'12abc'` is refused.
         */
        std::optional<std::int64_t> sqliteDecOrHexToInt64(std::string_view text) {
            if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
                size_t index = 2;
                while (index < text.size() && text[index] == '0') {
                    ++index;
                }
                std::uint64_t value = 0;
                size_t digitCount = 0;
                for (; index < text.size() && isHexDigit(text[index]); ++index, ++digitCount) {
                    value = value * 16 + static_cast<std::uint64_t>(hexDigitValue(text[index]));
                }
                // Sixteen significant digits are an int64's worth; a seventeenth, or anything that
                // is no hexadecimal digit at all, and SQLite refuses the value rather than cut it.
                if (index < text.size() || digitCount > 16) {
                    return std::nullopt;
                }
                return static_cast<std::int64_t>(value);
            }
            text = sqliteNumberPrefix(text);
            size_t index = 0;
            while (index < text.size() && isSqliteSpace(text[index])) {
                ++index;
            }
            bool negated = false;
            if (index < text.size() && (text[index] == '-' || text[index] == '+')) {
                negated = text[index] == '-';
                ++index;
            }
            const size_t zerosStart = index;
            while (index < text.size() && text[index] == '0') {
                ++index;
            }
            std::uint64_t value = 0;
            bool overflowed = false;
            size_t digitCount = 0;
            for (; index < text.size() && isDigit(text[index]); ++index, ++digitCount) {
                const std::uint64_t digit = static_cast<std::uint64_t>(text[index] - '0');
                if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
                    overflowed = true;
                }
                value = value * 10 + digit;
            }
            // `0` alone is a number even though it carries no digit past the zeros that were
            // skipped, while an empty text, a lone sign and trailing letters are all refused.
            if (digitCount == 0 && index == zerosStart) {
                return std::nullopt;
            }
            while (index < text.size() && isSqliteSpace(text[index])) {
                ++index;
            }
            if (index < text.size()) {
                return std::nullopt;
            }
            const std::uint64_t magnitudeLimit = negated ? 9223372036854775808ull : 9223372036854775807ull;
            if (overflowed || value > magnitudeLimit) {
                return std::nullopt;
            }
            // Negated through the unsigned two's complement, so that the one magnitude an int64
            // reaches only under a sign — `-9223372036854775808` — does not overflow on its way.
            return negated ? static_cast<std::int64_t>(~value + 1u) : static_cast<std::int64_t>(value);
        }

    }  // namespace

    std::int32_t sqlitePragmaSafetyLevel(std::string_view valueText) {
        if (!valueText.empty() && isDigit(valueText.front())) {
            // `getSafetyLevel()` answers a u8, so everything above the low byte of the int32 goes:
            // `= 260` is the 4 that `= 4` is, and `= 256` the 0 that `= 0` is.
            return sqlitePragmaInt32(valueText).value_or(0) & 0xFF;
        }
        const std::string lower = toLowerAscii(valueText);
        if (lower == "off" || lower == "no" || lower == "false") {
            return 0;
        }
        if (lower == "on" || lower == "yes" || lower == "true") {
            return 1;
        }
        if (lower == "full") {
            return 2;
        }
        if (lower == "extra") {
            return 3;
        }
        return 1;
    }

    std::int32_t sqlitePragmaAutoVacuum(std::string_view valueText) {
        const std::string lower = toLowerAscii(valueText);
        if (lower == "none") {
            return 0;
        }
        if (lower == "full") {
            return 1;
        }
        if (lower == "incremental") {
            return 2;
        }
        const std::int32_t value = sqlitePragmaInt32(valueText).value_or(0);
        return (value >= 0 && value <= 2) ? value : 0;
    }

    std::int64_t sqlitePragmaMaxPageCount(std::string_view valueText) {
        const std::int64_t value = sqliteDecOrHexToInt64(valueText).value_or(0);
        if (value < 0) {
            return 0;
        }
        constexpr std::int64_t limit = 0xfffffffe;
        return value > limit ? limit : value;
    }

    bool isCanonicalPragmaBooleanText(std::string_view valueText) {
        if (valueText == "0" || valueText == "1") {
            return true;
        }
        const std::string lower = toLowerAscii(valueText);
        return lower == "on" || lower == "off" || lower == "yes" || lower == "no" || lower == "true" ||
               lower == "false";
    }

    std::optional<PragmaValue> pragmaValue(const AstNode& valueNode) {
        if (const auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&valueNode)) {
            return PragmaValue{std::string(integerLiteral->value),
                               std::string(integerLiteral->value),
                               integerLiteral->location,
                               underlineLengthOf(integerLiteral->value)};
        }
        if (const auto* realLiteral = dynamic_cast<const RealLiteralNode*>(&valueNode)) {
            return PragmaValue{std::string(realLiteral->value),
                               std::string(realLiteral->value),
                               realLiteral->location,
                               underlineLengthOf(realLiteral->value)};
        }
        if (const auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(&valueNode)) {
            return PragmaValue{sqlStringLiteralText(stringLiteral->value),
                               std::string(stringLiteral->value),
                               stringLiteral->location,
                               underlineLengthOf(stringLiteral->value)};
        }
        if (const auto* columnRef = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
            // A name in double quotes or backquotes escapes its own quote by doubling it, the way a
            // string literal does, so `"a""b"` names `a"b`; a bracketed name has no escape at all.
            const std::string_view name = columnRef->columnName;
            const bool escapesItsQuote = !name.empty() && (name.front() == '"' || name.front() == '`');
            return PragmaValue{escapesItsQuote ? sqlStringLiteralText(name) : stripIdentifierQuotes(name),
                               std::string(columnRef->columnName),
                               columnRef->location,
                               underlineLengthOf(columnRef->columnName)};
        }
        if (auto keywordName = pragmaKeywordValueName(valueNode)) {
            // A keyword here is a name, not the literal it looks like: `TRUE` is the four letters
            // the reader gets and `CURRENT_DATE` is not today's date.
            return PragmaValue{*keywordName, *keywordName, valueNode.location, underlineLengthOf(*keywordName)};
        }
        if (const auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&valueNode)) {
            const bool numericOperand =
                unaryOperator->operand && (dynamic_cast<const IntegerLiteralNode*>(unaryOperator->operand.get()) ||
                                           dynamic_cast<const RealLiteralNode*>(unaryOperator->operand.get()));
            if (unaryOperator->unaryOperator == UnaryOperator::minus && numericOperand) {
                const PragmaValue operand = *pragmaValue(*unaryOperator->operand);
                // The minus sign belongs to the value the message quotes, so the underline starts
                // at the sign — as far as it shares the literal's line, the operand alone otherwise.
                SourceLocation location = operand.location;
                size_t length = operand.length;
                if (unaryOperator->location.line == location.line && unaryOperator->location.column < location.column) {
                    length = location.column + length - unaryOperator->location.column;
                    location = unaryOperator->location;
                }
                return PragmaValue{"-" + operand.text, "-" + operand.sqlText, location, length};
            }
        }
        return std::nullopt;
    }

    CodegenWarning pragmaValueWarning(std::string message, const PragmaValue& value) {
        if (value.length == 0) {
            return CodegenWarning{std::move(message)};
        }
        return CodegenWarning{std::move(message), value.location, value.length};
    }

    CodegenWarning pragmaValueWarning(std::string message, const AstNode& valueNode) {
        const std::optional<PragmaValue> value = pragmaValue(valueNode);
        if (!value) {
            return CodegenWarning{std::move(message)};
        }
        return pragmaValueWarning(std::move(message), *value);
    }

}  // namespace sqlite2orm
