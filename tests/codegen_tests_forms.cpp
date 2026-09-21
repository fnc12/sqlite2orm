#include "codegen_tests_common.hpp"
#include "source_file_text.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
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
     *  `"NAME"_builtin.scalar<signature, …>()` — `.aggregate<…>` for an aggregate and `.function<…>`
     *  where a name is both — and they are the form itself rather than the public wrapper in front
     *  of it, which is the whole reason to read them: a wrapper is a variadic template whatever the
     *  form behind it takes.
     */
    std::map<std::string, ArgumentCounts> declaredFormArgumentCounts(const std::string& headers) {
        std::map<std::string, ArgumentCounts> declared;
        const std::string_view marker = "\"_builtin.";
        for (size_t at = headers.find(marker); at != std::string::npos; at = headers.find(marker, at + 1)) {
            // The file documents the notation in its own comments, which name no form.
            const size_t lineStart = headers.rfind('\n', at) + 1;
            const size_t lineText = headers.find_first_not_of(" \t", lineStart);
            if (headers[lineText] == '*' || headers.compare(lineText, 2, "//") == 0) {
                continue;
            }
            const size_t nameStart = headers.rfind('"', at - 1) + 1;
            const size_t overloads = headers.find('<', at);
            REQUIRE(overloads != std::string::npos);
            const size_t kindStart = at + marker.size();
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
        const std::regex row{"\\{\"([a-z0-9_]+)\", SqliteOrmFormKind::\\w+, "
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

}  // namespace

// The registry has to answer for every name codegen may write a call of, or a name added to the
// validator's list goes on being generated with nothing recorded about it — which is the hole this
// whole gate is for. The two lists are read out of the sources and compared as sets: `knownFunctions()`
// is what makes a name a built-in rather than a user-defined function, and the registry is what
// says what the library does with it.
TEST_CASE("codegen: the form registry covers every built-in function name the validator knows") {
    const std::string validator = readSourceFile("src/validator.cpp");
    const std::string registry = readSourceFile("src/codegen_forms.cpp");

    const std::vector<std::string> known =
        namesMatching(initializerBodyAfter(validator, "static const std::unordered_set<std::string> functions"),
                      std::regex{"\"([a-z0-9_]+)\""});
    // The name a row is keyed by is its first field; the sqlite_orm spelling some rows carry after
    // it is a C++ name, not a SQL one.
    const std::vector<std::string> recorded = namesMatching(initializerBodyAfter(registry, "kFunctionForms"),
                                                            std::regex{"\\{\"([a-z0-9_]+)\", SqliteOrmFormKind::"});

    REQUIRE(known.size() == 106);
    REQUIRE(recorded == known);
}

// And the registry has to answer with the argument counts the library really declares. The names
// were guarded as a set from the start; the counts next to them were not, and eight rows had been
// read off the public wrapper — `template<class R = void, class... Args> coalesce(Args... args)`,
// which is variadic whatever the form behind it takes — rather than off the form, so they said
// "any number of arguments" for calls that compile nowhere. The declarations are machine-readable,
// so the counts are held to them the same way the names are: both lists come out of the sources
// and what disagrees is spelled out here, name by name, rather than left to a reader to notice.
//
// The disagreements below are the ones that belong: a row that deliberately generates no call
// although the library declares one, and the two names whose second form lives in
// `resolveFunctionCallForm` instead of in the row. `json_insert`, `json_replace`, `json_set` and
// `json_object` are NOT among them — they take their arguments in pairs, so what the headers
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

    // A parse that found nothing would agree with anything, so both counts are pinned: 90 of the
    // registry's 106 names are declared as a built-in form, and the headers declare more forms than
    // codegen has names for — `concat`, `median`, `snippet` and the rest, which reach codegen as
    // user-defined function calls because the validator does not know them.
    REQUIRE(declared.size() == 113);
    REQUIRE(compared == 90);
    REQUIRE(disagreements == std::vector<std::string>{
                                 // CHAR and TYPEOF are spelled `char_` and `typeof_`, names C++ already means
                                 // something by; codegen writes the SQL name, which is a cast and a compiler
                                 // extension rather than a call, so the row generates nothing under either name.
                                 "char: the registry takes no call at all, the headers declare 0 or more",
                                 // An FTS5 auxiliary function takes the table's hidden column first, and codegen
                                 // writes no such column, so no count of ordinary arguments resolves to the form.
                                 "highlight: the registry takes no call at all, the headers declare 4",
                                 // MAX and MIN are the aggregate in their one-argument form and the scalar overload
                                 // from two arguments on; the row carries the aggregate and `resolveFunctionCallForm`
                                 // picks between them, which is the one place that split is allowed to live.
                                 "max: the registry takes 1, the headers declare 1 or more",
                                 "min: the registry takes 1, the headers declare 1 or more",
                                 "typeof: the registry takes no call at all, the headers declare 1",
                             });
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

// A name the library has no call of at all is the quietest of the lot, because what it generates
// is not even a call: `typeof(x)` is a compiler extension in C++ and `char(65)` a cast, so the one
// refuses to build and the other builds into a `char` nobody asked for. The registry carries the
// spelling sqlite_orm does declare, and the message names it — `typeof_` and `char_` are the calls
// a future card teaches codegen to write.
TEST_CASE("codegen: a name sqlite_orm spells no call of is not generated") {
    const auto spelledOtherwise = generateFull("SELECT typeof(id) FROM users;");
    REQUIRE(spelledOtherwise.code.empty());
    REQUIRE(spelledOtherwise.warnings ==
            std::vector<CodegenWarning>{
                {"sqlite_orm declares no typeof(): the call it spells is typeof_(), which codegen does not "
                 "generate, so there is no form to generate the call as. SQLite takes the same call, so the SQL is "
                 "well formed and the generated code alone would not be",
                 SourceLocation{1, 8},
                 10},
                {kStatementNotGenerated}});

    const auto castingName = generateFull("SELECT char(65);");
    REQUIRE(castingName.code.empty());
    REQUIRE(castingName.warnings ==
            std::vector<CodegenWarning>{
                {"sqlite_orm declares no char(): the call it spells is char_(), which codegen does not generate, "
                 "so there is no form to generate the call as. SQLite takes the same call, so the SQL is well "
                 "formed and the generated code alone would not be",
                 SourceLocation{1, 8},
                 8},
                {kStatementNotGenerated}});

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
