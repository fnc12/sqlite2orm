#pragma once

#include <string>
#include <unordered_map>

namespace sqlite2orm {

    /**
     *  Chooses sqlite_orm codegen variants for decision points (phase 21.5).
     *  Map category → alternative `value` (e.g. expr_style → functional).
     *
     *  `column_alias_style`: `alias_tag` (default: colalias_* / generated alias_tag + get<>()) or
     *  `cpp20_literal` (constexpr orm_column_alias + as<name>; requires SQLITE_ORM_WITH_CPP20_ALIASES, C++20).
     */
    struct CodeGenPolicy {
        std::unordered_map<std::string, std::string> chosenAlternativeValueByCategory;
    };

}  // namespace sqlite2orm
