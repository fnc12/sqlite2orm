#pragma once

#include <sqlite2orm/ast.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sqlite2orm {

    /**
     *  The digit-level readers of numeric literal text, defined in `codegen_literals.cpp` and
     *  shared with the code that types values and reads PRAGMA arguments. Each is documented at
     *  its definition.
     */

    bool isDigit(char character);
    bool isHexDigit(char character);
    int hexDigitValue(char character);
    std::string significantDigits(std::string_view digits);
    bool hexLiteralIsUnsignedInCpp(std::string_view integerLiteral);
    double decimalLiteralDoubleValue(std::string_view decimalLiteral);
    std::string_view numericLiteralText(const AstNode& literal);
    std::optional<std::int64_t> integerLiteralInt64Value(const AstNode& value);

}  // namespace sqlite2orm
