#include "codegen_utils.h"
#include "codegen_context.h"

#include <sqlite2orm/utils.h>
#include <sqlite2orm/validator.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>

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

    const std::string kCommentPredicateGroupingCast =
        "A predicate under an operator is generated as `cast<int64_t>(predicate)`: sqlite_orm "
        "serializes IN, BETWEEN, LIKE, GLOB, MATCH, IS [NOT] NULL and NOT without parentheses, and "
        "SQLite binds them looser than the operator around them, so `1 - (a IS NULL)` would be read "
        "back as `(1 - a) IS NULL`. The CAST delimits the predicate and leaves what it stands for "
        "alone — a predicate is 0, 1 or NULL, and a CAST to INTEGER keeps all three, typeof included.";

    const std::string kCommentNotColumnPointer =
        "A column under a NOT is generated as `column<T>(&T::x)`: `operator!` is the one sqlite_orm "
        "operator that keeps the `c(...)` its operand carries instead of unwrapping it, and the walker "
        "that collects the tables a statement reads stops at such a wrapper — `select(not c(&T::x))` "
        "comes out with no FROM clause at all and throws `SQL logic error`. The column pointer names "
        "the same column and serializes to the same SQL.";

    const std::string kCommentNotValueAddedToZero =
        "A value under a NOT is generated as `(c(0) + value)`: sqlite_orm binds the values of a "
        "statement by walking its expression tree, and that walk stops at the `c(...)` a NOT keeps "
        "over its operand — the value of `select(not c(0))` is never bound, so the statement runs "
        "with an empty parameter and answers NULL. A binary operator unwraps what it is given, and "
        "`0 + x` is the numeric coercion SQLite applies to `x` in a boolean context anyway, so "
        "`NOT x` and `NOT (0 + x)` answer alike.";

    const std::string kCommentNegatedConditionCast =
        "A NOT over a NOT is generated as `not cast<int64_t>(not …)`: sqlite_orm's `negated_condition_t` "
        "— what a NOT and the `!predicate` spelling of a negated BETWEEN, LIKE, GLOB and MATCH produce — "
        "is neither negatable nor an operator argument, so a second NOT over it does not compile. The "
        "CAST leaves what the inner NOT stands for alone: it is 0, 1 or NULL, and a CAST to INTEGER "
        "keeps all three.";
    const std::string kCommentBitwiseResultCast =
        "A bitwise result column is generated as `cast<int64_t>(expr)`: sqlite_orm types `&`, `|`, "
        "`<<`, `>>` and `~` as `int`, so a result outside the int32 range comes back truncated "
        "(`9223372036854775807 & -1` reads back as -1). SQLite answers a bitwise operator with an "
        "INTEGER or a NULL whatever its operands hold, and a CAST to INTEGER keeps both, typeof "
        "included, so the CAST widens the C++ type and leaves the value alone.";

    const std::string kCommentAndOrPredicateArgumentCast =
        "An AND or an OR in the argument of a predicate is generated as `cast<int64_t>(…)`: "
        "sqlite_orm serializes IN, BETWEEN, LIKE, GLOB, MATCH and IS [NOT] NULL with no "
        "parentheses around their arguments, and SQLite binds AND and OR looser than every "
        "predicate, so `is_null(or_(1, 0))` would be read back as `1 OR (0 IS NULL)` and "
        "`between(1, or_(1, 0), 3)` would not even parse. The CAST delimits the argument and "
        "leaves what it stands for alone — an AND and an OR are 0, 1 or NULL, and a CAST to "
        "INTEGER keeps all three, typeof included.";

    const std::string kCommentOrTokenCallSpelling =
        "`OR` is generated as `or_(left, right)` and `||` as `conc(left, right)`: C++ spells both "
        "of them `||`, and sqlite_orm picks between the two by the operands — `operator||` builds "
        "the OR condition only when one of them is a condition (a comparison, AND, OR, IN, BETWEEN, "
        "LIKE, GLOB, IS [NOT] NULL, EXISTS or NOT) and the concatenation otherwise. So `1 OR 0` "
        "spelled `c(1) or 0` runs as `1 || 0` and answers '10', and `(a = 1) || 'x'` spelled "
        "`c(&T::a) == 1 || \"x\"` runs as `(a = 1) OR 'x'`. The call names the node it builds.";

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

    std::string_view binaryOperatorWithoutDefaultConstructor(BinaryOperator binaryOperator) {
        switch(binaryOperator) {
            case BinaryOperator::logicalOr:
            case BinaryOperator::logicalAnd:
            case BinaryOperator::equals:
            case BinaryOperator::notEquals:
            case BinaryOperator::lessThan:
            case BinaryOperator::lessOrEqual:
            case BinaryOperator::greaterThan:
            case BinaryOperator::greaterOrEqual:
            case BinaryOperator::isOp:
            case BinaryOperator::isNot:
            case BinaryOperator::isDistinctFrom:
            case BinaryOperator::isNotDistinctFrom:  return {};
            case BinaryOperator::add:                return "+";
            case BinaryOperator::subtract:           return "-";
            case BinaryOperator::multiply:           return "*";
            case BinaryOperator::divide:             return "/";
            case BinaryOperator::modulo:             return "%";
            case BinaryOperator::concatenate:        return "||";
            case BinaryOperator::bitwiseAnd:         return "&";
            case BinaryOperator::bitwiseOr:          return "|";
            case BinaryOperator::shiftLeft:          return "<<";
            case BinaryOperator::shiftRight:         return ">>";
            case BinaryOperator::jsonArrow:          return "->";
            case BinaryOperator::jsonArrow2:         return "->>";
        }
        return {};
    }

    namespace {
        // The window functions that take no argument, so the `name()` a star is generated as is the
        // same call as the one written without one.
        constexpr std::array<std::string_view, 5> kNullaryWindowFunctions{{
            "row_number", "rank", "dense_rank", "percent_rank", "cume_dist",
        }};
        // The window functions that take arguments, and MATCH in its function spelling.
        constexpr std::array<std::string_view, 7> kArgumentTakingAggregateForms{{
            "ntile", "lag", "lead", "first_value", "last_value", "nth_value", "match",
        }};

        bool nameIsIn(std::string_view name, std::span<const std::string_view> names) {
            return std::find(names.begin(), names.end(), name) != names.end();
        }
    }

    bool functionCallHasDefaultConstructor(std::string_view lowerFunctionName, bool star) {
        if(nameIsIn(lowerFunctionName, kNullaryWindowFunctions)) {
            return true;
        }
        if(star) {
            return lowerFunctionName == "count";
        }
        return nameIsIn(lowerFunctionName, kArgumentTakingAggregateForms);
    }

    bool functionCallFormHasNoFilter(std::string_view lowerFunctionName) {
        return nameIsIn(lowerFunctionName, kNullaryWindowFunctions) ||
               nameIsIn(lowerFunctionName, kArgumentTakingAggregateForms);
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
            // `or` and `||` are the same C++ token, which is also why the operator spelling is not
            // always the operator that was written — see `binaryOperatorNeedsCallSpelling`.
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

    const AstNode& generatedOperandNode(const AstNode& astNode) {
        if(auto* collateNode = dynamic_cast<const CollateNode*>(&astNode)) {
            // COLLATE has no sqlite_orm form, so the generated code is the operand's own.
            return generatedOperandNode(*collateNode->operand);
        }
        if(auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
            // A unary plus emits its operand and nothing else. Every other unary operator emits
            // a C++ expression of its own.
            if(unaryOp->unaryOperator == UnaryOperator::plus) {
                return generatedOperandNode(*unaryOp->operand);
            }
        }
        return astNode;
    }

    int sqlOperatorPrecedence(BinaryOperator binaryOperator) {
        // SQLite's own operator table, tightest first: `||` above `* / %` above `+ -` above the bit
        // operators above the ordering comparisons above the equality ones, the rank the predicates
        // share. `AND` and `OR` are looser than everything a predicate operand could regroup with.
        switch(binaryOperator) {
            case BinaryOperator::concatenate:
            case BinaryOperator::jsonArrow:
            case BinaryOperator::jsonArrow2:         return 1;
            case BinaryOperator::multiply:
            case BinaryOperator::divide:
            case BinaryOperator::modulo:             return 2;
            case BinaryOperator::add:
            case BinaryOperator::subtract:           return 3;
            case BinaryOperator::shiftLeft:
            case BinaryOperator::shiftRight:
            case BinaryOperator::bitwiseAnd:
            case BinaryOperator::bitwiseOr:          return 4;
            case BinaryOperator::lessThan:
            case BinaryOperator::lessOrEqual:
            case BinaryOperator::greaterThan:
            case BinaryOperator::greaterOrEqual:     return 5;
            case BinaryOperator::equals:
            case BinaryOperator::notEquals:
            case BinaryOperator::isOp:
            case BinaryOperator::isNot:
            case BinaryOperator::isDistinctFrom:
            case BinaryOperator::isNotDistinctFrom:  return kSqlPrecedencePredicate;
            case BinaryOperator::logicalAnd:         return kSqlPrecedenceAnd;
            case BinaryOperator::logicalOr:          return kSqlPrecedenceOr;
        }
        return kSqlPrecedencePredicate;
    }

    int serializedSqlPrecedence(const AstNode& astNode) {
        // A COLLATE and a unary plus emit their operand and nothing else, so the SQL this node is
        // serialized as is the one its operand is serialized as.
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if(auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
            if(unaryOp->unaryOperator == UnaryOperator::logicalNot) {
                return kSqlPrecedenceNot;
            }
            if(unaryOp->unaryOperator == UnaryOperator::minus) {
                // A negation is a constant or the parenthesized `(c(0) - …)` subtraction, both of
                // them terms. The exception is the one over a predicate, which keeps a bare unary
                // minus SQLite reads inside the predicate: `- "a" IS NULL` is `(- "a") IS NULL`.
                return negationFormFor(*unaryOp->operand) == NegationForm::unaryOverPredicate
                           ? serializedSqlPrecedence(*unaryOp->operand)
                           : kSqlPrecedenceTerm;
            }
            return kSqlPrecedenceTerm;
        }
        if(auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
            // The JSON arrows are the one binary operator serialized as a call, `json_extract(…)`,
            // which reads as one term; every other one is serialized as the SQL operator it was
            // parsed as, and binds the way SQLite binds that operator.
            if(binaryOp->binaryOperator == BinaryOperator::jsonArrow ||
               binaryOp->binaryOperator == BinaryOperator::jsonArrow2) {
                return kSqlPrecedenceTerm;
            }
            return sqlOperatorPrecedence(binaryOp->binaryOperator);
        }
        // Everything else is a term of its own in the serialized SQL: a literal, a column, a call,
        // CAST, CASE, a parenthesized subquery.
        return sqlPredicateLooserThanMinus(generatedNode).empty() ? kSqlPrecedenceTerm
                                                                  : kSqlPrecedencePredicate;
    }

    int serializedSqlPrecedenceAsBinaryOperand(const AstNode& astNode) {
        // sqlite_orm's binary operator and binary condition serializer parenthesizes an operand
        // that is itself a binary operator or condition — everything a `BinaryOperatorNode`
        // generates — so however loosely SQLite binds it, it comes back as one term there.
        if(dynamic_cast<const BinaryOperatorNode*>(&generatedOperandNode(astNode))) {
            return kSqlPrecedenceTerm;
        }
        return serializedSqlPrecedence(astNode);
    }

    bool predicateArgumentNeedsGroupingCast(const AstNode& astNode) {
        return serializedSqlPrecedence(astNode) >= kSqlPrecedenceAnd;
    }

    int generatedCppPrecedence(const AstNode& astNode, const CodeGenPolicy* policy) {
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if(auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
            // An operator the C++ `||` token would misread is generated as a call whatever the
            // policy asks for, and a call is a primary.
            if(policyEquals(policy, "expr_style", "functional") || binaryOperatorNeedsCallSpelling(*binaryOp)) {
                return kCppPrecedencePrimary;
            }
            return cppOperatorPrecedence(binaryOp->binaryOperator);
        }
        // Every remaining node emits either a C++ unary expression, which binds tighter than any
        // binary one, or a primary: a literal, a call, an already-parenthesized subtraction.
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

    const AstNode* withoutFoldedSigns(const AstNode& value, std::size_t& foldedMinusSigns) {
        foldedMinusSigns = 0;
        const AstNode* node = &value;
        while(auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(node)) {
            const bool sign = unaryOperator->unaryOperator == UnaryOperator::minus ||
                              unaryOperator->unaryOperator == UnaryOperator::plus;
            if(!sign || !unaryOperator->operand) {
                break;
            }
            if(unaryOperator->unaryOperator == UnaryOperator::minus) {
                ++foldedMinusSigns;
            }
            node = unaryOperator->operand.get();
        }
        return node;
    }

    namespace {

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
        std::size_t foldedSigns = 0;
        auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(withoutFoldedSigns(value, foldedSigns));
        if(integerLiteral == nullptr) {
            return false;
        }
        if(integerLiteralExceedsInt64(integerLiteral->value, foldedSigns % 2 != 0)) {
            return true;
        }
        // Only the innermost sign folds into the literal; SQLite negates the value the rest of
        // them stand on while it runs the statement, and negating the int64 minimum leaves the
        // integer range: `SELECT typeof(-9223372036854775808)` is integer, while
        // `typeof(-(-9223372036854775808))` is real.
        return foldedSigns > 1 && integerLiteralExceedsInt64(integerLiteral->value);
    }

    size_t underlineLengthOf(std::string_view sourceText) {
        const size_t lineBreak = sourceText.find('\n');
        return lineBreak == std::string_view::npos ? sourceText.size() : lineBreak;
    }

    std::string numericLiteralSqlText(const AstNode& value) {
        std::size_t foldedSigns = 0;
        const std::string_view text = numericLiteralText(*withoutFoldedSigns(value, foldedSigns));
        if(text.empty()) {
            return {};
        }
        return (foldedSigns % 2 != 0 ? "-" : "") + withoutDigitSeparators(text);
    }

    CodegenWarning numericLiteralWarning(std::string message, const AstNode& value) {
        std::size_t foldedSigns = 0;
        const AstNode& literal = *withoutFoldedSigns(value, foldedSigns);
        const std::string_view text = numericLiteralText(literal);
        if(text.empty()) {
            return CodegenWarning{std::move(message)};
        }
        SourceLocation location = literal.location;
        size_t length = underlineLengthOf(text);
        // The minus signs the message quotes along with the digits stand in front of the literal,
        // so the underline starts at the value rather than at the token it ends with.
        if(value.location.line == location.line && value.location.column < location.column) {
            length = location.column + length - value.location.column;
            location = value.location;
        }
        return CodegenWarning{std::move(message), location, length};
    }

    bool integerFieldCarriesValue(const AstNode& value) {
        std::size_t foldedSigns = 0;
        if(dynamic_cast<const RealLiteralNode*>(withoutFoldedSigns(value, foldedSigns))) {
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
            std::size_t foldedSigns = 0;
            const auto* integerLiteral =
                dynamic_cast<const IntegerLiteralNode*>(withoutFoldedSigns(value, foldedSigns));
            const bool negated = foldedSigns % 2 != 0;
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

    bool boolFieldCarriesValue(const AstNode& value) {
        if(dynamic_cast<const BoolLiteralNode*>(&value)) {
            // SQLite stores TRUE and FALSE as the integers 1 and 0, the whole range of the field.
            return true;
        }
        const std::optional<std::int64_t> number = integerLiteralInt64Value(value);
        return number && (*number == 0 || *number == 1);
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
        std::size_t foldedSigns = 0;
        const AstNode& literal = *withoutFoldedSigns(value, foldedSigns);
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

    bool integerLiteralExceedsInt32(std::string_view integerLiteral, bool negated) {
        if(isHexadecimalIntegerLiteral(integerLiteral)) {
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
            if(digits.size() < 8) {
                return false;
            }
            const std::string lowered = toLowerAscii(digits);
            if(digits.size() == 8) {
                return negated ? std::string_view(lowered) > hexInt32MinMagnitude : digits.front() >= '8';
            }
            if(digits.size() == 16) {
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
        return digits.size() > limit.size() ||
               (digits.size() == limit.size() && std::string_view(digits) > limit);
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
        // `(0 - a) BETWEEN 1 AND 9` rather than as the negation it stands for. A COLLATE over one
        // changes nothing here: it is dropped, and the predicate is what the operand generates.
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if(dynamic_cast<const InNode*>(&generatedNode)) {
            return "IN";
        }
        if(dynamic_cast<const BetweenNode*>(&generatedNode)) {
            return "BETWEEN";
        }
        if(dynamic_cast<const LikeNode*>(&generatedNode)) {
            return "LIKE";
        }
        if(dynamic_cast<const GlobNode*>(&generatedNode)) {
            return "GLOB";
        }
        if(dynamic_cast<const MatchNode*>(&generatedNode)) {
            return "MATCH";
        }
        if(dynamic_cast<const IsNullNode*>(&generatedNode)) {
            return "IS NULL";
        }
        if(dynamic_cast<const IsNotNullNode*>(&generatedNode)) {
            return "IS NOT NULL";
        }
        if(auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
            if(unaryOp->unaryOperator == UnaryOperator::logicalNot) {
                return "NOT";
            }
        }
        return {};
    }

    bool generatesSqliteOrmCondition(const AstNode& astNode) {
        // A COLLATE and a unary plus generate their operand and nothing else, so the sqlite_orm
        // node standing here is the one the operand generates.
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if(auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
            switch(binaryOperator->binaryOperator) {
            case BinaryOperator::equals:
            case BinaryOperator::notEquals:
            case BinaryOperator::lessThan:
            case BinaryOperator::lessOrEqual:
            case BinaryOperator::greaterThan:
            case BinaryOperator::greaterOrEqual:
            case BinaryOperator::logicalAnd:
                // A comparison is a `binary_condition`, `&&` an `and_condition_t`.
                return true;
            case BinaryOperator::logicalOr:
                // An OR is an `or_condition_t` either way: `or_()` builds one whatever its operands
                // are, and the `or` spelling is only generated when one of them is a condition.
                return true;
            default:
                // The arithmetic, bitwise and concatenation operators build a `binary_operator`,
                // the JSON arrows a `json_extract()` call, and an IS never reaches codegen.
                return false;
            }
        }
        if(auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
            // `not x` is a `negated_condition_t`; a negation generates a subtraction from zero and
            // `~x` a `bitwise_not_t`, neither of them a condition.
            return unaryOperator->unaryOperator == UnaryOperator::logicalNot;
        }
        // MATCH is the one predicate missing here on purpose: `match_t` derives from nothing, so
        // sqlite_orm does not count it as a condition either.
        return dynamic_cast<const InNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const BetweenNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const LikeNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const GlobNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const IsNullNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const IsNotNullNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const ExistsNode*>(&generatedNode) != nullptr;
    }

    bool binaryOperatorNeedsCallSpelling(const BinaryOperatorNode& binaryOperatorNode) {
        switch(binaryOperatorNode.binaryOperator) {
        case BinaryOperator::logicalOr:
            // `operator||` builds the `or_condition_t` only when one of its operands is a condition.
            if(!generatesSqliteOrmCondition(*binaryOperatorNode.lhs) &&
               !generatesSqliteOrmCondition(*binaryOperatorNode.rhs)) {
                return true;
            }
            break;
        case BinaryOperator::concatenate: {
            // …and the `conc_t` only when neither of them is. `||` binds tighter than every other
            // SQL operator, so a predicate operand is delimited with a CAST already — it is the
            // grouping the serialized SQL needs — and a `cast_t` is not a condition any more. What
            // is left is the conditions sqlite_orm serializes as one term of their own: the
            // comparisons, AND, OR and EXISTS.
            auto operandStaysCondition = [](const AstNode& operand) {
                return generatesSqliteOrmCondition(operand) &&
                       serializedSqlPrecedenceAsBinaryOperand(operand) == kSqlPrecedenceTerm;
            };
            if(operandStaysCondition(*binaryOperatorNode.lhs) || operandStaysCondition(*binaryOperatorNode.rhs)) {
                return true;
            }
            break;
        }
        default:
            return false;
        }
        // An operand spelled as a call of the same operator takes the rest of the chain with it, so
        // that one chain is spelled one way throughout: `1 OR 0 OR a = 1` comes out as
        // `or_(or_(1, 0), c(&User::a) == 1)` rather than as `or_(1, 0) or c(&User::a) == 1`.
        auto operandNeedsCallSpelling = [&](const AstNode& operand) {
            auto* binaryOperand = dynamic_cast<const BinaryOperatorNode*>(&generatedOperandNode(operand));
            return binaryOperand != nullptr &&
                   binaryOperand->binaryOperator == binaryOperatorNode.binaryOperator &&
                   binaryOperatorNeedsCallSpelling(*binaryOperand);
        };
        return operandNeedsCallSpelling(*binaryOperatorNode.lhs) ||
               operandNeedsCallSpelling(*binaryOperatorNode.rhs);
    }

    NegationForm negationFormFor(const AstNode& operand) {
        // SQLite's parser folds the sign into the literal the minus stands directly over — through
        // parentheses, but NOT through a COLLATE: `-(0x8000000000000000)` is the `hex literal too
        // big` it refuses, while `-(0x8000000000000000 COLLATE BINARY)` is a negation it computes
        // (9.22337203685478e+18, checked against sqlite3 3.51). So the literal a sign is folded
        // into is the operand as written, and a COLLATE over one keeps the subtraction form.
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
            auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&generatedOperandNode(astNode));
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

    bool generatesBoundValue(const AstNode& astNode) {
        const AstNode& generatedNode = generatedOperandNode(astNode);
        return generatesFoldedNegation(generatedNode) ||
               dynamic_cast<const IntegerLiteralNode*>(&generatedNode) ||
               dynamic_cast<const RealLiteralNode*>(&generatedNode) ||
               dynamic_cast<const StringLiteralNode*>(&generatedNode) ||
               dynamic_cast<const NullLiteralNode*>(&generatedNode) ||
               dynamic_cast<const BoolLiteralNode*>(&generatedNode) ||
               dynamic_cast<const BlobLiteralNode*>(&generatedNode);
    }

    bool generatesNegatedCondition(const AstNode& astNode) {
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if(auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
            return unaryOp->unaryOperator == UnaryOperator::logicalNot;
        }
        if(auto* between = dynamic_cast<const BetweenNode*>(&generatedNode)) {
            return between->negated;
        }
        if(auto* like = dynamic_cast<const LikeNode*>(&generatedNode)) {
            return like->negated;
        }
        if(auto* glob = dynamic_cast<const GlobNode*>(&generatedNode)) {
            return glob->negated;
        }
        if(auto* match = dynamic_cast<const MatchNode*>(&generatedNode)) {
            return match->negated;
        }
        return false;
    }

    bool isLeafNode(const AstNode& astNode) {
        const AstNode& generatedNode = generatedOperandNode(astNode);
        return generatesFoldedNegation(generatedNode) ||
               dynamic_cast<const IntegerLiteralNode*>(&generatedNode) ||
               dynamic_cast<const RealLiteralNode*>(&generatedNode) ||
               dynamic_cast<const StringLiteralNode*>(&generatedNode) ||
               dynamic_cast<const NullLiteralNode*>(&generatedNode) ||
               dynamic_cast<const BoolLiteralNode*>(&generatedNode) ||
               dynamic_cast<const BlobLiteralNode*>(&generatedNode) ||
               dynamic_cast<const CurrentDatetimeLiteralNode*>(&generatedNode) ||
               dynamic_cast<const ColumnRefNode*>(&generatedNode) ||
               dynamic_cast<const QualifiedColumnRefNode*>(&generatedNode) ||
               dynamic_cast<const NewRefNode*>(&generatedNode) ||
               dynamic_cast<const OldRefNode*>(&generatedNode) ||
               dynamic_cast<const ExcludedRefNode*>(&generatedNode) ||
               dynamic_cast<const RaiseNode*>(&generatedNode);
    }

    std::string wrap(std::string_view code) {
        return "c(" + std::string(code) + ")";
    }

    namespace {

        /** Whether `functionLower` is one of `names`. */
        bool isOneOfFunctions(std::string_view functionLower, std::initializer_list<std::string_view> names) {
            return std::find(names.begin(), names.end(), functionLower) != names.end();
        }

        /**
         *  Whether SQLite answers a call of this built-in function with a value even when an
         *  argument is NULL. Checked against libsqlite3 3.45.1 over a NULL argument: `hex(NULL)` is
         *  the empty text, `quote(NULL)` the text 'NULL', `typeof(NULL)` 'null', `char(NULL)` the
         *  empty text, `json_object('k', NULL)` '{"k":null}', `json_quote(NULL)` 'null',
         *  `randomblob(NULL)` and `zeroblob(NULL)` a blob; the ones taking no argument have nothing
         *  to propagate. The aggregates here answer a value over an empty rowset too: `count` 0,
         *  `total` 0.0, `json_group_array` '[]' and `json_group_object` '{}'.
         */
        bool sqliteFunctionNeverAnswersNull(std::string_view functionLower) {
            return isOneOfFunctions(functionLower, {"changes", "char", "count", "hex", "json_group_array",
                                                    "json_group_object", "json_object", "json_quote",
                                                    "last_insert_rowid", "pi", "quote", "random",
                                                    "randomblob", "total", "total_changes", "typeof",
                                                    "zeroblob"});
        }

        /**
         *  Whether the only way SQLite answers a call of this built-in function with NULL is a NULL
         *  argument. Every other known function answers NULL over arguments the SQL spells out —
         *  `nullif(1, 1)`, `date('bogus')`, `unicode('')`, `sign('abc')`, `substr(x'', 1)`,
         *  `printf('')`, `json_extract('{}', '$.a')`, `sqrt(-1)`, `ln(0)`, `sin('a')` — or, being an
         *  aggregate, over an empty rowset: `avg`, `group_concat`, `max`, `min` and `sum`. This is
         *  the list SQLite answered a value for over every non-NULL argument of a 11.7M expression
         *  corpus run through libsqlite3 3.45.1, the version this project links, which unlike the
         *  `sqlite3` CLI in the image carries the math functions. `iif` belongs here for its
         *  three-argument form alone, which is why the name is not enough to answer with — see
         *  `iifNotInItsThreeArgumentForm`.
         */
        bool sqliteFunctionOnlyPropagatesANullArgument(std::string_view functionLower) {
            return isOneOfFunctions(functionLower, {"abs", "coalesce", "glob", "ifnull", "iif", "instr",
                                                    "json", "json_array", "json_patch", "json_valid",
                                                    "length", "like", "likelihood", "likely", "lower",
                                                    "ltrim", "replace", "round", "rtrim", "soundex",
                                                    "trim", "unlikely", "upper"});
        }

        /**
         *  Whether `functionCall` is an `iif` in any arity but three. SQLite 3.48 added
         *  `iif(X, Y)` as a spelling of `iif(X, Y, NULL)`, so it answers NULL whenever X is false
         *  whatever the two arguments hold — `SELECT iif(0, 1)` is NULL — while the three-argument
         *  form merely propagates a NULL. sqlite_orm declares the three-argument form alone, as the
         *  common type of the second and third argument, so the short one is not typed nullably
         *  either; that it has no overload at all is what an arity check would report, and this file
         *  does not run one.
         */
        bool iifNotInItsThreeArgumentForm(const FunctionCallNode& functionCall, std::string_view functionLower) {
            return functionLower == "iif" && functionCall.arguments.size() != 3;
        }

        /** Whether any of `nodes`, the absent ones skipped, may be NULL. */
        bool anyOperandMayBeNull(std::initializer_list<const AstNode*> nodes) {
            for(const AstNode* node: nodes) {
                if(node && expressionMayBeNull(*node)) {
                    return true;
                }
            }
            return false;
        }

        /** Whether any argument of `functionCall` may be NULL. */
        bool anyFunctionArgumentMayBeNull(const FunctionCallNode& functionCall) {
            for(const AstNodePointer& argument: functionCall.arguments) {
                if(argument && expressionMayBeNull(*argument)) {
                    return true;
                }
            }
            return false;
        }

        /**
         *  Whether SQLite can answer a call of `functionCall` with NULL. Only the two lists above
         *  rule it out; every other built-in answers NULL over arguments that hold none, the same
         *  way an expression this file does not know does. The two directions do not cost the same:
         *  a call widened for nothing carries an `std::optional` that is always engaged, while a
         *  call left narrow hands the caller 0 or the empty string where the row held NULL.
         */
        bool functionCallMayBeNull(const FunctionCallNode& functionCall) {
            const std::string functionLower = toLowerAscii(functionCall.name);
            if(!isKnownSqlFunction(functionLower)) {
                // A user-defined function is called through the generated struct's `operator()`,
                // and its declared return type is what the row carries back, never a NULL.
                return false;
            }
            if(sqliteFunctionNeverAnswersNull(functionLower)) {
                return false;
            }
            if(iifNotInItsThreeArgumentForm(functionCall, functionLower)) {
                return true;
            }
            if(!sqliteFunctionOnlyPropagatesANullArgument(functionLower)) {
                return true;
            }
            return anyFunctionArgumentMayBeNull(functionCall);
        }

        /**
         *  Whether sqlite_orm already types a call of this built-in function nullably. This asks
         *  what the generated C++ carries back, not what SQLite can answer — that one is
         *  `expressionMayBeNull` — so a name can belong to both lists. `abs`, `max`,
         *  `min` and `sum` are declared `std::unique_ptr`, and `coalesce`, `ifnull`, `nullif`, `iif`
         *  (its three-argument form, the only one sqlite_orm declares), `likely`, `unlikely` and
         *  `likelihood` are declared as the result of an argument, so the call is nullable exactly
         *  when sqlite_orm types that argument nullably — a nullable column makes it an
         *  `std::optional`. Widening one of those would nest a second nullable around the first.
         *  This answers over the name alone, and that is also the hole it leaves: `length(a)` and
         *  `CAST(a AS INT)` are typed `int` however NULL the row is, so `iif(1, length(a), 2)` and
         *  `likely(length(a))` are typed `int` too and read a NULL back as 0. Asking the argument
         *  rather than the name is a rule of its own; a known hole, carded separately.
         */
        bool generatedFunctionResultIsAlreadyNullable(const FunctionCallNode& functionCall) {
            const std::string functionLower = toLowerAscii(functionCall.name);
            if(iifNotInItsThreeArgumentForm(functionCall, functionLower)) {
                return false;
            }
            return isOneOfFunctions(functionLower, {"abs", "coalesce", "ifnull", "iif", "likelihood",
                                                    "likely", "max", "min", "nullif", "sum", "unlikely"});
        }

    }  // namespace

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
        if(auto* between = dynamic_cast<const BetweenNode*>(&astNode)) {
            // `1 BETWEEN NULL AND 9` is NULL and `1 BETWEEN NULL AND 0` is 0, so an operand that
            // can be NULL is what makes the whole test one. Negating it changes nothing.
            return anyOperandMayBeNull({between->operand.get(), between->low.get(), between->high.get()});
        }
        if(auto* in = dynamic_cast<const InNode*>(&astNode)) {
            if(!in->tableName.empty() || in->subquerySelect) {
                // What the right-hand side holds is not spelled out here, and `1 IN (SELECT a)`
                // over a NULL row is NULL.
                return true;
            }
            if(in->values.empty()) {
                // An empty list is the one form SQLite answers without looking at the operand:
                // `NULL IN ()` is 0.
                return false;
            }
            if(expressionMayBeNull(*in->operand)) {
                return true;
            }
            for(const AstNodePointer& value: in->values) {
                if(value && expressionMayBeNull(*value)) {
                    return true;
                }
            }
            return false;
        }
        if(auto* like = dynamic_cast<const LikeNode*>(&astNode)) {
            // The ESCAPE operand counts too: `'x' LIKE 'x' ESCAPE NULL` is NULL rather than an error.
            return anyOperandMayBeNull({like->operand.get(), like->pattern.get(), like->escape.get()});
        }
        if(auto* glob = dynamic_cast<const GlobNode*>(&astNode)) {
            return anyOperandMayBeNull({glob->operand.get(), glob->pattern.get()});
        }
        if(auto* cast = dynamic_cast<const CastNode*>(&astNode)) {
            // A CAST changes the type of a value, never whether it is one: `CAST(NULL AS TEXT)` is NULL.
            return expressionMayBeNull(*cast->operand);
        }
        if(auto* functionCall = dynamic_cast<const FunctionCallNode*>(&astNode)) {
            return functionCallMayBeNull(*functionCall);
        }
        return true;
    }

    namespace {

        /** The largest magnitude a `double` still holds every integer up to, i.e. 2^53. */
        constexpr std::int64_t kDoubleExactIntegerLimit = 9007199254740992;

        /** What a `double` has to carry for an expression, see `MagnitudeBound`. */
        enum class ResultBound {
            /** SQLite answers a REAL or a NULL, both of which a `double` carries as they are. */
            exact,
            /** SQLite answers a REAL, a NULL or an INTEGER of at most `magnitude`. */
            bounded,
            /** Nothing is known: the SQL no longer spells the value out. */
            unbounded,
        };

        /**
         *  What sqlite_orm's `double` has to carry for an expression. The magnitude is an upper
         *  bound and lives in the int64 domain SQLite computes in — rounding it into a `double`
         *  would lose the very integers this bound exists to find — so every step past the int64
         *  range saturates to `unbounded` instead of growing. An INTEGER SQLite cannot hold is a
         *  REAL anyway, which the caller reads back exactly.
         */
        struct MagnitudeBound {
            ResultBound kind = ResultBound::unbounded;
            std::int64_t magnitude = 0;
        };

        constexpr MagnitudeBound exactBound{ResultBound::exact, 0};
        constexpr MagnitudeBound unboundedBound{ResultBound::unbounded, 0};

        MagnitudeBound boundedBy(std::int64_t magnitude) {
            return MagnitudeBound{ResultBound::bounded, magnitude};
        }

        /** Whether `bound` is an INTEGER answer of at most `magnitude`. */
        bool isBounded(const MagnitudeBound& bound) {
            return bound.kind == ResultBound::bounded;
        }

        /** `left + right` over two magnitudes, both of them at least zero, saturating to `unbounded`. */
        MagnitudeBound saturatingSum(std::int64_t left, std::int64_t right) {
            static constexpr std::int64_t int64Max = std::numeric_limits<std::int64_t>::max();
            return left > int64Max - right ? unboundedBound : boundedBy(left + right);
        }

        /** `left * right` over two magnitudes, both of them at least zero, saturating to `unbounded`. */
        MagnitudeBound saturatingProduct(std::int64_t left, std::int64_t right) {
            static constexpr std::int64_t int64Max = std::numeric_limits<std::int64_t>::max();
            if(left == 0 || right == 0) {
                return boundedBy(0);
            }
            return left > int64Max / right ? unboundedBound : boundedBy(left * right);
        }

        /** The magnitude of `value` as a bound, the int64 minimum saturating to `unbounded`. */
        MagnitudeBound magnitudeOf(std::int64_t value) {
            static constexpr std::int64_t int64Min = std::numeric_limits<std::int64_t>::min();
            // The magnitude of the int64 minimum does not fit an int64, and it is past the range a
            // double holds exactly anyway.
            return value == int64Min ? unboundedBound : boundedBy(value < 0 ? -value : value);
        }

        /**
         *  What a `double` has to carry for the value SQLite answers `astNode` with. A REAL and a
         *  NULL are carried as they are, so a node answering one of those is `exact`: `+`, `-`,
         *  `*`, `/` and `%` all answer a REAL as soon as an operand is one and a NULL as soon as
         *  an operand is one — checked over 1710 operand pairs against sqlite3 3.51. An INTEGER is
         *  bounded only while the SQL spells the operands out; a column, a call or a subquery is a
         *  value only SQLite knows.
         */
        MagnitudeBound integerMagnitudeBound(const AstNode& astNode) {
            if(auto* collate = dynamic_cast<const CollateNode*>(&astNode)) {
                // COLLATE decides how a value compares, not what the value is.
                return integerMagnitudeBound(*collate->operand);
            }
            std::size_t foldedSigns = 0;
            const AstNode& literal = *withoutFoldedSigns(astNode, foldedSigns);
            if(dynamic_cast<const IntegerLiteralNode*>(&literal) != nullptr) {
                const std::optional<std::int64_t> value = integerLiteralInt64Value(astNode);
                // A decimal literal past the int64 range — the sign SQLite folds in already
                // counted, so `-9223372036854775808` is not one — is a REAL to SQLite.
                return value ? magnitudeOf(*value) : exactBound;
            }
            if(dynamic_cast<const RealLiteralNode*>(&literal) != nullptr ||
               dynamic_cast<const NullLiteralNode*>(&literal) != nullptr) {
                return exactBound;
            }
            if(dynamic_cast<const BoolLiteralNode*>(&literal) != nullptr) {
                // SQLite spells TRUE and FALSE as the integers 1 and 0.
                return boundedBy(1);
            }
            if(auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
                switch(unaryOperator->unaryOperator) {
                case UnaryOperator::plus:
                case UnaryOperator::minus:
                    // `+x` is a no-op to SQLite and a negation keeps the magnitude it is given.
                    return integerMagnitudeBound(*unaryOperator->operand);
                case UnaryOperator::bitwiseNot: {
                    // `~` casts its operand to an INTEGER first, so a REAL operand is not carried
                    // through it the way it is through the arithmetic operators: `~1.5` answers
                    // the INTEGER -2 and `~1e300` the int64 minimum. Only an operand already
                    // bounded as an INTEGER bounds `~x`, which is `-x - 1`; a NULL answers NULL.
                    if(dynamic_cast<const NullLiteralNode*>(unaryOperator->operand.get()) != nullptr) {
                        return exactBound;
                    }
                    const MagnitudeBound operand = integerMagnitudeBound(*unaryOperator->operand);
                    return isBounded(operand) ? saturatingSum(operand.magnitude, 1) : unboundedBound;
                }
                case UnaryOperator::logicalNot:
                    // `NOT x` answers 0, 1 or NULL.
                    return boundedBy(1);
                }
                return unboundedBound;
            }
            if(auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&astNode)) {
                switch(binaryOperator->binaryOperator) {
                case BinaryOperator::add:
                case BinaryOperator::subtract:
                case BinaryOperator::multiply:
                case BinaryOperator::divide:
                case BinaryOperator::modulo:
                    break;
                default:
                    // Every other operator either answers a value of its own — a bitwise result is
                    // an INTEGER of any magnitude, a comparison a 0, 1 or NULL — or is not an
                    // operand an arithmetic result column is reached through.
                    return unboundedBound;
                }
                const MagnitudeBound left = integerMagnitudeBound(*binaryOperator->lhs);
                const MagnitudeBound right = integerMagnitudeBound(*binaryOperator->rhs);
                if(left.kind == ResultBound::exact || right.kind == ResultBound::exact) {
                    // A REAL operand makes the whole answer a REAL and a NULL operand a NULL.
                    return exactBound;
                }
                switch(binaryOperator->binaryOperator) {
                case BinaryOperator::add:
                case BinaryOperator::subtract:
                    return isBounded(left) && isBounded(right) ? saturatingSum(left.magnitude, right.magnitude)
                                                               : unboundedBound;
                case BinaryOperator::multiply:
                    // A factor of zero answers zero however large the other side is.
                    if((isBounded(left) && left.magnitude == 0) || (isBounded(right) && right.magnitude == 0)) {
                        return boundedBy(0);
                    }
                    return isBounded(left) && isBounded(right) ? saturatingProduct(left.magnitude, right.magnitude)
                                                               : unboundedBound;
                case BinaryOperator::divide:
                    // An integer division never grows the dividend, and a zero divisor is NULL, so
                    // the dividend bounds the quotient whatever the divisor is.
                    return left;
                case BinaryOperator::modulo:
                    // A remainder is smaller than both the dividend and the divisor, so either of
                    // them bounds it on its own.
                    if(isBounded(left) && isBounded(right)) {
                        return boundedBy(std::min(left.magnitude, right.magnitude));
                    }
                    return isBounded(left) ? left : right;
                default:
                    return unboundedBound;
                }
            }
            return unboundedBound;
        }

        /** An arithmetic operator node and the SQL token it is spelled with. */
        struct ArithmeticOperatorNode {
            const AstNode* node = nullptr;
            std::string_view operatorText;
        };

        /**
         *  The arithmetic operator a result column is read back through, together with the node it
         *  is located at; an empty text for a column read back through anything else.
         */
        ArithmeticOperatorNode doubleTypedResultOperator(const AstNode& astNode) {
            // A COLLATE and a unary plus emit their operand and nothing else, so the sqlite_orm
            // node the result column comes out as is the one the operand under them comes out as.
            const AstNode& generatedNode = generatedOperandNode(astNode);
            if(auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
                // A negation reaches sqlite_orm as the subtraction `0 - x`, which is typed
                // `double` like the other arithmetic operators; the folded and the warned forms
                // carry no operator of their own.
                if(unaryOperator->unaryOperator == UnaryOperator::minus &&
                   negationFormFor(*unaryOperator->operand) == NegationForm::zeroMinusSubtraction) {
                    return {&generatedNode, "-"};
                }
                return {};
            }
            if(auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
                switch(binaryOperator->binaryOperator) {
                case BinaryOperator::add:
                    return {&generatedNode, "+"};
                case BinaryOperator::subtract:
                    return {&generatedNode, "-"};
                case BinaryOperator::multiply:
                    return {&generatedNode, "*"};
                case BinaryOperator::divide:
                    return {&generatedNode, "/"};
                case BinaryOperator::modulo:
                    return {&generatedNode, "%"};
                default:
                    return {};
                }
            }
            return {};
        }

    }  // namespace

    bool selectResultNeedsIntegerCast(const AstNode& astNode) {
        // A COLLATE and a unary plus emit their operand and nothing else, so the sqlite_orm node
        // the result column comes out as — and with it the type the row is read back into — is the
        // one the operand under them comes out as.
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if(auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
            return unaryOperator->unaryOperator == UnaryOperator::bitwiseNot;
        }
        if(auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
            switch(binaryOperator->binaryOperator) {
            case BinaryOperator::bitwiseAnd:
            case BinaryOperator::bitwiseOr:
            case BinaryOperator::shiftLeft:
            case BinaryOperator::shiftRight:
                return true;
            default:
                return false;
            }
        }
        return false;
    }

    std::optional<CodegenWarning> selectResultDoublePrecisionWarning(const AstNode& astNode) {
        const ArithmeticOperatorNode arithmetic = doubleTypedResultOperator(astNode);
        if(arithmetic.operatorText.empty()) {
            return std::nullopt;
        }
        const MagnitudeBound bound = integerMagnitudeBound(astNode);
        if(bound.kind == ResultBound::exact ||
           (isBounded(bound) && bound.magnitude <= kDoubleExactIntegerLimit)) {
            return std::nullopt;
        }
        std::string message = "result column computed with `";
        message += arithmetic.operatorText;
        message +=
            "` is read back through a double: sqlite_orm types `+`, `-`, `*`, `/` and `%` as "
            "`double`, so an INTEGER result past 2^53 comes back rounded (9223372036854775807 "
            "reads back as 9223372036854775808)";
        // The operator token is what its node is located at, and it is what the message names.
        return CodegenWarning{std::move(message), arithmetic.node->location, arithmetic.operatorText.size()};
    }

    bool selectResultNeedsAsOptional(const AstNode& astNode) {
        // A COLLATE and a unary plus emit their operand and nothing else, so the sqlite_orm node
        // the result column comes out as — and with it the type the row is read back into — is the
        // one the operand under them comes out as.
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if(auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
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
                return expressionMayBeNull(generatedNode);
            }
        }
        if(auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
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
            return expressionMayBeNull(generatedNode);
        }
        if(dynamic_cast<const BetweenNode*>(&generatedNode) || dynamic_cast<const InNode*>(&generatedNode) ||
           dynamic_cast<const LikeNode*>(&generatedNode) || dynamic_cast<const GlobNode*>(&generatedNode)) {
            // sqlite_orm types `between_t`, `in_t`, `like_t` and `glob_t` — and the
            // `negated_condition_t` a NOT form comes out as — as `bool`, so a NULL row was read
            // back as false. A MATCH is left out: SQLite refuses `a MATCH 'x'` as a result column
            // outside an FTS table, and sqlite_orm has no result type for `match_t` either.
            return expressionMayBeNull(generatedNode);
        }
        if(dynamic_cast<const CastNode*>(&generatedNode)) {
            // `cast_t<T, E>` is typed T, the very type the CAST asks for, so a NULL row was read
            // back as the default of that type: `CAST(a AS TEXT)` came back as the empty string.
            return expressionMayBeNull(generatedNode);
        }
        if(auto* functionCall = dynamic_cast<const FunctionCallNode*>(&generatedNode)) {
            if(functionCall->over) {
                // A window call is left as it is generated. `row_number`, `rank`, `dense_rank`,
                // `percent_rank`, `cume_dist` and `ntile` are never NULL, and `lag`, `lead`,
                // `first_value`, `last_value` and `nth_value` are typed as their argument, so a
                // nullable argument carries its nullability through on its own. What this arm does
                // not cover is the NULL the window itself answers: over a NOT NULL column the
                // argument is a plain `int64_t`, while `lag(b) OVER (ORDER BY b)` is NULL on the
                // first row, and that NULL still reads back as 0. A known hole, carded separately.
                return false;
            }
            if(generatedFunctionResultIsAlreadyNullable(*functionCall)) {
                return false;
            }
            return expressionMayBeNull(generatedNode);
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
        // `DELETE_` rather than `DELETE`: the Windows SDK defines `DELETE` as a macro, and
        // sqlite_orm's journal_mode header only hides it while the enum is being declared, so in a
        // user translation unit that included <windows.h> the name is a macro again. sqlite_orm
        // declares `DELETE_ = DELETE` for exactly that, and it spells the same value everywhere.
        if(lower == "delete") return std::string{"sqlite_orm::journal_mode::DELETE_"};
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

    namespace {

        /**
         *  The name a keyword PRAGMA value reads as. SQLite's `nmnum` rule falls `ON`, `TRUE`,
         *  `FALSE` and `CURRENT_DATE` and its siblings back to a bare name, and the PRAGMA's reader
         *  gets those very letters — so `PRAGMA integrity_check(on)` looks a table called `on` up,
         *  it does not pass a boolean on.
         */
        std::optional<std::string> pragmaKeywordValueName(const AstNode& valueNode) {
            if(const auto* boolLiteral = dynamic_cast<const BoolLiteralNode*>(&valueNode)) {
                return std::string(boolLiteral->spelling);
            }
            if(const auto* currentDatetime = dynamic_cast<const CurrentDatetimeLiteralNode*>(&valueNode)) {
                switch(currentDatetime->kind) {
                    case CurrentDatetimeKind::date: return std::string{"current_date"};
                    case CurrentDatetimeKind::time: return std::string{"current_time"};
                    default:                        return std::string{"current_timestamp"};
                }
            }
            return std::nullopt;
        }

    }  // namespace

    std::optional<std::string> pragmaTableNameLiteral(const AstNode& valueNode) {
        if(const auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(&valueNode)) {
            return sqlStringToCpp(stringLiteral->value);
        }
        if(const auto* columnRef = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
            return identifierToCppStringLiteral(columnRef->columnName);
        }
        if(auto keywordName = pragmaKeywordValueName(valueNode)) {
            return identifierToCppStringLiteral(*keywordName);
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
            return PragmaValue{std::string(integerLiteral->value), std::string(integerLiteral->value),
                               integerLiteral->location, underlineLengthOf(integerLiteral->value)};
        }
        if(const auto* realLiteral = dynamic_cast<const RealLiteralNode*>(&valueNode)) {
            return PragmaValue{std::string(realLiteral->value), std::string(realLiteral->value),
                               realLiteral->location, underlineLengthOf(realLiteral->value)};
        }
        if(const auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(&valueNode)) {
            return PragmaValue{sqlStringLiteralText(stringLiteral->value), std::string(stringLiteral->value),
                               stringLiteral->location, underlineLengthOf(stringLiteral->value)};
        }
        if(const auto* columnRef = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
            return PragmaValue{stripIdentifierQuotes(columnRef->columnName), std::string(columnRef->columnName),
                               columnRef->location, underlineLengthOf(columnRef->columnName)};
        }
        if(auto keywordName = pragmaKeywordValueName(valueNode)) {
            // A keyword here is a name, not the literal it looks like: `TRUE` is the four letters
            // the reader gets and `CURRENT_DATE` is not today's date.
            return PragmaValue{*keywordName, *keywordName, valueNode.location, underlineLengthOf(*keywordName)};
        }
        if(const auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&valueNode)) {
            const bool numericOperand =
                unaryOperator->operand && (dynamic_cast<const IntegerLiteralNode*>(unaryOperator->operand.get()) ||
                                           dynamic_cast<const RealLiteralNode*>(unaryOperator->operand.get()));
            if(unaryOperator->unaryOperator == UnaryOperator::minus && numericOperand) {
                const PragmaValue operand = *pragmaValue(*unaryOperator->operand);
                // The minus sign belongs to the value the message quotes, so the underline starts
                // at the sign — as far as it shares the literal's line, the operand alone otherwise.
                SourceLocation location = operand.location;
                size_t length = operand.length;
                if(unaryOperator->location.line == location.line &&
                   unaryOperator->location.column < location.column) {
                    length = location.column + length - unaryOperator->location.column;
                    location = unaryOperator->location;
                }
                return PragmaValue{"-" + operand.text, "-" + operand.sqlText, location, length};
            }
        }
        return std::nullopt;
    }

    CodegenWarning pragmaValueWarning(std::string message, const PragmaValue& value) {
        if(value.length == 0) {
            return CodegenWarning{std::move(message)};
        }
        return CodegenWarning{std::move(message), value.location, value.length};
    }

    CodegenWarning pragmaValueWarning(std::string message, const AstNode& valueNode) {
        const std::optional<PragmaValue> value = pragmaValue(valueNode);
        if(!value) {
            return CodegenWarning{std::move(message)};
        }
        return pragmaValueWarning(std::move(message), *value);
    }

}  // namespace sqlite2orm
