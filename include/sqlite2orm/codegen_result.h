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
        /** Start of the relevant SQL token; `length` characters from here should be underlined. */
        std::optional<SourceLocation> location;
        /**
         *  Number of characters to underline from `location` (0 when unknown). The span stays on
         *  the line `location` names: a token written across lines — a quoted name or a string
         *  literal holding a newline, keywords split by one — is underlined up to the end of that
         *  line only, so a consumer drawing `length` characters from `location` within the line
         *  never runs past its end.
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
        std::vector<CodegenWarning> warnings;
        /** Optional hints for the generated table, from its CHECK, DEFAULT and generated-column expressions. */
        std::vector<std::string> comments;
    };

    struct CreateViewParts {
        std::string structDeclaration;
        std::string makeViewExpression;
        std::vector<DecisionPoint> decisionPoints;
        std::vector<CodegenWarning> warnings;
        std::vector<std::string> comments;
    };

}  // namespace sqlite2orm
