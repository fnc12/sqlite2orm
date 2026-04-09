#include "codegen_utils.h"

#include <sqlite2orm/utils.h>

#include <array>
#include <cctype>

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

    std::string colaliasBuiltinSlot(size_t slotIndex) {
        static constexpr std::array<std::string_view, 10> kColaliasSlots = {
            "colalias_i{}", "colalias_j{}", "colalias_k{}", "colalias_l{}", "colalias_m{}",
            "colalias_n{}", "colalias_o{}", "colalias_p{}", "colalias_q{}", "colalias_r{}",
        };
        if(slotIndex < kColaliasSlots.size()) {
            return std::string(kColaliasSlots[slotIndex]);
        }
        return "colalias_i{}";
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

    std::string normalizeSqlIdentifier(std::string_view sqlIdentifier) {
        return toLowerAscii(stripIdentifierQuotes(sqlIdentifier));
    }

    bool endsWith(std::string_view text, std::string_view suffix) {
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::optional<std::string> extractStorageSelectArgument(std::string_view generated) {
        constexpr std::string_view prefix = "auto rows = storage.select(";
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

    bool isLeafNode(const AstNode& astNode) {
        return dynamic_cast<const IntegerLiteralNode*>(&astNode) ||
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

    std::optional<bool> pragmaRecursiveTriggersBool(const AstNode& valueNode) {
        if(const auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&valueNode)) {
            if(integerLiteral->value == "0") return false;
            if(integerLiteral->value == "1") return true;
            return std::nullopt;
        }
        if(const auto* boolLiteral = dynamic_cast<const BoolLiteralNode*>(&valueNode)) {
            return boolLiteral->value;
        }
        if(const auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(&valueNode)) {
            if(stringLiteral->value.size() >= 2) {
                const std::string inner = toLowerAscii(stringLiteral->value.substr(1, stringLiteral->value.size() - 2));
                if(inner == "on" || inner == "yes" || inner == "true") return true;
                if(inner == "off" || inner == "no" || inner == "false") return false;
            }
        }
        if(const auto* columnRef = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
            const std::string token = toLowerAscii(columnRef->columnName);
            if(token == "on") return true;
            if(token == "off") return false;
        }
        return std::nullopt;
    }

}  // namespace sqlite2orm
