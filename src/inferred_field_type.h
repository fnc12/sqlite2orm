#pragma once

#include <string>

namespace sqlite2orm {

    struct InferredFieldType {
        std::string cppType;
        bool nullable = false;
    };

}  // namespace sqlite2orm
