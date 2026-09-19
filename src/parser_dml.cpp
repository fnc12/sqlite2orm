#include "parser_dml.h"
#include <sqlite2orm/parser.h>

namespace sqlite2orm {

    DmlParser::DmlParser(Parser& parser, TokenStream& tokenStream)
        : parser(parser), tokenStream(tokenStream) {}

    ConflictClause DmlParser::parseInsertOrConflictKeyword() {
        if(match(TokenType::kwRollback)) return ConflictClause::rollback;
        if(match(TokenType::kwAbort)) return ConflictClause::abort;
        if(match(TokenType::kwFail)) return ConflictClause::fail;
        if(match(TokenType::kwIgnore)) return ConflictClause::ignore;
        if(match(TokenType::kwReplace)) return ConflictClause::replace;
        return ConflictClause::none;
    }

    bool DmlParser::parseDmlQualifiedTable(std::optional<std::string>& schemaOut, std::string& tableOut) {
        schemaOut.reset();
        if(!isColumnNameToken()) return false;
        std::string first(current().value);
        advanceToken();
        if(match(TokenType::dot)) {
            if(!isColumnNameToken()) return false;
            schemaOut = std::move(first);
            tableOut = std::string(current().value);
            advanceToken();
        } else {
            tableOut = std::move(first);
        }
        return true;
    }

    bool DmlParser::parseCommaSeparatedUpdateAssignments(std::vector<UpdateAssignment>& out) {
        if(!isColumnNameToken()) return false;
        while(true) {
            UpdateAssignment assignment;
            assignment.columnName = std::string(current().value);
            advanceToken();
            if(!match(TokenType::eq)) return false;
            assignment.value = this->parser.parseExpression();
            if(!assignment.value) return false;
            out.push_back(std::move(assignment));
            if(!match(TokenType::comma)) {
                break;
            }
            if(!isColumnNameToken()) return false;
        }
        return true;
    }

    bool DmlParser::parseUpsertTail(InsertNode& node, bool replaceInto) {
        if(!check(TokenType::kwOn)) return true;
        if(replaceInto) return false;
        advanceToken();
        if(!match(TokenType::kwConflict)) return false;
        node.hasUpsertClause = true;
        if(check(TokenType::kwDo)) {
            advanceToken();
            if(!match(TokenType::kwNothing)) return false;
            node.upsertAction = InsertUpsertAction::doNothing;
            return true;
        }
        if(!match(TokenType::leftParen)) return false;
        if(!isColumnNameToken()) return false;
        node.upsertConflictColumns.push_back(std::string(current().value));
        advanceToken();
        while(match(TokenType::comma)) {
            if(!isColumnNameToken()) return false;
            node.upsertConflictColumns.push_back(std::string(current().value));
            advanceToken();
        }
        if(!match(TokenType::rightParen)) return false;
        if(match(TokenType::kwWhere)) {
            node.upsertConflictWhere = this->parser.parseExpression();
            if(!node.upsertConflictWhere) return false;
        }
        if(!match(TokenType::kwDo)) return false;
        if(match(TokenType::kwNothing)) {
            node.upsertAction = InsertUpsertAction::doNothing;
            return true;
        }
        if(!match(TokenType::kwUpdate)) return false;
        if(!match(TokenType::kwSet)) return false;
        if(!parseCommaSeparatedUpdateAssignments(node.upsertUpdateAssignments)) return false;
        node.upsertAction = InsertUpsertAction::doUpdate;
        if(match(TokenType::kwWhere)) {
            node.upsertUpdateWhere = this->parser.parseExpression();
            if(!node.upsertUpdateWhere) return false;
        }
        return true;
    }

    AstNodePointer DmlParser::parseInsertStatement(bool replaceInto) {
        auto location = current().location;
        auto node = std::make_unique<InsertNode>(location);
        node->replaceInto = replaceInto;
        if(replaceInto) {
            advanceToken();
        } else {
            advanceToken();
            if(match(TokenType::kwOr)) {
                node->orConflict = parseInsertOrConflictKeyword();
                if(node->orConflict == ConflictClause::none) {
                    return nullptr;
                }
            }
        }
        if(!match(TokenType::kwInto)) return nullptr;
        if(!parseDmlQualifiedTable(node->schemaName, node->tableName)) return nullptr;

        if(match(TokenType::leftParen)) {
            if(!isColumnNameToken()) return nullptr;
            node->columnNames.push_back(std::string(current().value));
            advanceToken();
            while(match(TokenType::comma)) {
                if(!isColumnNameToken()) return nullptr;
                node->columnNames.push_back(std::string(current().value));
                advanceToken();
            }
            if(!match(TokenType::rightParen)) return nullptr;
        }

        if(check(TokenType::kwDefault)) {
            advanceToken();
            if(!match(TokenType::kwValues)) return nullptr;
            node->dataKind = InsertDataKind::defaultValues;
            if(!parseUpsertTail(*node, replaceInto)) return nullptr;
            if(!parseReturningClause(node->returning)) return nullptr;
            return node;
        }
        if(check(TokenType::kwValues)) {
            advanceToken();
            node->dataKind = InsertDataKind::values;
            do {
                if(!match(TokenType::leftParen)) return nullptr;
                std::vector<AstNodePointer> row;
                if(check(TokenType::rightParen)) {
                    advanceToken();
                } else {
                    AstNodePointer expr = this->parser.parseExpression();
                    if(!expr) return nullptr;
                    row.push_back(std::move(expr));
                    while(match(TokenType::comma)) {
                        expr = this->parser.parseExpression();
                        if(!expr) return nullptr;
                        row.push_back(std::move(expr));
                    }
                    if(!match(TokenType::rightParen)) return nullptr;
                }
                node->valueRows.push_back(std::move(row));
            } while(match(TokenType::comma));
            if(!parseUpsertTail(*node, replaceInto)) return nullptr;
            if(!parseReturningClause(node->returning)) return nullptr;
            return node;
        }

        node->dataKind = InsertDataKind::selectQuery;
        node->selectStatement = this->parser.parseSelect();
        if(!node->selectStatement) return nullptr;
        if(!parseUpsertTail(*node, replaceInto)) return nullptr;
        if(!parseReturningClause(node->returning)) return nullptr;
        return node;
    }

    AstNodePointer DmlParser::parseUpdateStatement() {
        auto location = current().location;
        advanceToken();
        auto node = std::make_unique<UpdateNode>(location);
        if(match(TokenType::kwOr)) {
            node->orConflict = parseInsertOrConflictKeyword();
            if(node->orConflict == ConflictClause::none) {
                return nullptr;
            }
        }
        if(!parseDmlQualifiedTable(node->schemaName, node->tableName)) return nullptr;
        if(!match(TokenType::kwSet)) return nullptr;
        if(!parseCommaSeparatedUpdateAssignments(node->assignments)) return nullptr;
        if(match(TokenType::kwFrom)) {
            node->fromClause = this->parser.parseFromClause();
        }
        if(match(TokenType::kwWhere)) {
            node->whereClause = this->parser.parseExpression();
            if(!node->whereClause) return nullptr;
        }
        if(!parseReturningClause(node->returning)) return nullptr;
        return node;
    }

    AstNodePointer DmlParser::parseValuesStatement() {
        auto location = current().location;
        advanceToken();
        auto node = std::make_unique<SelectNode>(location);
        do {
            if(!match(TokenType::leftParen)) return nullptr;
            std::vector<AstNodePointer> row;
            if(!check(TokenType::rightParen)) {
                auto expr = this->parser.parseExpression();
                if(!expr) return nullptr;
                row.push_back(std::move(expr));
                while(match(TokenType::comma)) {
                    expr = this->parser.parseExpression();
                    if(!expr) return nullptr;
                    row.push_back(std::move(expr));
                }
            }
            if(!match(TokenType::rightParen)) return nullptr;

            if(node->columns.empty()) {
                for(auto& val : row) {
                    node->columns.push_back(SelectColumn{std::shared_ptr<AstNode>(std::move(val)), ""});
                }
            }
        } while(match(TokenType::comma));
        return node;
    }

    AstNodePointer DmlParser::parseDeleteStatement() {
        auto location = current().location;
        advanceToken();
        if(!match(TokenType::kwFrom)) return nullptr;
        auto node = std::make_unique<DeleteNode>(location);
        if(!parseDmlQualifiedTable(node->schemaName, node->tableName)) return nullptr;
        if(match(TokenType::kwWhere)) {
            node->whereClause = this->parser.parseExpression();
            if(!node->whereClause) return nullptr;
        }
        if(!parseReturningClause(node->returning)) return nullptr;
        return node;
    }

    bool DmlParser::parseReturningClause(std::vector<ReturningColumn>& out) {
        if(!match(TokenType::kwReturning)) {
            return true;
        }
        do {
            ReturningColumn column;
            if(match(TokenType::star)) {
                // expression=nullptr signals `*`
            } else {
                column.expression = this->parser.parseExpression();
                if(!column.expression) return false;
                if(match(TokenType::kwAs)) {
                    if(!isColumnNameToken()) return false;
                    column.alias = std::string(current().value);
                    advanceToken();
                } else if(isColumnNameToken() && !check(TokenType::comma) &&
                           !check(TokenType::semicolon) && !atEnd()) {
                    column.alias = std::string(current().value);
                    advanceToken();
                }
            }
            out.push_back(std::move(column));
        } while(match(TokenType::comma));
        return true;
    }

}  // namespace sqlite2orm
