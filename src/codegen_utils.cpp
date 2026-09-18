#include "codegen_utils.h"
#include "codegen_context.h"

#include <sqlite2orm/utils.h>

#include <array>
#include <cctype>
#include <cstdint>

namespace sqlite2orm {

    bool policyEquals(const CodeGenPolicy* policy, std::string_view category, std::string_view value) {
        if(!policy) {
            return false;
        }
        const auto it = policy->chosenAlternativeValueByCategory.find(std::string(category));
        return it != policy->chosenAlternativeValueByCategory.end() && it->second == value;
    }

    CodeGenPolicy policyWithOverride(const CodeGenPolicy* base, std::string_view category, std::string_view value) {
        CodeGenPolicy result;
        if(base) {
            result = *base;
        }
        result.chosenAlternativeValueByCategory[std::string(category)] = std::string(value);
        return result;
    }

    int policyTargetCppStandard(const CodeGenPolicy* policy) {
        return policy ? policy->targetCppStandard : CodeGenPolicy{}.targetCppStandard;
    }

    bool cpp20Allowed(const CodeGenPolicy* policy) {
        return policyTargetCppStandard(policy) >= 20;
    }

    std::string savepointGuardVariableName(std::string_view savepointName) {
        std::string variableName;
        for(const char c : stripIdentifierQuotes(savepointName)) {
            variableName += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
        }
        if(variableName.empty() || std::isdigit(static_cast<unsigned char>(variableName.front()))) {
            variableName.insert(variableName.begin(), '_');
        }
        return variableName + "_savepoint";
    }

    std::string colaliasBuiltinSlot(size_t slotIndex) {
        static constexpr std::array<std::string_view, 9> kBuiltinSlots = {
            "colalias_a{}", "colalias_b{}", "colalias_c{}", "colalias_d{}", "colalias_e{}",
            "colalias_f{}", "colalias_g{}", "colalias_h{}", "colalias_i{}",
        };
        if(slotIndex < kBuiltinSlots.size()) {
            return std::string(kBuiltinSlots[slotIndex]);
        }
        char letter = static_cast<char>('a' + slotIndex);
        if(letter > 'z') {
            letter = 'a';
        }
        return "sqlite_orm::internal::column_alias<'" + std::string(1, letter) + "'>{}";
    }

    std::string stripIdentifierQuotes(std::string_view identifier) {
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

    std::string toCppIdentifier(std::string_view sqlName) {
        auto stripped = stripIdentifierQuotes(sqlName);
        std::string result;
        result.reserve(stripped.size());
        for(char character : stripped) {
            if(std::isalnum(static_cast<unsigned char>(character)) || character == '_') {
                result += character;
            } else {
                result += '_';
            }
        }
        if(!result.empty() && std::isdigit(static_cast<unsigned char>(result[0]))) {
            result = "_" + result;
        }
        return result;
    }

    std::string identifierToCppStringLiteral(std::string_view sqlIdentifier) {
        auto body = stripIdentifierQuotes(sqlIdentifier);
        std::string result = "\"";
        for(char character : body) {
            if(character == '\\') {
                result += "\\\\";
            } else if(character == '"') {
                result += "\\\"";
            } else if(character == '\n') {
                result += "\\n";
            } else if(character == '\r') {
                result += "\\r";
            } else if(character == '\t') {
                result += "\\t";
            } else {
                result += character;
            }
        }
        result += '"';
        return result;
    }

    std::string sqlStringToCpp(std::string_view sqlString) {
        auto content = sqlString.substr(1, sqlString.size() - 2);
        std::string result = "\"";
        for(size_t index = 0; index < content.size(); ++index) {
            char character = content[index];
            if(character == '\'' && index + 1 < content.size() && content[index + 1] == '\'') {
                result += '\'';
                ++index;
            } else if(character == '\\') {
                result += "\\\\";
            } else if(character == '"') {
                result += "\\\"";
            } else if(character == '\n') {
                result += "\\n";
            } else if(character == '\r') {
                result += "\\r";
            } else if(character == '\t') {
                result += "\\t";
            } else {
                result += character;
            }
        }
        result += '"';
        return result;
    }

    std::string stripColumnAliasQuotes(std::string_view alias) {
        if(alias.size() >= 2) {
            char first = alias.front();
            char last = alias.back();
            if((first == '\'' && last == '\'') || (first == '"' && last == '"') || (first == '`' && last == '`') ||
               (first == '[' && last == ']')) {
                return std::string(alias.substr(1, alias.size() - 2));
            }
        }
        return std::string(alias);
    }

    bool isBuiltinColalias(std::string_view stripped) {
        return stripped.size() == 1 && stripped[0] >= 'a' && stripped[0] <= 'i';
    }

    std::string columnAliasTypeName(std::string_view rawAlias) {
        std::string stripped = stripColumnAliasQuotes(rawAlias);
        if(isBuiltinColalias(stripped)) {
            return "colalias_" + stripped;
        }
        std::string name = toCppIdentifier(stripped);
        if(!name.empty() && std::islower(static_cast<unsigned char>(name[0]))) {
            name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        }
        return name + "Alias";
    }

    bool needsCustomAliasStruct(std::string_view rawAlias) {
        std::string stripped = stripColumnAliasQuotes(rawAlias);
        return !isBuiltinColalias(stripped);
    }

    std::string generateColumnAliasPreamble(const std::vector<SelectColumn>& columns) {
        std::string preamble;
        std::vector<std::string> emitted;
        for(const auto& column : columns) {
            if(column.alias.empty()) continue;
            if(!needsCustomAliasStruct(column.alias)) continue;
            std::string typeName = columnAliasTypeName(column.alias);
            bool alreadyEmitted = false;
            for(const auto& existing : emitted) {
                if(existing == typeName) {
                    alreadyEmitted = true;
                    break;
                }
            }
            if(alreadyEmitted) continue;
            emitted.push_back(typeName);
            std::string displayName = stripColumnAliasQuotes(column.alias);
            std::string escaped;
            for(char character : displayName) {
                if(character == '\\')
                    escaped += "\\\\";
                else if(character == '"')
                    escaped += "\\\"";
                else
                    escaped += character;
            }
            preamble += "struct " + typeName +
                        " : sqlite_orm::alias_tag {\n"
                        "    static const std::string& get() {\n"
                        "        static const std::string res = \"" +
                        escaped +
                        "\";\n"
                        "        return res;\n"
                        "    }\n"
                        "};\n";
        }
        return preamble;
    }

    std::string columnAliasCpp20VarName(std::string_view rawAlias) {
        return toCppIdentifier(stripColumnAliasQuotes(rawAlias));
    }

    std::string generateCpp20ColumnAliasPreamble(const std::vector<SelectColumn>& columns) {
        std::string body;
        std::vector<std::string> emittedVars;
        for(const auto& column : columns) {
            if(column.alias.empty()) continue;
            std::string variableName = columnAliasCpp20VarName(column.alias);
            bool already = false;
            for(const auto& existing : emittedVars) {
                if(existing == variableName) {
                    already = true;
                    break;
                }
            }
            if(already) continue;
            emittedVars.push_back(variableName);
            std::string literal = identifierToCppStringLiteral(stripColumnAliasQuotes(column.alias));
            body += "constexpr orm_column_alias auto " + variableName + " = " + literal + "_col;\n";
        }
        return body;
    }

    std::string wrapWithColumnAlias(const std::string& expressionCode, const std::string& rawAlias, bool cpp20Style) {
        if(rawAlias.empty()) return expressionCode;
        if(cpp20Style) {
            return "as<" + columnAliasCpp20VarName(rawAlias) + ">(" + expressionCode + ")";
        }
        return "as<" + columnAliasTypeName(rawAlias) + ">(" + expressionCode + ")";
    }

    bool hasAnyColumnAlias(const std::vector<SelectColumn>& columns) {
        for(const auto& column : columns) {
            if(!column.alias.empty()) return true;
        }
        return false;
    }

    bool sqliteScalarFirstArgTextContext(std::string_view functionLower) {
        return functionLower == "instr" || functionLower == "substr" || functionLower == "substring" ||
               functionLower == "lower" || functionLower == "upper" || functionLower == "ltrim" ||
               functionLower == "rtrim" || functionLower == "trim" || functionLower == "replace" ||
               functionLower == "unicode" || functionLower == "soundex";
    }

    std::string defaultCppTypeForSyntheticColumn(std::string_view cppIdentifier) {
        const std::string lower = toLowerAscii(cppIdentifier);
        static constexpr std::array<std::string_view, 44> kLikelyText{{
            "address",   "author",    "body",      "caption",   "city",       "comment",    "comments",
            "company",   "country",   "currency",  "department", "description", "domain",    "email",
            "firstname", "headline",  "hostname",  "iban",      "label",      "language",   "lastname",
            "locale",    "login",     "message",   "name",      "nickname",   "notes",      "path",
            "phone",     "referrer",  "region",    "signature", "slug",       "street",     "subtitle",
            "surname",   "swift",     "timezone",  "title",     "uri",        "url",        "user_agent",
            "uuid",      "zip",
        }};
        for(std::string_view hint : kLikelyText) {
            if(lower == hint) {
                return "std::string";
            }
        }
        return "int";
    }

    const std::string kCommentCpp20ColumnAliases =
        "C++20 literal column aliases (`orm_column_alias`, string literal `_col`) require sqlite_orm to be "
        "built with the preprocessor macro SQLITE_ORM_WITH_CPP20_ALIASES defined. Your project may enable "
        "that via CMake target_compile_definitions, compiler `-D`, a config header, or any other suitable "
        "mechanism.";

    std::vector<SourceTableColumn> sourceTableColumnsFromCreateTable(const CreateTableNode& createTable) {
        std::vector<SourceTableColumn> columns;
        for(const ColumnDef& column : createTable.columns) {
            const auto cppType =
                column.typeName.empty() ? "std::vector<char>" : sqliteTypeToCpp(column.typeName);
            const bool nullable = !column.primaryKey && !column.notNull;
            // The expression is what makes a column generated; `generatedStorage` only tells
            // VIRTUAL from STORED, and stays `none` for the bare `AS (...)` spelling SQLite
            // documents as the default.
            const bool generated = column.generatedExpression != nullptr;
            columns.push_back(
                SourceTableColumn{stripIdentifierQuotes(column.name), cppType, nullable, generated});
        }
        return columns;
    }

    const std::string kCommentNegationAsZeroMinus =
        "Unary minus is generated as `0 - expr`: sqlite_orm's own unary minus reports a wrong result "
        "type, so it hands the caller 0 (and throws over a column), while `0 - expr` is what SQLite "
        "computes for `-expr` — same value and same typeof for every operand kind.";

    const std::string kCommentViewReflection =
        "SQL views map to sqlite_orm's reflection-based `make_view<T>()`: the struct's fields and the "
        "`[[= \"…\"_orm_name]]` annotation require a C++26 compiler with reflection (P2996/P3394). "
        "sqlite_orm detects support automatically (SQLITE_ORM_REFLECTION_SUPPORTED enables "
        "SQLITE_ORM_WITH_VIEW); on older compilers this code does not compile.";

    void appendUniqueStrings(std::vector<std::string>& destination, const std::vector<std::string>& source) {
        for(const auto& value : source) {
            bool dupe = false;
            for(const auto& existing : destination) {
                if(existing == value) {
                    dupe = true;
                    break;
                }
            }
            if(!dupe) {
                destination.push_back(value);
            }
        }
    }

    void appendUniqueWarnings(std::vector<CodegenWarning>& destination,
                              const std::vector<CodegenWarning>& source) {
        for(const auto& warning : source) {
            bool dupe = false;
            for(const auto& existing : destination) {
                if(existing.message == warning.message) {
                    dupe = true;
                    break;
                }
            }
            if(!dupe) {
                destination.push_back(warning);
            }
        }
    }

    void appendUniqueString(std::vector<std::string>& destination, const std::string& value) {
        for(const auto& existing : destination) {
            if(existing == value) return;
        }
        destination.push_back(value);
    }

    std::string_view binaryOperatorString(BinaryOperator binaryOperator) {
        switch(binaryOperator) {
            case BinaryOperator::logicalOr:          return " or ";
            case BinaryOperator::logicalAnd:         return " and ";
            case BinaryOperator::equals:             return " == ";
            case BinaryOperator::notEquals:          return " != ";
            case BinaryOperator::lessThan:           return " < ";
            case BinaryOperator::lessOrEqual:        return " <= ";
            case BinaryOperator::greaterThan:        return " > ";
            case BinaryOperator::greaterOrEqual:     return " >= ";
            case BinaryOperator::add:                return " + ";
            case BinaryOperator::subtract:           return " - ";
            case BinaryOperator::multiply:           return " * ";
            case BinaryOperator::divide:             return " / ";
            case BinaryOperator::modulo:             return " % ";
            case BinaryOperator::concatenate:        return " || ";
            case BinaryOperator::bitwiseAnd:         return " & ";
            case BinaryOperator::bitwiseOr:          return " | ";
            case BinaryOperator::shiftLeft:          return " << ";
            case BinaryOperator::shiftRight:         return " >> ";
            case BinaryOperator::isOp:
            case BinaryOperator::isNot:
            case BinaryOperator::isDistinctFrom:
            case BinaryOperator::isNotDistinctFrom:  return {};
            case BinaryOperator::jsonArrow:          return " -> ";
            case BinaryOperator::jsonArrow2:         return " ->> ";
        }
        return {};
    }

    std::string_view binaryFunctionalName(BinaryOperator binaryOperator) {
        switch(binaryOperator) {
            case BinaryOperator::logicalOr:          return "or_";
            case BinaryOperator::logicalAnd:         return "and_";
            case BinaryOperator::equals:             return "is_equal";
            case BinaryOperator::notEquals:          return "is_not_equal";
            case BinaryOperator::lessThan:           return "lesser_than";
            case BinaryOperator::lessOrEqual:        return "lesser_or_equal";
            case BinaryOperator::greaterThan:        return "greater_than";
            case BinaryOperator::greaterOrEqual:     return "greater_or_equal";
            case BinaryOperator::add:                return "add";
            case BinaryOperator::subtract:           return "sub";
            case BinaryOperator::multiply:           return "mul";
            case BinaryOperator::divide:             return "div";
            case BinaryOperator::modulo:             return "mod";
            case BinaryOperator::concatenate:        return "conc";
            case BinaryOperator::bitwiseAnd:         return "bitwise_and";
            case BinaryOperator::bitwiseOr:          return "bitwise_or";
            case BinaryOperator::shiftLeft:          return "bitwise_shift_left";
            case BinaryOperator::shiftRight:         return "bitwise_shift_right";
            case BinaryOperator::isOp:
            case BinaryOperator::isNot:
            case BinaryOperator::isDistinctFrom:
            case BinaryOperator::isNotDistinctFrom:  return {};
            case BinaryOperator::jsonArrow:          return "json_extract";
            case BinaryOperator::jsonArrow2:         return "json_extract";
        }
        return {};
    }

    int cppOperatorPrecedence(BinaryOperator binaryOperator) {
        switch(binaryOperator) {
            case BinaryOperator::multiply:
            case BinaryOperator::divide:
            case BinaryOperator::modulo:             return 5;
            case BinaryOperator::add:
            case BinaryOperator::subtract:           return 6;
            case BinaryOperator::shiftLeft:
            case BinaryOperator::shiftRight:         return 7;
            case BinaryOperator::lessThan:
            case BinaryOperator::lessOrEqual:
            case BinaryOperator::greaterThan:
            case BinaryOperator::greaterOrEqual:     return 9;
            case BinaryOperator::equals:
            case BinaryOperator::notEquals:          return 10;
            case BinaryOperator::bitwiseAnd:         return 11;
            case BinaryOperator::bitwiseOr:          return 13;
            case BinaryOperator::logicalAnd:         return 14;
            // `or` and `||` are the same C++ token; sqlite_orm reads the latter as a concatenation.
            case BinaryOperator::logicalOr:
            case BinaryOperator::concatenate:        return 15;
            // json_extract() is a call, and an IS operator never reaches an emitted operator at all.
            case BinaryOperator::jsonArrow:
            case BinaryOperator::jsonArrow2:
            case BinaryOperator::isOp:
            case BinaryOperator::isNot:
            case BinaryOperator::isDistinctFrom:
            case BinaryOperator::isNotDistinctFrom:  return kCppPrecedencePrimary;
        }
        return kCppPrecedencePrimary;
    }

    int generatedCppPrecedence(const AstNode& astNode, const CodeGenPolicy* policy) {
        if(auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&astNode)) {
            if(policyEquals(policy, "expr_style", "functional")) {
                return kCppPrecedencePrimary;
            }
            return cppOperatorPrecedence(binaryOp->binaryOperator);
        }
        if(auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
            // A unary plus emits its operand and nothing else. Every other unary operator emits
            // either a C++ unary expression, which binds tighter than any binary one, or the
            // parenthesized `(c(0) - …)` a negation becomes.
            if(unaryOp->unaryOperator == UnaryOperator::plus) {
                return generatedCppPrecedence(*unaryOp->operand, policy);
            }
            return kCppPrecedencePrimary;
        }
        if(auto* collateNode = dynamic_cast<const CollateNode*>(&astNode)) {
            // COLLATE has no sqlite_orm form, so the generated code is the operand's own.
            return generatedCppPrecedence(*collateNode->operand, policy);
        }
        return kCppPrecedencePrimary;
    }

    std::string normalizeSqlIdentifier(std::string_view sqlIdentifier) {
        return toLowerAscii(stripIdentifierQuotes(sqlIdentifier));
    }

    bool endsWith(std::string_view text, std::string_view suffix) {
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::optional<std::string> extractStorageSelectArgument(std::string_view generated,
                                                            std::string_view variableName) {
        const std::string prefix = "auto " + std::string(variableName) + " = storage.select(";
        if(generated.size() <= prefix.size() + 2 || !generated.starts_with(prefix) || !endsWith(generated, ");")) {
            return std::nullopt;
        }
        return std::string(generated.substr(prefix.size(), generated.size() - prefix.size() - 2));
    }

    std::string stripStoragePrefixAndTrailingSemicolon(std::string code) {
        static constexpr std::string_view kStoragePrefix = "storage.";
        if(code.size() >= kStoragePrefix.size() && code.compare(0, kStoragePrefix.size(), kStoragePrefix) == 0) {
            code.erase(0, kStoragePrefix.size());
        }
        while(!code.empty() && std::isspace(static_cast<unsigned char>(code.back()))) {
            code.pop_back();
        }
        if(!code.empty() && code.back() == ';') {
            code.pop_back();
        }
        while(!code.empty() && std::isspace(static_cast<unsigned char>(code.back()))) {
            code.pop_back();
        }
        return code;
    }

    std::string blobToCpp(std::string_view blobLiteral) {
        auto hex = blobLiteral.substr(2, blobLiteral.size() - 3);
        if(hex.empty()) {
            return "std::vector<char>{}";
        }
        std::string result = "std::vector<char>{";
        for(size_t index = 0; index < hex.size(); index += 2) {
            if(index > 0) result += ", ";
            result += "'\\x";
            result += hex[index];
            if(index + 1 < hex.size()) result += hex[index + 1];
            result += "'";
        }
        result += "}";
        return result;
    }

    std::string numericLiteralToCpp(std::string_view numericLiteral) {
        std::string result;
        result.reserve(numericLiteral.size());
        for(char character: numericLiteral) {
            result += character == '_' ? '\'' : character;
        }
        return result;
    }

    namespace {

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
            for(char character: digits) {
                if(character != '_' && (character != '0' || !result.empty())) {
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

    }  // namespace

    bool isHexadecimalIntegerLiteral(std::string_view integerLiteral) {
        return integerLiteral.size() > 1 && integerLiteral.front() == '0' &&
               (integerLiteral[1] == 'x' || integerLiteral[1] == 'X');
    }

    bool hexLiteralExceedsInt64(std::string_view integerLiteral) {
        return isHexadecimalIntegerLiteral(integerLiteral) && significantDigits(integerLiteral.substr(2)).size() > 16;
    }

    bool integerLiteralExceedsInt64(std::string_view integerLiteral, bool negated) {
        if(isHexadecimalIntegerLiteral(integerLiteral)) {
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
        return digits.size() > limit.size() ||
               (digits.size() == limit.size() && std::string_view(digits) > limit);
    }

    namespace {

        /** `value` with every folded minus sign taken off; `negated` ends up true for an odd count. */
        const AstNode* withoutFoldedSigns(const AstNode& value, bool& negated) {
            negated = false;
            const AstNode* node = &value;
            while(auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(node)) {
                if(unaryOperator->unaryOperator != UnaryOperator::minus) {
                    break;
                }
                negated = !negated;
                node = unaryOperator->operand.get();
            }
            return node;
        }

        /** The source text of a numeric literal node, or an empty view for any other node. */
        std::string_view numericLiteralText(const AstNode& literal) {
            if(auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&literal)) {
                return integerLiteral->value;
            }
            if(auto* realLiteral = dynamic_cast<const RealLiteralNode*>(&literal)) {
                return realLiteral->value;
            }
            return {};
        }

    }  // namespace

    bool isIntegerLiteralPastIntegerFieldRange(const AstNode& value) {
        bool negated = false;
        auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(withoutFoldedSigns(value, negated));
        return integerLiteral != nullptr && integerLiteralExceedsInt64(integerLiteral->value, negated);
    }

    std::string numericLiteralSqlText(const AstNode& value) {
        bool negated = false;
        const std::string_view text = numericLiteralText(*withoutFoldedSigns(value, negated));
        if(text.empty()) {
            return {};
        }
        return (negated ? "-" : "") + withoutDigitSeparators(text);
    }

    CodegenWarning numericLiteralWarning(std::string message, const AstNode& value) {
        bool negated = false;
        const AstNode& literal = *withoutFoldedSigns(value, negated);
        const std::string_view text = numericLiteralText(literal);
        if(text.empty()) {
            return CodegenWarning{std::move(message)};
        }
        SourceLocation location = literal.location;
        size_t length = text.size();
        // The minus signs the message quotes along with the digits stand in front of the literal,
        // so the underline starts at the value rather than at the token it ends with.
        if(value.location.line == location.line && value.location.column < location.column) {
            length = location.column + length - value.location.column;
            location = value.location;
        }
        return CodegenWarning{std::move(message), location, length};
    }

    bool integerFieldCarriesValue(const AstNode& value) {
        bool negated = false;
        if(dynamic_cast<const RealLiteralNode*>(withoutFoldedSigns(value, negated))) {
            // SQLite turns a REAL into an INTEGER only where the column affinity converts it back
            // and forth without loss, while a C++ field truncates it either way: `1.5` would reach
            // an int64_t field as 1 where SQLite keeps the 1.5 it stored.
            return false;
        }
        return !isIntegerLiteralPastIntegerFieldRange(value);
    }

    namespace {

        /**
         *  The int64 the integer literal `value` denotes, or nothing for any other node and for a
         *  decimal literal past the int64 range. SQLite reads a hexadecimal literal as a signed
         *  64-bit integer and wraps it around, so `0xFFFFFFFFFFFFFFFF` is -1.
         */
        std::optional<std::int64_t> integerLiteralInt64Value(const AstNode& value) {
            bool negated = false;
            const auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(withoutFoldedSigns(value, negated));
            if(integerLiteral == nullptr || integerLiteralExceedsInt64(integerLiteral->value, negated)) {
                return std::nullopt;
            }
            const std::string_view text = integerLiteral->value;
            const bool hexadecimal = isHexadecimalIntegerLiteral(text);
            std::uint64_t magnitude = 0;
            for(char character: hexadecimal ? text.substr(2) : text) {
                if(character == '_') {
                    continue;
                }
                magnitude = hexadecimal ? magnitude * 16 + static_cast<std::uint64_t>(hexDigitValue(character))
                                        : magnitude * 10 + static_cast<std::uint64_t>(character - '0');
            }
            // The magnitude of the int64 minimum does not fit an int64, so the sign folds in on
            // the unsigned value, where the wraparound is the one SQLite performs anyway.
            return static_cast<std::int64_t>(negated ? ~magnitude + 1 : magnitude);
        }

    }  // namespace

    bool doubleFieldCarriesValue(const AstNode& value) {
        const std::optional<std::int64_t> number = integerLiteralInt64Value(value);
        if(!number) {
            // A REAL literal initializes a `double` field as itself, and a decimal literal past
            // the int64 range is generated as a REAL, so neither of them narrows.
            return true;
        }
        const double asDouble = static_cast<double>(*number);
        static constexpr double int64Limit = 9223372036854775808.0;
        // `static_cast<double>(INT64_MAX)` rounds up to the limit itself, which no int64 holds.
        if(asDouble < -int64Limit || asDouble >= int64Limit) {
            return false;
        }
        return static_cast<std::int64_t>(asDouble) == *number;
    }

    ValueStorageClass valueStorageClass(const AstNode& value) {
        if(dynamic_cast<const NullLiteralNode*>(&value)) {
            return ValueStorageClass::null;
        }
        if(dynamic_cast<const StringLiteralNode*>(&value)) {
            return ValueStorageClass::text;
        }
        if(dynamic_cast<const BlobLiteralNode*>(&value)) {
            return ValueStorageClass::blob;
        }
        if(dynamic_cast<const BoolLiteralNode*>(&value)) {
            // SQLite reads TRUE and FALSE as the integers 1 and 0.
            return ValueStorageClass::numeric;
        }
        // A sign folds into a number only; in front of anything else it is an operator SQLite
        // computes, and the generated code spells that operator out rather than a value.
        bool negated = false;
        const AstNode& literal = *withoutFoldedSigns(value, negated);
        if(dynamic_cast<const IntegerLiteralNode*>(&literal) || dynamic_cast<const RealLiteralNode*>(&literal)) {
            return ValueStorageClass::numeric;
        }
        return ValueStorageClass::unknown;
    }

    ValueStorageClass fieldTypeStorageClass(std::string_view cppType) {
        // These are the types `sqliteTypeToCpp` gives a table column; a mapping added there and
        // left out here falls into `unknown`, which lets every value through the object form.
        if(cppType == "int64_t" || cppType == "int" || cppType == "double" || cppType == "bool") {
            return ValueStorageClass::numeric;
        }
        if(cppType == "std::string") {
            return ValueStorageClass::text;
        }
        if(cppType == "std::vector<char>") {
            return ValueStorageClass::blob;
        }
        return ValueStorageClass::unknown;
    }

    bool integerLiteralExceedsInt32(std::string_view integerLiteral) {
        if(isHexadecimalIntegerLiteral(integerLiteral)) {
            // SQLite reads a hex literal as a signed 64-bit integer and wraps it around, so the
            // digits alone say which side of the int32 range the value lands on: up to
            // `0x7FFFFFFF` it is a positive int32, from `0xFFFFFFFF80000000` on it has wrapped
            // back into one as a negative number, and everything in between needs an int64.
            static constexpr std::string_view wrappedInt32Min = "ffffffff80000000";
            const std::string digits = significantDigits(integerLiteral.substr(2));
            if(digits.size() < 8) {
                return false;
            }
            if(digits.size() == 8) {
                return digits.front() >= '8';
            }
            if(digits.size() == 16) {
                const std::string lowered = toLowerAscii(digits);
                return std::string_view(lowered) < wrappedInt32Min;
            }
            // Nine to fifteen digits are past `0xFFFFFFFF`, and a seventeenth one is past an
            // int64 altogether — `hexLiteralExceedsInt64` refuses that where it is compiled.
            return true;
        }
        static constexpr std::string_view int32Max = "2147483647";
        const std::string digits = significantDigits(integerLiteral);
        return digits.size() > int32Max.size() ||
               (digits.size() == int32Max.size() && std::string_view(digits) > int32Max);
    }

    std::string integerLiteralToCpp(std::string_view integerLiteral) {
        // A hexadecimal literal denotes the same number in both languages, but a decimal one with
        // leading zeros does not: SQLite reads `010` as 10, C++ as octal 8, and `0009` does not
        // compile at all. The separators standing between those zeros go away with them, so that
        // `0_9` becomes `9` rather than the octal constant `0'9`.
        if(isHexadecimalIntegerLiteral(integerLiteral)) {
            // Where C++ picks an unsigned type for the literal the two languages part ways again:
            // `0xFFFFFFFFFFFFFFFF` means -1 to SQLite and 18446744073709551615 to C++, and even
            // where the value itself survives, as it does for `0xDEADBEEF`, an unsigned operand
            // drags the rest of the expression along with it, so that `-1 > 0xDEADBEEF` is true in
            // C++ and false in SQLite. The cast restores the type SQLite gives the literal.
            if(hexLiteralIsUnsignedInCpp(integerLiteral)) {
                return "static_cast<int64_t>(" + numericLiteralToCpp(integerLiteral) + ")";
            }
            return numericLiteralToCpp(integerLiteral);
        }
        size_t firstSignificant = 0;
        while(firstSignificant + 1 < integerLiteral.size() &&
              (integerLiteral[firstSignificant] == '0' || integerLiteral[firstSignificant] == '_')) {
            ++firstSignificant;
        }
        const std::string_view significant = integerLiteral.substr(firstSignificant);
        // A decimal literal that does not fit in an int64 is a REAL for SQLite, which reads
        // `9223372036854775808` back as 9.22337203685478e+18. C++ would make that spelling an
        // unsigned constant, and `99999999999999999999` does not fit in any integer type at all,
        // so the fractional part turns it into the double SQLite computes.
        if(integerLiteralExceedsInt64(significant)) {
            return numericLiteralToCpp(significant) + ".0";
        }
        return numericLiteralToCpp(significant);
    }

    bool isNumericLiteral(const AstNode& astNode) {
        return dynamic_cast<const IntegerLiteralNode*>(&astNode) ||
               dynamic_cast<const RealLiteralNode*>(&astNode);
    }

    bool numericLiteralRejectsFoldedSign(const AstNode& astNode) {
        auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&astNode);
        return integerLiteral && hexLiteralIsInt64Min(integerLiteral->value);
    }

    std::string_view sqlPredicateLooserThanMinus(const AstNode& astNode) {
        // sqlite_orm parenthesizes the operands of a binary operator it serializes, and everything
        // else an expression can be — a literal, a column, a function call, CAST, CASE, a subquery,
        // `~x` — is a term SQLite reads as one unit. These predicates are the exception: they come
        // out bare and bind looser than `-`, so `0 - a BETWEEN 1 AND 9` would read as
        // `(0 - a) BETWEEN 1 AND 9` rather than as the negation it stands for.
        if(dynamic_cast<const InNode*>(&astNode)) {
            return "IN";
        }
        if(dynamic_cast<const BetweenNode*>(&astNode)) {
            return "BETWEEN";
        }
        if(dynamic_cast<const LikeNode*>(&astNode)) {
            return "LIKE";
        }
        if(dynamic_cast<const GlobNode*>(&astNode)) {
            return "GLOB";
        }
        if(dynamic_cast<const MatchNode*>(&astNode)) {
            return "MATCH";
        }
        if(dynamic_cast<const IsNullNode*>(&astNode)) {
            return "IS NULL";
        }
        if(dynamic_cast<const IsNotNullNode*>(&astNode)) {
            return "IS NOT NULL";
        }
        if(auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
            if(unaryOp->unaryOperator == UnaryOperator::logicalNot) {
                return "NOT";
            }
        }
        return {};
    }

    NegationForm negationFormFor(const AstNode& operand) {
        if(isNumericLiteral(operand)) {
            return numericLiteralRejectsFoldedSign(operand) ? NegationForm::zeroMinusSubtraction
                                                            : NegationForm::foldedIntoConstant;
        }
        if(generatesFoldedNegation(operand)) {
            // The operand is itself a constant with a sign already folded in, so C++ folds this
            // sign too: `- - -3` is the constant `-(-(-3))`.
            return NegationForm::foldedIntoConstant;
        }
        return sqlPredicateLooserThanMinus(operand).empty() ? NegationForm::zeroMinusSubtraction
                                                            : NegationForm::unaryOverPredicate;
    }

    namespace {
        /** The form a node's own negation takes, for a node that is not a negation at all. */
        std::optional<NegationForm> formOfNegationNode(const AstNode& astNode) {
            auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode);
            if(!unaryOp || unaryOp->unaryOperator != UnaryOperator::minus) {
                return std::nullopt;
            }
            return negationFormFor(*unaryOp->operand);
        }
    }

    bool generatesFoldedNegation(const AstNode& astNode) {
        return formOfNegationNode(astNode) == NegationForm::foldedIntoConstant;
    }

    bool generatesZeroMinusSubtraction(const AstNode& astNode) {
        return formOfNegationNode(astNode) == NegationForm::zeroMinusSubtraction;
    }

    bool isLeafNode(const AstNode& astNode) {
        return generatesFoldedNegation(astNode) ||
               dynamic_cast<const IntegerLiteralNode*>(&astNode) ||
               dynamic_cast<const RealLiteralNode*>(&astNode) ||
               dynamic_cast<const StringLiteralNode*>(&astNode) ||
               dynamic_cast<const NullLiteralNode*>(&astNode) ||
               dynamic_cast<const BoolLiteralNode*>(&astNode) ||
               dynamic_cast<const BlobLiteralNode*>(&astNode) ||
               dynamic_cast<const CurrentDatetimeLiteralNode*>(&astNode) ||
               dynamic_cast<const ColumnRefNode*>(&astNode) ||
               dynamic_cast<const QualifiedColumnRefNode*>(&astNode) ||
               dynamic_cast<const NewRefNode*>(&astNode) ||
               dynamic_cast<const OldRefNode*>(&astNode) ||
               dynamic_cast<const ExcludedRefNode*>(&astNode) ||
               dynamic_cast<const RaiseNode*>(&astNode);
    }

    std::string wrap(std::string_view code) {
        return "c(" + std::string(code) + ")";
    }

    bool expressionMayBeNull(const AstNode& astNode) {
        if(dynamic_cast<const IntegerLiteralNode*>(&astNode) ||
           dynamic_cast<const RealLiteralNode*>(&astNode) ||
           dynamic_cast<const StringLiteralNode*>(&astNode) ||
           dynamic_cast<const BoolLiteralNode*>(&astNode) ||
           dynamic_cast<const BlobLiteralNode*>(&astNode) ||
           dynamic_cast<const CurrentDatetimeLiteralNode*>(&astNode)) {
            return false;
        }
        if(dynamic_cast<const IsNullNode*>(&astNode) || dynamic_cast<const IsNotNullNode*>(&astNode) ||
           dynamic_cast<const ExistsNode*>(&astNode)) {
            // SQLite answers these over a NULL operand too; they are the tests for one.
            return false;
        }
        if(auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&astNode)) {
            switch(binaryOp->binaryOperator) {
            case BinaryOperator::isOp:
            case BinaryOperator::isNot:
            case BinaryOperator::isDistinctFrom:
            case BinaryOperator::isNotDistinctFrom:
                // `NULL IS 1` is 0, not NULL: these compare NULL rather than propagate it.
                return false;
            case BinaryOperator::divide:
            case BinaryOperator::modulo:
                // `1 / 0` and `1 % 0` are NULL in SQLite, however plain the operands are.
                return true;
            default:
                return expressionMayBeNull(*binaryOp->lhs) || expressionMayBeNull(*binaryOp->rhs);
            }
        }
        if(auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
            return expressionMayBeNull(*unaryOp->operand);
        }
        if(auto* collate = dynamic_cast<const CollateNode*>(&astNode)) {
            return expressionMayBeNull(*collate->operand);
        }
        return true;
    }

    bool selectResultNeedsAsOptional(const AstNode& astNode) {
        if(auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&astNode)) {
            switch(binaryOp->binaryOperator) {
            case BinaryOperator::isOp:
            case BinaryOperator::isNot:
            case BinaryOperator::isDistinctFrom:
            case BinaryOperator::isNotDistinctFrom:
            case BinaryOperator::jsonArrow:
            case BinaryOperator::jsonArrow2:
                // Not generated as a C++ binary operator: the first four never reach codegen (the
                // validator rejects them), the JSON arrows become a json_extract() call.
                return false;
            default:
                return expressionMayBeNull(astNode);
            }
        }
        if(auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
            // A unary operator is typed from the operator alone too — `-x` is generated as the
            // subtraction `(c(0) - x)`, `~x` as a `bitwise_not_t`. A sign folded into a numeric
            // constant leaves no operator behind, but a constant is never NULL either, so
            // `expressionMayBeNull` already answers no for it.
            if(unaryOp->unaryOperator == UnaryOperator::minus &&
               negationFormFor(*unaryOp->operand) == NegationForm::unaryOverPredicate) {
                // The one form codegen already warns has no working sqlite_orm spelling: it does
                // not compile at all, so there is no result type to widen.
                return false;
            }
            return expressionMayBeNull(astNode);
        }
        return false;
    }

    std::string sqliteTypeToCpp(std::string_view typeName) {
        std::string lower = toLowerAscii(typeName);
        if(lower.find("bool") != std::string::npos) return "bool";
        if(lower.find("int") != std::string::npos) return "int64_t";
        if(lower.find("char") != std::string::npos || lower.find("clob") != std::string::npos ||
           lower.find("text") != std::string::npos)
            return "std::string";
        if(lower.find("blob") != std::string::npos || lower.empty()) return "std::vector<char>";
        if(lower.find("real") != std::string::npos || lower.find("floa") != std::string::npos ||
           lower.find("doub") != std::string::npos)
            return "double";
        return "double";
    }

    std::string defaultInitializer(std::string_view cppType) {
        if(cppType == "int" || cppType == "int64_t") return " = 0";
        if(cppType == "double") return " = 0.0";
        if(cppType == "bool") return " = false";
        return "";
    }

    std::string toStructName(std::string_view sqlName) {
        auto base = toCppIdentifier(sqlName);
        std::string result;
        result.reserve(base.size());
        bool atWordStart = true;
        for(char character : base) {
            if(character == '_') {
                atWordStart = true;
                continue;
            }
            if(atWordStart) {
                result += static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
                atWordStart = false;
            } else {
                result += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
        }
        if(result.empty() && !base.empty()) {
            return base;
        }
        return result;
    }

    std::string_view joinSqliteOrmApiName(JoinKind joinKind) {
        switch(joinKind) {
            case JoinKind::crossJoin:        return "cross_join";
            case JoinKind::innerJoin:        return "inner_join";
            case JoinKind::leftJoin:         return "left_join";
            case JoinKind::leftOuterJoin:    return "left_outer_join";
            case JoinKind::joinPlain:        return "join";
            case JoinKind::naturalInnerJoin: return "natural_join";
            default:                         return "inner_join";
        }
    }

    std::string_view compoundSelectApi(CompoundSelectOperator compoundOperator) {
        switch(compoundOperator) {
            case CompoundSelectOperator::unionDistinct: return "union_";
            case CompoundSelectOperator::unionAll:      return "union_all";
            case CompoundSelectOperator::intersect:     return "intersect";
            case CompoundSelectOperator::except:        return "except";
        }
        return "union_";
    }

    std::string dmlInsertOrPrefix(ConflictClause conflictClause) {
        switch(conflictClause) {
            case ConflictClause::rollback: return "or_rollback(), ";
            case ConflictClause::abort:    return "or_abort(), ";
            case ConflictClause::fail:     return "or_fail(), ";
            case ConflictClause::ignore:   return "or_ignore(), ";
            case ConflictClause::replace:  return "or_replace(), ";
            default:                       return "";
        }
    }

    std::optional<std::string> journalModeSqlTokenToCppEnum(std::string_view token) {
        const std::string lower = toLowerAscii(stripIdentifierQuotes(token));
        if(lower == "delete") return std::string{"sqlite_orm::journal_mode::DELETE"};
        if(lower == "truncate") return std::string{"sqlite_orm::journal_mode::TRUNCATE"};
        if(lower == "persist") return std::string{"sqlite_orm::journal_mode::PERSIST"};
        if(lower == "memory") return std::string{"sqlite_orm::journal_mode::MEMORY"};
        if(lower == "wal") return std::string{"sqlite_orm::journal_mode::WAL"};
        if(lower == "off") return std::string{"sqlite_orm::journal_mode::OFF"};
        return std::nullopt;
    }

    std::optional<std::string> lockingModeSqlTokenToCppEnum(std::string_view token) {
        const std::string lower = toLowerAscii(stripIdentifierQuotes(token));
        if(lower == "normal") return std::string{"sqlite_orm::locking_mode::NORMAL"};
        if(lower == "exclusive") return std::string{"sqlite_orm::locking_mode::EXCLUSIVE"};
        return std::nullopt;
    }

    std::optional<std::string> pragmaTableNameLiteral(const AstNode& valueNode) {
        if(const auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(&valueNode)) {
            return sqlStringToCpp(stringLiteral->value);
        }
        if(const auto* columnRef = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
            return identifierToCppStringLiteral(columnRef->columnName);
        }
        return std::nullopt;
    }

    std::optional<std::string> pragmaJournalOrLockingValueToken(const AstNode& valueNode) {
        if(const auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(&valueNode)) {
            if(stringLiteral->value.size() >= 2 && stringLiteral->value.front() == '\'' &&
               stringLiteral->value.back() == '\'') {
                return std::string(stringLiteral->value.substr(1, stringLiteral->value.size() - 2));
            }
        }
        if(const auto* columnRef = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
            return std::string(columnRef->columnName);
        }
        return std::nullopt;
    }

    namespace {

        /** Contents of a quoted SQL string literal, with doubled quotes collapsed (`'it''s'` -> `it's`). */
        std::string sqlStringLiteralText(std::string_view literal) {
            if(literal.size() < 2) {
                return std::string(literal);
            }
            const char quote = literal.front();
            const std::string_view content = literal.substr(1, literal.size() - 2);
            std::string result;
            result.reserve(content.size());
            for(size_t index = 0; index < content.size(); ++index) {
                if(content[index] == quote && index + 1 < content.size() && content[index + 1] == quote) {
                    ++index;
                }
                result += content[index];
            }
            return result;
        }

    }  // namespace

    std::optional<std::int32_t> sqlitePragmaInt32(std::string_view valueText) {
        // `sqlite3GetInt32()` takes the sign off first and only then looks for a `0x` prefix, so a
        // signed value never reaches its hexadecimal branch: `-0x10` falls through to the decimal
        // one, which stops at the `x` and answers 0 rather than -16.
        bool negated = false;
        bool hexadecimal = false;
        if(!valueText.empty() && (valueText.front() == '-' || valueText.front() == '+')) {
            negated = valueText.front() == '-';
            valueText.remove_prefix(1);
        } else {
            hexadecimal = valueText.size() > 2 && valueText[0] == '0' &&
                          (valueText[1] == 'x' || valueText[1] == 'X') && isHexDigit(valueText[2]);
        }
        if(hexadecimal) {
            size_t index = 2;
            while(index < valueText.size() && valueText[index] == '0') {
                ++index;
            }
            std::uint32_t value = 0;
            size_t digitCount = 0;
            for(; index < valueText.size() && digitCount < 8 && isHexDigit(valueText[index]);
                ++index, ++digitCount) {
                value = value * 16 + static_cast<std::uint32_t>(hexDigitValue(valueText[index]));
            }
            // SQLite reads eight hexadecimal digits at most and refuses the value outright once the
            // sign bit is set or a ninth digit follows, rather than wrapping or truncating it.
            if((value & 0x80000000u) != 0 || (index < valueText.size() && isHexDigit(valueText[index]))) {
                return std::nullopt;
            }
            return static_cast<std::int32_t>(value);
        }
        if(valueText.empty() || !isDigit(valueText.front())) {
            return std::nullopt;
        }
        size_t index = 0;
        while(index < valueText.size() && valueText[index] == '0') {
            ++index;
        }
        std::int64_t value = 0;
        size_t digitCount = 0;
        for(; index < valueText.size() && digitCount < 11 && isDigit(valueText[index]); ++index, ++digitCount) {
            value = value * 10 + (valueText[index] - '0');
        }
        // The digits stop at the first character that is not one, so `1.5` reads as 1, and the
        // magnitude a negative value may reach is one larger: `-2147483648` is an int32, `2147483648`
        // is not.
        if(digitCount > 10 || value - (negated ? 1 : 0) > 2147483647) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(negated ? -value : value);
    }

    bool sqlitePragmaBoolean(std::string_view valueText) {
        if(!valueText.empty() && isDigit(valueText.front())) {
            // SQLite's `getSafetyLevel()` returns a u8, so the int32 loses everything above its low
            // byte before the `!= 0` test: `256` and `65536` are false while `255` and `257` are true.
            return (sqlitePragmaInt32(valueText).value_or(0) & 0xFF) != 0;
        }
        const std::string lower = toLowerAscii(valueText);
        return lower == "on" || lower == "yes" || lower == "true";
    }

    bool isCanonicalPragmaBooleanText(std::string_view valueText) {
        if(valueText == "0" || valueText == "1") {
            return true;
        }
        const std::string lower = toLowerAscii(valueText);
        return lower == "on" || lower == "off" || lower == "yes" || lower == "no" || lower == "true" ||
               lower == "false";
    }

    std::optional<PragmaValue> pragmaValue(const AstNode& valueNode) {
        if(const auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&valueNode)) {
            return PragmaValue{std::string(integerLiteral->value), std::string(integerLiteral->value)};
        }
        if(const auto* realLiteral = dynamic_cast<const RealLiteralNode*>(&valueNode)) {
            return PragmaValue{std::string(realLiteral->value), std::string(realLiteral->value)};
        }
        if(const auto* boolLiteral = dynamic_cast<const BoolLiteralNode*>(&valueNode)) {
            const std::string written = boolLiteral->value ? "true" : "false";
            return PragmaValue{written, written};
        }
        if(const auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(&valueNode)) {
            return PragmaValue{sqlStringLiteralText(stringLiteral->value), std::string(stringLiteral->value)};
        }
        if(const auto* columnRef = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
            return PragmaValue{stripIdentifierQuotes(columnRef->columnName), std::string(columnRef->columnName)};
        }
        if(const auto* currentDatetime = dynamic_cast<const CurrentDatetimeLiteralNode*>(&valueNode)) {
            // `CURRENT_DATE` and its two siblings are names to a PRAGMA — SQLite's `nmnum` rule
            // falls them back to an identifier, and the reader gets those very letters — so the
            // value is whatever that text reads as, not today's date.
            const std::string written = currentDatetime->kind == CurrentDatetimeKind::date ? "current_date"
                                        : currentDatetime->kind == CurrentDatetimeKind::time
                                            ? "current_time"
                                            : "current_timestamp";
            return PragmaValue{written, written};
        }
        if(const auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&valueNode)) {
            const bool numericOperand =
                unaryOperator->operand && (dynamic_cast<const IntegerLiteralNode*>(unaryOperator->operand.get()) ||
                                           dynamic_cast<const RealLiteralNode*>(unaryOperator->operand.get()));
            if(unaryOperator->unaryOperator == UnaryOperator::minus && numericOperand) {
                const PragmaValue operand = *pragmaValue(*unaryOperator->operand);
                return PragmaValue{"-" + operand.text, "-" + operand.sqlText};
            }
        }
        return std::nullopt;
    }

}  // namespace sqlite2orm
