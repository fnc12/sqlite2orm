#include <sqlite2orm/parser.h>
#include <sqlite2orm/utils.h>

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

        // SQLite precedence (higher number = tighter binding):
        // 0: OR
        // 1: AND
        // 2: =, ==, !=, <>
        // 3: <, <=, >, >=
        // 4: <<, >>, &, |
        // 5: +, - (binary)
        // 6: *, /, %
        // 7: ||
        // Unary (parsePrimary): NOT, -, +, ~
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

    const Token& Parser::current() const {
        return this->tokens.at(this->position);
    }

    const Token& Parser::peekToken(size_t offset) const {
        size_t index = this->position + offset;
        if(index >= this->tokens.size()) {
            return this->tokens.back();  // Eof
        }
        return this->tokens.at(index);
    }

    const Token& Parser::advanceToken() {
        const Token& token = this->tokens.at(this->position);
        if(token.type != TokenType::eof) {
            ++this->position;
        }
        return token;
    }

    bool Parser::atEnd() const {
        return current().type == TokenType::eof;
    }

    bool Parser::check(TokenType type) const {
        return current().type == type;
    }

    std::optional<Token> Parser::match(TokenType type) {
        if(check(type)) {
            return advanceToken();
        }
        return std::nullopt;
    }

    AstNodePointer Parser::parseExpression() {
        return parseBinaryExpression(0);
    }

    AstNodePointer Parser::parseBinaryExpression(int minPrecedence) {
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

    AstNodePointer Parser::tryParseSpecialPostfix(AstNodePointer& left) {
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
                    advanceToken();  // DISTINCT
                    advanceToken();  // FROM
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
                advanceToken();  // DISTINCT
                advanceToken();  // FROM
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
                    auto sub = parseSelect();
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

    AstNodePointer Parser::parsePrimary() {
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
            auto sub = parseSelect();
            if(!sub) return nullptr;
            if(!match(TokenType::rightParen)) return nullptr;
            return std::make_unique<ExistsNode>(std::move(sub), location);
        }

        if(check(TokenType::leftParen)) {
            const Token& openingParenthesisToken = current();
            advanceToken();
            if(check(TokenType::kwSelect)) {
                auto sub = parseSelect();
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

    AstNodePointer Parser::parseColumnRef() {
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

    AstNodePointer Parser::parseLiteral() {
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
            // SQLite PRAGMA values use ON/OFF like booleans; ON is tokenized as kwOn (not an identifier).
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

    AstNodePointer Parser::parseCast() {
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

    AstNodePointer Parser::parseCase() {
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

    bool Parser::isFunctionNameStart() const {
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

    AstNodePointer Parser::parseFunctionCall() {
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

    void Parser::parseOverOrderByList(std::vector<OrderByTerm>& out) {
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

    bool Parser::parseWindowFrameBound(WindowFrameBound& out) {
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

    std::unique_ptr<WindowFrameSpec> Parser::parseWindowFrameSpec() {
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
                // EXCLUDE NO OTHERS is the default — nothing to set
            } else {
                return nullptr;
            }
        }
        return frame;
    }

    bool Parser::parseOverClauseParenContents(OverClause& over, bool allowSimpleNamedWindow) {
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

    std::unique_ptr<OverClause> Parser::parseOverClauseBody() {
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

    bool Parser::isColumnNameToken() const {
        auto type = current().type;
        if(type == TokenType::identifier) return true;
        return type >= TokenType::kwAbort && type <= TokenType::kwWithout;
    }

    bool Parser::isColumnNameTokenAt(size_t offsetFromCurrent) const {
        size_t index = this->position + offsetFromCurrent;
        if(index >= this->tokens.size()) return false;
        auto type = this->tokens.at(index).type;
        if(type == TokenType::identifier) return true;
        return type >= TokenType::kwAbort && type <= TokenType::kwWithout;
    }

    bool Parser::isFromTableItemStart() const {
        return isColumnNameToken() ||
               (check(TokenType::leftParen) && peekToken(1).type == TokenType::kwSelect);
    }

    bool Parser::isFromTableItemStartOrParen() const {
        if(isFromTableItemStart()) return true;
        if(check(TokenType::leftParen) && isColumnNameTokenAt(1)) return true;
        return false;
    }

    std::string Parser::parseColumnTypeName() {
        std::string typeName;
        int parenthesisDepth = 0;
        while(!atEnd()) {
            if(parenthesisDepth == 0) {
                if(check(TokenType::comma) || check(TokenType::rightParen)) break;
                if(check(TokenType::kwPrimary) || check(TokenType::kwNot) ||
                   check(TokenType::kwUnique) || check(TokenType::kwCheck) ||
                   check(TokenType::kwDefault) || check(TokenType::kwCollate) ||
                   check(TokenType::kwReferences) || check(TokenType::kwGenerated) ||
                   check(TokenType::kwConstraint) || check(TokenType::kwAutoincrement) ||
                   check(TokenType::kwAs)) break;
            }

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
        return typeName;
    }

    ForeignKeyAction Parser::parseForeignKeyAction() {
        if(match(TokenType::kwCascade)) return ForeignKeyAction::cascade;
        if(match(TokenType::kwRestrict)) return ForeignKeyAction::restrict_;
        if(check(TokenType::kwSet) && peekToken(1).type == TokenType::kwNull) {
            advanceToken();
            advanceToken();
            return ForeignKeyAction::setNull;
        }
        if(check(TokenType::kwSet) && peekToken(1).type == TokenType::kwDefault) {
            advanceToken();
            advanceToken();
            return ForeignKeyAction::setDefault;
        }
        if(check(TokenType::kwNo) && peekToken(1).type == TokenType::kwAction) {
            advanceToken();
            advanceToken();
            return ForeignKeyAction::noAction;
        }
        return ForeignKeyAction::none;
    }

    ForeignKeyClause Parser::parseForeignKeyClause() {
        ForeignKeyClause fk;
        if(atEnd()) return fk;
        fk.table = std::string(current().value);
        advanceToken();
        if(match(TokenType::leftParen)) {
            if(!atEnd()) {
                fk.column = std::string(current().value);
                advanceToken();
            }
            match(TokenType::rightParen);
        }
        while(check(TokenType::kwOn) && !atEnd()) {
            advanceToken();
            if(match(TokenType::kwDelete)) {
                fk.onDelete = parseForeignKeyAction();
            } else if(match(TokenType::kwUpdate)) {
                fk.onUpdate = parseForeignKeyAction();
            } else {
                break;
            }
        }
        if(check(TokenType::kwNot) && peekToken(1).type == TokenType::kwDeferrable) {
            advanceToken();
            advanceToken();
            fk.deferrability = Deferrability::notDeferrable;
        } else if(match(TokenType::kwDeferrable)) {
            fk.deferrability = Deferrability::deferrable;
        }
        if(match(TokenType::kwInitially)) {
            if(match(TokenType::kwDeferred)) {
                fk.initially = InitialConstraintMode::deferred;
            } else if(match(TokenType::kwImmediate)) {
                fk.initially = InitialConstraintMode::immediate;
            }
        }
        return fk;
    }

    ConflictClause Parser::parseConflictClause() {
        if(!check(TokenType::kwOn)) return ConflictClause::none;
        advanceToken();
        if(!match(TokenType::kwConflict)) return ConflictClause::none;
        if(match(TokenType::kwRollback)) return ConflictClause::rollback;
        if(match(TokenType::kwAbort)) return ConflictClause::abort;
        if(match(TokenType::kwFail)) return ConflictClause::fail;
        if(match(TokenType::kwIgnore)) return ConflictClause::ignore;
        if(match(TokenType::kwReplace)) return ConflictClause::replace;
        return ConflictClause::none;
    }

    AstNodePointer Parser::parseDefaultValue() {
        if(match(TokenType::leftParen)) {
            auto expr = parseExpression();
            match(TokenType::rightParen);
            return expr;
        }
        if(check(TokenType::plus) || check(TokenType::minus)) {
            auto op = check(TokenType::minus)
                ? UnaryOperator::minus : UnaryOperator::plus;
            auto location = current().location;
            advanceToken();
            auto operand = parsePrimary();
            return std::make_unique<UnaryOperatorNode>(op, std::move(operand), location);
        }
        return parsePrimary();
    }

    void Parser::parseColumnConstraints(ColumnDef& columnDef) {
        while(!check(TokenType::comma) && !check(TokenType::rightParen) && !atEnd()) {
            if(match(TokenType::kwConstraint)) {
                if(isColumnNameToken()) advanceToken();
                continue;
            }
            if(check(TokenType::kwPrimary)) {
                advanceToken();
                match(TokenType::kwKey);
                columnDef.primaryKey = true;
                if(check(TokenType::kwAsc) || check(TokenType::kwDesc)) advanceToken();
                columnDef.primaryKeyConflict = parseConflictClause();
                if(match(TokenType::kwAutoincrement)) {
                    columnDef.autoincrement = true;
                }
            } else if(check(TokenType::kwNot) && peekToken(1).type == TokenType::kwNull) {
                advanceToken();
                advanceToken();
                columnDef.notNull = true;
                if(check(TokenType::kwOn)) {
                    advanceToken();
                    if(match(TokenType::kwConflict)) advanceToken();
                }
            } else if(check(TokenType::kwDefault)) {
                advanceToken();
                columnDef.defaultValue = parseDefaultValue();
            } else if(check(TokenType::kwUnique)) {
                advanceToken();
                columnDef.unique = true;
                columnDef.uniqueConflict = parseConflictClause();
            } else if(check(TokenType::kwCheck)) {
                advanceToken();
                if(match(TokenType::leftParen)) {
                    columnDef.checkExpression = parseExpression();
                    match(TokenType::rightParen);
                }
            } else if(check(TokenType::kwReferences)) {
                advanceToken();
                columnDef.foreignKey = parseForeignKeyClause();
            } else if(check(TokenType::kwCollate)) {
                advanceToken();
                if(!atEnd()) {
                    columnDef.collation = std::string(current().value);
                    advanceToken();
                }
            } else if(check(TokenType::kwGenerated) || (check(TokenType::kwAs) && peekToken(1).type == TokenType::leftParen)) {
                columnDef.generatedAlways = false;
                if(match(TokenType::kwGenerated)) {
                    match(TokenType::kwAlways);
                    columnDef.generatedAlways = true;
                }
                match(TokenType::kwAs);
                if(match(TokenType::leftParen)) {
                    columnDef.generatedExpression = parseExpression();
                    match(TokenType::rightParen);
                }
                if(match(TokenType::kwStored)) {
                    columnDef.generatedStorage = ColumnDef::GeneratedStorage::stored;
                } else if(match(TokenType::kwVirtual)) {
                    columnDef.generatedStorage = ColumnDef::GeneratedStorage::virtual_;
                }
            } else if(check(TokenType::kwAutoincrement)) {
                advanceToken();
                columnDef.autoincrement = true;
            } else if(check(TokenType::leftParen)) {
                int depth = 1;
                advanceToken();
                while(depth > 0 && !atEnd()) {
                    if(check(TokenType::leftParen)) depth++;
                    else if(check(TokenType::rightParen)) depth--;
                    if(depth > 0) advanceToken();
                }
                if(depth == 0) advanceToken();
            } else {
                advanceToken();
            }
        }
    }

    TableForeignKey Parser::parseTableForeignKey() {
        advanceToken();
        match(TokenType::kwKey);
        std::string column;
        if(match(TokenType::leftParen)) {
            if(!atEnd()) {
                column = std::string(current().value);
                advanceToken();
            }
            match(TokenType::rightParen);
        }
        match(TokenType::kwReferences);
        auto references = parseForeignKeyClause();
        return TableForeignKey{std::move(column), std::move(references)};
    }

    ColumnDef Parser::parseColumnDef() {
        std::string name(current().value);
        advanceToken();

        std::string typeName;
        if(!check(TokenType::comma) && !check(TokenType::rightParen) && !atEnd()) {
            typeName = parseColumnTypeName();
        }

        ColumnDef columnDef{std::move(name), std::move(typeName)};
        parseColumnConstraints(columnDef);
        return columnDef;
    }

    AstNodePointer Parser::parseCreate() {
        if(!check(TokenType::kwCreate)) return nullptr;
        auto location = current().location;
        advanceToken();

        if(match(TokenType::kwTemp)) {
            if(check(TokenType::kwTrigger)) {
                advanceToken();
                return parseCreateTriggerAfterKeyword(location, true);
            }
            if(check(TokenType::kwView)) {
                advanceToken();
                return parseCreateViewTail(location);
            }
            if(match(TokenType::kwVirtual)) {
                if(!match(TokenType::kwTable)) return nullptr;
                return parseCreateVirtualTableTail(location, true);
            }
            if(!match(TokenType::kwTable)) return nullptr;
            return parseCreateTableTail(location);
        }
        if(match(TokenType::kwTemporary)) {
            if(check(TokenType::kwTrigger)) {
                advanceToken();
                return parseCreateTriggerAfterKeyword(location, true);
            }
            if(check(TokenType::kwView)) {
                advanceToken();
                return parseCreateViewTail(location);
            }
            if(match(TokenType::kwVirtual)) {
                if(!match(TokenType::kwTable)) return nullptr;
                return parseCreateVirtualTableTail(location, true);
            }
            if(!match(TokenType::kwTable)) return nullptr;
            return parseCreateTableTail(location);
        }

        if(check(TokenType::kwTrigger)) {
            advanceToken();
            return parseCreateTriggerAfterKeyword(location, false);
        }
        if(match(TokenType::kwUnique)) {
            if(!match(TokenType::kwIndex)) return nullptr;
            return parseCreateIndexAfterKeyword(location, true);
        }
        if(check(TokenType::kwIndex)) {
            advanceToken();
            return parseCreateIndexAfterKeyword(location, false);
        }
        if(match(TokenType::kwVirtual)) {
            if(!match(TokenType::kwTable)) return nullptr;
            return parseCreateVirtualTableTail(location, false);
        }
        if(check(TokenType::kwView)) {
            advanceToken();
            return parseCreateViewTail(location);
        }
        if(!match(TokenType::kwTable)) return nullptr;
        return parseCreateTableTail(location);
    }

    AstNodePointer Parser::parseCreateViewTail(SourceLocation location) {
        bool ifNotExists = false;
        if(check(TokenType::kwIf)) {
            advanceToken();
            if(!match(TokenType::kwNot)) return nullptr;
            if(!match(TokenType::kwExists)) return nullptr;
            ifNotExists = true;
        }

        std::optional<std::string> viewSchemaName;
        std::string viewName;
        if(!parseDmlQualifiedTable(viewSchemaName, viewName)) return nullptr;

        std::vector<std::string> columnNames;
        if(match(TokenType::leftParen)) {
            if(!check(TokenType::rightParen)) {
                do {
                    if(!isColumnNameToken()) return nullptr;
                    columnNames.emplace_back(std::string(current().value));
                    advanceToken();
                } while(match(TokenType::comma));
            }
            if(!match(TokenType::rightParen)) return nullptr;
        }

        if(!match(TokenType::kwAs)) return nullptr;
        AstNodePointer selectAst = parseSelect();
        if(!selectAst) return nullptr;

        return std::make_unique<CreateViewNode>(location, ifNotExists, std::move(viewSchemaName), std::move(viewName),
                                                  std::move(columnNames), std::move(selectAst));
    }

    AstNodePointer Parser::parseCreateTableTail(SourceLocation location) {
        bool ifNotExists = false;
        if(check(TokenType::kwIf)) {
            advanceToken();
            if(!match(TokenType::kwNot)) return nullptr;
            if(!match(TokenType::kwExists)) return nullptr;
            ifNotExists = true;
        }

        if(!isColumnNameToken()) return nullptr;
        std::string tableName(current().value);
        advanceToken();

        if(match(TokenType::dot)) {
            if(!isColumnNameToken()) return nullptr;
            tableName = std::string(current().value);
            advanceToken();
        }

        if(!match(TokenType::leftParen)) return nullptr;

        std::vector<ColumnDef> columns;
        std::vector<TableForeignKey> foreignKeys;
        std::vector<TablePrimaryKey> primaryKeys;
        std::vector<TableUnique> uniques;
        std::vector<TableCheck> checks;

        auto parse_table_constraint = [&]() -> bool {
            if(check(TokenType::kwConstraint)) {
                advanceToken();
                if(!atEnd()) advanceToken();
            }
            if(check(TokenType::kwForeign)) {
                foreignKeys.push_back(parseTableForeignKey());
                return true;
            } else if(check(TokenType::kwPrimary)) {
                advanceToken();
                match(TokenType::kwKey);
                std::vector<std::string> cols;
                if(match(TokenType::leftParen)) {
                    if(!atEnd()) {
                        cols.emplace_back(current().value);
                        advanceToken();
                    }
                    while(match(TokenType::comma)) {
                        if(!atEnd()) {
                            cols.emplace_back(current().value);
                            advanceToken();
                        }
                    }
                    match(TokenType::rightParen);
                }
                primaryKeys.push_back(TablePrimaryKey{std::move(cols)});
                return true;
            } else if(check(TokenType::kwUnique)) {
                advanceToken();
                std::vector<std::string> cols;
                if(match(TokenType::leftParen)) {
                    if(!atEnd()) {
                        cols.emplace_back(current().value);
                        advanceToken();
                    }
                    while(match(TokenType::comma)) {
                        if(!atEnd()) {
                            cols.emplace_back(current().value);
                            advanceToken();
                        }
                    }
                    match(TokenType::rightParen);
                }
                uniques.push_back(TableUnique{std::move(cols)});
                return true;
            } else if(check(TokenType::kwCheck)) {
                advanceToken();
                if(match(TokenType::leftParen)) {
                    auto expression = parseExpression();
                    match(TokenType::rightParen);
                    checks.push_back(TableCheck{std::move(expression)});
                }
                return true;
            }
            return false;
        };

        if(!check(TokenType::rightParen)) {
            if(!parse_table_constraint()) {
                columns.push_back(parseColumnDef());
            }
            while(match(TokenType::comma)) {
                if(check(TokenType::rightParen)) break;
                if(!parse_table_constraint()) {
                    columns.push_back(parseColumnDef());
                }
            }
        }

        if(!match(TokenType::rightParen)) return nullptr;

        auto node = std::make_unique<CreateTableNode>(
            std::move(tableName), std::move(columns), std::move(foreignKeys), ifNotExists, location);
        node->primaryKeys = std::move(primaryKeys);
        node->uniques = std::move(uniques);
        node->checks = std::move(checks);

        while(check(TokenType::kwWithout) || check(TokenType::kwStrict)) {
            if(check(TokenType::kwWithout)) {
                advanceToken();
                if(!atEnd() && toLowerAscii(std::string(current().value)) == "rowid") {
                    advanceToken();
                    node->withoutRowid = true;
                }
            } else if(match(TokenType::kwStrict)) {
                node->strict = true;
            }
            if(!match(TokenType::comma)) break;
        }

        return node;
    }

    AstNodePointer Parser::parseCreateTriggerAfterKeyword(SourceLocation location, bool temporary) {
        bool ifNotExists = false;
        if(check(TokenType::kwIf)) {
            advanceToken();
            if(!match(TokenType::kwNot)) return nullptr;
            if(!match(TokenType::kwExists)) return nullptr;
            ifNotExists = true;
        }

        std::optional<std::string> triggerSchema;
        std::string triggerName;
        if(!isColumnNameToken()) return nullptr;
        std::string firstTriggerId(current().value);
        advanceToken();
        if(match(TokenType::dot)) {
            if(!isColumnNameToken()) return nullptr;
            triggerSchema = std::move(firstTriggerId);
            triggerName = std::string(current().value);
            advanceToken();
        } else {
            triggerName = std::move(firstTriggerId);
        }

        TriggerTiming timing;
        if(match(TokenType::kwBefore)) {
            timing = TriggerTiming::before;
        } else if(match(TokenType::kwAfter)) {
            timing = TriggerTiming::after;
        } else if(check(TokenType::kwInstead)) {
            advanceToken();
            if(!match(TokenType::kwOf)) return nullptr;
            timing = TriggerTiming::insteadOf;
        } else {
            return nullptr;
        }

        TriggerEventKind eventKind = TriggerEventKind::insert_;
        std::vector<std::string> updateOfColumns;
        if(match(TokenType::kwDelete)) {
            eventKind = TriggerEventKind::delete_;
        } else if(match(TokenType::kwInsert)) {
            eventKind = TriggerEventKind::insert_;
        } else if(match(TokenType::kwUpdate)) {
            if(match(TokenType::kwOf)) {
                eventKind = TriggerEventKind::updateOf;
                if(!isColumnNameToken()) return nullptr;
                updateOfColumns.emplace_back(current().value);
                advanceToken();
                while(match(TokenType::comma)) {
                    if(!isColumnNameToken()) return nullptr;
                    updateOfColumns.emplace_back(current().value);
                    advanceToken();
                }
            } else {
                eventKind = TriggerEventKind::update_;
            }
        } else {
            return nullptr;
        }

        if(!match(TokenType::kwOn)) return nullptr;
        std::optional<std::string> tableSchema;
        std::string tableName;
        if(!parseDmlQualifiedTable(tableSchema, tableName)) return nullptr;

        bool forEachRow = false;
        if(check(TokenType::kwFor)) {
            advanceToken();
            if(!match(TokenType::kwEach)) return nullptr;
            if(!match(TokenType::kwRow)) return nullptr;
            forEachRow = true;
        }

        AstNodePointer whenClause;
        if(match(TokenType::kwWhen)) {
            whenClause = parseExpression();
            if(!whenClause) return nullptr;
        }

        if(!match(TokenType::kwBegin)) return nullptr;

        if(check(TokenType::kwEnd)) return nullptr;

        std::vector<AstNodePointer> body;
        while(!check(TokenType::kwEnd) && !atEnd()) {
            AstNodePointer stmt = parseTriggerBodyStatement();
            if(!stmt) return nullptr;
            body.push_back(std::move(stmt));
            if(!match(TokenType::semicolon)) {
                if(check(TokenType::kwEnd)) {
                    break;
                }
                return nullptr;
            }
        }
        if(!match(TokenType::kwEnd)) return nullptr;

        auto node = std::make_unique<CreateTriggerNode>(location);
        node->triggerSchemaName = std::move(triggerSchema);
        node->triggerName = std::move(triggerName);
        node->ifNotExists = ifNotExists;
        node->temporary = temporary;
        node->timing = timing;
        node->eventKind = eventKind;
        node->updateOfColumns = std::move(updateOfColumns);
        node->tableSchemaName = std::move(tableSchema);
        node->tableName = std::move(tableName);
        node->forEachRow = forEachRow;
        node->whenClause = std::move(whenClause);
        node->bodyStatements = std::move(body);
        return node;
    }

    AstNodePointer Parser::parseTriggerBodyStatement() {
        if(check(TokenType::kwSelect)) {
            return parseSelect();
        }
        if(check(TokenType::kwInsert)) {
            return parseInsertStatement(false);
        }
        if(check(TokenType::kwReplace) && peekToken(1).type == TokenType::kwInto) {
            return parseInsertStatement(true);
        }
        if(check(TokenType::kwUpdate)) {
            return parseUpdateStatement();
        }
        if(check(TokenType::kwDelete)) {
            return parseDeleteStatement();
        }
        return nullptr;
    }

    bool Parser::parseIndexColumnSpec(IndexColumnSpec& out) {
        out = IndexColumnSpec{};
        out.expression = parseExpression();
        if(!out.expression) return false;
        if(auto* collateExpr = dynamic_cast<CollateNode*>(out.expression.get())) {
            out.collation = collateExpr->collationName;
            out.expression = std::move(collateExpr->operand);
        }
        while(true) {
            if(out.collation.empty() && match(TokenType::kwCollate)) {
                if(!isColumnNameToken()) return false;
                out.collation = std::string(current().value);
                advanceToken();
                continue;
            }
            if(match(TokenType::kwAsc)) {
                if(out.sortDirection != SortDirection::none) return false;
                out.sortDirection = SortDirection::asc;
                continue;
            }
            if(match(TokenType::kwDesc)) {
                if(out.sortDirection != SortDirection::none) return false;
                out.sortDirection = SortDirection::desc;
                continue;
            }
            break;
        }
        return true;
    }

    AstNodePointer Parser::parseCreateIndexAfterKeyword(SourceLocation location, bool unique) {
        bool ifNotExists = false;
        if(check(TokenType::kwIf)) {
            advanceToken();
            if(!match(TokenType::kwNot)) return nullptr;
            if(!match(TokenType::kwExists)) return nullptr;
            ifNotExists = true;
        }

        std::optional<std::string> indexSchema;
        std::string indexName;
        if(!isColumnNameToken()) return nullptr;
        std::string firstIndexId(current().value);
        advanceToken();
        if(match(TokenType::dot)) {
            if(!isColumnNameToken()) return nullptr;
            indexSchema = std::move(firstIndexId);
            indexName = std::string(current().value);
            advanceToken();
        } else {
            indexName = std::move(firstIndexId);
        }

        if(!match(TokenType::kwOn)) return nullptr;
        std::optional<std::string> tableSchema;
        std::string tableName;
        if(!parseDmlQualifiedTable(tableSchema, tableName)) return nullptr;

        if(!match(TokenType::leftParen)) return nullptr;
        std::vector<IndexColumnSpec> columns;
        if(!check(TokenType::rightParen)) {
            IndexColumnSpec spec;
            if(!parseIndexColumnSpec(spec)) return nullptr;
            columns.push_back(std::move(spec));
            while(match(TokenType::comma)) {
                IndexColumnSpec nextSpec;
                if(!parseIndexColumnSpec(nextSpec)) return nullptr;
                columns.push_back(std::move(nextSpec));
            }
        }
        if(!match(TokenType::rightParen)) return nullptr;

        AstNodePointer whereClause;
        if(match(TokenType::kwWhere)) {
            whereClause = parseExpression();
            if(!whereClause) return nullptr;
        }

        auto node = std::make_unique<CreateIndexNode>(location);
        node->unique = unique;
        node->ifNotExists = ifNotExists;
        node->indexSchemaName = std::move(indexSchema);
        node->indexName = std::move(indexName);
        node->tableSchemaName = std::move(tableSchema);
        node->tableName = std::move(tableName);
        node->indexedColumns = std::move(columns);
        node->whereClause = std::move(whereClause);
        return node;
    }

    AstNodePointer Parser::parseCreateVirtualTableTail(SourceLocation location, bool temporary) {
        bool ifNotExists = false;
        if(check(TokenType::kwIf)) {
            advanceToken();
            if(!match(TokenType::kwNot)) return nullptr;
            if(!match(TokenType::kwExists)) return nullptr;
            ifNotExists = true;
        }

        std::optional<std::string> tableSchema;
        std::string vtabName;
        if(!isColumnNameToken()) return nullptr;
        std::string firstTableId(current().value);
        advanceToken();
        if(match(TokenType::dot)) {
            if(!isColumnNameToken()) return nullptr;
            tableSchema = std::move(firstTableId);
            vtabName = std::string(current().value);
            advanceToken();
        } else {
            vtabName = std::move(firstTableId);
        }

        if(!match(TokenType::kwUsing)) return nullptr;
        if(!isColumnNameToken()) return nullptr;
        std::string moduleName(current().value);
        advanceToken();

        std::vector<AstNodePointer> moduleArguments;
        if(match(TokenType::leftParen)) {
            if(!check(TokenType::rightParen)) {
                while(true) {
                    auto arg = parseExpression();
                    if(!arg) return nullptr;
                    moduleArguments.push_back(std::move(arg));
                    if(match(TokenType::comma)) {
                        continue;
                    }
                    break;
                }
            }
            if(!match(TokenType::rightParen)) return nullptr;
        }

        auto node = std::make_unique<CreateVirtualTableNode>(location);
        node->temporary = temporary;
        node->ifNotExists = ifNotExists;
        node->tableSchemaName = std::move(tableSchema);
        node->tableName = std::move(vtabName);
        node->moduleName = std::move(moduleName);
        node->moduleArguments = std::move(moduleArguments);
        return node;
    }

    AstNodePointer Parser::parseSelectCore() {
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
            node->whereClause = parseExpression();
        }

        if(check(TokenType::kwGroup)) {
            advanceToken();
            match(TokenType::kwBy);
            GroupByClause groupByClause;
            groupByClause.expressions.push_back(parseExpression());
            while(match(TokenType::comma)) {
                groupByClause.expressions.push_back(parseExpression());
            }
            if(match(TokenType::kwHaving)) {
                groupByClause.having = parseExpression();
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
                if(!parseOverClauseParenContents(*winDef.definition, true)) {
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
                auto expr = parseExpression();
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

    AstNodePointer Parser::parseCompoundSelectCore() {
        if(check(TokenType::kwSelect)) {
            return parseSelectCore();
        }
        if(check(TokenType::kwValues)) {
            return parseValuesStatement();
        }
        return nullptr;
    }

    AstNodePointer Parser::parseSelectCompoundBody() {
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

    AstNodePointer Parser::parseSelect() {
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
                body = parseInsertStatement(false);
            } else if(check(TokenType::kwReplace) && peekToken(1).type == TokenType::kwInto) {
                body = parseInsertStatement(true);
            } else if(check(TokenType::kwUpdate)) {
                body = parseUpdateStatement();
            } else if(check(TokenType::kwDelete)) {
                body = parseDeleteStatement();
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

    SelectColumn Parser::parseSelectResultColumn() {
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
        auto expr = parseExpression();
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

    FromTableClause Parser::parseFromTableItem() {
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
                auto arg = parseExpression();
                if(arg) item.tableFunctionArgs.push_back(std::shared_ptr<AstNode>(std::move(arg)));
                while(match(TokenType::comma)) {
                    arg = parseExpression();
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

    std::vector<FromClauseItem> Parser::parseFromClause() {
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

    bool Parser::consumeJoinOperator(JoinKind& out) {
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

    void Parser::parseJoinConstraint(FromClauseItem& item) {
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
            item.onExpression = parseExpression();
        }
    }

    ConflictClause Parser::parseInsertOrConflictKeyword() {
        if(match(TokenType::kwRollback)) return ConflictClause::rollback;
        if(match(TokenType::kwAbort)) return ConflictClause::abort;
        if(match(TokenType::kwFail)) return ConflictClause::fail;
        if(match(TokenType::kwIgnore)) return ConflictClause::ignore;
        if(match(TokenType::kwReplace)) return ConflictClause::replace;
        return ConflictClause::none;
    }

    bool Parser::parseDmlQualifiedTable(std::optional<std::string>& schemaOut, std::string& tableOut) {
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

    bool Parser::parseCommaSeparatedUpdateAssignments(std::vector<UpdateAssignment>& out) {
        if(!isColumnNameToken()) return false;
        while(true) {
            UpdateAssignment assignment;
            assignment.columnName = std::string(current().value);
            advanceToken();
            if(!match(TokenType::eq)) return false;
            assignment.value = parseExpression();
            if(!assignment.value) return false;
            out.push_back(std::move(assignment));
            if(!match(TokenType::comma)) {
                break;
            }
            if(!isColumnNameToken()) return false;
        }
        return true;
    }

    bool Parser::parseUpsertTail(InsertNode& node, bool replaceInto) {
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
            node.upsertConflictWhere = parseExpression();
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
            node.upsertUpdateWhere = parseExpression();
            if(!node.upsertUpdateWhere) return false;
        }
        return true;
    }

    AstNodePointer Parser::parseInsertStatement(bool replaceInto) {
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
                    AstNodePointer expr = parseExpression();
                    if(!expr) return nullptr;
                    row.push_back(std::move(expr));
                    while(match(TokenType::comma)) {
                        expr = parseExpression();
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
        node->selectStatement = parseSelect();
        if(!node->selectStatement) return nullptr;
        if(!parseUpsertTail(*node, replaceInto)) return nullptr;
        if(!parseReturningClause(node->returning)) return nullptr;
        return node;
    }

    AstNodePointer Parser::parseUpdateStatement() {
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
            node->fromClause = parseFromClause();
        }
        if(match(TokenType::kwWhere)) {
            node->whereClause = parseExpression();
            if(!node->whereClause) return nullptr;
        }
        if(!parseReturningClause(node->returning)) return nullptr;
        return node;
    }

    AstNodePointer Parser::parseValuesStatement() {
        auto location = current().location;
        advanceToken();  // VALUES
        auto node = std::make_unique<SelectNode>(location);
        do {
            if(!match(TokenType::leftParen)) return nullptr;
            std::vector<AstNodePointer> row;
            if(!check(TokenType::rightParen)) {
                auto expr = parseExpression();
                if(!expr) return nullptr;
                row.push_back(std::move(expr));
                while(match(TokenType::comma)) {
                    expr = parseExpression();
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
            // only first row becomes columns; additional rows are not modeled
        } while(match(TokenType::comma));
        return node;
    }

    AstNodePointer Parser::parseDeleteStatement() {
        auto location = current().location;
        advanceToken();
        if(!match(TokenType::kwFrom)) return nullptr;
        auto node = std::make_unique<DeleteNode>(location);
        if(!parseDmlQualifiedTable(node->schemaName, node->tableName)) return nullptr;
        if(match(TokenType::kwWhere)) {
            node->whereClause = parseExpression();
            if(!node->whereClause) return nullptr;
        }
        if(!parseReturningClause(node->returning)) return nullptr;
        return node;
    }

    AstNodePointer Parser::parseTransactionControlStatement() {
        const SourceLocation location = current().location;
        if(check(TokenType::kwBegin)) {
            advanceToken();
            auto node = std::make_unique<TransactionControlNode>(location);
            node->kind = TransactionControlNode::Kind::begin;
            if(match(TokenType::kwDeferred)) {
                node->beginMode = BeginTransactionMode::deferred;
            } else if(match(TokenType::kwImmediate)) {
                node->beginMode = BeginTransactionMode::immediate;
            } else if(match(TokenType::kwExclusive)) {
                node->beginMode = BeginTransactionMode::exclusive;
            } else {
                node->beginMode = BeginTransactionMode::plain;
            }
            match(TokenType::kwTransaction);
            return node;
        }
        if(check(TokenType::kwCommit) || check(TokenType::kwEnd)) {
            advanceToken();
            match(TokenType::kwTransaction);
            auto node = std::make_unique<TransactionControlNode>(location);
            node->kind = TransactionControlNode::Kind::commit;
            return node;
        }
        if(check(TokenType::kwRollback)) {
            advanceToken();
            auto node = std::make_unique<TransactionControlNode>(location);
            node->kind = TransactionControlNode::Kind::rollback;
            if(match(TokenType::kwTo)) {
                (void)match(TokenType::kwSavepoint);
                if(!isColumnNameToken()) {
                    return nullptr;
                }
                node->rollbackToSavepoint = std::string(current().value);
                advanceToken();
                return node;
            }
            match(TokenType::kwTransaction);
            return node;
        }
        return nullptr;
    }

    AstNodePointer Parser::parseVacuumStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        if(atEnd() || check(TokenType::semicolon)) {
            return std::make_unique<VacuumStatementNode>(location);
        }
        if(isColumnNameToken()) {
            std::string schema = std::string(current().value);
            advanceToken();
            return std::make_unique<VacuumStatementNode>(location, std::move(schema));
        }
        return nullptr;
    }

    AstNodePointer Parser::parseDropStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        DropObjectKind kind;
        if(match(TokenType::kwTable)) {
            kind = DropObjectKind::table;
        } else if(match(TokenType::kwIndex)) {
            kind = DropObjectKind::index;
        } else if(match(TokenType::kwTrigger)) {
            kind = DropObjectKind::trigger;
        } else if(match(TokenType::kwView)) {
            kind = DropObjectKind::view;
        } else {
            return nullptr;
        }
        bool ifExists = false;
        if(check(TokenType::kwIf)) {
            advanceToken();
            if(!match(TokenType::kwNot)) {
                return nullptr;
            }
            if(!match(TokenType::kwExists)) {
                return nullptr;
            }
            ifExists = true;
        }
        std::optional<std::string> schemaName;
        std::string objectName;
        if(!parseDmlQualifiedTable(schemaName, objectName)) {
            return nullptr;
        }
        auto node = std::make_unique<DropStatementNode>(location);
        node->objectKind = kind;
        node->ifExists = ifExists;
        node->schemaName = std::move(schemaName);
        node->objectName = std::move(objectName);
        return node;
    }

    AstNodePointer Parser::parseAlterTableStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        if(!match(TokenType::kwTable)) {
            return nullptr;
        }
        std::optional<std::string> tableSchemaName;
        std::string tableName;
        if(!parseDmlQualifiedTable(tableSchemaName, tableName)) {
            return nullptr;
        }
        skipToSemicolon();
        return std::make_unique<AlterTableStatementNode>(location, std::move(tableSchemaName), std::move(tableName));
    }

    bool Parser::parseReturningClause(std::vector<ReturningColumn>& out) {
        if(!match(TokenType::kwReturning)) {
            return true;
        }
        do {
            ReturningColumn column;
            if(match(TokenType::star)) {
                // expression=nullptr signals `*`, same convention as SelectColumn
            } else {
                column.expression = parseExpression();
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

    AstNodePointer Parser::parseSavepointStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        if(!isColumnNameToken()) return nullptr;
        std::string name(current().value);
        advanceToken();
        return std::make_unique<SavepointNode>(location, std::move(name));
    }

    AstNodePointer Parser::parseReleaseStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        (void)match(TokenType::kwSavepoint);
        if(!isColumnNameToken()) return nullptr;
        std::string name(current().value);
        advanceToken();
        return std::make_unique<ReleaseNode>(location, std::move(name));
    }

    AstNodePointer Parser::parseAttachStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        (void)match(TokenType::kwDatabase);
        auto fileExpr = parseExpression();
        if(!fileExpr) return nullptr;
        if(!match(TokenType::kwAs)) return nullptr;
        if(!isColumnNameToken()) return nullptr;
        std::string schemaName(current().value);
        advanceToken();
        return std::make_unique<AttachDatabaseNode>(location, std::move(fileExpr), std::move(schemaName));
    }

    AstNodePointer Parser::parseDetachStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        (void)match(TokenType::kwDatabase);
        if(!isColumnNameToken()) return nullptr;
        std::string schemaName(current().value);
        advanceToken();
        return std::make_unique<DetachDatabaseNode>(location, std::move(schemaName));
    }

    AstNodePointer Parser::parseAnalyzeStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        auto node = std::make_unique<AnalyzeNode>(location);
        if(!atEnd() && isColumnNameToken()) {
            node->schemaOrTableName = std::string(current().value);
            advanceToken();
            if(match(TokenType::dot)) {
                if(!isColumnNameToken()) return nullptr;
                node->tableName = std::string(current().value);
                advanceToken();
            }
        }
        return node;
    }

    AstNodePointer Parser::parseReindexStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        auto node = std::make_unique<ReindexNode>(location);
        if(!atEnd() && isColumnNameToken()) {
            node->schemaOrObjectName = std::string(current().value);
            advanceToken();
            if(match(TokenType::dot)) {
                if(!isColumnNameToken()) return nullptr;
                node->objectName = std::string(current().value);
                advanceToken();
            }
        }
        return node;
    }

    AstNodePointer Parser::parsePragmaStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        auto node = std::make_unique<PragmaNode>(location);
        if(!isColumnNameToken()) return nullptr;
        std::string firstName(current().value);
        advanceToken();
        if(match(TokenType::dot)) {
            node->schemaName = std::move(firstName);
            if(!isColumnNameToken()) return nullptr;
            node->pragmaName = std::string(current().value);
            advanceToken();
        } else {
            node->pragmaName = std::move(firstName);
        }
        if(match(TokenType::eq) || match(TokenType::eq2)) {
            node->value = parseExpression();
            if(!node->value) return nullptr;
        } else if(match(TokenType::leftParen)) {
            node->value = parseExpression();
            if(!node->value) return nullptr;
            if(!match(TokenType::rightParen)) return nullptr;
        }
        return node;
    }

    AstNodePointer Parser::parseExplainStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        bool queryPlan = false;
        if(match(TokenType::kwQuery)) {
            if(!match(TokenType::kwPlan)) return nullptr;
            queryPlan = true;
        }
        AstNodePointer innerStatement;
        if(check(TokenType::kwSelect) || check(TokenType::kwWith)) {
            innerStatement = parseSelect();
        } else if(check(TokenType::kwInsert)) {
            innerStatement = parseInsertStatement(false);
        } else if(check(TokenType::kwReplace) && peekToken(1).type == TokenType::kwInto) {
            innerStatement = parseInsertStatement(true);
        } else if(check(TokenType::kwUpdate)) {
            innerStatement = parseUpdateStatement();
        } else if(check(TokenType::kwDelete)) {
            innerStatement = parseDeleteStatement();
        } else if(check(TokenType::kwCreate)) {
            innerStatement = parseCreate();
        } else if(check(TokenType::kwDrop)) {
            innerStatement = parseDropStatement();
        } else if(check(TokenType::kwAlter)) {
            innerStatement = parseAlterTableStatement();
        } else {
            innerStatement = parseExpression();
        }
        if(!innerStatement) return nullptr;
        return std::make_unique<ExplainNode>(location, queryPlan, std::move(innerStatement));
    }

    void Parser::skipToSemicolon() {
        int parenDepth = 0;
        while(!atEnd()) {
            const TokenType tokenType = current().type;
            if(tokenType == TokenType::semicolon && parenDepth == 0) {
                advanceToken();
                return;
            }
            if(tokenType == TokenType::leftParen) {
                ++parenDepth;
            } else if(tokenType == TokenType::rightParen && parenDepth > 0) {
                --parenDepth;
            }
            advanceToken();
        }
    }

    ParseResult Parser::parse(std::vector<Token> tokens) {
        this->tokens = std::move(tokens);
        this->position = 0;

        AstNodePointer astNodePointer;
        if(check(TokenType::kwCreate)) {
            astNodePointer = parseCreate();
        } else if(check(TokenType::kwSelect) || check(TokenType::kwWith)) {
            astNodePointer = parseSelect();
        } else if(check(TokenType::kwInsert)) {
            astNodePointer = parseInsertStatement(false);
        } else if(check(TokenType::kwReplace) && peekToken(1).type == TokenType::kwInto) {
            astNodePointer = parseInsertStatement(true);
        } else if(check(TokenType::kwValues)) {
            astNodePointer = parseSelectCompoundBody();
        } else if(check(TokenType::kwUpdate)) {
            astNodePointer = parseUpdateStatement();
        } else if(check(TokenType::kwDelete)) {
            astNodePointer = parseDeleteStatement();
        } else if(check(TokenType::kwBegin) || check(TokenType::kwCommit) || check(TokenType::kwRollback) ||
                  check(TokenType::kwEnd)) {
            astNodePointer = parseTransactionControlStatement();
        } else if(check(TokenType::kwVacuum)) {
            astNodePointer = parseVacuumStatement();
        } else if(check(TokenType::kwDrop)) {
            astNodePointer = parseDropStatement();
        } else if(check(TokenType::kwAlter)) {
            astNodePointer = parseAlterTableStatement();
        } else if(check(TokenType::kwSavepoint)) {
            astNodePointer = parseSavepointStatement();
        } else if(check(TokenType::kwRelease)) {
            astNodePointer = parseReleaseStatement();
        } else if(check(TokenType::kwAttach)) {
            astNodePointer = parseAttachStatement();
        } else if(check(TokenType::kwDetach)) {
            astNodePointer = parseDetachStatement();
        } else if(check(TokenType::kwAnalyze)) {
            astNodePointer = parseAnalyzeStatement();
        } else if(check(TokenType::kwReindex)) {
            astNodePointer = parseReindexStatement();
        } else if(check(TokenType::kwPragma)) {
            astNodePointer = parsePragmaStatement();
        } else if(check(TokenType::kwExplain)) {
            astNodePointer = parseExplainStatement();
        } else {
            astNodePointer = parseExpression();
        }

        if(!astNodePointer) {
            const Token& token = current();
            return ParseResult{nullptr, {ParseError{"unexpected token: " + std::string(token.value), token.location}}};
        }

        if(!atEnd()) {
            const Token& token = current();
            if(token.type != TokenType::semicolon) {
                return ParseResult{std::move(astNodePointer),
                                  {ParseError{"unexpected token after statement: " + std::string(token.value), token.location}}};
            }
        }

        return ParseResult{std::move(astNodePointer), {}};
    }

    std::vector<ParseResult> Parser::parseAll(std::vector<Token> tokensInput) {
        this->tokens = std::move(tokensInput);
        this->position = 0;

        std::vector<ParseResult> results;
        while(!atEnd()) {
            while(!atEnd() && current().type == TokenType::semicolon) {
                advanceToken();
            }
            if(atEnd()) {
                break;
            }

            size_t saved = this->position;
            std::vector<Token> slice;
            int parenDepth = 0;
            while(!atEnd()) {
                const Token& tok = current();
                if(tok.type == TokenType::leftParen) {
                    ++parenDepth;
                } else if(tok.type == TokenType::rightParen) {
                    --parenDepth;
                } else if(tok.type == TokenType::semicolon && parenDepth <= 0) {
                    break;
                }
                slice.push_back(tok);
                advanceToken();
            }
            if(!atEnd() && current().type == TokenType::semicolon) {
                slice.push_back(current());
                advanceToken();
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
