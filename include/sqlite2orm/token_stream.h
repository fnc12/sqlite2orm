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

        const std::vector<Token>& allTokens() const {
            return this->tokens;
        }
        size_t currentPosition() const {
            return this->position;
        }
        /**
         *  The source text consumed since the position `firstTokenIndex` names: from the first
         *  character of that token through the last one of the token before the current position,
         *  whatever was written between them included. Every token's text points into the one SQL
         *  the stream was reset with, and the span copies that stretch of it, so what comes back
         *  outlives the SQL. The end-of-input token carries no text of its own and is left out; a
         *  stream that has consumed nothing but such tokens yields an empty span.
         */
        SourceSpan consumedSpanFrom(size_t firstTokenIndex) const;
        void setPosition(size_t newPosition) {
            this->position = newPosition;
        }

      private:
        std::vector<Token> tokens;
        size_t position = 0;
    };

}  // namespace sqlite2orm
