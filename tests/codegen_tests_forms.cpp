#include "codegen_tests_common.hpp"
#include "source_file_text.hpp"

#include <sqlite2orm/validator.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace source_file_test_helpers;

// The expressibility gate: what sqlite_orm accepts is written down once, in the registry
// (`src/codegen_forms.cpp`), and every generated call is matched against it. A call the registry
// has no form for is not emitted at a guess — the consumer used to hear about such a call from a
// compiler, and the generated header looked right until then — but placeheld, which leaves the
// statement out and the warning underlining the very SQL behind.
//
// The spans below are the SQL the warning underlines, character for character. Each input was run
// through sqlite3 3.51.0 (the math and SOUNDEX names against an amalgamation built with
// SQLITE_ENABLE_MATH_FUNCTIONS and SQLITE_SOUNDEX), and what the message says SQLite does with the
// same call is what it did.

namespace {

    /** The distinct names `pattern` captures in `text`, sorted. */
    std::vector<std::string> namesMatching(const std::string& text, const std::regex& pattern) {
        std::vector<std::string> names;
        for (auto match = std::sregex_iterator{text.begin(), text.end(), pattern}; match != std::sregex_iterator{};
             ++match) {
            names.push_back((*match)[1].str());
        }
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        return names;
    }

    /** The `{ … }` body of the initializer that follows `opening` in `text`. */
    std::string initializerBodyAfter(const std::string& text, std::string_view opening) {
        const size_t start = text.find(opening);
        REQUIRE(start != std::string::npos);
        const size_t bodyStart = text.find('{', start + opening.size());
        REQUIRE(bodyStart != std::string::npos);
        const size_t bodyEnd = text.find("};", bodyStart);
        REQUIRE(bodyEnd != std::string::npos);
        return text.substr(bodyStart, bodyEnd - bodyStart);
    }

    /** The `maximum` of a form that takes any number of arguments from its `minimum` on. */
    constexpr size_t kOpenEnded = static_cast<size_t>(-1);

    /** Argument counts a declaration or a registry row accepts, the way `ArityRange` records them. */
    struct ArgumentCounts {
        size_t minimum = 1;
        size_t maximum = 0;

        bool acceptsNothing() const {
            return this->minimum > this->maximum;
        }

        /** The range as the disagreement list spells it out. */
        std::string text() const {
            if (this->acceptsNothing()) {
                return "no call at all";
            }
            if (this->maximum == kOpenEnded) {
                return std::to_string(this->minimum) + " or more";
            }
            if (this->minimum == this->maximum) {
                return std::to_string(this->minimum);
            }
            return std::to_string(this->minimum) + " to " + std::to_string(this->maximum);
        }
    };

    /** The two ranges as one, i.e. what a name accepts across the overloads declared for it. */
    ArgumentCounts merged(const ArgumentCounts& one, const ArgumentCounts& other) {
        if (one.acceptsNothing()) {
            return other;
        }
        if (other.acceptsNothing()) {
            return one;
        }
        const bool openEnded = one.maximum == kOpenEnded || other.maximum == kOpenEnded;
        return {std::min(one.minimum, other.minimum), openEnded ? kOpenEnded : std::max(one.maximum, other.maximum)};
    }

    /**
     *  `text` cut at the separators that stand outside any `<…>` or `(…)` of its own, which is how
     *  an overload list is told from the template arguments inside one signature.
     */
    std::vector<std::string> splitTopLevel(std::string_view text, char separator) {
        std::vector<std::string> parts;
        size_t depth = 0;
        size_t partStart = 0;
        for (size_t at = 0; at != text.size(); ++at) {
            const char character = text[at];
            if (character == '<' || character == '(') {
                ++depth;
            } else if (character == '>' || character == ')') {
                --depth;
            } else if (character == separator && depth == 0) {
                parts.push_back(std::string{text.substr(partStart, at - partStart)});
                partStart = at + 1;
            }
        }
        parts.push_back(std::string{text.substr(partStart)});
        for (std::string& part: parts) {
            const size_t first = part.find_first_not_of(" \t\r\n");
            part = first == std::string::npos ? std::string{}
                                              : part.substr(first, part.find_last_not_of(" \t\r\n") + 1 - first);
        }
        std::erase_if(parts, [](const std::string& part) {
            return part.empty();
        });
        return parts;
    }

    /**
     *  The argument counts one signature declares. A signature reads `Result(Argument, …)`, wrapped
     *  in `aggregate_sig<…>` or `scalar_sig<…>` where a name declares both kinds at once, and a
     *  trailing `variadic<…>` is the parameter pack that makes the count open-ended.
     */
    ArgumentCounts signatureArgumentCounts(std::string_view signature) {
        for (const std::string_view wrapper: {"aggregate_sig<", "scalar_sig<"}) {
            if (signature.substr(0, wrapper.size()) == wrapper) {
                signature = signature.substr(wrapper.size(), signature.size() - wrapper.size() - 1);
            }
        }
        size_t depth = 0;
        size_t parameters = std::string_view::npos;
        for (size_t at = 0; at != signature.size(); ++at) {
            if (signature[at] == '<') {
                ++depth;
            } else if (signature[at] == '>') {
                --depth;
            } else if (signature[at] == '(' && depth == 0) {
                parameters = at;
                break;
            }
        }
        REQUIRE(parameters != std::string_view::npos);
        const size_t closing = signature.rfind(')');
        REQUIRE(closing != std::string_view::npos);
        const std::vector<std::string> arguments =
            splitTopLevel(signature.substr(parameters + 1, closing - parameters - 1), ',');
        if (!arguments.empty() && arguments.back().rfind("variadic<", 0) == 0) {
            return {arguments.size() - 1, kOpenEnded};
        }
        return {arguments.size(), arguments.size()};
    }

    /**
     *  What every built-in form the sqlite_orm headers declare accepts, keyed by the SQL name the
     *  form is declared under and lowercased. The declarations read
     *  `"NAME"_builtin.scalar<signature, …>()` — `.aggregate<…>` for an aggregate and
     *  `.function<…>` where a name is both, the member on the next line where the declaration is
     *  long — and they are the form itself rather than the public wrapper in front of it, which is
     *  the whole reason to read them: a wrapper is a variadic template whatever the form behind it
     *  takes.
     */
    std::map<std::string, ArgumentCounts> declaredFormArgumentCounts(const std::string& headers) {
        std::map<std::string, ArgumentCounts> declared;
        const std::string_view marker = "\"_builtin";
        for (size_t at = headers.find(marker); at != std::string::npos; at = headers.find(marker, at + 1)) {
            // The file documents the notation in its own comments, which name no form.
            const size_t lineStart = headers.rfind('\n', at) + 1;
            const size_t lineText = headers.find_first_not_of(" \t", lineStart);
            if (headers[lineText] == '*' || headers.compare(lineText, 2, "//") == 0) {
                continue;
            }
            // A declaration too long for one line breaks before the member it calls:
            // `"UNHEX"_builtin` on one line, `.scalar<…>()` on the next.
            const size_t member = headers.find_first_not_of(" \t\r\n", at + marker.size());
            if (headers[member] != '.') {
                continue;
            }
            const size_t nameStart = headers.rfind('"', at - 1) + 1;
            const size_t overloads = headers.find('<', member);
            REQUIRE(overloads != std::string::npos);
            const size_t kindStart = member + 1;
            const std::string kind = headers.substr(kindStart, overloads - kindStart);
            if (kind != "scalar" && kind != "aggregate" && kind != "function") {
                continue;
            }
            size_t depth = 1;
            size_t past = overloads + 1;
            for (; depth != 0; ++past) {
                if (headers[past] == '<') {
                    ++depth;
                } else if (headers[past] == '>') {
                    --depth;
                }
            }
            std::string name = headers.substr(nameStart, at - nameStart);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char letter) {
                return static_cast<char>(std::tolower(letter));
            });
            ArgumentCounts counts;
            for (const std::string& signature:
                 splitTopLevel(std::string_view{headers}.substr(overloads + 1, past - overloads - 2), ',')) {
                counts = merged(counts, signatureArgumentCounts(signature));
            }
            const auto recorded = declared.find(name);
            declared[name] = recorded == declared.end() ? counts : merged(recorded->second, counts);
        }
        return declared;
    }

    /** The `ormArity` column of the registry, keyed by the SQL name each row is written for. */
    std::map<std::string, ArgumentCounts> registryArgumentCounts(const std::string& registry) {
        std::map<std::string, ArgumentCounts> rows;
        const std::string body = initializerBodyAfter(registry, "kFunctionForms");
        const std::regex row{"\\{\"([a-z0-9_]+)\",\\s*SqliteOrmFormKind::\\w+,\\s*"
                             "(?:kAcceptsNothing|\\{(\\d+), (kVariadicArity|\\d+)\\})"};
        for (auto match = std::sregex_iterator{body.begin(), body.end(), row}; match != std::sregex_iterator{};
             ++match) {
            if (!(*match)[2].matched) {
                rows[(*match)[1].str()] = ArgumentCounts{};
                continue;
            }
            const size_t minimum = static_cast<size_t>(std::stoul((*match)[2].str()));
            const std::string maximum = (*match)[3].str();
            rows[(*match)[1].str()] =
                ArgumentCounts{minimum,
                               maximum == "kVariadicArity" ? kOpenEnded : static_cast<size_t>(std::stoul(maximum))};
        }
        return rows;
    }

    /** The SQL text each `…_string` type in the headers serializes a call to, lowercased. */
    std::map<std::string, std::string> serializedSqlNames(const std::string& headers) {
        std::map<std::string, std::string> names;
        const std::regex declaration{"struct ([A-Za-z0-9_]+_string) \\{[^}]*return \"([^\"]*)\";"};
        for (auto match = std::sregex_iterator{headers.begin(), headers.end(), declaration};
             match != std::sregex_iterator{};
             ++match) {
            std::string sqlName = (*match)[2].str();
            std::transform(sqlName.begin(), sqlName.end(), sqlName.begin(), [](unsigned char letter) {
                return static_cast<char>(std::tolower(letter));
            });
            names[(*match)[1].str()] = sqlName;
        }
        return names;
    }

    /**
     *  `sql -> cppName` for every built-in factory in the headers whose C++ name is not the SQL it
     *  serializes to. The factories read
     *  `builtin_function_t<Result, internal::NAME_string, …> cppName(…)`, an aggregate the same
     *  with `builtin_aggregate_function_t`, and the `…_string` they name is what the call comes out
     *  as in SQL — so the two halves of the pair are read off one declaration and compared.
     */
    std::map<std::string, std::string> factoriesSpelledOtherwise(const std::string& headers, size_t& factories) {
        const std::map<std::string, std::string> sqlNames = serializedSqlNames(headers);
        std::map<std::string, std::string> spelledOtherwise;
        std::vector<std::string> pairs;
        const std::regex factory{"builtin_(?:aggregate_)?function_t<[^;]*?internal::([A-Za-z0-9_]+_string)[^;]*?>"
                                 "\\s*([A-Za-z0-9_]+)\\("};
        for (auto match = std::sregex_iterator{headers.begin(), headers.end(), factory};
             match != std::sregex_iterator{};
             ++match) {
            const auto sqlName = sqlNames.find((*match)[1].str());
            REQUIRE(sqlName != sqlNames.end());
            const std::string cppName = (*match)[2].str();
            pairs.push_back(sqlName->second + " -> " + cppName);
            std::string lowered = cppName;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char letter) {
                return static_cast<char>(std::tolower(letter));
            });
            if (lowered != sqlName->second) {
                spelledOtherwise[sqlName->second] = cppName;
            }
        }
        std::sort(pairs.begin(), pairs.end());
        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
        factories = pairs.size();
        return spelledOtherwise;
    }

    /** `sql -> cppName` for every registry row carrying a sqlite_orm spelling of its own. */
    std::map<std::string, std::string> registrySpellings(const std::string& registry) {
        std::map<std::string, std::string> spellings;
        const std::string body = initializerBodyAfter(registry, "kFunctionForms");
        const std::regex row{"\\{\"([a-z0-9_]+)\",\\s*SqliteOrmFormKind::\\w+,\\s*(?:kAcceptsNothing|\\{[^}]*\\}),\\s*"
                             "(?:kAcceptsNothing|\\{[^}]*\\}),\\s*FunctionResultType::\\w+,\\s*\"([a-z0-9_]+)\"\\}"};
        for (auto match = std::sregex_iterator{body.begin(), body.end(), row}; match != std::sregex_iterator{};
             ++match) {
            spellings[(*match)[1].str()] = (*match)[2].str();
        }
        return spellings;
    }

    /** A `sql -> cppName` map as the lines a disagreement is spelled out in. */
    std::vector<std::string> spellingLines(const std::map<std::string, std::string>& spellings) {
        std::vector<std::string> lines;
        for (const auto& [sqlName, cppName]: spellings) {
            lines.push_back(sqlName + " -> " + cppName);
        }
        return lines;
    }

    /**
     *  The sqlite_orm types declaring an `over()`, read off the headers the build fetches: the
     *  member is declared inside the type it belongs to, so the struct or class opened last before
     *  a declaration of it is its owner.
     */
    std::vector<std::string> typesDeclaringOver(const std::string& headers) {
        std::vector<std::string> owners;
        std::string opened;
        const std::regex opening{"^\\s*(?:struct|class)\\s+([A-Za-z_][A-Za-z0-9_]*)"};
        std::istringstream lines{headers};
        for (std::string line; std::getline(lines, line);) {
            std::smatch match;
            if (std::regex_search(line, match, opening)) {
                opened = match[1].str();
            }
            if (line.find("over(OverArgs... overArgs)") != std::string::npos) {
                owners.push_back(opened);
            }
        }
        std::sort(owners.begin(), owners.end());
        owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
        return owners;
    }

}  // namespace

// `over()` is declared apart from `filter()` — the window functions carry one and not the other —
// so the registry answers the two questions apart, and this holds its `over()` answer to the
// headers the same way the argument counts are held to them. Every type below is one a registry
// kind is generated as: `builtin_aggregate_function_t` and `builtin_aggregate_function_call` are
// `builtinAggregate` on the legacy and the C++20 header path, `count_asterisk_t` is
// `countAsterisk`, the eleven `_t` window types are `windowFunction`, and
// `filtered_aggregate_function` is what a `filter()` over an aggregate or over `count(*)` returns,
// which is why those two take a FILTER and an OVER at once. The kinds absent from the list are the ones
// `formTakesOver` says no for: `builtin_function_t` and `builtin_function_call`
// (`builtinScalar`), `match_t` (`matchFunction`), `count_asterisk_without_type`
// (`countWithoutType`), and the two kinds with no form at all.
TEST_CASE("codegen: the forms the registry gives an over() to are the ones declaring it in the headers") {
    const std::vector<std::string> owners =
        typesDeclaringOver(readSourceFile(SQLITE2ORM_TEST_SQLITE_ORM_INCLUDE "/sqlite_orm/sqlite_orm.h"));

    REQUIRE(owners == std::vector<std::string>{"builtin_aggregate_function_call",
                                               "builtin_aggregate_function_t",
                                               "count_asterisk_t",
                                               "cume_dist_t",
                                               "dense_rank_t",
                                               "filtered_aggregate_function",
                                               "first_value_t",
                                               "lag_t",
                                               "last_value_t",
                                               "lead_t",
                                               "nth_value_t",
                                               "ntile_t",
                                               "percent_rank_t",
                                               "rank_t",
                                               "row_number_t"});
}

// The registry is the one list of built-in names there is: `isKnownSqlFunction`, which is what
// makes a call a built-in rather than a user-defined function, answers from it. It used to answer
// from a list of its own in the validator, and the two drifted — 23 names sqlite_orm declares a
// form for, `concat()`, `format()` and `unixepoch()` among them, were in neither, so a call of one
// was generated as a `func<Concat>()` over a stub the consumer had to register, for a function
// SQLite and the library both have. So the names are held to the headers the other way round as
// well: every form the headers declare is a row, and a name added to the headers and not to the
// registry is spelled out here rather than left to be generated as a user-defined function.
TEST_CASE("codegen: every built-in form the sqlite_orm headers declare is a name the registry knows") {
    const std::vector<std::string> recorded =
        namesMatching(initializerBodyAfter(readSourceFile("src/codegen_forms.cpp"), "kFunctionForms"),
                      std::regex{"\\{\"([a-z0-9_]+)\",\\s*SqliteOrmFormKind::"});
    const std::map<std::string, ArgumentCounts> declared =
        declaredFormArgumentCounts(readSourceFile(SQLITE2ORM_TEST_SQLITE_ORM_INCLUDE "/sqlite_orm/sqlite_orm.h"));

    std::vector<std::string> unknown;
    for (const auto& [name, counts]: declared) {
        if (!std::binary_search(recorded.begin(), recorded.end(), name)) {
            unknown.push_back(name);
        }
    }
    REQUIRE(recorded.size() == 131);
    REQUIRE(unknown == std::vector<std::string>{});

    for (const std::string& name: recorded) {
        INFO("the registry carries " << name);
        REQUIRE(sqlite2orm::isKnownSqlFunction(name));
    }
    REQUIRE_FALSE(sqlite2orm::isKnownSqlFunction("my_function"));
}

// And the registry has to answer with the argument counts the library really declares. The names
// were guarded as a set from the start; the counts next to them were not, and eight rows had been
// read off the public wrapper — `template<class R = void, class... Args> coalesce(Args... args)`,
// which is variadic whatever the form behind it takes — rather than off the form, so they said
// "any number of arguments" for calls that compile nowhere. The declarations are machine-readable,
// so the counts are held to them the same way the names are: both lists come out of the sources
// and what disagrees is spelled out here, name by name, rather than left to a reader to notice.
//
// The disagreements below are the ones that belong: the rows that deliberately generate no call
// although the library declares one, and the two names whose second form lives in
// `resolveFunctionCallForm` instead of in the row. CHAR and TYPEOF are not among them — the
// headers declare them under the C++ names `char_` and `typeof_`, and a row's counts have to
// match the form whatever the call is spelled as. `json_insert`, `json_replace`, `json_set`,
// `json_array_insert` and `json_object` are NOT among them — they take their arguments in pairs, so what the headers
// declare is every other count, which a range says as the whole stretch on both sides.
TEST_CASE("codegen: the registry's argument counts are the ones the sqlite_orm headers declare") {
    const std::map<std::string, ArgumentCounts> declared =
        declaredFormArgumentCounts(readSourceFile(SQLITE2ORM_TEST_SQLITE_ORM_INCLUDE "/sqlite_orm/sqlite_orm.h"));
    const std::map<std::string, ArgumentCounts> rows = registryArgumentCounts(readSourceFile("src/codegen_forms.cpp"));

    size_t compared = 0;
    std::vector<std::string> disagreements;
    for (const auto& [name, row]: rows) {
        const auto form = declared.find(name);
        if (form == declared.end()) {
            // A window function, MATCH, GLOB, LIKE and the two table-valued names are declared
            // elsewhere, each as a factory of its own rather than as a built-in form.
            continue;
        }
        ++compared;
        if (row.minimum == form->second.minimum && row.maximum == form->second.maximum) {
            continue;
        }
        disagreements.push_back(name + ": the registry takes " + row.text() + ", the headers declare " +
                                form->second.text());
    }

    // A parse that found nothing would agree with anything, so both counts are pinned: 115 of the
    // registry's 131 names are declared as a built-in form, which is every form the headers declare.
    REQUIRE(declared.size() == 115);
    REQUIRE(compared == 115);
    REQUIRE(disagreements == std::vector<std::string>{
                                 // An FTS5 auxiliary function takes the table's hidden column first, and codegen
                                 // writes no such column, so no count of ordinary arguments resolves to the form.
                                 "bm25: the registry takes no call at all, the headers declare 1 or more",
                                 "highlight: the registry takes no call at all, the headers declare 4",
                                 // MAX and MIN are the aggregate in their one-argument form and the scalar overload
                                 // from two arguments on; the row carries the aggregate and `resolveFunctionCallForm`
                                 // picks between them, which is the one place that split is allowed to live.
                                 "max: the registry takes 1, the headers declare 1 or more",
                                 "min: the registry takes 1, the headers declare 1 or more",
                                 "snippet: the registry takes no call at all, the headers declare 6",
                                 // The public factory in front of the form does not compile whatever it is called
                                 // with, so the row takes no call: see `SqliteOrmFormKind::uncompilableFactory`.
                                 "sqlite_offset: the registry takes no call at all, the headers declare 1",
                             });
}

// The spelling column is held to the headers the same way the argument counts are. Codegen wrote
// the SQL name for every call, which for four of them is not a call of the form at all: `typeof`
// is a compiler extension in C++, `char` a type, `if` a statement, and `mod` is sqlite_orm's `%`
// operator rather than its MOD function. Each declaration names both halves at once — the C++ factory and the
// `…_string` whose `serialize()` is the SQL it comes out as — so what the library spells otherwise
// is read out of them and compared with what the registry says it spells otherwise.
TEST_CASE("codegen: the registry spells a call the way the sqlite_orm headers declare it") {
    size_t factories = 0;
    const std::map<std::string, std::string> spelledOtherwise =
        factoriesSpelledOtherwise(readSourceFile(SQLITE2ORM_TEST_SQLITE_ORM_INCLUDE "/sqlite_orm/sqlite_orm.h"),
                                  factories);

    // A parse that found nothing would agree with anything: the headers declare 112 built-in
    // factories, and these four are every one whose C++ name is not the SQL it serializes.
    REQUIRE(factories == 112);
    REQUIRE(spellingLines(spelledOtherwise) ==
            std::vector<std::string>{"char -> char_", "if -> if_", "mod -> mod_f", "typeof -> typeof_"});

    // Every one of them is a row, and the row spells it the way the headers do.
    const std::map<std::string, std::string> rows = registrySpellings(readSourceFile("src/codegen_forms.cpp"));
    REQUIRE(spellingLines(rows) ==
            std::vector<std::string>{"char -> char_", "if -> if_", "mod -> mod_f", "typeof -> typeof_"});
    for (const auto& [sqlName, cppName]: rows) {
        INFO("the registry spells " << sqlName << " " << cppName);
        REQUIRE(spelledOtherwise.at(sqlName) == cppName);
    }
}

// A built-in call sqlite_orm has no overload for was the widest hole of all: nothing warned, the
// code came out reading exactly like a call SQLite runs, and the consumer learned otherwise from a
// compiler. `abs(id, 1)` is the case the card was written from.
TEST_CASE("codegen: a built-in written with an argument count sqlite_orm has no overload for is not generated") {
    const auto tooMany = generateFull("SELECT abs(id, 1) FROM users;");
    REQUIRE(tooMany.code.empty());
    REQUIRE(tooMany.warnings ==
            std::vector<CodegenWarning>{
                {"abs() takes 1 argument in sqlite_orm, and the call is written with 2 arguments: the library "
                 "declares no overload for that many, so there is no form to generate the call as. SQLite refuses "
                 "the same call — wrong number of arguments to function abs() — but stores a trigger or a view "
                 "holding it",
                 SourceLocation{1, 8},
                 10},
                {kStatementNotGenerated}});

    const auto tooFew = generateFull("SELECT substr(name) FROM users;");
    REQUIRE(tooFew.code.empty());
    REQUIRE(tooFew.warnings ==
            std::vector<CodegenWarning>{
                {"substr() takes 2 to 3 arguments in sqlite_orm, and the call is written with 1 argument: the "
                 "library declares no overload for that many, so there is no form to generate the call as. SQLite "
                 "refuses the same call — wrong number of arguments to function substr() — but stores a trigger or "
                 "a view holding it",
                 SourceLocation{1, 8},
                 12},
                {kStatementNotGenerated}});

    const auto pastTheRange = generateFull("SELECT round(1, 2, 3);");
    REQUIRE(pastTheRange.code.empty());
    REQUIRE(pastTheRange.warnings ==
            std::vector<CodegenWarning>{
                {"round() takes 1 to 2 arguments in sqlite_orm, and the call is written with 3 arguments: the "
                 "library declares no overload for that many, so there is no form to generate the call as. SQLite "
                 "refuses the same call — wrong number of arguments to function round() — but stores a trigger or "
                 "a view holding it",
                 SourceLocation{1, 8},
                 14},
                {kStatementNotGenerated}});

    // A variadic form has a minimum all the same, and the message names it as one.
    const auto underAVariadicMinimum = generateFull("SELECT json_insert() FROM users;");
    REQUIRE(underAVariadicMinimum.code.empty());
    REQUIRE(underAVariadicMinimum.warnings ==
            std::vector<CodegenWarning>{
                {"json_insert() takes 3 arguments or more in sqlite_orm, and the call is written with 0 arguments: "
                 "the library declares no overload for that many, so there is no form to generate the call as. "
                 "SQLite takes the same call, so the SQL is well formed and the generated code alone would not be",
                 SourceLocation{1, 8},
                 13},
                {kStatementNotGenerated}});
}

// The minimum of a variadic form is the one place a public wrapper hides what the library takes.
// `coalesce`, `json_extract`, `printf` and `strftime` are each fronted by a wrapper as variadic as
// C++ gets — `template<class R = void, class... Args> coalesce(Args... args)` — while the form
// behind it, `"COALESCE"_builtin.scalar<…(anything, anything, variadic<anything>)>`, wants two
// arguments and up. Reading the wrapper answers "any number" for all four, which is how the
// registry came to accept calls that compile nowhere: each of the four below was put through
// `c++ -std=c++20 -fsyntax-only` and through `-std=c++17`, which takes the headers' legacy branch,
// against the pinned revision. The C++20 branch refuses all four; the legacy branch, whose
// factories really are plain variadic templates, takes them, and the registry records the
// narrower of the two because the generated code has to build on both.
TEST_CASE("codegen: a variadic form is refused under its minimum, which its public wrapper hides") {
    const auto oneArgument = generateFull("SELECT coalesce(id) FROM users;");
    REQUIRE(oneArgument.code.empty());
    REQUIRE(oneArgument.warnings ==
            std::vector<CodegenWarning>{
                {"coalesce() takes 2 arguments or more in sqlite_orm, and the call is written with 1 argument: the "
                 "library declares no overload for that many, so there is no form to generate the call as. SQLite "
                 "refuses the same call — wrong number of arguments to function coalesce() — but stores a trigger "
                 "or a view holding it",
                 SourceLocation{1, 8},
                 12},
                {kStatementNotGenerated}});

    const auto pathless = generateFull("SELECT json_extract(name) FROM users;");
    REQUIRE(pathless.code.empty());
    REQUIRE(pathless.warnings ==
            std::vector<CodegenWarning>{
                {"json_extract() takes 2 arguments or more in sqlite_orm, and the call is written with 1 argument: "
                 "the library declares no overload for that many, so there is no form to generate the call as. "
                 "SQLite takes the same call, so the SQL is well formed and the generated code alone would not be",
                 SourceLocation{1, 8},
                 18},
                {kStatementNotGenerated}});

    // Both of these take a format string first and the values after it, so neither has an
    // argument-less form however variadic the wrapper reads.
    const auto noFormat = generateFull("SELECT printf() FROM users;");
    REQUIRE(noFormat.code.empty());
    REQUIRE(noFormat.warnings ==
            std::vector<CodegenWarning>{
                {"printf() takes 1 argument or more in sqlite_orm, and the call is written with 0 arguments: the "
                 "library declares no overload for that many, so there is no form to generate the call as. SQLite "
                 "takes the same call, so the SQL is well formed and the generated code alone would not be",
                 SourceLocation{1, 8},
                 8},
                {kStatementNotGenerated}});

    const auto noTimestring = generateFull("SELECT strftime() FROM users;");
    REQUIRE(noTimestring.code.empty());
    REQUIRE(noTimestring.warnings ==
            std::vector<CodegenWarning>{
                {"strftime() takes 1 argument or more in sqlite_orm, and the call is written with 0 arguments: the "
                 "library declares no overload for that many, so there is no form to generate the call as. SQLite "
                 "takes the same call, so the SQL is well formed and the generated code alone would not be",
                 SourceLocation{1, 8},
                 10},
                {kStatementNotGenerated}});
}

// An FTS5 auxiliary function is declared over the table itself: `highlight(posts, 0, '<b>',
// '</b>')` reads `posts` as a column reference, and sqlite_orm's factory takes that argument as an
// `fts5::hidden::any` column of the mapped virtual table. Codegen writes an ordinary column there,
// which matches no overload at any argument count — `highlight(&Posts::body, 0, "a", "b")` was put
// through the compiler against the pinned headers on both branches and builds on neither — so the
// row accepts nothing rather than naming a count. SQLite prepares the call whatever it is written
// with and refuses it when it runs: `unable to use function highlight in the requested context`.
TEST_CASE("codegen: an FTS5 auxiliary function is not generated at any argument count") {
    const auto result = generateFull("SELECT highlight(name, 0, 'a', 'b') FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"highlight() is an FTS5 auxiliary function: sqlite_orm takes the FTS5 table's hidden column for "
                 "its first argument, the way highlight(posts, …) names the table, and codegen writes no such "
                 "column, so there is no form to generate the call as. SQLite prepares the same call whatever it "
                 "is written with — an FTS5 auxiliary function is registered for any argument list — and answers "
                 "`unable to use function highlight in the requested context` outside a query over the table",
                 SourceLocation{1, 8},
                 28},
                {kStatementNotGenerated}});

    // `snippet()` and `bm25()` are the other two, declared over the same hidden column.
    const auto snippet = generateFull("SELECT snippet(name, 0, 'a', 'b', '...', 8) FROM users;");
    REQUIRE(snippet.code.empty());
    REQUIRE(snippet.warnings ==
            std::vector<CodegenWarning>{
                {"snippet() is an FTS5 auxiliary function: sqlite_orm takes the FTS5 table's hidden column for "
                 "its first argument, the way snippet(posts, …) names the table, and codegen writes no such "
                 "column, so there is no form to generate the call as. SQLite prepares the same call whatever it "
                 "is written with — an FTS5 auxiliary function is registered for any argument list — and answers "
                 "`unable to use function snippet in the requested context` outside a query over the table",
                 SourceLocation{1, 8},
                 36},
                {kStatementNotGenerated}});
    const auto bm25 = generateFull("SELECT bm25(name) FROM users;");
    REQUIRE(bm25.code.empty());
    REQUIRE(bm25.warnings ==
            std::vector<CodegenWarning>{
                {"bm25() is an FTS5 auxiliary function: sqlite_orm takes the FTS5 table's hidden column for "
                 "its first argument, the way bm25(posts, …) names the table, and codegen writes no such "
                 "column, so there is no form to generate the call as. SQLite prepares the same call whatever it "
                 "is written with — an FTS5 auxiliary function is registered for any argument list — and answers "
                 "`unable to use function bm25 in the requested context` outside a query over the table",
                 SourceLocation{1, 8},
                 10},
                {kStatementNotGenerated}});
}

// The registry records what SQLite accepts next to what sqlite_orm accepts because the two
// disagree, and a message that named only one of them would be wrong half the time. SQLite has
// taken `iif(X, Y)` since 3.48 — `SELECT iif(0, 1)` answers NULL on 3.51.0 — while sqlite_orm
// declares the three-argument form alone, so here the SQL is well formed and the generated code
// alone would not have been.
TEST_CASE("codegen: an arity sqlite_orm lacks and SQLite accepts says so") {
    const auto result = generateFull("SELECT iif(0, 1);");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"iif() takes 3 arguments in sqlite_orm, and the call is written with 2 arguments: the library "
                 "declares no overload for that many, so there is no form to generate the call as. SQLite takes "
                 "the same call, so the SQL is well formed and the generated code alone would not be",
                 SourceLocation{1, 8},
                 9},
                {kStatementNotGenerated}});

    // The message spells the name as written, the way SQLite echoes it in its own diagnostics.
    const auto asWritten = generateFull("SELECT IIF(0, 1) || 'x';");
    REQUIRE(asWritten.code.empty());
    REQUIRE(asWritten.warnings ==
            std::vector<CodegenWarning>{
                {"IIF() takes 3 arguments in sqlite_orm, and the call is written with 2 arguments: the library "
                 "declares no overload for that many, so there is no form to generate the call as. SQLite takes "
                 "the same call, so the SQL is well formed and the generated code alone would not be",
                 SourceLocation{1, 8},
                 9},
                {kStatementNotGenerated}});
}

// The four names sqlite_orm spells otherwise, which are every one of them: the headers declare 112
// built-in factories, and `char_`, `typeof_`, `mod_f` and `if_` are the only ones whose C++ name is
// not the SQL they serialize. Codegen used to write the SQL name, which is a compiler extension for
// `typeof(x)` and a cast for `char(65)` rather than a call of the form, so the statement went out
// unbuildable until the gate started holding it back — and holding it back left a query sqlite_orm
// can express with no code at all. The spelling the library declares is the registry's to know, and
// it is what the call comes out as. The C++ names come from the headers the build fetches:
// `typeof_` is declared over one argument and `char_` over any number, so a call written with
// another count is refused on its arity like every other built-in.
TEST_CASE("codegen: a name sqlite_orm spells otherwise is generated under the library's spelling") {
    REQUIRE(generate("SELECT typeof(id) FROM users;") == "auto rows = storage.select(typeof_(&Users::id));");
    // The spelling follows the name rather than the case it is written in, the way every other
    // built-in is generated.
    REQUIRE(generate("SELECT TYPEOF(id) FROM users;") == "auto rows = storage.select(typeof_(&Users::id));");
    REQUIRE(generate("SELECT char(65);") == "auto rows = storage.select(char_(65));");
    REQUIRE(generate("SELECT char(65, id) FROM users;") == "auto rows = storage.select(char_(65, &Users::id));");
    // CHAR takes any number of arguments in the library and in SQLite alike — `SELECT char()` is
    // the empty text on sqlite3 3.51.0 — so the argument-less call is generated too.
    REQUIRE(generate("SELECT char();") == "auto rows = storage.select(char_());");
    // `f(*)` is the argument-less call to SQLite (`quote(char(*))` is '' on sqlite3 3.51.0), and the
    // star form is spelled the same way — a bare `char()` would compile as a zero char.
    REQUIRE(generate("SELECT char(*) FROM users;") == "auto rows = storage.select(char_(), from<Users>());");
    REQUIRE(generate("SELECT typeof(name) FROM users WHERE typeof(id) = 'integer';") ==
            "auto rows = storage.select(typeof_(&Users::name), where(typeof_(&Users::id) == \"integer\"));");

    // MOD is the third, and the one that was generating a call all along — the wrong one.
    // sqlite_orm's `mod()` is the `%` operator (`mod_t` serializes `%`), while the MOD function is
    // `mod_f` (`mod_string` serializes `MOD`), and the two are different functions: on sqlite3
    // 3.51.0 built with SQLITE_ENABLE_MATH_FUNCTIONS, `SELECT mod(7.5, 2)` is 1.5 and
    // `SELECT 7.5 % 2` is 1.0, `%` truncating its operands to integers where MOD does not.
    REQUIRE(generate("SELECT mod(size, 2) FROM users;") ==
            "auto rows = storage.select(as_optional(mod_f(&Users::size, 2)));");

    // IF is the fourth: SQLite has taken it as a spelling of IIF since 3.48 — sqlite3 3.51.0
    // answers `SELECT if(1, 2, 3)` with 2 — and IF being a keyword, the call never used to parse
    // (`unexpected token: if`). sqlite_orm declares it as `if_`, over the same three arguments as
    // `iif`, so it is read back the way an IIF is and refused at the counts an IIF is.
    REQUIRE(generate("SELECT if(id > 1, id, 2) FROM users;") ==
            "auto rows = storage.select(if_(c(&Users::id) > 1, &Users::id, 2));");
    const auto shortIf = generateFull("SELECT if(0, 1);");
    REQUIRE(shortIf.code.empty());
    REQUIRE(shortIf.warnings ==
            std::vector<CodegenWarning>{
                {"if() takes 3 arguments in sqlite_orm, and the call is written with 2 arguments: the library "
                 "declares no overload for that many, so there is no form to generate the call as. SQLite takes "
                 "the same call, so the SQL is well formed and the generated code alone would not be",
                 SourceLocation{1, 8},
                 8},
                {kStatementNotGenerated}});

    // And the arity gate keeps answering for the name: sqlite3 3.51.0 refuses this very call with
    // `wrong number of arguments to function typeof()`.
    const auto tooMany = generateFull("SELECT typeof(id, 1) FROM users;");
    REQUIRE(tooMany.code.empty());
    REQUIRE(tooMany.warnings ==
            std::vector<CodegenWarning>{
                {"typeof() takes 1 argument in sqlite_orm, and the call is written with 2 arguments: the library "
                 "declares no overload for that many, so there is no form to generate the call as. SQLite refuses "
                 "the same call — wrong number of arguments to function typeof() — but stores a trigger or a view "
                 "holding it",
                 SourceLocation{1, 8},
                 13},
                {kStatementNotGenerated}});
}

// A name the library has no call of at all is the quietest of the lot, because what it generates
// is not even a call. The two names it merely spells otherwise are generated under that spelling;
// what is left here is a name it has no form for under any spelling.
TEST_CASE("codegen: a name sqlite_orm spells no call of is not generated") {
    // A table-valued function is no scalar call in either place: sqlite3 3.51.0 answers `no such
    // function: json_each` for this very statement.
    const auto noFunctionAtAll = generateFull("SELECT json_each('[1]');");
    REQUIRE(noFunctionAtAll.code.empty());
    REQUIRE(noFunctionAtAll.warnings ==
            std::vector<CodegenWarning>{
                {"sqlite_orm declares no json_each(), so there is no form to generate the call as. SQLite has no "
                 "such function either — it refuses the same call with no such function: json_each",
                 SourceLocation{1, 8},
                 16},
                {kStatementNotGenerated}});
}

// sqlite_orm declares SQLITE_OFFSET under SQLITE_ENABLE_OFFSET_SQL_FUNC, and the public factory in
// front of the form — in both branches of the pinned headers, and on the library's `dev` as well —
// checks its argument with a `polyfill::disjunction` the library does not define, so
// `sqlite_offset(&Users::id)` stops at the compiler whatever it is called with. SQLite built with
// the option takes the call — `SELECT sqlite_offset(x) FROM t` answers the row's byte offset on
// 3.51.0 — so the SQL is fine and the generated code alone would not be. Before the name was known
// it went out as a `func<SqliteOffset>()` over a stub the consumer was told to register.
TEST_CASE("codegen: a name whose sqlite_orm factory does not compile is not generated") {
    const auto result = generateFull("SELECT sqlite_offset(id) FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"sqlite_offset() is declared in sqlite_orm, but its factory does not compile on the revision the "
                 "generated code is built against, whatever it is called with, so there is no form to generate "
                 "the call as. SQLite takes the same call, so the SQL is well formed and the generated code alone "
                 "would not be",
                 SourceLocation{1, 8},
                 17},
                {kStatementNotGenerated}});
}

// The underline is measured in characters, not bytes: `abs(ключ, 1)` is twelve characters written
// with seventeen bytes, and the consumer draws the twelve.
TEST_CASE("codegen: a refused call over non-ASCII SQL is underlined in characters") {
    const auto result = generateFull("SELECT abs(ключ, 1) FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"abs() takes 1 argument in sqlite_orm, and the call is written with 2 arguments: the library "
                 "declares no overload for that many, so there is no form to generate the call as. SQLite refuses "
                 "the same call — wrong number of arguments to function abs() — but stores a trigger or a view "
                 "holding it",
                 SourceLocation{1, 8},
                 12},
                {kStatementNotGenerated}});
}

// The call SQLite stores rather than prepares is the one that reaches codegen from `--db`, so the
// refusal has to survive a trigger body — and underline the call where it stands there.
TEST_CASE("codegen: a refused call inside a trigger body is underlined where it stands") {
    const auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON users BEGIN SELECT abs(NEW.id, 1); END;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"abs() takes 1 argument in sqlite_orm, and the call is written with 2 arguments: the library "
                 "declares no overload for that many, so there is no form to generate the call as. SQLite refuses "
                 "the same call — wrong number of arguments to function abs() — but stores a trigger or a view "
                 "holding it",
                 SourceLocation{1, 54},
                 14},
                {"a statement in the trigger body is not mapped to sqlite_orm codegen", SourceLocation{1, 47}, 21},
                {kStatementNotGenerated}});
}

// The other side of the gate, and the one that keeps it from swallowing working code: a call the
// registry has a form for is generated exactly as before. Every call below compiles against the
// pinned headers — `coalesce(&Users::id, 1)` and `max(&Users::id, &Users::size)` are the two-and-up
// overloads, and MAX in that form is the scalar one rather than the aggregate, the same split
// SQLite makes.
TEST_CASE("codegen: a call the registry has a form for is generated unchanged") {
    REQUIRE(generate("SELECT substr(name, 1, 2) FROM users;") ==
            "auto rows = storage.select(as_optional(substr(&Users::name, 1, 2)));");
    REQUIRE(generate("SELECT round(1.5), log(2, 8), coalesce(id, 1), max(id, size) FROM users;") ==
            "auto rows = storage.select(columns(round(1.5), as_optional(log(2, 8)), coalesce(&Users::id, 1), "
            "max(&Users::id, &Users::size)));");
}

// The names sqlite_orm declares a form for and codegen did not know — 23 of them, the functions
// modern SQLite added among them — were generated as user-defined functions: a `func<Concat>()`
// over a stub struct, and `storage.create_scalar_function<Concat>()` for the consumer to register
// what SQLite and the library both have built in. The output compiled and looked right, and the
// stub's `operator()` answered 0 in place of the concatenation. Each call below is generated as the
// library's own form instead, and each was compiled against the pinned headers on both branches
// (`-std=c++20` and `-std=c++17`) and run against sqlite3 3.53.0 built with
// SQLITE_ENABLE_PERCENTILE, printing what the sqlite3 shell prints for the same statement. They
// take a FILTER and an OVER where SQLite does: `median`, the percentiles and `string_agg` are
// aggregates.
TEST_CASE("codegen: a built-in sqlite_orm declares is generated as the built-in, not as a user-defined function") {
    REQUIRE(generate("SELECT concat(name, id), concat_ws(',', name, id), format('%d', id) FROM users;") ==
            "auto rows = storage.select(columns(concat(&Users::name, &Users::id), "
            "as_optional(concat_ws(\",\", &Users::name, &Users::id)), as_optional(format(\"%d\", &Users::id))));");
    REQUIRE(generate("SELECT unixepoch(), unixepoch('2020-01-01'), timediff('2020-01-02', '2020-01-01');") ==
            "auto rows = storage.select(columns(as_optional(unixepoch()), as_optional(unixepoch(\"2020-01-01\")), "
            "as_optional(timediff(\"2020-01-02\", \"2020-01-01\"))));");
    REQUIRE(generate("SELECT octet_length(name), unhex('4142'), unhex('41-42', '-') FROM users;") ==
            "auto rows = storage.select(columns(as_optional(octet_length(&Users::name)), as_optional(unhex(\"4142\")), "
            "as_optional(unhex(\"41-42\", \"-\"))));");
    // `median()` and the percentiles are declared `std::unique_ptr<double>`, nullable already, so
    // they carry no `as_optional` of their own; `string_agg()` is declared `std::string`.
    REQUIRE(generate("SELECT string_agg(name, ','), median(size), percentile(size, 50), "
                     "percentile_cont(size, 0.5), percentile_disc(size, 0.5) FROM users;") ==
            "auto rows = storage.select(columns(as_optional(string_agg(&Users::name, \",\")), median(&Users::size), "
            "percentile(&Users::size, 50), percentile_cont(&Users::size, 0.5), percentile_disc(&Users::size, 0.5)));");
    REQUIRE(generate("SELECT median(size) OVER (), string_agg(name, ',') FILTER (WHERE id > 1) FROM users;") ==
            "auto rows = storage.select(columns(median(&Users::size).over(), "
            "as_optional(string_agg(&Users::name, \",\").filter(where(c(&Users::id) > 1)))));");
    // SQLite answers these with a value whatever they are given, `sqlite_compileoption_get()`
    // aside, which is NULL past the last option and declared `std::unique_ptr<std::string>`.
    REQUIRE(generate("SELECT sqlite_version(), sqlite_source_id(), sqlite_compileoption_used('THREADSAFE'), "
                     "sqlite_compileoption_get(0);") ==
            "auto rows = storage.select(columns(sqlite_version(), sqlite_source_id(), "
            "sqlite_compileoption_used(\"THREADSAFE\"), sqlite_compileoption_get(0)));");
    // `unistr_quote(NULL)` is the text 'NULL', where `unistr(NULL)` is NULL.
    REQUIRE(generate("SELECT unistr(name), unistr_quote(name) FROM users;") ==
            "auto rows = storage.select(columns(as_optional(unistr(&Users::name)), unistr_quote(&Users::name)));");
    REQUIRE(generate("SELECT json_pretty(name), json_pretty(name, '  '), json_error_position(name), "
                     "json_array_insert(name, '$[0]', 1) FROM users;") ==
            "auto rows = storage.select(columns(as_optional(json_pretty(&Users::name)), "
            "as_optional(json_pretty(&Users::name, \"  \")), as_optional(json_error_position(&Users::name)), "
            "as_optional(json_array_insert(&Users::name, \"$[0]\", 1))));");

    // The gate answers for them like for every other built-in: sqlite3 3.51.0 refuses this very
    // call with `wrong number of arguments to function concat()`.
    const auto noArguments = generateFull("SELECT concat();");
    REQUIRE(noArguments.code.empty());
    REQUIRE(noArguments.warnings ==
            std::vector<CodegenWarning>{
                {"concat() takes 1 argument or more in sqlite_orm, and the call is written with 0 arguments: the "
                 "library declares no overload for that many, so there is no form to generate the call as. SQLite "
                 "refuses the same call — wrong number of arguments to function concat() — but stores a trigger or "
                 "a view holding it",
                 SourceLocation{1, 8},
                 8},
                {kStatementNotGenerated}});
}
