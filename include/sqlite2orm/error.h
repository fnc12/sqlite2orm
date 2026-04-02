#pragma once

#include <sqlite2orm/token.h>

#include <string>
#include <vector>

namespace sqlite2orm {

    struct ParseError {
        std::string message;
        SourceLocation location;

        bool operator==(const ParseError& other) const {
            return message == other.message;
        }
    };

    struct ValidationError {
        std::string message;
        SourceLocation location;
        std::string nodeType;

        bool operator==(const ValidationError& other) const {
            return message == other.message && nodeType == other.nodeType;
        }
    };

}  // namespace sqlite2orm
