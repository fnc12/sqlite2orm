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
        /**
         *  One result column, or nothing when no expression stands where one is required — a
         *  trailing comma in the result list, which SQLite refuses as a syntax error. A parsed
         *  column with no expression is the bare `*`.
         */
        std::optional<SelectColumn> parseSelectResultColumn();
        std::vector<FromClauseItem> parseFromClause();
        FromTableClause parseFromTableItem();
        bool consumeJoinOperator(JoinKind& out);
        void parseJoinConstraint(FromClauseItem& item);
        bool isFromTableItemStart() const;
        bool isFromTableItemStartOrParen() const;

      private:
        /** `parseSelect` without the nesting level it takes: an optional WITH clause and its body. */
        AstNodePointer parseSelectStatement();

        /**
         *  Takes the level the query about to be parsed will sit on, and answers whether there is
         *  one left. On `false` the statement is refused with a parse error instead of recursing
         *  until the stack runs out.
         */
        bool enterQueryLevel();

        /** Queries already on the path from the statement down to what is parsed now. */
        size_t queryDepth = 0;

        Parser& parser;
        TokenStream& tokenStream;

        const Token& current() const {
            return this->tokenStream.current();
        }
        const Token& peekToken(size_t offset = 0) const {
            return this->tokenStream.peekToken(offset);
        }
        const Token& advanceToken() {
            return this->tokenStream.advanceToken();
        }
        bool atEnd() const {
            return this->tokenStream.atEnd();
        }
        bool check(TokenType type) const {
            return this->tokenStream.check(type);
        }
        std::optional<Token> match(TokenType type) {
            return this->tokenStream.match(type);
        }
        bool isColumnNameToken() const {
            return this->tokenStream.isColumnNameToken();
        }
        bool isColumnNameTokenAt(size_t offset) const {
            return this->tokenStream.isColumnNameTokenAt(offset);
        }
    };

}  // namespace sqlite2orm
