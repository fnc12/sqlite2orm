#include <sqlite2orm/token_stream.h>

#include <algorithm>

namespace sqlite2orm {

    void TokenStream::reset(std::vector<Token> newTokens) {
        this->tokens = std::move(newTokens);
        this->position = 0;
    }

    const Token& TokenStream::current() const {
        return this->tokens.at(this->position);
    }

    const Token& TokenStream::peekToken(size_t offset) const {
        size_t index = this->position + offset;
        if(index >= this->tokens.size()) {
            return this->tokens.back();
        }
        return this->tokens.at(index);
    }

    const Token& TokenStream::advanceToken() {
        const Token& token = this->tokens.at(this->position);
        if(token.type != TokenType::eof) {
            ++this->position;
        }
        return token;
    }

    SourceSpan TokenStream::consumedSpanFrom(size_t firstTokenIndex) const {
        size_t lastTokenIndex = std::min(this->position, this->tokens.size());
        while(lastTokenIndex > firstTokenIndex && this->tokens.at(lastTokenIndex - 1).value.empty()) {
            --lastTokenIndex;
        }
        if(lastTokenIndex <= firstTokenIndex || firstTokenIndex >= this->tokens.size()) {
            return SourceSpan{};
        }
        const Token& first = this->tokens.at(firstTokenIndex);
        const Token& last = this->tokens.at(lastTokenIndex - 1);
        if(first.value.empty()) {
            return SourceSpan{};
        }
        const size_t length =
            static_cast<size_t>(last.value.data() + last.value.size() - first.value.data());
        return SourceSpan{first.location, std::string_view(first.value.data(), length)};
    }

    bool TokenStream::atEnd() const {
        return current().type == TokenType::eof;
    }

    bool TokenStream::check(TokenType type) const {
        return current().type == type;
    }

    std::optional<Token> TokenStream::match(TokenType type) {
        if(check(type)) {
            return advanceToken();
        }
        return std::nullopt;
    }

    bool TokenStream::isColumnNameToken() const {
        auto type = current().type;
        if(type == TokenType::identifier || type == TokenType::stringLiteral) return true;
        return type >= TokenType::kwAbort && type <= TokenType::kwWithout;
    }

    bool TokenStream::isColumnNameTokenAt(size_t offsetFromCurrent) const {
        size_t index = this->position + offsetFromCurrent;
        if(index >= this->tokens.size()) return false;
        auto type = this->tokens.at(index).type;
        if(type == TokenType::identifier || type == TokenType::stringLiteral) return true;
        return type >= TokenType::kwAbort && type <= TokenType::kwWithout;
    }

    void TokenStream::skipToSemicolon() {
        int parenDepth = 0;
        while(!atEnd()) {
            const TokenType tokenType = current().type;
            if(tokenType == TokenType::semicolon && parenDepth == 0) {
                advanceToken();
                return;
            }
            if(tokenType == TokenType::leftParen) {
                ++parenDepth;
            } else if(tokenType == TokenType::rightParen && parenDepth > 0) {
                --parenDepth;
            }
            advanceToken();
        }
    }

}  // namespace sqlite2orm
