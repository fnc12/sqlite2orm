#pragma once

#include <sqlite2orm/token.h>

#include <string_view>
#include <vector>
#include <string>
#include <optional>
#include <stdexcept>

namespace sqlite2orm {

    struct TokenizeError : std::runtime_error {
        SourceLocation location;

        TokenizeError(std::string message, SourceLocation loc)
            : std::runtime_error(std::move(message)), location(loc) {}
    };

    class Tokenizer {
      public:
        std::vector<Token> tokenize(std::string_view sql);

      private:
        std::string_view sql;
        size_t position = 0;
        size_t line = 1;
        size_t column = 1;

        bool atEnd() const;
        char peek() const;
        char peekAhead(size_t offset) const;
        char advance();
        void skipWhitespaceAndComments();
        void skipLineComment();
        void skipBlockComment();

        Token makeToken(TokenType type, size_t start, SourceLocation loc) const;

        Token readIdentifierOrKeyword(SourceLocation loc);
        Token readQuotedIdentifier(char quote, SourceLocation loc);
        Token readStringLiteral(SourceLocation loc);
        Token readNumericLiteral(SourceLocation loc);
        Token readBlobLiteral(SourceLocation loc);
        Token readBindParameter(SourceLocation loc);
        Token readOperatorOrPunctuation(SourceLocation loc);
    };

}  // namespace sqlite2orm
