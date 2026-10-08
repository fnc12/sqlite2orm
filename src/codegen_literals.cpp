#include "codegen_utils.h"
#include "codegen_literal_digits.h"

#include <sqlite2orm/utils.h>

#include <algorithm>
#include <cctype>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace sqlite2orm {

    bool blobLiteralIsEmpty(std::string_view blobLiteral) {
        return blobLiteral.size() <= 3;
    }

    std::string blobToCpp(std::string_view blobLiteral) {
        auto hex = blobLiteral.substr(2, blobLiteral.size() - 3);
        if (hex.empty()) {
            return "std::vector<char>{}";
        }
        std::string result = "std::vector<char>{";
        for (size_t index = 0; index < hex.size(); index += 2) {
            if (index > 0)
                result += ", ";
            result += "'\\x";
            result += hex[index];
            if (index + 1 < hex.size())
                result += hex[index + 1];
            result += "'";
        }
        result += "}";
        return result;
    }

    std::string numericLiteralToCpp(std::string_view numericLiteral) {
        std::string result;
        result.reserve(numericLiteral.size());
        for (char character: numericLiteral) {
            result += character == '_' ? '\'' : character;
        }
        return result;
    }

    bool isDigit(char character) {
        return std::isdigit(static_cast<unsigned char>(character)) != 0;
    }

    bool isHexDigit(char character) {
        return std::isxdigit(static_cast<unsigned char>(character)) != 0;
    }

    int hexDigitValue(char character) {
        return isDigit(character) ? character - '0' : (character | 0x20) - 'a' + 10;
    }

    // Neither the `_` separators SQLite allows between digits nor the leading zeros carry any
    // value, so both go away before a literal is measured against the int64 range.
    std::string significantDigits(std::string_view digits) {
        std::string result;
        result.reserve(digits.size());
        for (char character: digits) {
            if (character != '_' && (character != '0' || !result.empty())) {
                result += character;
            }
        }
        return result;
    }

    // SQLite reads every hex literal as a signed 64-bit integer, while C++ gives one the
    // first type of `int`, `unsigned int`, `long`, ... it fits in. Two spans of the range end
    // up unsigned there: the 8 digits of `0xDEADBEEF` overflow an `int` and land in an
    // `unsigned int`, and the 16 digits of `0xFFFFFFFFFFFFFFFF` overflow an `int64_t` and land
    // in an `unsigned long`. Anything between the two spans stays signed, and a seventeenth
    // digit `hexLiteralExceedsInt64` catches before the literal gets here.
    bool hexLiteralIsUnsignedInCpp(std::string_view integerLiteral) {
        const std::string digits = significantDigits(integerLiteral.substr(2));
        return (digits.size() == 8 || digits.size() == 16) && digits.front() >= '8';
    }

    /**
     *  The double the text of a decimal literal reads as, which is what SQLite computes with
     *  and what a C++ compiler makes of the same spelling.
     *
     *  `std::strtod` reads the radix character of the current `LC_NUMERIC`, which a host
     *  embedding this library leaves wherever its own startup put it, so the `.` SQLite spells
     *  a literal with is rewritten to whatever that locale expects. Without it a locale that
     *  separates with a comma stops the parse at the `.`, and `1.0e400` reads back as a finite
     *  1 — the answer the callers here rest on not getting. Overflowing to an infinity is
     *  `strtod`'s own answer and the one they ask about; a stream extraction would report a
     *  failure and the largest finite double instead.
     */
    double decimalLiteralDoubleValue(std::string_view decimalLiteral) {
        const std::string_view radix = std::localeconv()->decimal_point;
        std::string digits;
        digits.reserve(decimalLiteral.size());
        for (char character: decimalLiteral) {
            // The `_` separators SQLite allows between digits carry no value.
            if (character == '_') {
                continue;
            }
            if (character == '.') {
                digits += radix;
            } else {
                digits += character;
            }
        }
        return std::strtod(digits.c_str(), nullptr);
    }

    namespace {

        // The decimal text SQLite renders the value of an integer literal as, which is what its
        // JSON path expansion is built from: a hexadecimal literal is a signed 64-bit integer
        // there, `0xFFFFFFFFFFFFFFFF` wrapping around to -1, and a folded minus sign negates the
        // magnitude the digits spell. The caller keeps a literal past the int64 range out: SQLite
        // reads that one as a REAL, and a REAL is no array index.
        std::string integerLiteralDecimalText(std::string_view integerLiteral, bool negated) {
            const bool hexadecimal = isHexadecimalIntegerLiteral(integerLiteral);
            const std::string digits = significantDigits(integerLiteral.substr(hexadecimal ? 2 : 0));
            std::uint64_t magnitude = 0;
            for (const char digit: digits) {
                magnitude = magnitude * (hexadecimal ? 16 : 10) + static_cast<std::uint64_t>(hexDigitValue(digit));
            }
            const std::int64_t value =
                negated ? static_cast<std::int64_t>(~magnitude + 1) : static_cast<std::int64_t>(magnitude);
            return std::to_string(value);
        }

        // Whether the significand of a literal names a value of its own: `1e-400` underflows to a
        // zero and `0.0e999` is written as one, and only the first of the two is a rounded value.
        bool significandIsNonZero(std::string_view decimalLiteral) {
            for (char character: decimalLiteral) {
                if (character == 'e' || character == 'E') {
                    break;
                }
                if (isDigit(character) && character != '0') {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    bool isHexadecimalIntegerLiteral(std::string_view integerLiteral) {
        return integerLiteral.size() > 1 && integerLiteral.front() == '0' &&
               (integerLiteral[1] == 'x' || integerLiteral[1] == 'X');
    }

    bool hexLiteralExceedsInt64(std::string_view integerLiteral) {
        return isHexadecimalIntegerLiteral(integerLiteral) && significantDigits(integerLiteral.substr(2)).size() > 16;
    }

    bool integerLiteralExceedsInt64(std::string_view integerLiteral, bool negated) {
        if (isHexadecimalIntegerLiteral(integerLiteral)) {
            // A hex literal never leaves the range: SQLite wraps it around, and the one that
            // would need a seventeenth digit is `hexLiteralExceedsInt64`, refused before this.
            return false;
        }
        // SQLite decides the type of a decimal literal with the sign already folded in, the way
        // `codeInteger()` does, so the magnitude an int64 holds is one larger for a negative one:
        // `-9223372036854775808` is an INTEGER while `9223372036854775808` is a REAL.
        static constexpr std::string_view int64Max = "9223372036854775807";
        static constexpr std::string_view int64MinMagnitude = "9223372036854775808";
        const std::string_view limit = negated ? int64MinMagnitude : int64Max;
        const std::string digits = significantDigits(integerLiteral);
        return digits.size() > limit.size() || (digits.size() == limit.size() && std::string_view(digits) > limit);
    }

    const AstNode* withoutFoldedSigns(const AstNode& value, std::size_t& foldedMinusSigns) {
        foldedMinusSigns = 0;
        const AstNode* node = &value;
        while (auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(node)) {
            const bool sign = unaryOperator->unaryOperator == UnaryOperator::minus ||
                              unaryOperator->unaryOperator == UnaryOperator::plus;
            if (!sign || !unaryOperator->operand) {
                break;
            }
            if (unaryOperator->unaryOperator == UnaryOperator::minus) {
                ++foldedMinusSigns;
            }
            node = unaryOperator->operand.get();
        }
        return node;
    }

    /** The source text of a numeric literal node, or an empty view for any other node. */
    std::string_view numericLiteralText(const AstNode& literal) {
        if (auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&literal)) {
            return integerLiteral->value;
        }
        if (auto* realLiteral = dynamic_cast<const RealLiteralNode*>(&literal)) {
            return realLiteral->value;
        }
        return {};
    }

    bool isIntegerLiteralPastIntegerFieldRange(const AstNode& value) {
        std::size_t foldedSigns = 0;
        auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(withoutFoldedSigns(value, foldedSigns));
        if (integerLiteral == nullptr) {
            return false;
        }
        if (integerLiteralExceedsInt64(integerLiteral->value, foldedSigns % 2 != 0)) {
            return true;
        }
        // Only the innermost sign folds into the literal; SQLite negates the value the rest of
        // them stand on while it runs the statement, and negating the int64 minimum leaves the
        // integer range: `SELECT typeof(-9223372036854775808)` is integer, while
        // `typeof(-(-9223372036854775808))` is real.
        return foldedSigns > 1 && integerLiteralExceedsInt64(integerLiteral->value);
    }

    std::string numericLiteralSqlText(const AstNode& value) {
        std::size_t foldedSigns = 0;
        const std::string_view text = numericLiteralText(*withoutFoldedSigns(value, foldedSigns));
        if (text.empty()) {
            return {};
        }
        return (foldedSigns % 2 != 0 ? "-" : "") + withoutDigitSeparators(text);
    }

    CodegenWarning numericLiteralWarning(std::string message, const AstNode& value) {
        std::size_t foldedSigns = 0;
        const AstNode& literal = *withoutFoldedSigns(value, foldedSigns);
        const std::string_view text = numericLiteralText(literal);
        if (text.empty()) {
            return CodegenWarning{std::move(message)};
        }
        SourceLocation location = literal.location;
        size_t length = underlineLengthOf(text);
        // The minus signs the message quotes along with the digits stand in front of the literal,
        // so the underline starts at the value rather than at the token it ends with.
        if (value.location.line == location.line && value.location.column < location.column) {
            length = location.column + length - value.location.column;
            location = value.location;
        }
        return CodegenWarning{std::move(message), location, length};
    }

    bool integerFieldCarriesValue(const AstNode& value) {
        std::size_t foldedSigns = 0;
        if (dynamic_cast<const RealLiteralNode*>(withoutFoldedSigns(value, foldedSigns))) {
            // SQLite turns a REAL into an INTEGER only where the column affinity converts it back
            // and forth without loss, while a C++ field truncates it either way: `1.5` would reach
            // an int64_t field as 1 where SQLite keeps the 1.5 it stored.
            return false;
        }
        return !isIntegerLiteralPastIntegerFieldRange(value);
    }

    /**
     *  The int64 the integer literal `value` denotes, or nothing for any other node and for a
     *  decimal literal past the int64 range. SQLite reads a hexadecimal literal as a signed
     *  64-bit integer and wraps it around, so `0xFFFFFFFFFFFFFFFF` is -1.
     */
    std::optional<std::int64_t> integerLiteralInt64Value(const AstNode& value) {
        std::size_t foldedSigns = 0;
        const auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(withoutFoldedSigns(value, foldedSigns));
        const bool negated = foldedSigns % 2 != 0;
        if (integerLiteral == nullptr || integerLiteralExceedsInt64(integerLiteral->value, negated)) {
            return std::nullopt;
        }
        const std::string_view text = integerLiteral->value;
        const bool hexadecimal = isHexadecimalIntegerLiteral(text);
        std::uint64_t magnitude = 0;
        for (char character: hexadecimal ? text.substr(2) : text) {
            if (character == '_') {
                continue;
            }
            magnitude = hexadecimal ? magnitude * 16 + static_cast<std::uint64_t>(hexDigitValue(character))
                                    : magnitude * 10 + static_cast<std::uint64_t>(character - '0');
        }
        // The magnitude of the int64 minimum does not fit an int64, so the sign folds in on
        // the unsigned value, where the wraparound is the one SQLite performs anyway.
        return static_cast<std::int64_t>(negated ? ~magnitude + 1 : magnitude);
    }

    bool doubleFieldCarriesValue(const AstNode& value) {
        const std::optional<std::int64_t> number = integerLiteralInt64Value(value);
        if (!number) {
            // A REAL literal initializes a `double` field as itself, and a decimal literal past
            // the int64 range is generated as a REAL, so neither of them narrows.
            return true;
        }
        const double asDouble = static_cast<double>(*number);
        static constexpr double int64Limit = 9223372036854775808.0;
        // `static_cast<double>(INT64_MAX)` rounds up to the limit itself, which no int64 holds.
        if (asDouble < -int64Limit || asDouble >= int64Limit) {
            return false;
        }
        return static_cast<std::int64_t>(asDouble) == *number;
    }

    bool boolFieldCarriesValue(const AstNode& value) {
        if (dynamic_cast<const BoolLiteralNode*>(&value)) {
            // SQLite stores TRUE and FALSE as the integers 1 and 0, the whole range of the field.
            return true;
        }
        const std::optional<std::int64_t> number = integerLiteralInt64Value(value);
        return number && (*number == 0 || *number == 1);
    }

    ValueStorageClass valueStorageClass(const AstNode& value) {
        if (dynamic_cast<const NullLiteralNode*>(&value)) {
            return ValueStorageClass::null;
        }
        if (dynamic_cast<const StringLiteralNode*>(&value)) {
            return ValueStorageClass::text;
        }
        if (dynamic_cast<const BlobLiteralNode*>(&value)) {
            return ValueStorageClass::blob;
        }
        if (dynamic_cast<const BoolLiteralNode*>(&value)) {
            // SQLite reads TRUE and FALSE as the integers 1 and 0.
            return ValueStorageClass::numeric;
        }
        // A sign folds into a number only; in front of anything else it is an operator SQLite
        // computes, and the generated code spells that operator out rather than a value.
        std::size_t foldedSigns = 0;
        const AstNode& literal = *withoutFoldedSigns(value, foldedSigns);
        if (dynamic_cast<const IntegerLiteralNode*>(&literal) || dynamic_cast<const RealLiteralNode*>(&literal)) {
            return ValueStorageClass::numeric;
        }
        return ValueStorageClass::unknown;
    }

    ValueStorageClass fieldTypeStorageClass(std::string_view cppType) {
        // These are the types `sqliteTypeToCpp` gives a table column; a mapping added there and
        // left out here falls into `unknown`, which lets every value through the object form.
        if (cppType == "int64_t" || cppType == "int" || cppType == "double" || cppType == "bool") {
            return ValueStorageClass::numeric;
        }
        if (cppType == "std::string") {
            return ValueStorageClass::text;
        }
        if (cppType == "std::vector<char>") {
            return ValueStorageClass::blob;
        }
        return ValueStorageClass::unknown;
    }

    bool integerLiteralExceedsInt32(std::string_view integerLiteral, bool negated) {
        if (isHexadecimalIntegerLiteral(integerLiteral)) {
            // SQLite reads a hex literal as a signed 64-bit integer and wraps it around, so the
            // digits alone say which side of the int32 range the value lands on: up to
            // `0x7FFFFFFF` it is a positive int32, from `0xFFFFFFFF80000000` on it has wrapped
            // back into one as a negative number, and everything in between needs an int64.
            // A folded-in minus sign slides that window by one, the same way it does for a
            // decimal magnitude: `-0x80000000` is -2147483648 and an int32 holds it, while
            // `-0xFFFFFFFF80000000` is 2147483648 and one does not.
            static constexpr std::string_view wrappedInt32Min = "ffffffff80000000";
            static constexpr std::string_view negatedWrappedInt32Max = "ffffffff80000001";
            static constexpr std::string_view hexInt32MinMagnitude = "80000000";
            const std::string digits = significantDigits(integerLiteral.substr(2));
            if (digits.size() < 8) {
                return false;
            }
            const std::string lowered = toLowerAscii(digits);
            if (digits.size() == 8) {
                return negated ? std::string_view(lowered) > hexInt32MinMagnitude : digits.front() >= '8';
            }
            if (digits.size() == 16) {
                return std::string_view(lowered) < (negated ? negatedWrappedInt32Max : wrappedInt32Min);
            }
            // Nine to fifteen digits are past `0xFFFFFFFF`, and a seventeenth one is past an
            // int64 altogether — `hexLiteralExceedsInt64` refuses that where it is compiled.
            return true;
        }
        static constexpr std::string_view int32Max = "2147483647";
        static constexpr std::string_view int32MinMagnitude = "2147483648";
        const std::string_view limit = negated ? int32MinMagnitude : int32Max;
        const std::string digits = significantDigits(integerLiteral);
        return digits.size() > limit.size() || (digits.size() == limit.size() && std::string_view(digits) > limit);
    }

    bool numericLiteralGeneratesInfinity(std::string_view numericLiteral) {
        // SQLite reads every hex literal as a signed 64-bit integer and refuses a seventeenth
        // digit, so no value of one is ever past the range of a double.
        return !isHexadecimalIntegerLiteral(numericLiteral) && std::isinf(decimalLiteralDoubleValue(numericLiteral));
    }

    std::string realLiteralToCpp(std::string_view realLiteral) {
        if (numericLiteralGeneratesInfinity(realLiteral)) {
            return std::string(kInfinityCppExpression);
        }
        // A value too small for a double rounds to a zero, which is the value SQLite reads as
        // well — `SELECT 1e-400` answers 0.0 — and which C++ warns about in the same breath as an
        // overflow (`floating constant truncated to zero`). A literal written as a zero keeps its
        // own spelling: there is nothing rounded about it.
        if (decimalLiteralDoubleValue(realLiteral) == 0.0 && significandIsNonZero(realLiteral)) {
            return "0.0";
        }
        return numericLiteralToCpp(realLiteral);
    }

    std::string integerLiteralToCpp(std::string_view integerLiteral) {
        // A hexadecimal literal denotes the same number in both languages, but a decimal one with
        // leading zeros does not: SQLite reads `010` as 10, C++ as octal 8, and `0009` does not
        // compile at all. The separators standing between those zeros go away with them, so that
        // `0_9` becomes `9` rather than the octal constant `0'9`.
        if (isHexadecimalIntegerLiteral(integerLiteral)) {
            // Where C++ picks an unsigned type for the literal the two languages part ways again:
            // `0xFFFFFFFFFFFFFFFF` means -1 to SQLite and 18446744073709551615 to C++, and even
            // where the value itself survives, as it does for `0xDEADBEEF`, an unsigned operand
            // drags the rest of the expression along with it, so that `-1 > 0xDEADBEEF` is true in
            // C++ and false in SQLite. The cast restores the type SQLite gives the literal.
            if (hexLiteralIsUnsignedInCpp(integerLiteral)) {
                return "static_cast<int64_t>(" + numericLiteralToCpp(integerLiteral) + ")";
            }
            return numericLiteralToCpp(integerLiteral);
        }
        size_t firstSignificant = 0;
        while (firstSignificant + 1 < integerLiteral.size() &&
               (integerLiteral[firstSignificant] == '0' || integerLiteral[firstSignificant] == '_')) {
            ++firstSignificant;
        }
        const std::string_view significant = integerLiteral.substr(firstSignificant);
        // A decimal literal that does not fit in an int64 is a REAL for SQLite, which reads
        // `9223372036854775808` back as 9.22337203685478e+18. C++ would make that spelling an
        // unsigned constant, and `99999999999999999999` does not fit in any integer type at all,
        // so the fractional part turns it into the double SQLite computes.
        if (integerLiteralExceedsInt64(significant)) {
            // Far enough past the int64 range the double runs out too — a 1 followed by 309 zeros
            // is an Inf to SQLite — and C++ has no literal for that value either.
            if (numericLiteralGeneratesInfinity(significant)) {
                return std::string(kInfinityCppExpression);
            }
            return numericLiteralToCpp(significant) + ".0";
        }
        return numericLiteralToCpp(significant);
    }

    std::optional<std::string> jsonArrowPathExpansion(const AstNode& pathOperand) {
        // A COLLATE and a unary plus stand for the value under them, which is the value the
        // operator expands.
        const AstNode& written = generatedOperandNode(pathOperand);
        if (auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(&written)) {
            const std::string label = sqlStringLiteralText(stringLiteral->value);
            if (!label.empty() && label.front() == '$') {
                return label;
            }
            // A label of nothing but ASCII letters, digits and `_` needs no quoting, and an empty
            // one takes this branch as well: `$.` is what SQLite builds for it, and what it then
            // refuses as a bad JSON path — as it refuses the bare `''` the operator was written
            // with, only naming the other of the two in the message.
            const bool unquotable = std::all_of(label.begin(), label.end(), [](char character) {
                return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') || character == '_';
            });
            if (unquotable) {
                return "$." + label;
            }
            if (label.size() >= 3 && label.front() == '[' && label.back() == ']') {
                return "$" + label;
            }
            // The label goes in quoted and unescaped, exactly as SQLite pastes it in, so a label
            // holding a quote of its own builds the same path here as it does there.
            return "$.\"" + label + "\"";
        }
        if (auto* boolLiteral = dynamic_cast<const BoolLiteralNode*>(&written)) {
            // `TRUE` and `FALSE` are the integers 1 and 0, and an integer is an array index.
            return boolLiteral->value ? "$[1]" : "$[0]";
        }
        std::size_t foldedSigns = 0;
        const AstNode& signless = *withoutFoldedSigns(written, foldedSigns);
        auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&signless);
        if (integerLiteral == nullptr) {
            return std::nullopt;
        }
        const bool negated = foldedSigns % 2 != 0;
        // Past the int64 range the literal is a REAL for SQLite, which expands as a label rather
        // than as an index, spelled the way SQLite renders a REAL as text. That rendering is not
        // reproduced here, so the operand is left to the caller to report.
        if (integerLiteralExceedsInt64(integerLiteral->value, negated) ||
            hexLiteralExceedsInt64(integerLiteral->value)) {
            return std::nullopt;
        }
        const std::string decimal = integerLiteralDecimalText(integerLiteral->value, negated);
        // SQLite counts a negative index from the right of the array, which `$[#-N]` spells.
        return "$[" + std::string(decimal.front() == '-' ? "#" : "") + decimal + "]";
    }

}  // namespace sqlite2orm
