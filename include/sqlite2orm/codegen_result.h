#pragma once

#include <string>
#include <vector>

namespace sqlite2orm {

    struct Option {
        std::string value;
        std::string code;
        std::string description;
        bool hidden = false;
        /** Optional notes when this alternative is shown or chosen (e.g. build requirements); any consumer may show them. */
        std::vector<std::string> comments;

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
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
        /** Optional hints for the generated snippet (deduplicated when merging fragments). */
        std::vector<std::string> comments;

        bool operator==(const CodeGenResult&) const = default;
    };

    struct CreateTableParts {
        std::string structDeclaration;
        std::string makeTableExpression;
        std::vector<std::string> warnings;
    };

    struct CreateViewParts {
        std::string structDeclaration;
        std::string makeViewExpression;
        std::vector<DecisionPoint> decisionPoints;
        std::vector<std::string> warnings;
        std::vector<std::string> comments;
    };

}  // namespace sqlite2orm
