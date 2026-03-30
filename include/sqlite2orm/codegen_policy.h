#pragma once

#include <string>
#include <unordered_map>

namespace sqlite2orm {

    /**
     *  Chooses sqlite_orm codegen variants for decision points (phase 21.5).
     *  Map category → alternative `value` (e.g. expr_style → functional).
     */
    struct CodeGenPolicy {
        std::unordered_map<std::string, std::string> chosenAlternativeValueByCategory;
    };

}  // namespace sqlite2orm
