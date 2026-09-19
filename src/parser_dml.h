#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/token_stream.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sqlite2orm {

    class Parser;

    class DmlParser {
      public:
        DmlParser(Parser& parser, TokenStream& tokenStream);

        ConflictClause parseInsertOrConflictKeyword();
        bool parseDmlQualifiedTable(std::optional<std::string>& schemaOut, std::string& tableOut);
        AstNodePointer parseInsertStatement(bool replaceInto);
        AstNodePointer parseUpdateStatement();
        AstNodePointer parseValuesStatement();
        AstNodePointer parseDeleteStatement();
        bool parseCommaSeparatedUpdateAssignments(std::vector<UpdateAssignment>& out);
        bool parseUpsertTail(InsertNode& node, bool replaceInto);
        bool parseReturningClause(std::vector<ReturningColumn>& out);

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
    };

}  // namespace sqlite2orm
