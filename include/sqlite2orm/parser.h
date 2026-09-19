#pragma once

#include <sqlite2orm/token.h>
#include <sqlite2orm/token_stream.h>
#include <sqlite2orm/ast.h>
#include <sqlite2orm/error.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sqlite2orm {

    /**
     *  What one parse answers with. Everything in it owns its text: the AST keeps no view into the
     *  SQL the tokens were made from, so a consumer is free to parse once, let the SQL go, and
     *  generate from the AST whenever it likes.
     */
    struct ParseResult {
        AstNodePointer astNodePointer;
        std::vector<ParseError> errors;

        explicit operator bool() const {
            return astNodePointer != nullptr && errors.empty();
        }

        bool operator==(const ParseResult& other) const {
            if (this->errors != other.errors) {
                return false;
            }
            return astNodesEqual(this->astNodePointer, other.astNodePointer);
        }
    };

    class ExpressionParser;
    class SelectParser;
    class DmlParser;
    class DdlParser;

    class Parser {
      public:
        Parser();
        ~Parser();
        Parser(const Parser&) = delete;
        Parser& operator=(const Parser&) = delete;

        /**
         *  Parses the first statement of `tokens`. The SQL those tokens view into has to be alive
         *  for the call; the AST that comes back copies what it needs out of it and is readable
         *  afterwards on its own.
         */
        ParseResult parse(std::vector<Token> tokens);
        /** Same, for every semicolon-separated statement; the same lifetimes hold. */
        std::vector<ParseResult> parseAll(std::vector<Token> tokens);

        TokenStream& tokens() {
            return this->tokenStream;
        }
        const TokenStream& tokens() const {
            return this->tokenStream;
        }

        AstNodePointer parseExpression();
        AstNodePointer parsePrimary();
        AstNodePointer parseSelect();
        AstNodePointer parseSelectCompoundBody();
        AstNodePointer parseCompoundSelectCore();
        AstNodePointer parseInsertStatement(bool replaceInto);
        AstNodePointer parseUpdateStatement();
        AstNodePointer parseValuesStatement();
        AstNodePointer parseDeleteStatement();
        bool parseOverClauseParenContents(OverClause& over, bool allowSimpleNamedWindow);
        bool parseDmlQualifiedTable(std::optional<std::string>& schemaOut, std::string& tableOut);
        std::vector<FromClauseItem> parseFromClause();

        /**
         *  Hands `parse` an error a recursive-descent helper hit. Those helpers answer with a null
         *  node and nothing else, which `parse` would otherwise report as "unexpected token"; the
         *  first error reported this way is the one the statement is refused with.
         */
        void reportError(ParseError error);

      private:
        TokenStream tokenStream;
        std::optional<ParseError> pendingError;
        std::unique_ptr<ExpressionParser> expressionParser;
        std::unique_ptr<SelectParser> selectParser;
        std::unique_ptr<DmlParser> dmlParser;
        std::unique_ptr<DdlParser> ddlParser;
    };

}  // namespace sqlite2orm
