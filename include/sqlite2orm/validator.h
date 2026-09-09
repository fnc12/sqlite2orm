#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/error.h>

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace sqlite2orm {

    /** Whether `lowerName` (already lower-cased) is a SQLite built-in function sqlite2orm recognizes. */
    bool isKnownSqlFunction(std::string_view lowerName);

    class Validator {
      public:
        std::vector<ValidationError> validate(const AstNode& ast);

      private:
        std::unordered_set<std::string> knownCteTableNames;
    };

}  // namespace sqlite2orm
