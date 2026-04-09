#pragma once

#include <sqlite2orm/token.h>

#include <optional>
#include <vector>

namespace sqlite2orm {

    class TokenStream {
      public:
        void reset(std::vector<Token> newTokens);

        const Token& current() const;
        const Token& peekToken(size_t offset = 0) const;
        const Token& advanceToken();
        bool atEnd() const;
        bool check(TokenType type) const;
        std::optional<Token> match(TokenType type);
        void skipToSemicolon();

        bool isColumnNameToken() const;
        bool isColumnNameTokenAt(size_t offsetFromCurrent) const;

        const std::vector<Token>& allTokens() const { return this->tokens; }
        size_t currentPosition() const { return this->position; }
        void setPosition(size_t newPosition) { this->position = newPosition; }

      private:
        std::vector<Token> tokens;
        size_t position = 0;
    };

}  // namespace sqlite2orm
