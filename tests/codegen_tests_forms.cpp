#include "codegen_tests_common.hpp"
#include "source_file_text.hpp"

#include <algorithm>
#include <regex>
#include <string>
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
                {"json_insert() takes 1 argument or more in sqlite_orm, and the call is written with 0 arguments: "
                 "the library declares no overload for that many, so there is no form to generate the call as. "
                 "SQLite takes the same call, so the SQL is well formed and the generated code alone would not be",
                 SourceLocation{1, 8},
                 13},
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
// registry has a form for is generated exactly as before. `coalesce(id)` is the deliberate
// asymmetry — sqlite_orm's `coalesce` takes one argument and SQLite wants two, and the gate asks
// what the library accepts, not what SQLite does; a call SQLite refuses is the validator's business.
TEST_CASE("codegen: a call the registry has a form for is generated unchanged") {
    REQUIRE(generate("SELECT substr(name, 1, 2) FROM users;") ==
            "auto rows = storage.select(as_optional(substr(&Users::name, 1, 2)));");
    REQUIRE(generate("SELECT round(1.5), log(2, 8), coalesce(id), max(id, size) FROM users;") ==
            "auto rows = storage.select(columns(round(1.5), as_optional(log(2, 8)), coalesce(&Users::id), "
            "max(&Users::id, &Users::size)));");
}
