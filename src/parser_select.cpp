#include "parser_select.h"
#include <sqlite2orm/parser.h>

namespace sqlite2orm {

    SelectParser::SelectParser(Parser& parser, TokenStream& tokenStream)
        : parser(parser), tokenStream(tokenStream) {}

    bool SelectParser::isFromTableItemStart() const {
        return isColumnNameToken() ||
               (check(TokenType::leftParen) && peekToken(1).type == TokenType::kwSelect);
    }

    bool SelectParser::isFromTableItemStartOrParen() const {
        if(isFromTableItemStart()) return true;
        if(check(TokenType::leftParen) && isColumnNameTokenAt(1)) return true;
        return false;
    }

    AstNodePointer SelectParser::parseSelectCore() {
        if(!check(TokenType::kwSelect)) {
            return nullptr;
        }
        auto location = current().location;
        advanceToken();

        auto node = std::make_unique<SelectNode>(location);

        if(match(TokenType::kwDistinct)) {
            node->distinct = true;
        } else if(match(TokenType::kwAll)) {
            node->selectAll = true;
        }

        node->columns.push_back(parseSelectResultColumn());
        while(match(TokenType::comma)) {
            node->columns.push_back(parseSelectResultColumn());
        }

        if(match(TokenType::kwFrom)) {
            node->fromClause = parseFromClause();
        }

        if(match(TokenType::kwWhere)) {
            node->whereClause = this->parser.parseExpression();
        }

        if(check(TokenType::kwGroup)) {
            advanceToken();
            match(TokenType::kwBy);
            GroupByClause groupByClause;
            groupByClause.expressions.push_back(this->parser.parseExpression());
            while(match(TokenType::comma)) {
                groupByClause.expressions.push_back(this->parser.parseExpression());
            }
            if(match(TokenType::kwHaving)) {
                groupByClause.having = this->parser.parseExpression();
            }
            node->groupBy = std::move(groupByClause);
        }

        if(match(TokenType::kwWindow)) {
            do {
                if(!isColumnNameToken()) {
                    return nullptr;
                }
                NamedWindowDefinition winDef;
                winDef.name = std::string(current().value);
                advanceToken();
                if(!match(TokenType::kwAs)) {
                    return nullptr;
                }
                if(!match(TokenType::leftParen)) {
                    return nullptr;
                }
                winDef.definition = std::make_unique<OverClause>();
                if(!this->parser.parseOverClauseParenContents(*winDef.definition, true)) {
                    return nullptr;
                }
                if(!match(TokenType::rightParen)) {
                    return nullptr;
                }
                node->namedWindows.push_back(std::move(winDef));
            } while(match(TokenType::comma));
        }

        if(check(TokenType::kwOrder)) {
            advanceToken();
            match(TokenType::kwBy);
            do {
                auto expr = this->parser.parseExpression();
                if(!expr) return nullptr;
                std::string collation;
                if(auto* collateExpr = dynamic_cast<CollateNode*>(expr.get())) {
                    collation = collateExpr->collationName;
                    expr = std::move(collateExpr->operand);
                }
                SortDirection direction = SortDirection::none;
                if(match(TokenType::kwAsc)) {
                    direction = SortDirection::asc;
                } else if(match(TokenType::kwDesc)) {
                    direction = SortDirection::desc;
                }
                NullsOrdering nullsOrdering = NullsOrdering::none;
                if(match(TokenType::kwNulls)) {
                    if(match(TokenType::kwFirst)) {
                        nullsOrdering = NullsOrdering::first;
                    } else if(match(TokenType::kwLast)) {
                        nullsOrdering = NullsOrdering::last;
                    }
                }
                node->orderBy.push_back(
                    OrderByTerm{std::move(expr), direction, std::move(collation), nullsOrdering});
            } while(match(TokenType::comma));
        }

        if(match(TokenType::kwLimit)) {
            if(!atEnd() && current().type == TokenType::integerLiteral) {
                node->limitValue = std::stoi(std::string(current().value));
                advanceToken();
            }
            if(match(TokenType::kwOffset)) {
                if(!atEnd() && current().type == TokenType::integerLiteral) {
                    node->offsetValue = std::stoi(std::string(current().value));
                    advanceToken();
                }
            }
        }

        return node;
    }

    AstNodePointer SelectParser::parseCompoundSelectCore() {
        if(check(TokenType::kwSelect)) {
            return parseSelectCore();
        }
        if(check(TokenType::kwValues)) {
            return this->parser.parseValuesStatement();
        }
        return nullptr;
    }

    AstNodePointer SelectParser::parseSelectCompoundBody() {
        AstNodePointer firstCore = parseCompoundSelectCore();
        if(!firstCore) {
            return nullptr;
        }
        std::vector<CompoundSelectOperator> compoundOperators;
        std::vector<AstNodePointer> selectCores;
        selectCores.push_back(std::move(firstCore));
        SourceLocation compoundLocation = selectCores.front()->location;

        while(true) {
            std::optional<CompoundSelectOperator> compoundOperator;
            if(match(TokenType::kwUnion)) {
                if(match(TokenType::kwAll)) {
                    compoundOperator = CompoundSelectOperator::unionAll;
                } else {
                    compoundOperator = CompoundSelectOperator::unionDistinct;
                }
            } else if(match(TokenType::kwIntersect)) {
                compoundOperator = CompoundSelectOperator::intersect;
            } else if(match(TokenType::kwExcept)) {
                compoundOperator = CompoundSelectOperator::except;
            }
            if(!compoundOperator) {
                break;
            }
            compoundOperators.push_back(*compoundOperator);
            AstNodePointer nextCore = parseCompoundSelectCore();
            if(!nextCore) {
                return nullptr;
            }
            selectCores.push_back(std::move(nextCore));
        }

        if(compoundOperators.empty()) {
            return std::move(selectCores.at(0));
        }
        return std::make_unique<CompoundSelectNode>(std::move(selectCores), std::move(compoundOperators),
                                                    compoundLocation);
    }

    AstNodePointer SelectParser::parseSelect() {
        SourceLocation withLocation = current().location;
        std::optional<WithClause> withClause;
        if(check(TokenType::kwWith)) {
            withLocation = current().location;
            advanceToken();
            WithClause wc;
            if(match(TokenType::kwRecursive)) {
                wc.recursive = true;
            }
            do {
                CommonTableExpression cte;
                if(!isColumnNameToken()) {
                    return nullptr;
                }
                cte.cteName = std::string(current().value);
                advanceToken();
                if(match(TokenType::leftParen)) {
                    if(!check(TokenType::rightParen)) {
                        do {
                            if(!isColumnNameToken()) {
                                return nullptr;
                            }
                            cte.columnNames.push_back(std::string(current().value));
                            advanceToken();
                        } while(match(TokenType::comma));
                    }
                    if(!match(TokenType::rightParen)) {
                        return nullptr;
                    }
                }
                if(!match(TokenType::kwAs)) {
                    return nullptr;
                }
                if(match(TokenType::kwMaterialized)) {
                    cte.materialization = CteMaterialization::materialized;
                } else if(match(TokenType::kwNot)) {
                    if(!match(TokenType::kwMaterialized)) {
                        return nullptr;
                    }
                    cte.materialization = CteMaterialization::notMaterialized;
                }
                if(!match(TokenType::leftParen)) {
                    return nullptr;
                }
                AstNodePointer subq = parseSelect();
                if(!subq) {
                    return nullptr;
                }
                if(!match(TokenType::rightParen)) {
                    return nullptr;
                }
                cte.query = std::move(subq);
                wc.tables.push_back(std::move(cte));
            } while(match(TokenType::comma));
            withClause = std::move(wc);
        }

        AstNodePointer body;
        if(withClause) {
            if(check(TokenType::kwSelect)) {
                body = parseSelectCompoundBody();
            } else if(check(TokenType::kwInsert)) {
                body = this->parser.parseInsertStatement(false);
            } else if(check(TokenType::kwReplace) && peekToken(1).type == TokenType::kwInto) {
                body = this->parser.parseInsertStatement(true);
            } else if(check(TokenType::kwUpdate)) {
                body = this->parser.parseUpdateStatement();
            } else if(check(TokenType::kwDelete)) {
                body = this->parser.parseDeleteStatement();
            } else {
                return nullptr;
            }
        } else {
            body = parseSelectCompoundBody();
        }
        if(!body) {
            return nullptr;
        }
        if(!withClause) {
            return body;
        }
        return std::make_unique<WithQueryNode>(std::move(*withClause), std::move(body), withLocation);
    }

    SelectColumn SelectParser::parseSelectResultColumn() {
        if(check(TokenType::star)) {
            advanceToken();
            return SelectColumn{nullptr, ""};
        }
        if(isColumnNameTokenAt(0) && peekToken(1).type == TokenType::dot && isColumnNameTokenAt(2) &&
           peekToken(3).type == TokenType::dot && peekToken(4).type == TokenType::star) {
            auto location = current().location;
            std::string schemaName = std::string(current().value);
            advanceToken();
            advanceToken();
            std::string tableName = std::string(current().value);
            advanceToken();
            advanceToken();
            advanceToken();
            return SelectColumn{
                std::make_shared<QualifiedAsteriskNode>(std::move(schemaName), std::move(tableName), location), ""};
        }
        if(isColumnNameToken() && !atEnd() && peekToken(1).type == TokenType::dot &&
           peekToken(2).type == TokenType::star) {
            std::string tableName = std::string(current().value);
            auto location = current().location;
            advanceToken();
            advanceToken();
            advanceToken();
            return SelectColumn{std::make_shared<QualifiedAsteriskNode>(std::move(tableName), location), ""};
        }
        auto expr = this->parser.parseExpression();
        std::string alias;
        if(match(TokenType::kwAs)) {
            if(!atEnd()) {
                alias = std::string(current().value);
                advanceToken();
            }
        } else if(!atEnd() && check(TokenType::identifier)) {
            alias = std::string(current().value);
            advanceToken();
        }
        return SelectColumn{std::shared_ptr<AstNode>(std::move(expr)), std::move(alias)};
    }

    FromTableClause SelectParser::parseFromTableItem() {
        if(check(TokenType::leftParen) && peekToken(1).type == TokenType::kwSelect) {
            advanceToken();
            AstNodePointer subquery = parseSelect();
            if(!subquery) {
                return FromTableClause{};
            }
            if(!match(TokenType::rightParen)) {
                return FromTableClause{};
            }
            FromTableClause item;
            item.derivedSelect = std::shared_ptr<AstNode>(std::move(subquery));
            if(match(TokenType::kwAs)) {
                if(isColumnNameToken()) {
                    item.alias = std::string(current().value);
                    advanceToken();
                }
            } else if(isColumnNameToken()) {
                item.alias = std::string(current().value);
                advanceToken();
            }
            return item;
        }

        FromTableClause item;
        if(!isColumnNameToken()) return item;
        std::string first = std::string(current().value);
        advanceToken();
        if(match(TokenType::dot) && isColumnNameToken()) {
            item.schemaName = std::move(first);
            item.tableName = std::string(current().value);
            advanceToken();
        } else {
            item.tableName = std::move(first);
        }
        if(check(TokenType::leftParen)) {
            advanceToken();
            if(!check(TokenType::rightParen)) {
                auto arg = this->parser.parseExpression();
                if(arg) item.tableFunctionArgs.push_back(std::shared_ptr<AstNode>(std::move(arg)));
                while(match(TokenType::comma)) {
                    arg = this->parser.parseExpression();
                    if(arg) item.tableFunctionArgs.push_back(std::shared_ptr<AstNode>(std::move(arg)));
                }
            }
            match(TokenType::rightParen);
        }
        if(match(TokenType::kwAs)) {
            if(isColumnNameToken()) {
                item.alias = std::string(current().value);
                advanceToken();
            }
        } else if(check(TokenType::identifier)) {
            item.alias = std::string(current().value);
            advanceToken();
        }
        return item;
    }

    std::vector<FromClauseItem> SelectParser::parseFromClause() {
        std::vector<FromClauseItem> items;
        if(!isFromTableItemStartOrParen()) return items;

        auto parseOneUnit = [&](JoinKind leadingJoin, bool expectConstraint) {
            if(check(TokenType::leftParen) && !isFromTableItemStart()) {
                advanceToken();
                auto innerItems = parseFromClause();
                match(TokenType::rightParen);
                if(!innerItems.empty()) {
                    innerItems[0].leadingJoin = leadingJoin;
                    if(expectConstraint) {
                        parseJoinConstraint(innerItems[0]);
                    }
                }
                items.insert(items.end(),
                    std::make_move_iterator(innerItems.begin()),
                    std::make_move_iterator(innerItems.end()));
            } else {
                FromClauseItem item;
                item.leadingJoin = leadingJoin;
                item.table = parseFromTableItem();
                if(expectConstraint) {
                    parseJoinConstraint(item);
                }
                items.push_back(std::move(item));
            }
        };

        parseOneUnit(JoinKind::none, false);

        while(true) {
            if(match(TokenType::comma)) {
                if(!isFromTableItemStartOrParen()) break;
                parseOneUnit(JoinKind::crossJoin, false);
                continue;
            }
            JoinKind joinKind = JoinKind::none;
            if(!consumeJoinOperator(joinKind)) break;
            if(!isFromTableItemStartOrParen()) break;
            parseOneUnit(joinKind, true);
        }
        return items;
    }

    bool SelectParser::consumeJoinOperator(JoinKind& out) {
        if(check(TokenType::kwCross) && peekToken(1).type == TokenType::kwJoin) {
            advanceToken();
            advanceToken();
            out = JoinKind::crossJoin;
            return true;
        }
        if(check(TokenType::kwNatural)) {
            if(peekToken(1).type == TokenType::kwLeft) {
                if(peekToken(2).type == TokenType::kwOuter && peekToken(3).type == TokenType::kwJoin) {
                    advanceToken();
                    advanceToken();
                    advanceToken();
                    advanceToken();
                    out = JoinKind::naturalLeftJoin;
                    return true;
                }
                if(peekToken(2).type == TokenType::kwJoin) {
                    advanceToken();
                    advanceToken();
                    advanceToken();
                    out = JoinKind::naturalLeftJoin;
                    return true;
                }
                return false;
            }
            if(peekToken(1).type == TokenType::kwInner && peekToken(2).type == TokenType::kwJoin) {
                advanceToken();
                advanceToken();
                advanceToken();
                out = JoinKind::naturalInnerJoin;
                return true;
            }
            if(peekToken(1).type == TokenType::kwJoin) {
                advanceToken();
                advanceToken();
                out = JoinKind::naturalInnerJoin;
                return true;
            }
            return false;
        }
        if(check(TokenType::kwInner) && peekToken(1).type == TokenType::kwJoin) {
            advanceToken();
            advanceToken();
            out = JoinKind::innerJoin;
            return true;
        }
        if(check(TokenType::kwLeft)) {
            if(peekToken(1).type == TokenType::kwOuter && peekToken(2).type == TokenType::kwJoin) {
                advanceToken();
                advanceToken();
                advanceToken();
                out = JoinKind::leftOuterJoin;
                return true;
            }
            if(peekToken(1).type == TokenType::kwJoin) {
                advanceToken();
                advanceToken();
                out = JoinKind::leftJoin;
                return true;
            }
            return false;
        }
        if(check(TokenType::kwJoin)) {
            advanceToken();
            out = JoinKind::joinPlain;
            return true;
        }
        return false;
    }

    void SelectParser::parseJoinConstraint(FromClauseItem& item) {
        switch(item.leadingJoin) {
        case JoinKind::crossJoin:
        case JoinKind::naturalInnerJoin:
        case JoinKind::naturalLeftJoin:
            return;
        default:
            break;
        }
        if(match(TokenType::kwUsing)) {
            if(match(TokenType::leftParen)) {
                while(isColumnNameToken()) {
                    item.usingColumnNames.push_back(std::string(current().value));
                    advanceToken();
                    if(!match(TokenType::comma)) {
                        break;
                    }
                }
                match(TokenType::rightParen);
            }
        } else if(match(TokenType::kwOn)) {
            item.onExpression = this->parser.parseExpression();
        }
    }

}  // namespace sqlite2orm
