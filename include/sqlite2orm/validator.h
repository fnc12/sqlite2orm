#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/error.h>

#include <string>
#include <unordered_set>
#include <vector>

namespace sqlite2orm {

    class Validator {
      public:
        std::vector<ValidationError> validate(const AstNode& ast);

      private:
        std::unordered_set<std::string> knownCteTableNames;
    };

}  // namespace sqlite2orm
