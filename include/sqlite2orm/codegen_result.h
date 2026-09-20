#pragma once

#include <sqlite2orm/token.h>

#include <optional>
#include <string>
#include <vector>

namespace sqlite2orm {

    /**
     *  A codegen warning, optionally anchored to a span of the source SQL so a consumer can
     *  underline the relevant text. Implicitly constructible from a string, so the many plain
     *  `warnings.push_back("…")` sites keep compiling; only sites with a known location set one.
     *  Equality covers the span as well as the message, so a test that pins a warning pins what it
     *  underlines: anchoring a warning that was plain updates every expectation of it.
     */
    struct CodegenWarning {
        std::string message;
        /**
         *  Start of the relevant SQL token; `length` characters from here should be underlined.
         *  `column` counts characters, not bytes, so a consumer holding the SQL as text underlines
         *  from it directly — see `length`.
         */
        std::optional<SourceLocation> location;
        /**
         *  Number of characters to underline from `location` (0 when unknown). A character is one
         *  Unicode code point, whatever it takes to write in UTF-8: `«ü»` is three characters
         *  wherever it stands, both in this length and in the column of `location`, so SQL that is
         *  not all ASCII — SQLite takes non-ASCII in bare identifiers as readily as in quoted
         *  names and string literals — underlines the same text a consumer measuring characters
         *  expects. Whoever indexes the SQL by byte instead has to convert. So does a consumer
         *  counting UTF-16 code units — a browser string, a UTF-16 string type — but only above
         *  the basic multilingual plane: a code point written with a surrogate pair there, `🙂`
         *  among them, is one character here and two of those units.
         *
         *  The span stays on the line `location` names: a token written across lines — a quoted
         *  name or a string literal holding a newline, keywords split by one — is underlined up to
         *  the end of that line only, so a consumer drawing `length` characters from `location`
         *  within the line never runs past its end.
         */
        size_t length = 0;

        CodegenWarning() = default;
        CodegenWarning(std::string message) : message(std::move(message)) {}
        CodegenWarning(const char* message) : message(message) {}
        CodegenWarning(std::string message, SourceLocation location, size_t length) :
            message(std::move(message)), location(location), length(length) {}

        bool operator==(const CodegenWarning&) const = default;
    };

    struct Option {
        std::string value;
        std::string code;
        std::string description;
        bool hidden = false;
        /** Optional notes when this alternative is shown or chosen (e.g. build requirements); any consumer may show them. */
        std::vector<std::string> comments;
        /**
         *  Minimum C++ standard this variant compiles against (14 by default; 20 for options that
         *  rely on C++20 sqlite_orm features). Options requiring more than `CodeGenPolicy::targetCppStandard`
         *  are dropped before the result is returned, so a consumer never sees an unusable variant.
         */
        int minCppStandard = 14;

        bool operator==(const Option&) const = default;
    };

    struct DecisionPoint {
        int id = 0;
        std::string category;
        std::string chosenValue;
        std::string chosenCode;
        std::vector<Option> options;

        bool operator==(const DecisionPoint&) const = default;
    };

    struct CodeGenResult {
        std::string code;
        std::vector<DecisionPoint> decisionPoints;
        std::vector<CodegenWarning> warnings;
        std::vector<std::string> errors;
        /**
         *  Optional hints explaining the forms the snippet was generated as, deduplicated by text.
         *  Every entry point that generates from an AST node reports the ones recorded while it ran,
         *  so a whole statement carries the comments of every clause of its body and a single node
         *  carries its own. A hint explains generated code, so a fragment that is thrown away — a
         *  subquery replaced by a placeholder, a statement that ends up with no `code` at all —
         *  reports none of the ones its generation recorded.
         */
        std::vector<std::string> comments;

        bool operator==(const CodeGenResult&) const = default;
    };

    struct CreateTableParts {
        std::string structDeclaration;
        std::string makeTableExpression;
        /**
         *  The `table_mapping_style` decision point, offered only when the policy targets C++26:
         *  `make_table` (the classical form) against `reflection` (an annotated struct mapped by
         *  `make_table<T>()`). Each option's `code` is the pair these parts hold — the struct
         *  declaration, a blank line and the make_table expression — so an option stands for the
         *  whole mapping of the table, not for one of the two halves.
         */
        std::vector<DecisionPoint> decisionPoints;
        std::vector<CodegenWarning> warnings;
        /**
         *  Optional hints for the generated table, from its CHECK, DEFAULT and generated-column
         *  expressions. Empty when `makeTableExpression` is: a table that is not merged into the
         *  storage is code the consumer never gets, so nothing is left for a hint to explain.
         */
        std::vector<std::string> comments;
        /**
         *  Whether `structDeclaration` is the reflected form, i.e. whether it carries sqlite_orm
         *  annotations. Whoever places the declaration has to know: the names inside an annotation
         *  get unqualified lookup at the point of the struct, not inside whatever function uses the
         *  mapping, so such a struct needs sqlite_orm's names visible where it is written.
         */
        bool structIsReflected = false;
    };

    struct CreateViewParts {
        std::string structDeclaration;
        std::string makeViewExpression;
        std::vector<DecisionPoint> decisionPoints;
        std::vector<CodegenWarning> warnings;
        /**
         *  Optional hints for the generated view, from the expressions of its body. Empty when
         *  `makeViewExpression` is, for the same reason as the table's: a view that is not merged
         *  into the storage leaves no generated form for a hint to be about.
         */
        std::vector<std::string> comments;
    };

}  // namespace sqlite2orm
