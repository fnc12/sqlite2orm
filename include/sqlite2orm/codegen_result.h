#pragma once

#include <string>
#include <vector>

namespace sqlite2orm {

    struct Alternative {
        std::string value;
        std::string code;
        std::string description;
        bool hidden = false;
        /** Optional notes when this alternative is shown or chosen (e.g. build requirements); any consumer may show them. */
        std::vector<std::string> comments;

        bool operator==(const Alternative&) const = default;
    };

    struct DecisionPoint {
        int id = 0;
        std::string category;
        std::string chosenValue;
        std::string chosenCode;
        std::vector<Alternative> alternatives;

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

}  // namespace sqlite2orm
