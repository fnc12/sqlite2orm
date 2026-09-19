#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/token_stream.h>

#include <memory>
#include <optional>
#include <vector>

namespace sqlite2orm {

    class Parser;

    class SelectParser {
      public:
        SelectParser(Parser& parser, TokenStream& tokenStream);

        AstNodePointer parseSelect();
        AstNodePointer parseSelectCompoundBody();
        AstNodePointer parseCompoundSelectCore();
        AstNodePointer parseSelectCore();
        SelectColumn parseSelectResultColumn();
        std::vector<FromClauseItem> parseFromClause();
        FromTableClause parseFromTableItem();
        bool consumeJoinOperator(JoinKind& out);
        void parseJoinConstraint(FromClauseItem& item);
        bool isFromTableItemStart() const;
        bool isFromTableItemStartOrParen() const;

      private:
        Parser& parser;
        TokenStream& tokenStream;

        const Token& current() const { return this->tokenStream.current(); }
        const Token& peekToken(size_t offset = 0) const { return this->tokenStream.peekToken(offset); }
        const Token& advanceToken() { return this->tokenStream.advanceToken(); }
        bool atEnd() const { return this->tokenStream.atEnd(); }
        bool check(TokenType type) const { return this->tokenStream.check(type); }
        std::optional<Token> match(TokenType type) { return this->tokenStream.match(type); }
        bool isColumnNameToken() const { return this->tokenStream.isColumnNameToken(); }
        bool isColumnNameTokenAt(size_t offset) const { return this->tokenStream.isColumnNameTokenAt(offset); }
    };

}  // namespace sqlite2orm
