#include "codegen_utils.h"
#include "codegen_context.h"

#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace sqlite2orm {

    bool policyEquals(const CodeGenPolicy* policy, std::string_view category, std::string_view value) {
        if (!policy) {
            return false;
        }
        const auto it = policy->chosenAlternativeValueByCategory.find(std::string(category));
        return it != policy->chosenAlternativeValueByCategory.end() && it->second == value;
    }

    CodeGenPolicy policyWithOverride(const CodeGenPolicy* base, std::string_view category, std::string_view value) {
        CodeGenPolicy result;
        if (base) {
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
        for (const char c: stripIdentifierQuotes(savepointName)) {
            variableName += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
        }
        if (variableName.empty() || std::isdigit(static_cast<unsigned char>(variableName.front()))) {
            variableName.insert(variableName.begin(), '_');
        }
        return variableName + "_savepoint";
    }

    std::string colaliasBuiltinSlot(size_t slotIndex) {
        static constexpr std::array<std::string_view, 9> kBuiltinSlots = {
            "colalias_a{}",
            "colalias_b{}",
            "colalias_c{}",
            "colalias_d{}",
            "colalias_e{}",
            "colalias_f{}",
            "colalias_g{}",
            "colalias_h{}",
            "colalias_i{}",
        };
        if (slotIndex < kBuiltinSlots.size()) {
            return std::string(kBuiltinSlots[slotIndex]);
        }
        char letter = static_cast<char>('a' + slotIndex);
        if (letter > 'z') {
            letter = 'a';
        }
        return "sqlite_orm::internal::column_alias<'" + std::string(1, letter) + "'>{}";
    }

    std::string stripIdentifierQuotes(std::string_view identifier) {
        if (identifier.size() >= 2) {
            char first = identifier.front();
            char last = identifier.back();
            if ((first == '"' && last == '"') || (first == '\'' && last == '\'') || (first == '`' && last == '`') ||
                (first == '[' && last == ']')) {
                return std::string(identifier.substr(1, identifier.size() - 2));
            }
        }
        return std::string(identifier);
    }

    namespace {

        /** `value` in upper-case hexadecimal, padded with leading zeros to `digits` of them. */
        std::string hexDigits(char32_t value, size_t digits) {
            static constexpr std::string_view kDigits = "0123456789ABCDEF";
            std::string result(digits, '0');
            for (size_t index = digits; index > 0; --index) {
                result[index - 1] = kDigits[value & 0xFu];
                value >>= 4;
            }
            return result;
        }

        /**
         *  The keywords of C++ through C++26, the alternative spellings of operators (`and`, `not`)
         *  included: words spelled like an identifier that no declaration can be named. Sorted, as
         *  a name is looked up by bisection.
         */
        constexpr std::array<std::string_view, 93> kCppKeywords = {
            "alignas",
            "alignof",
            "and",
            "and_eq",
            "asm",
            "auto",
            "bitand",
            "bitor",
            "bool",
            "break",
            "case",
            "catch",
            "char",
            "char16_t",
            "char32_t",
            "char8_t",
            "class",
            "co_await",
            "co_return",
            "co_yield",
            "compl",
            "concept",
            "const",
            "const_cast",
            "consteval",
            "constexpr",
            "constinit",
            "continue",
            "contract_assert",
            "decltype",
            "default",
            "delete",
            "do",
            "double",
            "dynamic_cast",
            "else",
            "enum",
            "explicit",
            "export",
            "extern",
            "false",
            "float",
            "for",
            "friend",
            "goto",
            "if",
            "inline",
            "int",
            "long",
            "mutable",
            "namespace",
            "new",
            "noexcept",
            "not",
            "not_eq",
            "nullptr",
            "operator",
            "or",
            "or_eq",
            "private",
            "protected",
            "public",
            "register",
            "reinterpret_cast",
            "requires",
            "return",
            "short",
            "signed",
            "sizeof",
            "static",
            "static_assert",
            "static_cast",
            "struct",
            "switch",
            "template",
            "this",
            "thread_local",
            "throw",
            "true",
            "try",
            "typedef",
            "typeid",
            "typename",
            "union",
            "unsigned",
            "using",
            "virtual",
            "void",
            "volatile",
            "wchar_t",
            "while",
            "xor",
            "xor_eq",
        };
        static_assert(std::is_sorted(kCppKeywords.begin(), kCppKeywords.end()));

        bool isCppKeyword(std::string_view name) {
            return std::binary_search(kCppKeywords.begin(), kCppKeywords.end(), name);
        }

        /** `sqlName` spelled in the characters of a C++ identifier; see `toCppIdentifier()`. */
        std::string cppIdentifierCharacters(std::string_view sqlName) {
            auto stripped = stripIdentifierQuotes(sqlName);
            std::string result;
            result.reserve(stripped.size());
            for (size_t index = 0; index < stripped.size();) {
                const Utf8Character character = decodeUtf8Character(std::string_view(stripped).substr(index));
                index += character.length;
                if (!character.valid) {
                    result += 'x' + hexDigits(character.codePoint, 2);
                    continue;
                }
                // The ASCII classification is spelled out rather than asked of <cctype>, whose answer
                // for the bytes above 0x7F depends on the locale the program happens to run in.
                const char32_t codePoint = character.codePoint;
                if ((codePoint >= 'a' && codePoint <= 'z') || (codePoint >= 'A' && codePoint <= 'Z') ||
                    (codePoint >= '0' && codePoint <= '9') || codePoint == '_') {
                    result += static_cast<char>(codePoint);
                } else if (codePoint < 0x80) {
                    result += '_';
                } else if (codePoint <= 0xFFFF) {
                    result += 'u' + hexDigits(codePoint, 4);
                } else {
                    result += 'U' + hexDigits(codePoint, 8);
                }
            }
            if (result.empty()) {
                // SQLite takes an empty name — `CREATE TABLE t("" INTEGER)` is a table with a column
                // called nothing — and C++ has no identifier of no characters, so the one character
                // that carries no letters of its own stands for it.
                return "_";
            }
            if (result[0] >= '0' && result[0] <= '9') {
                result = "_" + result;
            }
            return result;
        }

    }  // namespace

    std::string toCppIdentifier(std::string_view sqlName) {
        std::string result = cppIdentifierCharacters(sqlName);
        if (isCppKeyword(result)) {
            // `class` and `char` are ordinary column names, and a member cannot be named either:
            // the trailing `_` is how sqlite_orm itself names such things (`char_`, `typeof_`).
            result += '_';
        }
        return result;
    }

    bool isAnnotationConstantValueCode(std::string_view code) {
        // `default_value(nullptr)` is a `default_t<std::nullptr_t>`, a structural type the compiler
        // folds like the numbers below.
        if (code == "true" || code == "false" || code == "nullptr") {
            return true;
        }
        std::string_view body = code;
        if (!body.empty() && (body.front() == '-' || body.front() == '+')) {
            body.remove_prefix(1);
        }
        if (body.empty() || !std::isdigit(static_cast<unsigned char>(body.front()))) {
            return false;
        }
        // Everything a numeric literal is spelled with: the digits and hex letters, the radix and
        // exponent markers, an exponent's sign, the digit separator `'` and the integer/floating-point
        // suffixes. A call, a string or an operator brings a character that is none of them — and so
        // does `_`, which outside a user-defined-literal suffix is no part of a numeric literal at
        // all (SQLite's own separator is rewritten to `'` long before a default gets here).
        static constexpr std::string_view kNumericLiteralCharacters = "0123456789abcdefABCDEF.xXpP+-'LlUuFf";
        return body.find_first_not_of(kNumericLiteralCharacters) == std::string_view::npos;
    }

    std::string identifierToCppStringLiteral(std::string_view sqlIdentifier) {
        return cppStringLiteral(stripIdentifierQuotes(sqlIdentifier));
    }

    std::string sqlStringLiteralText(std::string_view literal) {
        if (literal.size() < 2) {
            return std::string(literal);
        }
        const char quote = literal.front();
        const std::string_view content = literal.substr(1, literal.size() - 2);
        std::string result;
        result.reserve(content.size());
        for (size_t index = 0; index < content.size(); ++index) {
            if (content[index] == quote && index + 1 < content.size() && content[index + 1] == quote) {
                ++index;
            }
            result += content[index];
        }
        return result;
    }

    std::string cppStringLiteral(std::string_view text) {
        std::string result = "\"";
        result.reserve(text.size() + 2);
        for (size_t index = 0; index < text.size();) {
            const Utf8Character character = decodeUtf8Character(text.substr(index));
            index += character.length;
            const char32_t codePoint = character.codePoint;
            if (character.valid && codePoint >= 0x80) {
                result += text.substr(index - character.length, character.length);
            } else if (codePoint == '\\') {
                result += "\\\\";
            } else if (codePoint == '"') {
                result += "\\\"";
            } else if (codePoint == '\n') {
                result += "\\n";
            } else if (codePoint == '\r') {
                result += "\\r";
            } else if (codePoint == '\t') {
                result += "\\t";
            } else if (codePoint < 0x20 || codePoint >= 0x7F) {
                // Any other control character, and a byte that is no UTF-8 character at all (SQLite
                // takes both in a name), is written as its three-digit octal escape: unlike `\x`,
                // which runs on through every hex digit after it, an octal escape stops after three
                // digits, so a digit that follows in the text stays a character of its own.
                result += '\\';
                result += static_cast<char>('0' + ((codePoint >> 6) & 7));
                result += static_cast<char>('0' + ((codePoint >> 3) & 7));
                result += static_cast<char>('0' + (codePoint & 7));
            } else {
                result += static_cast<char>(codePoint);
            }
        }
        result += '"';
        return result;
    }

    std::string sqlStringToCpp(std::string_view sqlString) {
        return cppStringLiteral(sqlStringLiteralText(sqlString));
    }

    std::string stripColumnAliasQuotes(std::string_view alias) {
        if (alias.size() >= 2) {
            char first = alias.front();
            char last = alias.back();
            if ((first == '\'' && last == '\'') || (first == '"' && last == '"') || (first == '`' && last == '`') ||
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
        if (isBuiltinColalias(stripped)) {
            return "colalias_" + stripped;
        }
        // The name only begins the struct's (`class` makes `ClassAlias`), which no keyword can be.
        std::string name = cppIdentifierCharacters(stripped);
        if (!name.empty() && std::islower(static_cast<unsigned char>(name[0]))) {
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
        for (const auto& column: columns) {
            if (column.alias.empty())
                continue;
            if (!needsCustomAliasStruct(column.alias))
                continue;
            std::string typeName = columnAliasTypeName(column.alias);
            bool alreadyEmitted = false;
            for (const auto& existing: emitted) {
                if (existing == typeName) {
                    alreadyEmitted = true;
                    break;
                }
            }
            if (alreadyEmitted)
                continue;
            emitted.push_back(typeName);
            preamble += "struct " + typeName +
                        " : sqlite_orm::alias_tag {\n"
                        "    static const std::string& get() {\n"
                        "        static const std::string res = " +
                        cppStringLiteral(stripColumnAliasQuotes(column.alias)) +
                        ";\n"
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
        for (const auto& column: columns) {
            if (column.alias.empty())
                continue;
            std::string variableName = columnAliasCpp20VarName(column.alias);
            bool already = false;
            for (const auto& existing: emittedVars) {
                if (existing == variableName) {
                    already = true;
                    break;
                }
            }
            if (already)
                continue;
            emittedVars.push_back(variableName);
            std::string literal = identifierToCppStringLiteral(stripColumnAliasQuotes(column.alias));
            body += "constexpr orm_column_alias auto " + variableName + " = " + literal + "_col;\n";
        }
        return body;
    }

    SpannedCode wrapWithColumnAlias(SpannedCode expressionCode, const std::string& rawAlias, bool cpp20Style) {
        if (rawAlias.empty())
            return expressionCode;
        if (cpp20Style) {
            return "as<" + columnAliasCpp20VarName(rawAlias) + ">(" + expressionCode + ")";
        }
        return "as<" + columnAliasTypeName(rawAlias) + ">(" + expressionCode + ")";
    }

    bool hasAnyColumnAlias(const std::vector<SelectColumn>& columns) {
        for (const auto& column: columns) {
            if (!column.alias.empty())
                return true;
        }
        return false;
    }

    SourceSpan firstColumnAliasSpan(const std::vector<SelectColumn>& columns) {
        for (const auto& column: columns) {
            if (!column.aliasSpan.text.empty())
                return column.aliasSpan;
        }
        return SourceSpan{};
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
            "address",  "author",     "body",       "caption",     "city",     "comment",  "comments",  "company",
            "country",  "currency",   "department", "description", "domain",   "email",    "firstname", "headline",
            "hostname", "iban",       "label",      "language",    "lastname", "locale",   "login",     "message",
            "name",     "nickname",   "notes",      "path",        "phone",    "referrer", "region",    "signature",
            "slug",     "street",     "subtitle",   "surname",     "swift",    "timezone", "title",     "uri",
            "url",      "user_agent", "uuid",       "zip",
        }};
        for (std::string_view hint: kLikelyText) {
            if (lower == hint) {
                return "std::string";
            }
        }
        return "int";
    }

    std::string widerInferredCppType(std::string_view left, std::string_view right) {
        // The order the types widen in. `bool` sits below `int` because SQLite spells TRUE as the
        // integer 1, and `std::string` sits above everything: sqlite3_column_text renders an
        // INTEGER and a REAL as the text SQLite prints for them, while a number read out of a TEXT
        // value is 0.
        // `std::vector<char>` — the field of a BLOB column — sits above that: it reads every
        // storage class as the bytes of it, while an `std::string` stops at the first NUL byte a
        // blob holds.
        static constexpr std::array<std::string_view, 6> kWideningOrder{{
            "bool",
            "int",
            "int64_t",
            "double",
            "std::string",
            "std::vector<char>",
        }};
        // `int64_t` and `double` hold values the other one does not, so neither widens into the
        // other: a `double` loses every integer past 2^53 — `9007199254740993` read through one
        // comes back as `9007199254740992` — while an `int64_t` loses the fractional part of a
        // REAL. The two are siblings in the order above rather than neighbours, and the type that
        // holds both is the `std::string` over them. `int` and `bool` do widen into a `double`,
        // which holds every value an int32 has exactly.
        if ((left == "int64_t" && right == "double") || (left == "double" && right == "int64_t")) {
            return "std::string";
        }
        const auto rank = [](std::string_view type) -> std::optional<std::size_t> {
            for (std::size_t index = 0; index < kWideningOrder.size(); ++index) {
                if (kWideningOrder.at(index) == type) {
                    return index;
                }
            }
            return std::nullopt;
        };
        const std::optional<std::size_t> leftRank = rank(left);
        const std::optional<std::size_t> rightRank = rank(right);
        if (!leftRank || !rightRank) {
            // Neither caller hands over a type the order above does not name: the order holds
            // every type `CodeGeneratorContext::inferTypeFromNode` answers with and every field a
            // schema column is read into. This keeps the fold total rather than describing a
            // widening of its own: what the caller accumulated so far comes back unchanged.
            return std::string(left);
        }
        return std::string(kWideningOrder.at(std::max(*leftRank, *rightRank)));
    }

    const std::string kCommentCpp20ColumnAliases =
        "C++20 literal column aliases (`orm_column_alias`, string literal `_col`) require sqlite_orm to be "
        "built with the preprocessor macro SQLITE_ORM_WITH_CPP20_ALIASES defined. Your project may enable "
        "that via CMake target_compile_definitions, compiler `-D`, a config header, or any other suitable "
        "mechanism.";

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

    const std::string kCommentConcatenationCast =
        "A NOT over a concatenation is generated as `not cast<double>(…)`: sqlite_orm's `conc_t` is "
        "`binary_operator<L, R, conc_string>` and nothing else — neither negatable nor an operator "
        "argument — so `not (c(&T::a) || \"x\")` does not compile. A concatenation answers TEXT or "
        "NULL, and SQLite reads the truth of a text value through its real value: `NOT ('0' || '.5')` "
        "is 0, where `NOT CAST('0' || '.5' AS INTEGER)` is 1. A CAST to REAL parses the text exactly "
        "as that truth test does, so `NOT x` and `NOT CAST(x AS REAL)` answer alike.";

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

    const std::string kCommentNotPredicateArgumentCast =
        "A NOT in the argument of a predicate is generated as `cast<int64_t>(…)`: sqlite_orm "
        "serializes a negation as a prefix `NOT` — `!between(…)`, `!like(…)` and `not x` alike — and "
        "IN, BETWEEN, LIKE, GLOB, MATCH and IS [NOT] NULL with no parentheses around their "
        "arguments, and SQLite binds NOT looser than every predicate, so `is_null(!between(b, 1, 2))` "
        "would be read back as `NOT (b BETWEEN 1 AND 2 IS NULL)`. The CAST delimits the argument and "
        "leaves what it stands for alone — a NOT is 0, 1 or NULL, and a CAST to INTEGER keeps all "
        "three, typeof included.";

    const std::string kCommentPredicatePatternCast =
        "A predicate in the pattern or the ESCAPE of a LIKE, GLOB or MATCH is generated as "
        "`cast<int64_t>(…)`: sqlite_orm serializes those with no parentheses around their arguments, "
        "and SQLite reads the predicates left-associatively at one rank, so `a LIKE (b IS NULL)` "
        "would be read back as `(a LIKE b) IS NULL`. The CAST delimits the predicate and leaves what "
        "it stands for alone — a predicate is 0, 1 or NULL, and a CAST to INTEGER keeps all three, "
        "typeof included.";

    const std::string kCommentAndOrQuotedOperand =
        "An operand of an AND or an OR that sqlite_orm does not recognize is generated as "
        "`c(operand)`: `or_()` and `and_()` assert that both arguments are bindable values or "
        "operands sqlite_orm knows, and `operator&&` is declared only where one of the operands "
        "is a condition or an operator argument. A MATCH, a CURRENT_DATE / CURRENT_TIME / "
        "CURRENT_TIMESTAMP, a window call and a FILTERed aggregate are none of those, and a pair "
        "like `a + 1 AND a + 2` is neither, so `b MATCH 'x' OR b MATCH 'y'` and `a + 1 AND a + 2` "
        "would not compile at all. `c()` hands the expression over as it stands: `or_()`, `and_()` "
        "and `operator&&` all unwrap the `quoted_expression_t` back to the expression it holds, so "
        "the condition built — and the SQL it serializes to — is the one the bare operand would "
        "have built.";

    const std::string kCommentConcatenationQuotedOperand =
        "An operand of a `||` concatenation that sqlite_orm does not recognize is generated as "
        "`c(operand)`: the concatenation `operator||` is declared only where one of the operands is "
        "a concatenation or an operator argument. An arithmetic operator is neither, and a built-in "
        "function call is one only where the headers take their C++20 path — g++ does, Apple clang "
        "does not — so `upper(t) || 'x'` would compile on one compiler and not on the other. `c()` "
        "hands the expression over as it stands: `operator||` unwraps the `quoted_expression_t` back "
        "to the expression it holds, so the SQL it serializes to is the one the bare operand would "
        "have given.";

    const std::string kCommentBetweenBoundsWidened =
        "The bounds of a BETWEEN are generated as `static_cast<int64_t>(…)`: sqlite_orm's "
        "`between(A, T, T)` deduces one C++ type from the two of them, and C++ types an integer "
        "constant by its magnitude, so `between(&User::a, 1, 3000000000)` is an `int` next to a "
        "64-bit constant and does not compile. The cast goes on every bound that is not already "
        "an `int64_t`, rather than on the narrower one: the type a 64-bit constant is given is "
        "`long` where an `int64_t` is a `long long`, and the two are distinct types even where "
        "both are 64 bits wide. It leaves the values alone — SQLite carries every INTEGER as a "
        "signed 64-bit number anyway, TRUE and FALSE among them.";

    const std::string kCommentInValuesWidened =
        "The values of an IN list are generated as `static_cast<int64_t>(…)`: sqlite_orm's "
        "`in(A, std::initializer_list<E>)` deduces one C++ type from the whole list, and C++ types "
        "an integer constant by its magnitude, so `in(&User::a, {1, 3000000000})` is an `int` next "
        "to a 64-bit constant and does not compile. The cast goes on every value that is not "
        "already an `int64_t`, rather than on the narrower ones: the type a 64-bit constant is "
        "given is `long` where an `int64_t` is a `long long`, and the two are distinct types even "
        "where both are 64 bits wide. It leaves the values alone — SQLite carries every INTEGER as "
        "a signed 64-bit number anyway, TRUE and FALSE among them. A `bindParamN` beside them is "
        "not cast: its type is the one you declare, and a cast there would change the value it "
        "binds rather than only its type, so declare it `int64_t`.";

    const std::string kCommentOrTokenCallSpelling =
        "`OR` is generated as `or_(left, right)` and `||` as `conc(left, right)`: C++ spells both "
        "of them `||`, and sqlite_orm picks between the two by the operands — `operator||` builds "
        "the OR condition only when one of them is a condition (a comparison, AND, OR, IN, BETWEEN, "
        "LIKE, GLOB, IS [NOT] NULL, EXISTS or NOT) and the concatenation otherwise. So `1 OR 0` "
        "spelled `c(1) or 0` runs as `1 || 0` and answers '10', and `(a = 1) || 'x'` spelled "
        "`c(&T::a) == 1 || \"x\"` runs as `(a = 1) OR 'x'`. The call names the node it builds.";

    const std::string kCommentOrMatchLiteralKept =
        "A constant beside a MATCH under an OR is generated as "
        "`c(internal::literal_holder<T>{value})`: sqlite_orm binds every other value as a "
        "parameter, and SQLite folds only a constant written into the statement. `body MATCH 'x' "
        "OR 1` is always true and never calls MATCH, while `body MATCH ? OR ?` leaves MATCH "
        "outside the FTS index and fails at run time with `unable to use function MATCH in the "
        "requested context`. The `literal_holder` is serialized into the SQL as written, and `c()` "
        "hands it to `or_()`, which is the only spelling that keeps it.";

    const std::string kCommentDistinctFromSqliteVersion =
        "`IS DISTINCT FROM` is generated as `is_distinct_from(left, right)` and `IS NOT DISTINCT "
        "FROM` as `is_not_distinct_from(left, right)`, which sqlite_orm declares only where the "
        "sqlite3.h it is built against is SQLite 3.39.0 or newer (`SQLITE_VERSION_NUMBER >= "
        "3039000`): the release that added the operator, so no older SQLite accepts the statement "
        "either. Built against older headers the calls do not compile, and `is_not(left, right)` "
        "and `is(left, right)` are the same comparisons — SQLite defines `IS DISTINCT FROM` as "
        "`IS NOT` and `IS NOT DISTINCT FROM` as `IS`.";

    const std::string kCommentIsTruthTest =
        "A TRUE or FALSE on the right of `IS`, `IS NOT` or `IS [NOT] DISTINCT FROM` makes the "
        "operator a truth test of its left operand, not a comparison with 1 or 0: SQLite answers "
        "`2 IS TRUE` with 1 where `2 IS 1` is 0, and `'x' IS FALSE` with 1 where `'x' IS 0` is 0. "
        "sqlite_orm binds `true` and `false` as 1 and 0, so the left operand is handed to the call "
        "as `and_(left, true)`, which is 1 where the operand is true, 0 where it is false and NULL "
        "where it is NULL — the truth value the keyword is compared with.";

    const std::string kCommentTableReflection =
        "The table is mapped by sqlite_orm's reflection-based `make_table<T>()`: the columns and "
        "their constraints are read off the struct's members and `[[= …]]` annotations, and the "
        "`[[= \"…\"_orm_name]]` annotation supplies the table name. This requires a C++26 compiler "
        "with reflection (P2996/P3394); sqlite_orm detects support automatically "
        "(SQLITE_ORM_REFLECTION_SUPPORTED). The `make_table` alternative of the `table_mapping_style` "
        "decision point is the classical form and compiles from C++14 on.";

    const std::string kCommentAliasedFromSources =
        "A select over aliased FROM sources names them with `from<...>()`: left to itself sqlite_orm "
        "builds the FROM out of the recordsets the arguments mention, and the two forms that would "
        "stand for such a source there drop its alias — `cross_join<alias_b<T>>()` serializes as a "
        "plain `CROSS JOIN \"t\"` (only a join carrying an ON writes an alias out), and "
        "`count<alias_a<T>>()` names no table at all. The named list is the same FROM the SQL wrote, "
        "in the same order, a comma between two sources reading as the CROSS JOIN it stands for.";

    const std::string kCommentViewReflection =
        "SQL views map to sqlite_orm's reflection-based `make_view<T>()`: the struct's fields and the "
        "`[[= \"…\"_orm_name]]` annotation require a C++26 compiler with reflection (P2996/P3394). "
        "sqlite_orm detects support automatically (SQLITE_ORM_REFLECTION_SUPPORTED enables "
        "SQLITE_ORM_WITH_VIEW); on older compilers this code does not compile.";

    void appendUniqueWarnings(std::vector<CodegenWarning>& destination, const std::vector<CodegenWarning>& source) {
        // The views point into the messages themselves, so `destination` must not reallocate while
        // they are held: reserving for the whole of `source` up front keeps every element in place.
        destination.reserve(destination.size() + source.size());
        std::unordered_set<std::string_view> seenMessages;
        seenMessages.reserve(destination.size() + source.size());
        for (const auto& existing: destination) {
            seenMessages.insert(existing.message);
        }
        for (const auto& warning: source) {
            if (seenMessages.insert(warning.message).second) {
                destination.push_back(warning);
            }
        }
    }

    void appendUniqueComments(std::vector<CodegenComment>& destination, const std::vector<CodegenComment>& source) {
        for (const auto& comment: source) {
            appendUniqueComment(destination, comment);
        }
    }

    void appendUniqueComment(std::vector<CodegenComment>& destination, const CodegenComment& comment) {
        for (const auto& existing: destination) {
            if (existing.message == comment.message)
                return;
        }
        destination.push_back(comment);
    }

    std::string normalizeSqlIdentifier(std::string_view sqlIdentifier) {
        return toLowerAscii(stripIdentifierQuotes(sqlIdentifier));
    }

    std::string normalizeSchemaObjectName(std::string_view objectName) {
        return toLowerAscii(objectName);
    }

    bool isImplicitRowIdName(std::string_view sqlIdentifier) {
        const auto normalized = normalizeSqlIdentifier(sqlIdentifier);
        return normalized == "rowid" || normalized == "oid" || normalized == "_rowid_";
    }

    bool endsWith(std::string_view text, std::string_view suffix) {
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::optional<std::string> extractStorageSelectArgument(std::string_view generated, std::string_view variableName) {
        const std::string prefix = "auto " + std::string(variableName) + " = storage.select(";
        if (generated.size() <= prefix.size() + 2 || !generated.starts_with(prefix) || !endsWith(generated, ");")) {
            return std::nullopt;
        }
        return std::string(generated.substr(prefix.size(), generated.size() - prefix.size() - 2));
    }

    std::string stripStoragePrefixAndTrailingSemicolon(std::string code) {
        static constexpr std::string_view kStoragePrefix = "storage.";
        if (code.size() >= kStoragePrefix.size() && code.compare(0, kStoragePrefix.size(), kStoragePrefix) == 0) {
            code.erase(0, kStoragePrefix.size());
        }
        while (!code.empty() && std::isspace(static_cast<unsigned char>(code.back()))) {
            code.pop_back();
        }
        if (!code.empty() && code.back() == ';') {
            code.pop_back();
        }
        while (!code.empty() && std::isspace(static_cast<unsigned char>(code.back()))) {
            code.pop_back();
        }
        return code;
    }

    size_t underlineLengthOf(std::string_view sourceText) {
        const size_t lineBreak = sourceText.find('\n');
        return utf8CharacterCount(lineBreak == std::string_view::npos ? sourceText : sourceText.substr(0, lineBreak));
    }

    std::optional<CodegenWarning> recordMemberName(std::string_view owner,
                                                   std::string_view sqlName,
                                                   std::string_view memberName,
                                                   const SourceSpan& nameSpan,
                                                   std::map<std::string, std::string>& membersByName,
                                                   MemberDeclaration declaration) {
        const auto anchored = [&nameSpan](std::string message) {
            if (nameSpan.text.empty()) {
                return CodegenWarning{std::move(message)};
            }
            return CodegenWarning{std::move(message), nameSpan.location, underlineLengthOf(nameSpan.text)};
        };
        const bool columnAlias = declaration == MemberDeclaration::cteColumnAlias;
        const auto [iterator, inserted] = membersByName.emplace(std::string(memberName), std::string(sqlName));
        if (!inserted) {
            return anchored(std::string(owner) + ": columns `" + iterator->second + "` and `" + std::string(sqlName) +
                            "` are both named `" + std::string(memberName) + "` in C++; " +
                            (columnAlias ? "the generated code declares their column alias twice"
                                         : "the generated struct declares that member twice") +
                            " and does not compile");
        }
        if (sqlName != memberName) {
            const std::string_view reason =
                isCppKeyword(stripIdentifierQuotes(sqlName)) ? "is a C++ keyword" : "is not a C++ identifier";
            std::string message = std::string(owner) + ": column `" + std::string(sqlName) + "` " +
                                  std::string(reason) + "; " +
                                  (columnAlias ? "the column alias declared for it is named after `"
                                               : "the member holding it is named `") +
                                  std::string(memberName) + "`";
            if (declaration == MemberDeclaration::reflectedViewMember) {
                message += ", and a view is mapped with the names of its members, so the column is named that in "
                           "the mapping too";
            }
            return anchored(std::move(message));
        }
        return std::nullopt;
    }

    CodegenWarning sourceSpanWarning(std::string message, const SourceSpan& sourceSpan) {
        if (sourceSpan.text.empty()) {
            return CodegenWarning{std::move(message)};
        }
        return CodegenWarning{std::move(message), sourceSpan.location, underlineLengthOf(sourceSpan.text)};
    }

    CodegenWarning sourceSpanWarning(std::string message, const AstNode& astNode) {
        return sourceSpanWarning(std::move(message), astNode.sourceSpan);
    }

    std::optional<CodegenWarning> tableIndexHintWarning(const TableIndexHint& hint) {
        // A hint picks the plan, never the rows, so the statement without it answers what the SQL
        // does. What it loses is the plan itself, and, for INDEXED BY, the refusal: SQLite refuses
        // the statement where the table has no such index (`no such index: i`) or where it cannot
        // plan with it (`no query solution`, a partial index the WHERE does not imply), both on
        // 3.51.0, while the generated code runs there.
        switch (hint.kind) {
            case TableIndexHintKind::none:
                return std::nullopt;
            case TableIndexHintKind::notIndexed:
                return sourceSpanWarning("NOT INDEXED has no sqlite_orm form; the statement is generated without it "
                                         "and answers the same rows, but SQLite may search the table with an index",
                                         hint.sourceSpan);
            case TableIndexHintKind::indexedBy:
                return sourceSpanWarning("INDEXED BY has no sqlite_orm form; the statement is generated without it "
                                         "and answers the same rows, but SQLite may plan it without index " +
                                             hint.indexName +
                                             ", and the generated code runs where SQLite refuses the statement "
                                             "for an index the table lacks or cannot be planned with",
                                         hint.sourceSpan);
        }
        return std::nullopt;
    }

    CodegenComment sourceSpanComment(std::string message, const SourceSpan& sourceSpan) {
        if (sourceSpan.text.empty()) {
            return CodegenComment{std::move(message)};
        }
        return CodegenComment{std::move(message), sourceSpan.location, underlineLengthOf(sourceSpan.text)};
    }

    CodegenComment sourceSpanComment(std::string message, const AstNode& astNode) {
        return sourceSpanComment(std::move(message), astNode.sourceSpan);
    }

    namespace {

        /** The `/*` … `*\/` placeholder text a funnelled placeholder generates for `label`. */
        std::string placeholderCode(std::string_view label) {
            return "/* " + std::string(label) + " */";
        }

    }  // namespace

    CodeGenResult unsupportedPlaceholder(CodeGeneratorContext& context,
                                         std::string_view label,
                                         std::string message,
                                         const AstNode& astNode,
                                         CodeGenResult carried) {
        context.recordPlaceholder(PlaceholderSlot::expression);
        carried.code = placeholderCode(label);
        carried.expressionSpans.clear();
        carried.warnings.push_back(sourceSpanWarning(std::move(message), astNode));
        return carried;
    }

    CodeGenResult unsupportedPlaceholder(CodeGeneratorContext& context,
                                         std::string_view label,
                                         const PlaceholderMessage& message,
                                         const AstNode& astNode,
                                         CodeGenResult carried) {
        context.recordPlaceholder(PlaceholderSlot::expression);
        carried.code = placeholderCode(label);
        carried.expressionSpans.clear();
        carried.warnings.push_back(sourceSpanWarning(message(carried.code), astNode));
        return carried;
    }

    CodeGenResult unsupportedStatementPlaceholder(CodeGeneratorContext& context,
                                                  std::string_view label,
                                                  std::string message,
                                                  const AstNode& astNode,
                                                  CodeGenResult carried) {
        context.recordPlaceholder(PlaceholderSlot::statement);
        carried.code = placeholderCode(label);
        carried.expressionSpans.clear();
        carried.warnings.push_back(sourceSpanWarning(std::move(message), astNode));
        return carried;
    }

    CodeGenResult selectLikeSubqueryForm(CodeGenerator& coordinator,
                                         CodeGeneratorContext& context,
                                         const AstNode& node,
                                         bool& compound) {
        const bool wasCompound = std::exchange(context.emittedCompoundSelectForm, false);
        CodeGenResult result = coordinator.tryCodegenSelectLikeSubquery(node);
        compound = context.emittedCompoundSelectForm;
        context.emittedCompoundSelectForm = wasCompound;
        return result;
    }

    std::string sqliteTypeToCpp(std::string_view typeName) {
        std::string lower = toLowerAscii(typeName);
        if (lower.find("bool") != std::string::npos)
            return "bool";
        return castTypeToCpp(typeName);
    }

    std::string castTypeToCpp(std::string_view typeName) {
        std::string lower = toLowerAscii(typeName);
        if (lower.find("bool") != std::string::npos || lower.find("int") != std::string::npos)
            return "int64_t";
        if (lower.find("char") != std::string::npos || lower.find("clob") != std::string::npos ||
            lower.find("text") != std::string::npos)
            return "std::string";
        if (lower.find("blob") != std::string::npos || lower.empty())
            return "std::vector<char>";
        if (lower.find("real") != std::string::npos || lower.find("floa") != std::string::npos ||
            lower.find("doub") != std::string::npos)
            return "double";
        return "double";
    }

    std::string defaultInitializer(std::string_view cppType) {
        if (cppType == "int" || cppType == "int64_t")
            return " = 0";
        if (cppType == "double")
            return " = 0.0";
        if (cppType == "bool")
            return " = false";
        return "";
    }

    std::string toStructName(std::string_view sqlName) {
        auto base = toCppIdentifier(sqlName);
        std::string result;
        result.reserve(base.size());
        bool atWordStart = true;
        for (char character: base) {
            if (character == '_') {
                atWordStart = true;
                continue;
            }
            if (atWordStart) {
                result += static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
                atWordStart = false;
            } else {
                result += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
        }
        if (result.empty() && !base.empty()) {
            return base;
        }
        return result;
    }

    std::string_view joinSqliteOrmApiName(JoinKind joinKind) {
        switch (joinKind) {
            case JoinKind::crossJoin:
                return "cross_join";
            case JoinKind::innerJoin:
                return "inner_join";
            case JoinKind::leftJoin:
                return "left_join";
            case JoinKind::leftOuterJoin:
                return "left_outer_join";
            case JoinKind::joinPlain:
                return "join";
            case JoinKind::naturalInnerJoin:
                return "natural_join";
            default:
                return "inner_join";
        }
    }

    std::string_view compoundSelectApi(CompoundSelectOperator compoundOperator) {
        switch (compoundOperator) {
            case CompoundSelectOperator::unionDistinct:
                return "union_";
            case CompoundSelectOperator::unionAll:
                return "union_all";
            case CompoundSelectOperator::intersect:
                return "intersect";
            case CompoundSelectOperator::except:
                return "except";
        }
        return "union_";
    }

    std::string dmlInsertOrPrefix(ConflictClause conflictClause) {
        switch (conflictClause) {
            case ConflictClause::rollback:
                return "or_rollback(), ";
            case ConflictClause::abort:
                return "or_abort(), ";
            case ConflictClause::fail:
                return "or_fail(), ";
            case ConflictClause::ignore:
                return "or_ignore(), ";
            case ConflictClause::replace:
                return "or_replace(), ";
            default:
                return "";
        }
    }

}  // namespace sqlite2orm
