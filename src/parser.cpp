#include <sqlite2orm/parser.h>

#include "parser_expression.h"
#include "parser_select.h"
#include "parser_dml.h"
#include "parser_ddl.h"

namespace sqlite2orm {

    Parser::Parser()
        : expressionParser(std::make_unique<ExpressionParser>(*this, this->tokenStream)),
          selectParser(std::make_unique<SelectParser>(*this, this->tokenStream)),
          dmlParser(std::make_unique<DmlParser>(*this, this->tokenStream)),
          ddlParser(std::make_unique<DdlParser>(*this, this->tokenStream)) {}

    Parser::~Parser() = default;

    AstNodePointer Parser::parseExpression() {
        return this->expressionParser->parseExpression();
    }

    AstNodePointer Parser::parsePrimary() {
        return this->expressionParser->parsePrimary();
    }

    bool Parser::parseOverClauseParenContents(OverClause& over, bool allowSimpleNamedWindow) {
        return this->expressionParser->parseOverClauseParenContents(over, allowSimpleNamedWindow);
    }

    AstNodePointer Parser::parseSelect() {
        return this->selectParser->parseSelect();
    }

    AstNodePointer Parser::parseSelectCompoundBody() {
        return this->selectParser->parseSelectCompoundBody();
    }

    AstNodePointer Parser::parseCompoundSelectCore() {
        return this->selectParser->parseCompoundSelectCore();
    }

    std::vector<FromClauseItem> Parser::parseFromClause() {
        return this->selectParser->parseFromClause();
    }

    AstNodePointer Parser::parseInsertStatement(bool replaceInto) {
        return this->dmlParser->parseInsertStatement(replaceInto);
    }

    AstNodePointer Parser::parseUpdateStatement() {
        return this->dmlParser->parseUpdateStatement();
    }

    AstNodePointer Parser::parseValuesStatement() {
        return this->dmlParser->parseValuesStatement();
    }

    AstNodePointer Parser::parseDeleteStatement() {
        return this->dmlParser->parseDeleteStatement();
    }

    bool Parser::parseDmlQualifiedTable(std::optional<std::string>& schemaOut, std::string& tableOut) {
        return this->dmlParser->parseDmlQualifiedTable(schemaOut, tableOut);
    }

    ParseResult Parser::parse(std::vector<Token> tokens) {
        this->tokenStream.reset(std::move(tokens));

        AstNodePointer astNodePointer;
        if(this->tokenStream.check(TokenType::kwCreate)) {
            astNodePointer = this->ddlParser->parseCreate();
        } else if(this->tokenStream.check(TokenType::kwSelect) || this->tokenStream.check(TokenType::kwWith)) {
            astNodePointer = parseSelect();
        } else if(this->tokenStream.check(TokenType::kwInsert)) {
            astNodePointer = parseInsertStatement(false);
        } else if(this->tokenStream.check(TokenType::kwReplace) && this->tokenStream.peekToken(1).type == TokenType::kwInto) {
            astNodePointer = parseInsertStatement(true);
        } else if(this->tokenStream.check(TokenType::kwValues)) {
            astNodePointer = parseSelectCompoundBody();
        } else if(this->tokenStream.check(TokenType::kwUpdate)) {
            astNodePointer = parseUpdateStatement();
        } else if(this->tokenStream.check(TokenType::kwDelete)) {
            astNodePointer = parseDeleteStatement();
        } else if(this->tokenStream.check(TokenType::kwBegin) || this->tokenStream.check(TokenType::kwCommit) ||
                  this->tokenStream.check(TokenType::kwRollback) || this->tokenStream.check(TokenType::kwEnd)) {
            astNodePointer = this->ddlParser->parseTransactionControlStatement();
        } else if(this->tokenStream.check(TokenType::kwVacuum)) {
            astNodePointer = this->ddlParser->parseVacuumStatement();
        } else if(this->tokenStream.check(TokenType::kwDrop)) {
            astNodePointer = this->ddlParser->parseDropStatement();
        } else if(this->tokenStream.check(TokenType::kwAlter)) {
            astNodePointer = this->ddlParser->parseAlterTableStatement();
        } else if(this->tokenStream.check(TokenType::kwSavepoint)) {
            astNodePointer = this->ddlParser->parseSavepointStatement();
        } else if(this->tokenStream.check(TokenType::kwRelease)) {
            astNodePointer = this->ddlParser->parseReleaseStatement();
        } else if(this->tokenStream.check(TokenType::kwAttach)) {
            astNodePointer = this->ddlParser->parseAttachStatement();
        } else if(this->tokenStream.check(TokenType::kwDetach)) {
            astNodePointer = this->ddlParser->parseDetachStatement();
        } else if(this->tokenStream.check(TokenType::kwAnalyze)) {
            astNodePointer = this->ddlParser->parseAnalyzeStatement();
        } else if(this->tokenStream.check(TokenType::kwReindex)) {
            astNodePointer = this->ddlParser->parseReindexStatement();
        } else if(this->tokenStream.check(TokenType::kwPragma)) {
            astNodePointer = this->ddlParser->parsePragmaStatement();
        } else if(this->tokenStream.check(TokenType::kwExplain)) {
            astNodePointer = this->ddlParser->parseExplainStatement();
        } else {
            astNodePointer = parseExpression();
        }

        if(!astNodePointer) {
            const Token& token = this->tokenStream.current();
            return ParseResult{nullptr, {ParseError{"unexpected token: " + std::string(token.value), token.location}}};
        }

        if(!this->tokenStream.atEnd()) {
            const Token& token = this->tokenStream.current();
            if(token.type != TokenType::semicolon) {
                return ParseResult{std::move(astNodePointer),
                                  {ParseError{"unexpected token after statement: " + std::string(token.value), token.location}}};
            }
        }

        return ParseResult{std::move(astNodePointer), {}};
    }

    std::vector<ParseResult> Parser::parseAll(std::vector<Token> tokensInput) {
        this->tokenStream.reset(std::move(tokensInput));

        std::vector<ParseResult> results;
        while(!this->tokenStream.atEnd()) {
            while(!this->tokenStream.atEnd() && this->tokenStream.current().type == TokenType::semicolon) {
                this->tokenStream.advanceToken();
            }
            if(this->tokenStream.atEnd()) {
                break;
            }

            // CREATE [TEMP|TEMPORARY] TRIGGER bodies contain semicolon-terminated statements
            // between BEGIN and END (the SQLite grammar requires them), so a semicolon ends the
            // whole statement only after the trigger body's END.
            bool isCreateTrigger = false;
            if(this->tokenStream.check(TokenType::kwCreate)) {
                const TokenType afterCreate = this->tokenStream.peekToken(1).type;
                if(afterCreate == TokenType::kwTrigger) {
                    isCreateTrigger = true;
                } else if((afterCreate == TokenType::kwTemp || afterCreate == TokenType::kwTemporary) &&
                          this->tokenStream.peekToken(2).type == TokenType::kwTrigger) {
                    isCreateTrigger = true;
                }
            }

            std::vector<Token> slice;
            int parenDepth = 0;
            int caseDepth = 0;
            bool inTriggerBody = false;
            bool triggerBodyClosed = false;
            while(!this->tokenStream.atEnd()) {
                const Token& tok = this->tokenStream.current();
                if(tok.type == TokenType::leftParen) {
                    ++parenDepth;
                } else if(tok.type == TokenType::rightParen) {
                    --parenDepth;
                } else if(isCreateTrigger && tok.type == TokenType::kwCase) {
                    ++caseDepth;
                } else if(isCreateTrigger && tok.type == TokenType::kwBegin && !inTriggerBody) {
                    inTriggerBody = true;
                } else if(isCreateTrigger && tok.type == TokenType::kwEnd) {
                    if(caseDepth > 0) {
                        --caseDepth;
                    } else if(inTriggerBody) {
                        triggerBodyClosed = true;
                    }
                } else if(tok.type == TokenType::semicolon && parenDepth <= 0 &&
                          (!isCreateTrigger || !inTriggerBody || triggerBodyClosed)) {
                    break;
                }
                slice.push_back(tok);
                this->tokenStream.advanceToken();
            }
            if(!this->tokenStream.atEnd() && this->tokenStream.current().type == TokenType::semicolon) {
                slice.push_back(this->tokenStream.current());
                this->tokenStream.advanceToken();
            }

            if(slice.empty() || slice.back().type == TokenType::eof) {
                continue;
            }
            slice.push_back(Token{TokenType::eof, "", slice.back().location});

            Parser inner;
            results.push_back(inner.parse(std::move(slice)));
        }
        return results;
    }

}  // namespace sqlite2orm
