#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/error.h>

#include <vector>

namespace sqlite2orm {

    class Validator {
      public:
        std::vector<ValidationError> validate(const AstNode& ast);
    };

}  // namespace sqlite2orm
