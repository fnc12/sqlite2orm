#pragma once

#include <sqlite2orm/token.h>
#include <sqlite2orm/ast.h>
#include <sqlite2orm/error.h>

#include <memory>
#include <optional>
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

    class Parser {
      public:
        ParseResult parse(std::vector<Token> tokens);

      private:
        std::vector<Token> tokens;
        size_t position = 0;

        const Token& current() const;
        const Token& peekToken(size_t offset = 0) const;
        const Token& advanceToken();
        bool atEnd() const;
        bool check(TokenType type) const;
        std::optional<Token> match(TokenType type);

        AstNodePointer parseExpression();
        AstNodePointer parseBinaryExpression(int minPrecedence);
        AstNodePointer parsePrimary();
        AstNodePointer parseLiteral();
        AstNodePointer parseColumnRef();
        AstNodePointer parseFunctionCall();
        /** Parses `OVER …` when `current()` is `kwOver`; returns nullptr on failure. */
        std::unique_ptr<OverClause> parseOverClauseBody();
        /**
         * Parses the inside of `( ... )` for `OVER (...)` / `WINDOW w AS (...)`.
         * Leaves the closing `)` to the caller. If `allowSimpleNamedWindow`, accepts `( other_window )`.
         */
        bool parseOverClauseParenContents(OverClause& over, bool allowSimpleNamedWindow);
        void parseOverOrderByList(std::vector<OrderByTerm>& out);
        std::unique_ptr<WindowFrameSpec> parseWindowFrameSpec();
        bool parseWindowFrameBound(WindowFrameBound& out);
        AstNodePointer parseCast();
        AstNodePointer parseCase();
        AstNodePointer tryParseSpecialPostfix(AstNodePointer& left);
        bool isFunctionNameStart() const;

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
        bool isColumnNameToken() const;
        bool isColumnNameTokenAt(size_t offsetFromCurrent) const;
        bool isFromTableItemStart() const;

        AstNodePointer parseSelect();
        /** `parseSelectCore` plus UNION/INTERSECT/EXCEPT arms (no leading WITH). */
        AstNodePointer parseSelectCompoundBody();
        AstNodePointer parseSelectCore();
        SelectColumn parseSelectResultColumn();
        /** Parse a complete FROM clause (first table + comma/join items), handling (join-clause). */
        std::vector<FromClauseItem> parseFromClause();
        FromTableClause parseFromTableItem();
        bool consumeJoinOperator(JoinKind& out);
        void parseJoinConstraint(FromClauseItem& item);
        bool isFromTableItemStartOrParen() const;

        ConflictClause parseInsertOrConflictKeyword();
        bool parseDmlQualifiedTable(std::optional<std::string>& schemaOut, std::string& tableOut);
        AstNodePointer parseInsertStatement(bool replaceInto);
        AstNodePointer parseUpdateStatement();
        AstNodePointer parseValuesStatement();
        AstNodePointer parseDeleteStatement();
        bool parseCommaSeparatedUpdateAssignments(std::vector<UpdateAssignment>& out);
        bool parseUpsertTail(InsertNode& node, bool replaceInto);

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
        bool parseReturningClause(std::vector<ReturningColumn>& out);

        /** Skip tokens until `;` at paren depth 0 (statement end); consumes the semicolon if present. */
        void skipToSemicolon();
    };

}  // namespace sqlite2orm
