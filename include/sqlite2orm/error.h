#pragma once

#include <sqlite2orm/token.h>

#include <string>
#include <vector>

namespace sqlite2orm {

    struct ParseError {
        std::string message;
        SourceLocation location;

        bool operator==(const ParseError&) const = default;
    };

    struct ValidationError {
        std::string message;
        SourceLocation location;
        std::string nodeType;

        bool operator==(const ValidationError&) const = default;
    };

}  // namespace sqlite2orm
