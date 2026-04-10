#include "codegen_expression.h"
#include "codegen_context.h"
#include "codegen_utils.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    ExpressionCodeGenerator::ExpressionCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context)
        : coordinator(coordinator), context(context) {}

    CodeGenResult ExpressionCodeGenerator::generateExpression(const AstNode& astNode) {
        if(auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&astNode)) {
            return CodeGenResult{std::string(integerLiteral->value), {}};
        } else if(auto* realLiteral = dynamic_cast<const RealLiteralNode*>(&astNode)) {
            return CodeGenResult{std::string(realLiteral->value), {}};
        } else if(auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(&astNode)) {
            return CodeGenResult{sqlStringToCpp(stringLiteral->value), {}};
        } else if(dynamic_cast<const NullLiteralNode*>(&astNode)) {
            return CodeGenResult{"nullptr", {}};
        } else if(auto* boolLiteral = dynamic_cast<const BoolLiteralNode*>(&astNode)) {
            return CodeGenResult{boolLiteral->value ? "true" : "false", {}};
        } else if(auto* raiseNode = dynamic_cast<const RaiseNode*>(&astNode)) {
            if(raiseNode->kind == RaiseKind::ignore) {
                return CodeGenResult{"raise_ignore()", {}};
            }
            if(!raiseNode->message) {
                return CodeGenResult{"raise_ignore()", {}};
            }
            std::vector<std::string> raiseWarnings;
            const char* api = "raise_abort";
            if(raiseNode->kind == RaiseKind::rollback) {
                api = "raise_rollback";
            } else if(raiseNode->kind == RaiseKind::abort) {
                api = "raise_abort";
            } else if(raiseNode->kind == RaiseKind::fail) {
                api = "raise_fail";
            }
            if(auto* strLit = dynamic_cast<const StringLiteralNode*>(raiseNode->message.get())) {
                return CodeGenResult{std::string(api) + "(" + sqlStringToCpp(strLit->value) + ")", {}};
            }
            raiseWarnings.push_back(
                "RAISE(ROLLBACK|ABORT|FAIL, ...) message should be a SQL string literal for sqlite_orm raise_*()");
            return CodeGenResult{std::string(api) + "(\"\")", {}, std::move(raiseWarnings)};
        } else if(auto* blobLiteral = dynamic_cast<const BlobLiteralNode*>(&astNode)) {
            return CodeGenResult{blobToCpp(blobLiteral->value), {}};
        } else if(auto* currentDt = dynamic_cast<const CurrentDatetimeLiteralNode*>(&astNode)) {
            switch(currentDt->kind) {
            case CurrentDatetimeKind::time:
                return CodeGenResult{"current_time()", {}};
            case CurrentDatetimeKind::date:
                return CodeGenResult{"current_date()", {}};
            case CurrentDatetimeKind::timestamp:
                return CodeGenResult{"current_timestamp()", {}};
            }
            return CodeGenResult{"current_timestamp()", {}};
        } else if(auto* columnRef = dynamic_cast<const ColumnRefNode*>(&astNode)) {
            {
                std::string normalized = toLowerAscii(stripIdentifierQuotes(columnRef->columnName));
                auto aliasIt = this->context.activeSelectColumnAliases.find(normalized);
                if(aliasIt != this->context.activeSelectColumnAliases.end()) {
                    if(this->context.useCpp20ColumnAliasStyle()) {
                        auto varIt = this->context.activeSelectColumnAliasCpp20Vars.find(normalized);
                        if(varIt != this->context.activeSelectColumnAliasCpp20Vars.end()) {
                            return CodeGenResult{varIt->second, {}};
                        }
                    }
                    return CodeGenResult{"get<" + aliasIt->second + ">()", {}};
                }
            }
            auto cppName = toCppIdentifier(columnRef->columnName);
            this->context.registerPrefixColumn(cppName, this->context.syntheticColumnCppType(cppName));
            if(this->context.implicitSingleSourceCteTypedef && this->context.implicitCteFromTableKeyNorm) {
                const std::string colKey = normalizeSqlIdentifier(columnRef->columnName);
                const std::string pipe = *this->context.implicitCteFromTableKeyNorm + "|" + colKey;
                if(this->context.withCteCpp20Monikers()) {
                    auto monIt =
                        this->context.withCteCpp20MonikerVarByCteKey.find(*this->context.implicitCteFromTableKeyNorm);
                    auto colIt = this->context.withCteCpp20ColVarByPipeKey.find(pipe);
                    if(monIt != this->context.withCteCpp20MonikerVarByCteKey.end() &&
                       colIt != this->context.withCteCpp20ColVarByPipeKey.end()) {
                        return CodeGenResult{monIt->second + "->*" + colIt->second, {}};
                    }
                    if(monIt != this->context.withCteCpp20MonikerVarByCteKey.end()) {
                        if(!this->context.isExplicitCteColumn(*this->context.implicitCteFromTableKeyNorm,
                                                              columnRef->columnName)) {
                            auto baseIt =
                                this->context.cteBaseStructByKey.find(*this->context.implicitCteFromTableKeyNorm);
                            if(baseIt != this->context.cteBaseStructByKey.end()) {
                                return CodeGenResult{monIt->second + "->*&" + baseIt->second + "::" + cppName, {}};
                            }
                        }
                    }
                }
                if(this->context.withCteLegacyColalias()) {
                    auto colIt = this->context.withCteLegacyColVarByPipeKey.find(pipe);
                    if(colIt != this->context.withCteLegacyColVarByPipeKey.end()) {
                        std::string cteCol =
                            "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" + colIt->second + ")";
                        return CodeGenResult{std::move(cteCol), {}};
                    }
                }
                {
                    auto indexedIt = this->context.withCteIndexedColVarByPipeKey.find(pipe);
                    if(indexedIt != this->context.withCteIndexedColVarByPipeKey.end()) {
                        std::string cteCol = "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" +
                                             indexedIt->second + ")";
                        return CodeGenResult{std::move(cteCol), {}};
                    }
                }
            }
            if(this->context.implicitSingleSourceCteTypedef) {
                if(this->context.implicitCteFromTableKeyNorm) {
                    const std::string fallbackPipe =
                        *this->context.implicitCteFromTableKeyNorm + "|" +
                        normalizeSqlIdentifier(columnRef->columnName);
                    auto indexedIt = this->context.withCteIndexedColVarByPipeKey.find(fallbackPipe);
                    if(indexedIt != this->context.withCteIndexedColVarByPipeKey.end()) {
                        std::string cteCol = "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" +
                                             indexedIt->second + ")";
                        return CodeGenResult{std::move(cteCol), {}};
                    }
                    if(this->context.isExplicitCteColumn(*this->context.implicitCteFromTableKeyNorm,
                                                         columnRef->columnName)) {
                        std::string colLit =
                            identifierToCppStringLiteral(stripIdentifierQuotes(columnRef->columnName));
                        std::string cteCol =
                            "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" + colLit + ")";
                        return CodeGenResult{std::move(cteCol), {}};
                    }
                    auto baseIt =
                        this->context.cteBaseStructByKey.find(*this->context.implicitCteFromTableKeyNorm);
                    if(baseIt != this->context.cteBaseStructByKey.end()) {
                        std::string cteCol = "column<" + *this->context.implicitSingleSourceCteTypedef + ">(&" +
                                             baseIt->second + "::" + cppName + ")";
                        return CodeGenResult{std::move(cteCol), {}};
                    }
                }
                std::string colLit = identifierToCppStringLiteral(stripIdentifierQuotes(columnRef->columnName));
                std::string cteCol =
                    "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" + colLit + ")";
                return CodeGenResult{std::move(cteCol), {}};
            }
            std::string memberPointer = "&" + this->context.structName + "::" + cppName;
            std::string columnPointer = "column<" + this->context.structName + ">(" + memberPointer + ")";
            const bool useColumnPtr =
                policyEquals(this->context.codeGenPolicy, "column_ref_style", "column_pointer");
            const std::string& emittedCol = useColumnPtr ? columnPointer : memberPointer;
            const std::string chosenCol = useColumnPtr ? "column_pointer" : "member_pointer";
            return CodeGenResult{
                emittedCol,
                {DecisionPoint{this->context.nextDecisionPointId++,
                               "column_ref_style",
                               chosenCol,
                               emittedCol,
                               {Alternative{"column_pointer", columnPointer,
                                            "explicit mapped type (inheritance / ambiguity)"}}}}};
        } else if(auto* qualifiedRef = dynamic_cast<const QualifiedColumnRefNode*>(&astNode)) {
            std::vector<std::string> qualWarnings;
            if(qualifiedRef->schemaName) {
                qualWarnings.push_back(
                    "schema-qualified column " + std::string(*qualifiedRef->schemaName) + "." +
                    std::string(qualifiedRef->tableName) + "." + std::string(qualifiedRef->columnName) +
                    " is not represented in sqlite_orm mapping; generated column reference uses table `" +
                    std::string(qualifiedRef->tableName) + "` only");
            }
            std::string tableKeyNorm = normalizeSqlIdentifier(qualifiedRef->tableName);
            auto cteIt = this->context.activeCteTypedefByTableKey.find(tableKeyNorm);
            if(cteIt != this->context.activeCteTypedefByTableKey.end()) {
                const std::string colKey = normalizeSqlIdentifier(qualifiedRef->columnName);
                const std::string pipe = tableKeyNorm + "|" + colKey;
                if(this->context.withCteCpp20Monikers()) {
                    auto monIt = this->context.withCteCpp20MonikerVarByCteKey.find(tableKeyNorm);
                    auto colIt = this->context.withCteCpp20ColVarByPipeKey.find(pipe);
                    if(monIt != this->context.withCteCpp20MonikerVarByCteKey.end() &&
                       colIt != this->context.withCteCpp20ColVarByPipeKey.end()) {
                        return CodeGenResult{monIt->second + "->*" + colIt->second, {}, std::move(qualWarnings)};
                    }
                    if(monIt != this->context.withCteCpp20MonikerVarByCteKey.end()) {
                        if(!this->context.isExplicitCteColumn(tableKeyNorm, qualifiedRef->columnName)) {
                            auto baseIt = this->context.cteBaseStructByKey.find(tableKeyNorm);
                            if(baseIt != this->context.cteBaseStructByKey.end()) {
                                std::string colCpp = toCppIdentifier(qualifiedRef->columnName);
                                return CodeGenResult{monIt->second + "->*&" + baseIt->second + "::" + colCpp, {},
                                                     std::move(qualWarnings)};
                            }
                        }
                    }
                }
                if(this->context.withCteLegacyColalias()) {
                    auto colIt = this->context.withCteLegacyColVarByPipeKey.find(pipe);
                    if(colIt != this->context.withCteLegacyColVarByPipeKey.end()) {
                        return CodeGenResult{"column<" + cteIt->second + ">(" + colIt->second + ")", {},
                                             std::move(qualWarnings)};
                    }
                }
                {
                    auto indexedIt = this->context.withCteIndexedColVarByPipeKey.find(pipe);
                    if(indexedIt != this->context.withCteIndexedColVarByPipeKey.end()) {
                        return CodeGenResult{"column<" + cteIt->second + ">(" + indexedIt->second + ")", {},
                                             std::move(qualWarnings)};
                    }
                }
                if(this->context.isExplicitCteColumn(tableKeyNorm, qualifiedRef->columnName)) {
                    std::string colLit =
                        identifierToCppStringLiteral(stripIdentifierQuotes(qualifiedRef->columnName));
                    return CodeGenResult{"column<" + cteIt->second + ">(" + colLit + ")", {},
                                         std::move(qualWarnings)};
                }
                auto baseIt = this->context.cteBaseStructByKey.find(tableKeyNorm);
                if(baseIt != this->context.cteBaseStructByKey.end()) {
                    std::string colCpp = toCppIdentifier(qualifiedRef->columnName);
                    return CodeGenResult{"column<" + cteIt->second + ">(&" + baseIt->second + "::" + colCpp + ")", {},
                                         std::move(qualWarnings)};
                }
                std::string colLit = identifierToCppStringLiteral(stripIdentifierQuotes(qualifiedRef->columnName));
                return CodeGenResult{"column<" + cteIt->second + ">(" + colLit + ")", {},
                                     std::move(qualWarnings)};
            }
            std::string tableKey = std::string(qualifiedRef->tableName);
            auto tableAliasIt = this->context.activeTableAliases.find(tableKey);
            if(tableAliasIt != this->context.activeTableAliases.end()) {
                const auto& info = tableAliasIt->second;
                const std::string colCpp = toCppIdentifier(qualifiedRef->columnName);
                this->context.registerPrefixColumn(colCpp, this->context.syntheticColumnCppType(colCpp));
                std::string code;
                if(this->context.useCpp20TableAliasStyle()) {
                    code = info.ormAliasType + "->*&" + info.baseStructName + "::" + colCpp;
                } else {
                    code = "alias_column<" + info.ormAliasType + ">(&" + info.baseStructName + "::" + colCpp + ")";
                }
                return CodeGenResult{std::move(code), {}, std::move(qualWarnings)};
            }
            std::string structForColumn = toStructName(qualifiedRef->tableName);
            auto aliasIt = this->context.fromTableAliasToStructName.find(tableKey);
            if(aliasIt != this->context.fromTableAliasToStructName.end()) {
                structForColumn = aliasIt->second;
            }
            const std::string colCpp = toCppIdentifier(qualifiedRef->columnName);
            this->context.registerPrefixColumn(colCpp, this->context.syntheticColumnCppType(colCpp));
            std::string memberPointer = "&" + structForColumn + "::" + toCppIdentifier(qualifiedRef->columnName);
            std::string columnPointer = "column<" + structForColumn + ">(" + memberPointer + ")";
            const bool useQColPtr =
                policyEquals(this->context.codeGenPolicy, "column_ref_style", "column_pointer");
            const std::string& emittedQ = useQColPtr ? columnPointer : memberPointer;
            const std::string chosenQ = useQColPtr ? "column_pointer" : "member_pointer";
            return CodeGenResult{
                emittedQ,
                {DecisionPoint{this->context.nextDecisionPointId++,
                               "column_ref_style",
                               chosenQ,
                               emittedQ,
                               {Alternative{"column_pointer", columnPointer,
                                            "explicit mapped type (inheritance / ambiguity)"}}}},
                std::move(qualWarnings)};
        } else if(auto* qualifiedAsterisk = dynamic_cast<const QualifiedAsteriskNode*>(&astNode)) {
            std::vector<std::string> qualifiedAsteriskWarnings;
            if(qualifiedAsterisk->schemaName) {
                qualifiedAsteriskWarnings.push_back(
                    "schema-qualified SELECT result column " + *qualifiedAsterisk->schemaName + "." +
                    qualifiedAsterisk->tableName +
                    ".* is not represented in sqlite_orm; generated code uses asterisk<" +
                    toStructName(qualifiedAsterisk->tableName) + ">() (table type only)");
            }
            auto tableAliasIt = this->context.activeTableAliases.find(qualifiedAsterisk->tableName);
            if(tableAliasIt != this->context.activeTableAliases.end()) {
                return CodeGenResult{"asterisk<" + tableAliasIt->second.ormAliasType + ">()", {},
                                     std::move(qualifiedAsteriskWarnings)};
            }
            return CodeGenResult{"asterisk<" + toStructName(qualifiedAsterisk->tableName) + ">()", {},
                                 std::move(qualifiedAsteriskWarnings)};
        } else if(auto* newRef = dynamic_cast<const NewRefNode*>(&astNode)) {
            auto cppName = toCppIdentifier(newRef->columnName);
            this->context.registerColumn(cppName, defaultCppTypeForSyntheticColumn(cppName));
            return CodeGenResult{"new_(&" + this->context.structName + "::" + cppName + ")", {}};
        } else if(auto* oldRef = dynamic_cast<const OldRefNode*>(&astNode)) {
            auto cppName = toCppIdentifier(oldRef->columnName);
            this->context.registerColumn(cppName, defaultCppTypeForSyntheticColumn(cppName));
            return CodeGenResult{"old(&" + this->context.structName + "::" + cppName + ")", {}};
        } else if(auto* excludedRef = dynamic_cast<const ExcludedRefNode*>(&astNode)) {
            auto cppName = toCppIdentifier(excludedRef->columnName);
            return CodeGenResult{"excluded(&" + this->context.structName + "::" + cppName + ")", {}};
        } else if(auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&astNode)) {
            if(binaryOp->binaryOperator == BinaryOperator::isOp ||
               binaryOp->binaryOperator == BinaryOperator::isNot ||
               binaryOp->binaryOperator == BinaryOperator::isDistinctFrom ||
               binaryOp->binaryOperator == BinaryOperator::isNotDistinctFrom) {
                this->context.accumulatedErrors.push_back(
                    "binary IS / IS NOT / IS [NOT] DISTINCT FROM "
                    "is not supported in sqlite_orm");
                return CodeGenResult{"/* unsupported IS expression */"};
            }
            auto leftResult = this->coordinator.generateNode(*binaryOp->lhs);
            auto rightResult = this->coordinator.generateNode(*binaryOp->rhs);

            if(auto* leftCol = dynamic_cast<const ColumnRefNode*>(binaryOp->lhs.get())) {
                this->context.registerPrefixColumn(toCppIdentifier(leftCol->columnName),
                                                   this->context.inferTypeFromNode(*binaryOp->rhs));
            }
            if(auto* rightCol = dynamic_cast<const ColumnRefNode*>(binaryOp->rhs.get())) {
                this->context.registerPrefixColumn(toCppIdentifier(rightCol->columnName),
                                                   this->context.inferTypeFromNode(*binaryOp->lhs));
            }

            auto decisionPoints = std::move(leftResult.decisionPoints);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(rightResult.decisionPoints.begin()),
                                  std::make_move_iterator(rightResult.decisionPoints.end()));

            bool leftLeaf = isLeafNode(*binaryOp->lhs);
            bool rightLeaf = isLeafNode(*binaryOp->rhs);

            auto nodeIsNoWrapRef = [&](const AstNode* node) -> bool {
                if(auto* qr = dynamic_cast<const QualifiedColumnRefNode*>(node)) {
                    if(this->context.activeTableAliases.find(std::string(qr->tableName)) !=
                       this->context.activeTableAliases.end())
                        return true;
                    if(this->context.withCteCpp20Monikers()) {
                        auto key = normalizeSqlIdentifier(qr->tableName);
                        if(this->context.activeCteTypedefByTableKey.find(key) !=
                           this->context.activeCteTypedefByTableKey.end())
                            return true;
                    }
                }
                if(dynamic_cast<const ColumnRefNode*>(node)) {
                    if(this->context.withCteCpp20Monikers() && this->context.implicitSingleSourceCteTypedef)
                        return true;
                }
                return false;
            };
            bool leftNoWrap = false;
            if(auto* leftCol = dynamic_cast<const ColumnRefNode*>(binaryOp->lhs.get())) {
                leftNoWrap = this->context.columnRefIsSelectAliasNoWrap(*leftCol);
            }
            if(nodeIsNoWrapRef(binaryOp->lhs.get())) {
                leftNoWrap = true;
            }
            bool rightNoWrap = false;
            if(auto* rightCol = dynamic_cast<const ColumnRefNode*>(binaryOp->rhs.get())) {
                rightNoWrap = this->context.columnRefIsSelectAliasNoWrap(*rightCol);
            }
            if(nodeIsNoWrapRef(binaryOp->rhs.get())) {
                rightNoWrap = true;
            }

            std::string wrappedLeft = (leftLeaf && !leftNoWrap) ? wrap(leftResult.code) : leftResult.code;
            std::string wrappedRight = (rightLeaf && !rightNoWrap) ? wrap(rightResult.code) : rightResult.code;

            auto funcName = binaryFunctionalName(binaryOp->binaryOperator);
            std::string functionalCode =
                std::string(funcName) + "(" + leftResult.code + ", " + rightResult.code + ")";

            auto op = binaryOperatorString(binaryOp->binaryOperator);
            std::string wrapLeftCode = wrappedLeft + std::string(op) + rightResult.code;
            std::string wrapRightCode = leftResult.code + std::string(op) + wrappedRight;
            std::string wrapBothCode = wrappedLeft + std::string(op) + wrappedRight;

            std::string chosenExprVal = "operator_wrap_left";
            std::string emittedExpr = wrapLeftCode;
            if(policyEquals(this->context.codeGenPolicy, "expr_style", "operator_wrap_right")) {
                chosenExprVal = "operator_wrap_right";
                emittedExpr = wrapRightCode;
            } else if(policyEquals(this->context.codeGenPolicy, "expr_style", "functional")) {
                chosenExprVal = "functional";
                emittedExpr = functionalCode;
            } else if(policyEquals(this->context.codeGenPolicy, "expr_style", "operator_wrap_both")) {
                chosenExprVal = "operator_wrap_both";
                emittedExpr = wrapBothCode;
            }
            if(binaryOp->binaryOperator == BinaryOperator::jsonArrow ||
               binaryOp->binaryOperator == BinaryOperator::jsonArrow2) {
                chosenExprVal = "functional";
                emittedExpr = functionalCode;
            }

            decisionPoints.push_back(DecisionPoint{
                this->context.nextDecisionPointId++,
                "expr_style",
                chosenExprVal,
                emittedExpr,
                {
                    Alternative{"operator_wrap_right", wrapRightCode, "wrap right operand"},
                    Alternative{"functional", functionalCode, "functional style"},
                    Alternative{"operator_wrap_both", wrapBothCode, "wrap both operands", true},
                }});

            std::vector<std::string> binWarnings;
            binWarnings.insert(binWarnings.end(),
                               std::make_move_iterator(leftResult.warnings.begin()),
                               std::make_move_iterator(leftResult.warnings.end()));
            binWarnings.insert(binWarnings.end(),
                               std::make_move_iterator(rightResult.warnings.begin()),
                               std::make_move_iterator(rightResult.warnings.end()));

            if(binaryOp->binaryOperator == BinaryOperator::jsonArrow ||
               binaryOp->binaryOperator == BinaryOperator::jsonArrow2) {
                binWarnings.push_back("JSON -> / ->> operator is mapped to json_extract() "
                                      "— return type may differ from sqlite");
            }

            std::vector<std::string> binComments;
            appendUniqueStrings(binComments, leftResult.comments);
            appendUniqueStrings(binComments, rightResult.comments);
            return CodeGenResult{std::move(emittedExpr), std::move(decisionPoints), std::move(binWarnings), {},
                                 std::move(binComments)};
        } else if(auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*unaryOp->operand);
            auto decisionPoints = std::move(operandResult.decisionPoints);

            if(unaryOp->unaryOperator == UnaryOperator::plus) {
                return CodeGenResult{operandResult.code, std::move(decisionPoints), {}, {},
                                     std::move(operandResult.comments)};
            }

            bool operandLeaf = isLeafNode(*unaryOp->operand);
            bool operandNoWrap = false;
            if(auto* opCol = dynamic_cast<const ColumnRefNode*>(unaryOp->operand.get())) {
                operandNoWrap = this->context.columnRefIsSelectAliasNoWrap(*opCol);
                if(this->context.withCteCpp20Monikers() && this->context.implicitSingleSourceCteTypedef) {
                    operandNoWrap = true;
                }
            }
            if(auto* opQCol = dynamic_cast<const QualifiedColumnRefNode*>(unaryOp->operand.get())) {
                if(this->context.activeTableAliases.find(std::string(opQCol->tableName)) !=
                   this->context.activeTableAliases.end()) {
                    operandNoWrap = true;
                }
                if(this->context.withCteCpp20Monikers()) {
                    auto key = normalizeSqlIdentifier(opQCol->tableName);
                    if(this->context.activeCteTypedefByTableKey.find(key) !=
                       this->context.activeCteTypedefByTableKey.end()) {
                        operandNoWrap = true;
                    }
                }
            }
            std::string operandStr;
            if(operandLeaf && !operandNoWrap) {
                operandStr = wrap(operandResult.code);
            } else if(!operandLeaf) {
                operandStr = "(" + operandResult.code + ")";
            } else {
                operandStr = operandResult.code;
            }

            std::string_view unaryFuncName;
            std::string_view opStr;
            if(unaryOp->unaryOperator == UnaryOperator::minus) {
                unaryFuncName = "minus";
                opStr = "-";
            } else if(unaryOp->unaryOperator == UnaryOperator::bitwiseNot) {
                unaryFuncName = "bitwise_not";
                opStr = "~";
            } else {
                unaryFuncName = "not_";
                opStr = "not ";
            }
            std::string operatorCode = std::string(opStr) + operandStr;
            std::string functionalCode = std::string(unaryFuncName) + "(" + operandResult.code + ")";

            std::vector<Alternative> alternatives;
            if(unaryOp->unaryOperator == UnaryOperator::logicalNot) {
                alternatives.push_back(Alternative{"operator_excl", "!" + operandStr, "use ! instead of not"});
            }
            alternatives.push_back(Alternative{"functional", functionalCode, "functional style"});

            std::string chosenUnaryVal = "operator";
            std::string emittedUnary = operatorCode;
            if(policyEquals(this->context.codeGenPolicy, "expr_style", "functional")) {
                chosenUnaryVal = "functional";
                emittedUnary = functionalCode;
            } else if(policyEquals(this->context.codeGenPolicy, "expr_style", "operator_excl") &&
                      unaryOp->unaryOperator == UnaryOperator::logicalNot) {
                chosenUnaryVal = "operator_excl";
                emittedUnary = "!" + operandStr;
            }

            decisionPoints.push_back(
                DecisionPoint{this->context.nextDecisionPointId++, "expr_style", chosenUnaryVal, emittedUnary,
                              std::move(alternatives)});

            return CodeGenResult{std::move(emittedUnary), std::move(decisionPoints), {}, {},
                                 std::move(operandResult.comments)};
        } else if(auto* isNullNode = dynamic_cast<const IsNullNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*isNullNode->operand);
            return CodeGenResult{"is_null(" + operandResult.code + ")", std::move(operandResult.decisionPoints), {},
                                 {}, std::move(operandResult.comments)};
        } else if(auto* isNotNullNode = dynamic_cast<const IsNotNullNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*isNotNullNode->operand);
            return CodeGenResult{"is_not_null(" + operandResult.code + ")",
                                 std::move(operandResult.decisionPoints), {}, {},
                                 std::move(operandResult.comments)};
        } else if(auto* betweenNode = dynamic_cast<const BetweenNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*betweenNode->operand);
            auto lowResult = this->coordinator.generateNode(*betweenNode->low);
            auto highResult = this->coordinator.generateNode(*betweenNode->high);

            if(auto* col = dynamic_cast<const ColumnRefNode*>(betweenNode->operand.get())) {
                this->context.registerPrefixColumn(toCppIdentifier(col->columnName),
                                                   this->context.inferTypeFromNode(*betweenNode->low));
            }

            auto decisionPoints = std::move(operandResult.decisionPoints);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(lowResult.decisionPoints.begin()),
                                  std::make_move_iterator(lowResult.decisionPoints.end()));
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(highResult.decisionPoints.begin()),
                                  std::make_move_iterator(highResult.decisionPoints.end()));

            std::string betweenCode =
                "between(" + operandResult.code + ", " + lowResult.code + ", " + highResult.code + ")";
            std::string code = betweenNode->negated ? "not_(" + betweenCode + ")" : betweenCode;
            return CodeGenResult{code, std::move(decisionPoints)};
        } else if(auto* subqueryNode = dynamic_cast<const SubqueryNode*>(&astNode)) {
            auto sub = this->coordinator.tryCodegenSelectLikeSubquery(*subqueryNode->select);
            if(sub.code.empty()) {
                sub.warnings.insert(sub.warnings.begin(),
                                    "scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen");
                return CodeGenResult{"/* (SELECT ...) */", std::move(sub.decisionPoints), std::move(sub.warnings)};
            }
            return CodeGenResult{sub.code, std::move(sub.decisionPoints), std::move(sub.warnings)};
        } else if(auto* existsNode = dynamic_cast<const ExistsNode*>(&astNode)) {
            auto sub = this->coordinator.tryCodegenSelectLikeSubquery(*existsNode->select);
            if(sub.code.empty()) {
                sub.warnings.insert(sub.warnings.begin(),
                                    "EXISTS (SELECT ...) is not mapped to sqlite_orm codegen");
                return CodeGenResult{"/* EXISTS (SELECT ...) */", std::move(sub.decisionPoints),
                                     std::move(sub.warnings)};
            }
            return CodeGenResult{"exists(" + sub.code + ")", std::move(sub.decisionPoints),
                                 std::move(sub.warnings)};
        } else if(auto* inNode = dynamic_cast<const InNode*>(&astNode)) {
            if(!inNode->tableName.empty()) {
                auto operandResult = this->coordinator.generateNode(*inNode->operand);
                const std::string normalizedTableKey = normalizeSqlIdentifier(inNode->tableName);
                auto cteIt = this->context.activeCteTypedefByTableKey.find(normalizedTableKey);
                if(cteIt != this->context.activeCteTypedefByTableKey.end()) {
                    const std::string& cteTypedef = cteIt->second;
                    auto colNamesIt = this->context.cteColumnNamesByTableKey.find(normalizedTableKey);
                    std::string selectColumnCode;
                    if(colNamesIt != this->context.cteColumnNamesByTableKey.end() &&
                       !colNamesIt->second.empty()) {
                        const auto& columnNames = colNamesIt->second;
                        if(this->context.withCteCpp20Monikers()) {
                            auto monikerIt =
                                this->context.withCteCpp20MonikerVarByCteKey.find(normalizedTableKey);
                            std::string monikerVar =
                                (monikerIt != this->context.withCteCpp20MonikerVarByCteKey.end())
                                    ? monikerIt->second
                                    : cteTypedef;
                            if(columnNames.size() == 1) {
                                const std::string colKey =
                                    normalizedTableKey + "|" + normalizeSqlIdentifier(columnNames[0]);
                                auto colVarIt = this->context.withCteCpp20ColVarByPipeKey.find(colKey);
                                if(colVarIt != this->context.withCteCpp20ColVarByPipeKey.end()) {
                                    selectColumnCode = monikerVar + "->*" + colVarIt->second;
                                } else {
                                    auto baseIt = this->context.cteBaseStructByKey.find(normalizedTableKey);
                                    std::string baseStruct =
                                        baseIt != this->context.cteBaseStructByKey.end()
                                            ? baseIt->second
                                            : this->context.structName;
                                    selectColumnCode = monikerVar + "->*&" + baseStruct +
                                                       "::" + toCppIdentifier(columnNames[0]);
                                }
                            } else {
                                std::string cols;
                                for(size_t columnIndex = 0; columnIndex < columnNames.size(); ++columnIndex) {
                                    if(columnIndex > 0) cols += ", ";
                                    const std::string colKey = normalizedTableKey + "|" +
                                                               normalizeSqlIdentifier(columnNames[columnIndex]);
                                    auto colVarIt = this->context.withCteCpp20ColVarByPipeKey.find(colKey);
                                    if(colVarIt != this->context.withCteCpp20ColVarByPipeKey.end()) {
                                        cols += monikerVar + "->*" + colVarIt->second;
                                    } else {
                                        auto baseIt = this->context.cteBaseStructByKey.find(normalizedTableKey);
                                        std::string baseStruct =
                                            baseIt != this->context.cteBaseStructByKey.end()
                                                ? baseIt->second
                                                : this->context.structName;
                                        cols += monikerVar + "->*&" + baseStruct +
                                                "::" + toCppIdentifier(columnNames[columnIndex]);
                                    }
                                }
                                selectColumnCode = "columns(" + cols + ")";
                            }
                        } else {
                            if(columnNames.size() == 1) {
                                selectColumnCode =
                                    "column<" + cteTypedef + ">(" +
                                    identifierToCppStringLiteral(columnNames[0]) + ")";
                            } else {
                                std::string cols;
                                for(size_t columnIndex = 0; columnIndex < columnNames.size(); ++columnIndex) {
                                    if(columnIndex > 0) cols += ", ";
                                    cols += "column<" + cteTypedef + ">(" +
                                            identifierToCppStringLiteral(columnNames[columnIndex]) + ")";
                                }
                                selectColumnCode = "columns(" + cols + ")";
                            }
                        }
                    } else {
                        selectColumnCode = "asterisk<" + cteTypedef + ">()";
                    }
                    std::string inFunc = inNode->negated ? "not_in" : "in";
                    std::string code =
                        inFunc + "(" + operandResult.code + ", select(" + selectColumnCode + "))";
                    return CodeGenResult{std::move(code), std::move(operandResult.decisionPoints),
                                         std::move(operandResult.warnings)};
                }
                operandResult.warnings.push_back("IN table-name is not supported in sqlite_orm codegen");
                return CodeGenResult{"/* " + operandResult.code + " IN " + inNode->tableName + " */",
                                     std::move(operandResult.decisionPoints),
                                     std::move(operandResult.warnings)};
            }
            if(inNode->subquerySelect) {
                auto operandResult = this->coordinator.generateNode(*inNode->operand);
                auto sub = this->coordinator.tryCodegenSelectLikeSubquery(*inNode->subquerySelect);
                auto decisionPoints = std::move(operandResult.decisionPoints);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(sub.decisionPoints.begin()),
                                      std::make_move_iterator(sub.decisionPoints.end()));
                std::vector<std::string> inSubWarnings;
                inSubWarnings.insert(inSubWarnings.end(),
                                     std::make_move_iterator(operandResult.warnings.begin()),
                                     std::make_move_iterator(operandResult.warnings.end()));
                inSubWarnings.insert(inSubWarnings.end(),
                                     std::make_move_iterator(sub.warnings.begin()),
                                     std::make_move_iterator(sub.warnings.end()));
                if(sub.code.empty()) {
                    inSubWarnings.push_back("IN (SELECT ...) is not mapped to sqlite_orm codegen");
                    return CodeGenResult{"/* IN (SELECT ...) */", std::move(decisionPoints),
                                         std::move(inSubWarnings)};
                }
                std::string code = inNode->negated
                                       ? "not_in(" + operandResult.code + ", " + sub.code + ")"
                                       : "in(" + operandResult.code + ", " + sub.code + ")";
                return CodeGenResult{code, std::move(decisionPoints), std::move(inSubWarnings)};
            }
            auto operandResult = this->coordinator.generateNode(*inNode->operand);
            auto decisionPoints = std::move(operandResult.decisionPoints);

            if(auto* col = dynamic_cast<const ColumnRefNode*>(inNode->operand.get())) {
                if(!inNode->values.empty()) {
                    this->context.registerPrefixColumn(toCppIdentifier(col->columnName),
                                                       this->context.inferTypeFromNode(*inNode->values.at(0)));
                }
            }

            std::string valuesList;
            for(size_t valueIndex = 0; valueIndex < inNode->values.size(); ++valueIndex) {
                auto valueResult = this->coordinator.generateNode(*inNode->values.at(valueIndex));
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(valueResult.decisionPoints.begin()),
                                      std::make_move_iterator(valueResult.decisionPoints.end()));
                if(valueIndex > 0)
                    valuesList += ", ";
                valuesList += valueResult.code;
            }

            std::string inCode = "in(" + operandResult.code + ", {" + valuesList + "})";
            if(inNode->negated) {
                std::string notInCode = "not_in(" + operandResult.code + ", {" + valuesList + "})";
                decisionPoints.push_back(
                    DecisionPoint{this->context.nextDecisionPointId++, "negation_style", "not_in", notInCode,
                                  {Alternative{"not_wrapper", "not_(" + inCode + ")", "use not_() wrapper"}}});
                return CodeGenResult{notInCode, std::move(decisionPoints)};
            }
            return CodeGenResult{inCode, std::move(decisionPoints)};
        } else if(auto* likeNode = dynamic_cast<const LikeNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*likeNode->operand);
            auto patternResult = this->coordinator.generateNode(*likeNode->pattern);

            if(auto* col = dynamic_cast<const ColumnRefNode*>(likeNode->operand.get())) {
                this->context.registerPrefixColumn(toCppIdentifier(col->columnName), "std::string");
            }

            auto decisionPoints = std::move(operandResult.decisionPoints);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(patternResult.decisionPoints.begin()),
                                  std::make_move_iterator(patternResult.decisionPoints.end()));

            std::string likeCode = "like(" + operandResult.code + ", " + patternResult.code;
            if(likeNode->escape) {
                auto escapeResult = this->coordinator.generateNode(*likeNode->escape);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(escapeResult.decisionPoints.begin()),
                                      std::make_move_iterator(escapeResult.decisionPoints.end()));
                likeCode += ", " + escapeResult.code;
            }
            likeCode += ")";

            std::string code = likeNode->negated ? "not_(" + likeCode + ")" : likeCode;
            return CodeGenResult{code, std::move(decisionPoints)};
        } else if(auto* globNode = dynamic_cast<const GlobNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*globNode->operand);
            auto patternResult = this->coordinator.generateNode(*globNode->pattern);

            if(auto* col = dynamic_cast<const ColumnRefNode*>(globNode->operand.get())) {
                this->context.registerPrefixColumn(toCppIdentifier(col->columnName), "std::string");
            }

            auto decisionPoints = std::move(operandResult.decisionPoints);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(patternResult.decisionPoints.begin()),
                                  std::make_move_iterator(patternResult.decisionPoints.end()));

            std::string globCode = "glob(" + operandResult.code + ", " + patternResult.code + ")";
            std::string code = globNode->negated ? "not_(" + globCode + ")" : globCode;
            return CodeGenResult{code, std::move(decisionPoints)};
        } else if(auto* castNode = dynamic_cast<const CastNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*castNode->operand);
            std::string cppType = sqliteTypeToCpp(castNode->typeName);
            return CodeGenResult{"cast<" + cppType + ">(" + operandResult.code + ")",
                                 std::move(operandResult.decisionPoints)};
        } else if(auto* caseNode = dynamic_cast<const CaseNode*>(&astNode)) {
            std::vector<DecisionPoint> decisionPoints;
            std::string returnType = "int";
            if(!caseNode->branches.empty()) {
                returnType = this->context.inferTypeFromNode(*caseNode->branches.at(0).result);
            }
            std::string code = "case_<" + returnType + ">(";
            if(caseNode->operand) {
                auto operandResult = this->coordinator.generateNode(*caseNode->operand);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(operandResult.decisionPoints.begin()),
                                      std::make_move_iterator(operandResult.decisionPoints.end()));
                code += operandResult.code;
            }
            code += ")";
            for(auto& branch : caseNode->branches) {
                auto condResult = this->coordinator.generateNode(*branch.condition);
                auto resResult = this->coordinator.generateNode(*branch.result);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(condResult.decisionPoints.begin()),
                                      std::make_move_iterator(condResult.decisionPoints.end()));
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(resResult.decisionPoints.begin()),
                                      std::make_move_iterator(resResult.decisionPoints.end()));
                code += ".when(" + condResult.code + ", then(" + resResult.code + "))";
            }
            if(caseNode->elseResult) {
                auto elseResult = this->coordinator.generateNode(*caseNode->elseResult);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(elseResult.decisionPoints.begin()),
                                      std::make_move_iterator(elseResult.decisionPoints.end()));
                code += ".else_(" + elseResult.code + ")";
            }
            code += ".end()";
            return CodeGenResult{code, std::move(decisionPoints)};
        } else if(auto* bindParam = dynamic_cast<const BindParameterNode*>(&astNode)) {
            std::string paramStr(bindParam->value);
            std::string cppVar;
            if(paramStr.size() > 1 && (paramStr[0] == ':' || paramStr[0] == '@' || paramStr[0] == '$')) {
                cppVar = toCppIdentifier(paramStr.substr(1));
            } else if(paramStr == "?") {
                cppVar = "bindParam" + std::to_string(++this->context.nextBindParamIndex);
            } else if(paramStr.size() > 1 && paramStr[0] == '?') {
                cppVar = "bindParam" + paramStr.substr(1);
            } else {
                cppVar = "/* " + paramStr + " */";
            }
            return CodeGenResult{cppVar, {},
                                 {"bind parameter " + paramStr + " -> C++ variable '" + cppVar +
                                  "'; for prepared statements use storage.prepare() + get<N>(stmt)"}};
        } else if(auto* collateNode = dynamic_cast<const CollateNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*collateNode->operand);
            operandResult.warnings.push_back("COLLATE " + collateNode->collationName +
                                              " on expressions is not directly supported in sqlite_orm codegen");
            return operandResult;
        } else if(auto* funcCall = dynamic_cast<const FunctionCallNode*>(&astNode)) {
            std::string funcName = toLowerAscii(funcCall->name);
            std::vector<DecisionPoint> decisionPoints;
            std::vector<std::string> funcWarnings;
            std::string baseCode;

            if(funcCall->star) {
                if(funcName == "count" && !this->context.fromTableAliasToStructName.empty()) {
                    const std::string& countRowType = this->context.implicitSingleSourceCteTypedef
                                                          ? *this->context.implicitSingleSourceCteTypedef
                                                          : this->context.structName;
                    baseCode = "count<" + countRowType + ">()";
                } else {
                    baseCode = funcName + "()";
                }
            } else {
                std::string argList;
                for(size_t argIndex = 0; argIndex < funcCall->arguments.size(); ++argIndex) {
                    auto argResult = this->coordinator.generateNode(*funcCall->arguments.at(argIndex));
                    decisionPoints.insert(decisionPoints.end(),
                                          std::make_move_iterator(argResult.decisionPoints.begin()),
                                          std::make_move_iterator(argResult.decisionPoints.end()));
                    funcWarnings.insert(funcWarnings.end(),
                                        std::make_move_iterator(argResult.warnings.begin()),
                                        std::make_move_iterator(argResult.warnings.end()));
                    if(argIndex > 0)
                        argList += ", ";
                    argList += argResult.code;
                }
                if(!funcCall->star && !funcCall->arguments.empty() &&
                   sqliteScalarFirstArgTextContext(funcName)) {
                    if(auto* col = dynamic_cast<const ColumnRefNode*>(funcCall->arguments.at(0).get())) {
                        this->context.registerPrefixColumn(toCppIdentifier(col->columnName), "std::string");
                    }
                }
                if(funcCall->distinct && !argList.empty()) {
                    baseCode = funcName + "(distinct(" + argList + "))";
                } else {
                    baseCode = funcName + "(" + argList + ")";
                }
            }

            if(funcCall->filterWhere) {
                auto filterResult = this->coordinator.generateNode(*funcCall->filterWhere);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(filterResult.decisionPoints.begin()),
                                      std::make_move_iterator(filterResult.decisionPoints.end()));
                funcWarnings.insert(funcWarnings.end(),
                                    std::make_move_iterator(filterResult.warnings.begin()),
                                    std::make_move_iterator(filterResult.warnings.end()));
                baseCode += ".filter(where(" + filterResult.code + "))";
            }
            if(funcCall->over) {
                std::string overArgs = this->codegenOverClause(*funcCall->over, decisionPoints, funcWarnings);
                if(overArgs.empty()) {
                    funcWarnings.push_back(
                        "OVER clause could not be mapped to sqlite_orm (.over(...)); emitted bare function only");
                } else {
                    baseCode += ".over(" + overArgs + ")";
                }
            }
            return CodeGenResult{std::move(baseCode), std::move(decisionPoints), std::move(funcWarnings)};
        }
        return CodeGenResult{};
    }

    std::string ExpressionCodeGenerator::codegenWindowFrameBound(const WindowFrameBound& bound,
                                                                 std::vector<DecisionPoint>& decisionPoints,
                                                                 std::vector<std::string>& warnings) {
        switch(bound.kind) {
        case WindowFrameBoundKind::unboundedPreceding:
            return "unbounded_preceding()";
        case WindowFrameBoundKind::currentRow:
            return "current_row()";
        case WindowFrameBoundKind::unboundedFollowing:
            return "unbounded_following()";
        case WindowFrameBoundKind::exprPreceding:
        case WindowFrameBoundKind::exprFollowing: {
            if(!bound.expr) {
                return {};
            }
            auto expressionResult = this->coordinator.generateNode(*bound.expr);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(expressionResult.decisionPoints.begin()),
                                  std::make_move_iterator(expressionResult.decisionPoints.end()));
            warnings.insert(warnings.end(), std::make_move_iterator(expressionResult.warnings.begin()),
                            std::make_move_iterator(expressionResult.warnings.end()));
            if(bound.kind == WindowFrameBoundKind::exprPreceding) {
                return "preceding(" + expressionResult.code + ")";
            }
            return "following(" + expressionResult.code + ")";
        }
        }
        return {};
    }

    std::string ExpressionCodeGenerator::codegenWindowFrameSpec(const WindowFrameSpec& frame,
                                                                std::vector<DecisionPoint>& decisionPoints,
                                                                std::vector<std::string>& warnings) {
        std::string_view frameApi;
        switch(frame.unit) {
        case WindowFrameUnit::rows:
            frameApi = "rows";
            break;
        case WindowFrameUnit::range:
            frameApi = "range";
            break;
        case WindowFrameUnit::groups:
            frameApi = "groups";
            break;
        }
        std::string start = this->codegenWindowFrameBound(frame.start, decisionPoints, warnings);
        std::string end = this->codegenWindowFrameBound(frame.end, decisionPoints, warnings);
        if(start.empty() || end.empty()) {
            return {};
        }
        std::string core = std::string(frameApi) + "(" + start + ", " + end + ")";
        switch(frame.exclude) {
        case WindowFrameExcludeKind::currentRow:
            core += ".exclude_current_row()";
            break;
        case WindowFrameExcludeKind::group:
            core += ".exclude_group()";
            break;
        case WindowFrameExcludeKind::ties:
            core += ".exclude_ties()";
            break;
        case WindowFrameExcludeKind::none:
            break;
        }
        return core;
    }

    std::string ExpressionCodeGenerator::codegenOverClause(const OverClause& overClause,
                                                           std::vector<DecisionPoint>& decisionPoints,
                                                           std::vector<std::string>& warnings) {
        if(overClause.namedWindow) {
            return "window_ref(" + identifierToCppStringLiteral(*overClause.namedWindow) + ")";
        }
        std::vector<std::string> parts;
        if(!overClause.partitionBy.empty()) {
            std::string inner;
            for(size_t partitionByIndex = 0; partitionByIndex < overClause.partitionBy.size(); ++partitionByIndex) {
                if(partitionByIndex > 0) {
                    inner += ", ";
                }
                auto partitionResult = this->coordinator.generateNode(*overClause.partitionBy.at(partitionByIndex));
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(partitionResult.decisionPoints.begin()),
                                      std::make_move_iterator(partitionResult.decisionPoints.end()));
                warnings.insert(warnings.end(), std::make_move_iterator(partitionResult.warnings.begin()),
                                std::make_move_iterator(partitionResult.warnings.end()));
                inner += partitionResult.code;
            }
            parts.push_back("partition_by(" + inner + ")");
        }
        if(!overClause.orderBy.empty()) {
            auto formatOrderTerm = [&](const OrderByTerm& term) -> std::string {
                auto expressionResult = this->coordinator.generateNode(*term.expression);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(expressionResult.decisionPoints.begin()),
                                      std::make_move_iterator(expressionResult.decisionPoints.end()));
                warnings.insert(warnings.end(), std::make_move_iterator(expressionResult.warnings.begin()),
                                std::make_move_iterator(expressionResult.warnings.end()));
                std::string termCode = "order_by(" + expressionResult.code + ")";
                if(term.direction == SortDirection::asc) {
                    termCode += ".asc()";
                } else if(term.direction == SortDirection::desc) {
                    termCode += ".desc()";
                }
                if(!term.collation.empty()) {
                    std::string collLower = toLowerAscii(term.collation);
                    if(collLower == "nocase") {
                        termCode += ".collate_nocase()";
                    } else if(collLower == "binary") {
                        termCode += ".collate_binary()";
                    } else if(collLower == "rtrim") {
                        termCode += ".collate_rtrim()";
                    } else {
                        termCode += ".collate(" + identifierToCppStringLiteral(term.collation) + ")";
                        warnings.push_back(
                            "COLLATE " + term.collation +
                            " in window ORDER BY is not a built-in collation; generated .collate(...) uses literal name");
                    }
                }
                return termCode;
            };
            if(overClause.orderBy.size() == 1) {
                parts.push_back(formatOrderTerm(overClause.orderBy.at(0)));
            } else {
                std::string multi = "multi_order_by(";
                for(size_t orderIndex = 0; orderIndex < overClause.orderBy.size(); ++orderIndex) {
                    if(orderIndex > 0) {
                        multi += ", ";
                    }
                    multi += formatOrderTerm(overClause.orderBy.at(orderIndex));
                }
                multi += ")";
                parts.push_back(std::move(multi));
            }
        }
        if(overClause.frame) {
            std::string frameCode = this->codegenWindowFrameSpec(*overClause.frame, decisionPoints, warnings);
            if(!frameCode.empty()) {
                parts.push_back(std::move(frameCode));
            }
        }
        if(parts.empty()) {
            return {};
        }
        std::string joined = parts.at(0);
        for(size_t partIndex = 1; partIndex < parts.size(); ++partIndex) {
            joined += ", ";
            joined += parts.at(partIndex);
        }
        return joined;
    }

}  // namespace sqlite2orm
