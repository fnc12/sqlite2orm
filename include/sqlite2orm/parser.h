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

    struct ParseResult {
        AstNodePointer astNodePointer;
        std::vector<ParseError> errors;

        explicit operator bool() const { return astNodePointer != nullptr && errors.empty(); }

        bool operator==(const ParseResult& other) const {
            if(this->errors != other.errors) {
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

        ParseResult parse(std::vector<Token> tokens);
        std::vector<ParseResult> parseAll(std::vector<Token> tokens);

        TokenStream& tokens() { return this->tokenStream; }
        const TokenStream& tokens() const { return this->tokenStream; }

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

      private:
        TokenStream tokenStream;
        std::unique_ptr<ExpressionParser> expressionParser;
        std::unique_ptr<SelectParser> selectParser;
        std::unique_ptr<DmlParser> dmlParser;
        std::unique_ptr<DdlParser> ddlParser;
    };

}  // namespace sqlite2orm
