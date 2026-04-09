#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/token_stream.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sqlite2orm {

    class Parser;

    class DdlParser {
      public:
        DdlParser(Parser& parser, TokenStream& tokenStream);

        AstNodePointer parseCreate();
        AstNodePointer parseCreateViewTail(SourceLocation location);
        AstNodePointer parseCreateTableTail(SourceLocation location);
        AstNodePointer parseCreateTriggerAfterKeyword(SourceLocation location, bool temporary);
        AstNodePointer parseCreateIndexAfterKeyword(SourceLocation location, bool unique);
        AstNodePointer parseCreateVirtualTableTail(SourceLocation location, bool temporary);
        bool parseIndexColumnSpec(IndexColumnSpec& out);
        AstNodePointer parseTriggerBodyStatement();
        ColumnDef parseColumnDef();
        std::string parseColumnTypeName();
        void parseColumnConstraints(ColumnDef& columnDef);
        AstNodePointer parseDefaultValue();
        ConflictClause parseConflictClause();
        ForeignKeyClause parseForeignKeyClause();
        ForeignKeyAction parseForeignKeyAction();
        TableForeignKey parseTableForeignKey();

        AstNodePointer parseTransactionControlStatement();
        AstNodePointer parseVacuumStatement();
        AstNodePointer parseDropStatement();
        AstNodePointer parseAlterTableStatement();
        AstNodePointer parseSavepointStatement();
        AstNodePointer parseReleaseStatement();
        AstNodePointer parseAttachStatement();
        AstNodePointer parseDetachStatement();
        AstNodePointer parseAnalyzeStatement();
        AstNodePointer parseReindexStatement();
        AstNodePointer parsePragmaStatement();
        AstNodePointer parseExplainStatement();

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
        void skipToSemicolon() { this->tokenStream.skipToSemicolon(); }
    };

}  // namespace sqlite2orm
