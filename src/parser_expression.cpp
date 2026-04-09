#include "parser_expression.h"
#include <sqlite2orm/parser.h>

#include <cctype>
#include <optional>
#include <string_view>

namespace sqlite2orm {

    namespace {
        bool equalsIgnoreCase(std::string_view a, std::string_view b) {
            if(a.size() != b.size()) return false;
            for(size_t i = 0; i < a.size(); ++i) {
                if(std::tolower(static_cast<unsigned char>(a[i])) !=
                   std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        }

        struct BinaryOperatorInfo {
            BinaryOperator binaryOperator;
            int precedence;
        };

        std::optional<BinaryOperatorInfo> getBinaryOperatorInfo(TokenType tokenType) {
            switch(tokenType) {
                case TokenType::kwOr:      return BinaryOperatorInfo{BinaryOperator::logicalOr, 0};
                case TokenType::kwAnd:     return BinaryOperatorInfo{BinaryOperator::logicalAnd, 1};
                case TokenType::eq:         return BinaryOperatorInfo{BinaryOperator::equals, 2};
                case TokenType::eq2:        return BinaryOperatorInfo{BinaryOperator::equals, 2};
                case TokenType::ne:         return BinaryOperatorInfo{BinaryOperator::notEquals, 2};
                case TokenType::ltGt:       return BinaryOperatorInfo{BinaryOperator::notEquals, 2};
                case TokenType::lt:         return BinaryOperatorInfo{BinaryOperator::lessThan, 3};
                case TokenType::le:         return BinaryOperatorInfo{BinaryOperator::lessOrEqual, 3};
                case TokenType::gt:         return BinaryOperatorInfo{BinaryOperator::greaterThan, 3};
                case TokenType::ge:         return BinaryOperatorInfo{BinaryOperator::greaterOrEqual, 3};
                case TokenType::shiftLeft:  return BinaryOperatorInfo{BinaryOperator::shiftLeft, 4};
                case TokenType::shiftRight: return BinaryOperatorInfo{BinaryOperator::shiftRight, 4};
                case TokenType::ampersand:  return BinaryOperatorInfo{BinaryOperator::bitwiseAnd, 4};
                case TokenType::pipe:       return BinaryOperatorInfo{BinaryOperator::bitwiseOr, 4};
                case TokenType::plus:       return BinaryOperatorInfo{BinaryOperator::add, 5};
                case TokenType::minus:      return BinaryOperatorInfo{BinaryOperator::subtract, 5};
                case TokenType::star:       return BinaryOperatorInfo{BinaryOperator::multiply, 6};
                case TokenType::slash:      return BinaryOperatorInfo{BinaryOperator::divide, 6};
                case TokenType::percent:    return BinaryOperatorInfo{BinaryOperator::modulo, 6};
                case TokenType::pipe2:      return BinaryOperatorInfo{BinaryOperator::concatenate, 7};
                case TokenType::arrow:      return BinaryOperatorInfo{BinaryOperator::jsonArrow, 8};
                case TokenType::arrow2:     return BinaryOperatorInfo{BinaryOperator::jsonArrow2, 8};
                default:                    return std::nullopt;
            }
        }
    }

    ExpressionParser::ExpressionParser(Parser& parser, TokenStream& tokenStream)
        : parser(parser), tokenStream(tokenStream) {}

    AstNodePointer ExpressionParser::parseExpression() {
        return parseBinaryExpression(0);
    }

    AstNodePointer ExpressionParser::parseBinaryExpression(int minPrecedence) {
        auto left = parsePrimary();
        if(!left) return nullptr;

        while(true) {
            auto operatorInfo = getBinaryOperatorInfo(current().type);
            if(operatorInfo && operatorInfo->precedence >= minPrecedence) {
                auto location = current().location;
                advanceToken();

                auto right = parseBinaryExpression(operatorInfo->precedence + 1);
                if(!right) return nullptr;

                left = std::make_unique<BinaryOperatorNode>(
                    operatorInfo->binaryOperator, std::move(left), std::move(right), location);
                continue;
            }

            if(check(TokenType::kwCollate)) {
                auto collateLoc = current().location;
                advanceToken();
                if(!isColumnNameToken()) return nullptr;
                std::string collation(current().value);
                advanceToken();
                left = std::make_unique<CollateNode>(std::move(left), std::move(collation), collateLoc);
                continue;
            }

            if(minPrecedence <= 2) {
                auto special = tryParseSpecialPostfix(left);
                if(special) {
                    left = std::move(special);
                    continue;
                }
            }

            break;
        }

        return left;
    }

    AstNodePointer ExpressionParser::tryParseSpecialPostfix(AstNodePointer& left) {
        auto location = current().location;

        if(check(TokenType::kwIsnull)) {
            advanceToken();
            return std::make_unique<IsNullNode>(std::move(left), location);
        }

        if(check(TokenType::kwNotnull)) {
            advanceToken();
            return std::make_unique<IsNotNullNode>(std::move(left), location);
        }

        if(check(TokenType::kwIs)) {
            advanceToken();
            if(check(TokenType::kwNot)) {
                advanceToken();
                if(check(TokenType::kwNull)) {
                    advanceToken();
                    return std::make_unique<IsNotNullNode>(std::move(left), location);
                }
                if(check(TokenType::kwDistinct) && peekToken(1).type == TokenType::kwFrom) {
                    advanceToken();
                    advanceToken();
                    auto right = parseBinaryExpression(3);
                    if(!right) return nullptr;
                    return std::make_unique<BinaryOperatorNode>(
                        BinaryOperator::isNotDistinctFrom, std::move(left), std::move(right), location);
                }
                auto right = parseBinaryExpression(3);
                if(!right) return nullptr;
                return std::make_unique<BinaryOperatorNode>(
                    BinaryOperator::isNot, std::move(left), std::move(right), location);
            }
            if(check(TokenType::kwNull)) {
                advanceToken();
                return std::make_unique<IsNullNode>(std::move(left), location);
            }
            if(check(TokenType::kwDistinct) && peekToken(1).type == TokenType::kwFrom) {
                advanceToken();
                advanceToken();
                auto right = parseBinaryExpression(3);
                if(!right) return nullptr;
                return std::make_unique<BinaryOperatorNode>(
                    BinaryOperator::isDistinctFrom, std::move(left), std::move(right), location);
            }
            auto right = parseBinaryExpression(3);
            if(!right) return nullptr;
            return std::make_unique<BinaryOperatorNode>(
                BinaryOperator::isOp, std::move(left), std::move(right), location);
        }

        bool negated = false;
        if(check(TokenType::kwNot)) {
            auto nextType = peekToken(1).type;
            if(nextType == TokenType::kwBetween || nextType == TokenType::kwIn ||
               nextType == TokenType::kwLike || nextType == TokenType::kwGlob) {
                advanceToken();
                negated = true;
            } else if(nextType == TokenType::kwNull) {
                advanceToken();
                advanceToken();
                return std::make_unique<IsNotNullNode>(std::move(left), location);
            } else {
                return nullptr;
            }
        }

        if(check(TokenType::kwBetween)) {
            advanceToken();
            auto low = parseBinaryExpression(3);
            if(!low) return nullptr;
            if(!match(TokenType::kwAnd)) return nullptr;
            auto high = parseBinaryExpression(3);
            if(!high) return nullptr;
            return std::make_unique<BetweenNode>(
                std::move(left), std::move(low), std::move(high), negated, location);
        }

        if(check(TokenType::kwIn)) {
            advanceToken();
            if(check(TokenType::leftParen)) {
                advanceToken();
                if(check(TokenType::kwSelect)) {
                    auto sub = this->parser.parseSelect();
                    if(!sub) return nullptr;
                    if(!match(TokenType::rightParen)) return nullptr;
                    return std::make_unique<InNode>(std::move(left), std::vector<AstNodePointer>{},
                                                    std::move(sub), negated, location);
                }
                std::vector<AstNodePointer> values;
                if(!check(TokenType::rightParen)) {
                    auto val = parseExpression();
                    if(!val) return nullptr;
                    values.push_back(std::move(val));
                    while(match(TokenType::comma)) {
                        val = parseExpression();
                        if(!val) return nullptr;
                        values.push_back(std::move(val));
                    }
                }
                if(!match(TokenType::rightParen)) return nullptr;
                return std::make_unique<InNode>(
                    std::move(left), std::move(values), nullptr, negated, location);
            }
            if(isColumnNameToken()) {
                std::string tblName(current().value);
                advanceToken();
                return std::make_unique<InNode>(std::move(left), std::move(tblName), negated, location);
            }
            return nullptr;
        }

        if(check(TokenType::kwLike)) {
            advanceToken();
            auto pattern = parseBinaryExpression(3);
            if(!pattern) return nullptr;
            AstNodePointer escape;
            if(match(TokenType::kwEscape)) {
                escape = parseBinaryExpression(3);
                if(!escape) return nullptr;
            }
            return std::make_unique<LikeNode>(
                std::move(left), std::move(pattern), std::move(escape), negated, location);
        }

        if(check(TokenType::kwGlob)) {
            advanceToken();
            auto pattern = parseBinaryExpression(3);
            if(!pattern) return nullptr;
            return std::make_unique<GlobNode>(
                std::move(left), std::move(pattern), negated, location);
        }

        return nullptr;
    }

    AstNodePointer ExpressionParser::parsePrimary() {
        if(current().type == TokenType::kwNot) {
            auto location = current().location;
            advanceToken();
            auto operand = parsePrimary();
            if(!operand) return nullptr;
            return std::make_unique<UnaryOperatorNode>(UnaryOperator::logicalNot, std::move(operand), location);
        }
        if(current().type == TokenType::minus) {
            auto location = current().location;
            advanceToken();
            auto operand = parsePrimary();
            if(!operand) return nullptr;
            return std::make_unique<UnaryOperatorNode>(UnaryOperator::minus, std::move(operand), location);
        }
        if(current().type == TokenType::plus) {
            auto location = current().location;
            advanceToken();
            auto operand = parsePrimary();
            if(!operand) return nullptr;
            return std::make_unique<UnaryOperatorNode>(UnaryOperator::plus, std::move(operand), location);
        }
        if(current().type == TokenType::tilde) {
            auto location = current().location;
            advanceToken();
            auto operand = parsePrimary();
            if(!operand) return nullptr;
            return std::make_unique<UnaryOperatorNode>(UnaryOperator::bitwiseNot, std::move(operand), location);
        }

        if(check(TokenType::kwCast)) {
            return parseCast();
        }
        if(check(TokenType::kwCase)) {
            return parseCase();
        }

        if(check(TokenType::kwRaise)) {
            auto location = current().location;
            advanceToken();
            if(!match(TokenType::leftParen)) return nullptr;
            if(match(TokenType::kwIgnore)) {
                if(!match(TokenType::rightParen)) return nullptr;
                return std::make_unique<RaiseNode>(RaiseKind::ignore, nullptr, location);
            }
            RaiseKind raiseKind;
            if(match(TokenType::kwRollback)) {
                raiseKind = RaiseKind::rollback;
            } else if(match(TokenType::kwAbort)) {
                raiseKind = RaiseKind::abort;
            } else if(match(TokenType::kwFail)) {
                raiseKind = RaiseKind::fail;
            } else {
                return nullptr;
            }
            if(!match(TokenType::comma)) return nullptr;
            AstNodePointer message = parseExpression();
            if(!message) return nullptr;
            if(!match(TokenType::rightParen)) return nullptr;
            return std::make_unique<RaiseNode>(raiseKind, std::move(message), location);
        }

        if(check(TokenType::kwExists)) {
            auto location = current().location;
            advanceToken();
            if(!match(TokenType::leftParen)) return nullptr;
            if(!check(TokenType::kwSelect)) return nullptr;
            auto sub = this->parser.parseSelect();
            if(!sub) return nullptr;
            if(!match(TokenType::rightParen)) return nullptr;
            return std::make_unique<ExistsNode>(std::move(sub), location);
        }

        if(check(TokenType::leftParen)) {
            const Token& openingParenthesisToken = current();
            advanceToken();
            if(check(TokenType::kwSelect)) {
                auto sub = this->parser.parseSelect();
                if(!sub) return nullptr;
                if(!match(TokenType::rightParen)) return nullptr;
                return std::make_unique<SubqueryNode>(std::move(sub), openingParenthesisToken.location);
            }
            auto expr = parseExpression();
            if(!expr) return nullptr;
            if(!match(TokenType::rightParen)) return nullptr;
            return expr;
        }

        if(isFunctionNameStart() && peekToken(1).type == TokenType::leftParen) {
            return parseFunctionCall();
        }

        if(check(TokenType::kwExcluded)) {
            auto location = current().location;
            advanceToken();
            if(!match(TokenType::dot)) return nullptr;
            if(!isColumnNameToken()) return nullptr;
            std::string_view col = current().value;
            advanceToken();
            return std::make_unique<ExcludedRefNode>(col, location);
        }

        if(current().type == TokenType::identifier) {
            return parseColumnRef();
        }

        if(check(TokenType::bindParameter)) {
            const Token& token = current();
            advanceToken();
            return std::make_unique<BindParameterNode>(token.value, token.location);
        }

        return parseLiteral();
    }

    AstNodePointer ExpressionParser::parseColumnRef() {
        const Token& firstToken = current();

        if(peekToken(1).type == TokenType::dot && isColumnNameTokenAt(2) && peekToken(3).type == TokenType::dot &&
           isColumnNameTokenAt(4)) {
            if(!equalsIgnoreCase(firstToken.value, "new") && !equalsIgnoreCase(firstToken.value, "old")) {
                advanceToken();
                if(!match(TokenType::dot)) return nullptr;
                const Token& tableToken = current();
                if(!isColumnNameToken()) return nullptr;
                advanceToken();
                if(!match(TokenType::dot)) return nullptr;
                const Token& columnToken = current();
                if(!isColumnNameToken()) return nullptr;
                advanceToken();
                return std::make_unique<QualifiedColumnRefNode>(firstToken.value, tableToken.value, columnToken.value,
                                                                  firstToken.location);
            }
        }

        if(peekToken(1).type == TokenType::dot && peekToken(2).type == TokenType::identifier) {
            advanceToken();
            advanceToken();
            const Token& columnToken = advanceToken();

            if(equalsIgnoreCase(firstToken.value, "new")) {
                return std::make_unique<NewRefNode>(columnToken.value, firstToken.location);
            }
            if(equalsIgnoreCase(firstToken.value, "old")) {
                return std::make_unique<OldRefNode>(columnToken.value, firstToken.location);
            }
            return std::make_unique<QualifiedColumnRefNode>(firstToken.value, columnToken.value, firstToken.location);
        }

        advanceToken();
        return std::make_unique<ColumnRefNode>(firstToken.value, firstToken.location);
    }

    AstNodePointer ExpressionParser::parseLiteral() {
        const Token& token = current();
        switch(token.type) {
            case TokenType::integerLiteral: {
                advanceToken();
                return std::make_unique<IntegerLiteralNode>(token.value, token.location);
            }
            case TokenType::realLiteral: {
                advanceToken();
                return std::make_unique<RealLiteralNode>(token.value, token.location);
            }
            case TokenType::stringLiteral: {
                advanceToken();
                return std::make_unique<StringLiteralNode>(token.value, token.location);
            }
            case TokenType::blobLiteral: {
                advanceToken();
                return std::make_unique<BlobLiteralNode>(token.value, token.location);
            }
            case TokenType::kwNull: {
                advanceToken();
                return std::make_unique<NullLiteralNode>(token.location);
            }
            case TokenType::kwTrue: {
                advanceToken();
                return std::make_unique<BoolLiteralNode>(true, token.location);
            }
            case TokenType::kwFalse: {
                advanceToken();
                return std::make_unique<BoolLiteralNode>(false, token.location);
            }
            case TokenType::kwOn: {
                advanceToken();
                return std::make_unique<BoolLiteralNode>(true, token.location);
            }
            case TokenType::kwCurrentTime: {
                advanceToken();
                return std::make_unique<CurrentDatetimeLiteralNode>(CurrentDatetimeKind::time, token.location);
            }
            case TokenType::kwCurrentDate: {
                advanceToken();
                return std::make_unique<CurrentDatetimeLiteralNode>(CurrentDatetimeKind::date, token.location);
            }
            case TokenType::kwCurrentTimestamp: {
                advanceToken();
                return std::make_unique<CurrentDatetimeLiteralNode>(CurrentDatetimeKind::timestamp, token.location);
            }
            default:
                return nullptr;
        }
    }

    AstNodePointer ExpressionParser::parseCast() {
        auto location = current().location;
        advanceToken();
        if(!match(TokenType::leftParen)) return nullptr;

        auto operand = parseExpression();
        if(!operand) return nullptr;
        if(!match(TokenType::kwAs)) return nullptr;

        std::string typeName;
        int parenthesisDepth = 0;
        while(true) {
            if(atEnd()) return nullptr;
            if(check(TokenType::rightParen) && parenthesisDepth == 0) break;

            if(!typeName.empty()) {
                char last = typeName.back();
                bool currentIsSpecial = check(TokenType::leftParen) ||
                                         check(TokenType::rightParen) ||
                                         check(TokenType::comma);
                if(last != '(' && !currentIsSpecial) {
                    typeName += " ";
                }
            }

            if(check(TokenType::leftParen)) parenthesisDepth++;
            else if(check(TokenType::rightParen)) parenthesisDepth--;

            typeName += std::string(current().value);
            advanceToken();
        }

        if(!match(TokenType::rightParen)) return nullptr;
        return std::make_unique<CastNode>(std::move(operand), std::move(typeName), location);
    }

    AstNodePointer ExpressionParser::parseCase() {
        auto location = current().location;
        advanceToken();

        AstNodePointer operand;
        if(!check(TokenType::kwWhen)) {
            operand = parseExpression();
            if(!operand) return nullptr;
        }

        std::vector<CaseBranch> branches;
        while(match(TokenType::kwWhen)) {
            auto condition = parseExpression();
            if(!condition) return nullptr;
            if(!match(TokenType::kwThen)) return nullptr;
            auto result = parseExpression();
            if(!result) return nullptr;
            branches.push_back(CaseBranch{std::move(condition), std::move(result)});
        }

        if(branches.empty()) return nullptr;

        AstNodePointer elseResult;
        if(match(TokenType::kwElse)) {
            elseResult = parseExpression();
            if(!elseResult) return nullptr;
        }

        if(!match(TokenType::kwEnd)) return nullptr;
        return std::make_unique<CaseNode>(
            std::move(operand), std::move(branches), std::move(elseResult), location);
    }

    bool ExpressionParser::isFunctionNameStart() const {
        auto type = current().type;
        if(type == TokenType::identifier) return true;
        switch(type) {
            case TokenType::kwReplace:
            case TokenType::kwLike:
            case TokenType::kwGlob:
            case TokenType::kwMatch:
                return true;
            default:
                return false;
        }
    }

    AstNodePointer ExpressionParser::parseFunctionCall() {
        auto location = current().location;
        std::string name(current().value);
        advanceToken();
        advanceToken();

        bool distinct = false;
        bool star = false;
        std::vector<AstNodePointer> arguments;

        if(check(TokenType::star)) {
            star = true;
            advanceToken();
        } else if(!check(TokenType::rightParen)) {
            if(match(TokenType::kwDistinct)) {
                distinct = true;
            }
            auto arg = parseExpression();
            if(!arg) return nullptr;
            arguments.push_back(std::move(arg));
            while(match(TokenType::comma)) {
                arg = parseExpression();
                if(!arg) return nullptr;
                arguments.push_back(std::move(arg));
            }
        }

        if(!match(TokenType::rightParen)) return nullptr;
        auto node = std::make_unique<FunctionCallNode>(
            std::move(name), std::move(arguments), distinct, star, location);
        if(match(TokenType::kwFilter)) {
            if(!match(TokenType::leftParen)) return nullptr;
            if(!match(TokenType::kwWhere)) return nullptr;
            auto filt = parseExpression();
            if(!filt) return nullptr;
            if(!match(TokenType::rightParen)) return nullptr;
            node->filterWhere = std::move(filt);
        }
        if(check(TokenType::kwOver)) {
            node->over = parseOverClauseBody();
            if(!node->over) {
                return nullptr;
            }
        }
        return node;
    }

    void ExpressionParser::parseOverOrderByList(std::vector<OrderByTerm>& out) {
        auto parseOne = [&]() {
            auto expr = parseExpression();
            if(!expr) return;
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
            out.push_back(OrderByTerm{
                std::shared_ptr<AstNode>(expr.release()), direction, {}, nullsOrdering});
        };
        parseOne();
        while(match(TokenType::comma)) {
            parseOne();
        }
    }

    bool ExpressionParser::parseWindowFrameBound(WindowFrameBound& out) {
        if(match(TokenType::kwCurrent)) {
            if(!match(TokenType::kwRow)) return false;
            out.kind = WindowFrameBoundKind::currentRow;
            return true;
        }
        if(match(TokenType::kwUnbounded)) {
            if(match(TokenType::kwPreceding)) {
                out.kind = WindowFrameBoundKind::unboundedPreceding;
                return true;
            }
            if(match(TokenType::kwFollowing)) {
                out.kind = WindowFrameBoundKind::unboundedFollowing;
                return true;
            }
            return false;
        }
        AstNodePointer expr = parseExpression();
        if(!expr) return false;
        if(match(TokenType::kwPreceding)) {
            out.kind = WindowFrameBoundKind::exprPreceding;
            out.expr = std::move(expr);
            return true;
        }
        if(match(TokenType::kwFollowing)) {
            out.kind = WindowFrameBoundKind::exprFollowing;
            out.expr = std::move(expr);
            return true;
        }
        return false;
    }

    std::unique_ptr<WindowFrameSpec> ExpressionParser::parseWindowFrameSpec() {
        auto frame = std::make_unique<WindowFrameSpec>();
        if(match(TokenType::kwRows)) {
            frame->unit = WindowFrameUnit::rows;
        } else if(match(TokenType::kwRange)) {
            frame->unit = WindowFrameUnit::range;
        } else if(match(TokenType::kwGroups)) {
            frame->unit = WindowFrameUnit::groups;
        } else {
            return nullptr;
        }
        if(match(TokenType::kwBetween)) {
            if(!parseWindowFrameBound(frame->start)) return nullptr;
            if(!match(TokenType::kwAnd)) return nullptr;
            if(!parseWindowFrameBound(frame->end)) return nullptr;
        } else {
            if(!parseWindowFrameBound(frame->start)) return nullptr;
            frame->end.kind = WindowFrameBoundKind::currentRow;
        }
        if(match(TokenType::kwExclude)) {
            if(match(TokenType::kwCurrent) && match(TokenType::kwRow)) {
                frame->exclude = WindowFrameExcludeKind::currentRow;
            } else if(match(TokenType::kwGroup)) {
                frame->exclude = WindowFrameExcludeKind::group;
            } else if(match(TokenType::kwTies)) {
                frame->exclude = WindowFrameExcludeKind::ties;
            } else if(match(TokenType::kwNo) && match(TokenType::kwOthers)) {
                // EXCLUDE NO OTHERS is the default
            } else {
                return nullptr;
            }
        }
        return frame;
    }

    bool ExpressionParser::parseOverClauseParenContents(OverClause& over, bool allowSimpleNamedWindow) {
        if(allowSimpleNamedWindow && isColumnNameToken() && peekToken(1).type == TokenType::rightParen) {
            over.namedWindow = std::string(current().value);
            advanceToken();
            return true;
        }
        while(!check(TokenType::rightParen) && !atEnd()) {
            if(match(TokenType::kwPartition)) {
                if(!match(TokenType::kwBy)) return false;
                do {
                    AstNodePointer partExpr = parseExpression();
                    if(!partExpr) return false;
                    over.partitionBy.push_back(std::move(partExpr));
                } while(match(TokenType::comma));
            } else if(match(TokenType::kwOrder)) {
                if(!match(TokenType::kwBy)) return false;
                parseOverOrderByList(over.orderBy);
            } else if(check(TokenType::kwRows) || check(TokenType::kwRange) || check(TokenType::kwGroups)) {
                over.frame = parseWindowFrameSpec();
                if(!over.frame) return false;
            } else {
                return false;
            }
        }
        return true;
    }

    std::unique_ptr<OverClause> ExpressionParser::parseOverClauseBody() {
        if(!match(TokenType::kwOver)) {
            return nullptr;
        }
        auto over = std::make_unique<OverClause>();
        if(!check(TokenType::leftParen)) {
            if(!isColumnNameToken()) {
                return nullptr;
            }
            over->namedWindow = std::string(current().value);
            advanceToken();
            return over;
        }
        if(!match(TokenType::leftParen)) {
            return nullptr;
        }
        if(!parseOverClauseParenContents(*over, true)) {
            return nullptr;
        }
        if(!match(TokenType::rightParen)) {
            return nullptr;
        }
        return over;
    }

}  // namespace sqlite2orm
