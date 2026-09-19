#include "codegen_expression.h"
#include "codegen_context.h"
#include "codegen_utils.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>
#include <sqlite2orm/validator.h>

namespace sqlite2orm {

    namespace {
        /**
         *  `code`, delimited with a CAST to INTEGER when the argument it was generated from stands
         *  for an AND or an OR, and the comment that explains the CAST appended to `comments`. A
         *  predicate serializer parenthesizes no argument of its own, and SQLite binds AND and OR
         *  looser than every predicate, so a bare one there would take the predicate into itself.
         */
        std::string groupPredicateArgument(std::string code, const AstNode& argumentNode,
                                           std::vector<std::string>& comments) {
            if(!predicateArgumentNeedsGroupingCast(argumentNode)) {
                return code;
            }
            appendUniqueString(comments, kCommentAndOrPredicateArgumentCast);
            return "cast<" + sqliteTypeToCpp("INTEGER") + ">(" + code + ")";
        }
    }

    ExpressionCodeGenerator::ExpressionCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context)
        : coordinator(coordinator), context(context) {}

    CodeGenResult ExpressionCodeGenerator::generateExpression(const AstNode& astNode) {
        if(auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&astNode)) {
            if(hexLiteralExceedsInt64(integerLiteral->value)) {
                // SQLite raises `hex literal too big` in `codeInteger()`, i.e. when it compiles
                // the expression, so `SELECT 0x10000000000000000` is refused while a DEFAULT, a
                // CHECK or a view body keeps the literal and only a use of them fails. C++ has no
                // literal for it either, so a stored clause hands it to the generator that owns
                // the clause, which leaves the clause out of the generated code.
                std::string literal = withoutDigitSeparators(integerLiteral->value);
                if(this->context.storedExpression) {
                    this->context.storedHexLiteralsTooBig.push_back(std::move(literal));
                } else {
                    this->context.accumulatedErrors.push_back("hex literal too big: " + std::move(literal));
                }
                return CodeGenResult{{}, {}};
            }
            return CodeGenResult{integerLiteralToCpp(integerLiteral->value), {}};
        } else if(auto* realLiteral = dynamic_cast<const RealLiteralNode*>(&astNode)) {
            return CodeGenResult{numericLiteralToCpp(realLiteral->value), {}};
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
            std::vector<CodegenWarning> raiseWarnings;
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
                        return CodeGenResult{monIt->second + "->*" + colIt->second, {}, {}, {}};
                    }
                    if(monIt != this->context.withCteCpp20MonikerVarByCteKey.end()) {
                        if(!this->context.isExplicitCteColumn(*this->context.implicitCteFromTableKeyNorm,
                                                              columnRef->columnName)) {
                            auto baseIt =
                                this->context.cteBaseStructByKey.find(*this->context.implicitCteFromTableKeyNorm);
                            if(baseIt != this->context.cteBaseStructByKey.end()) {
                                return CodeGenResult{monIt->second + "->*&" + baseIt->second + "::" + cppName, {}, {}, {}};
                            }
                        }
                    }
                }
                if(this->context.withCteLegacyColalias()) {
                    auto colIt = this->context.withCteLegacyColVarByPipeKey.find(pipe);
                    if(colIt != this->context.withCteLegacyColVarByPipeKey.end()) {
                        std::string cteCol =
                            "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" + colIt->second + ")";
                        return CodeGenResult{std::move(cteCol), {}, {}, {}};
                    }
                }
                {
                    auto indexedIt = this->context.withCteIndexedColVarByPipeKey.find(pipe);
                    if(indexedIt != this->context.withCteIndexedColVarByPipeKey.end()) {
                        std::string cteCol = "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" +
                                             indexedIt->second + ")";
                        return CodeGenResult{std::move(cteCol), {}, {}, {}};
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
                        return CodeGenResult{std::move(cteCol), {}, {}, {}};
                    }
                    if(this->context.isExplicitCteColumn(*this->context.implicitCteFromTableKeyNorm,
                                                         columnRef->columnName)) {
                        std::string colLit =
                            identifierToCppStringLiteral(stripIdentifierQuotes(columnRef->columnName));
                        std::string cteCol =
                            "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" + colLit + ")";
                        return CodeGenResult{std::move(cteCol), {}, {}, {}};
                    }
                    auto baseIt =
                        this->context.cteBaseStructByKey.find(*this->context.implicitCteFromTableKeyNorm);
                    if(baseIt != this->context.cteBaseStructByKey.end()) {
                        std::string cteCol = "column<" + *this->context.implicitSingleSourceCteTypedef + ">(&" +
                                             baseIt->second + "::" + cppName + ")";
                        return CodeGenResult{std::move(cteCol), {}, {}, {}};
                    }
                }
                std::string colLit = identifierToCppStringLiteral(stripIdentifierQuotes(columnRef->columnName));
                std::string cteCol =
                    "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" + colLit + ")";
                return CodeGenResult{std::move(cteCol), {}, {}, {}};
            }
            std::string memberPointer = "&" + this->context.structName + "::" + cppName;
            std::string columnPointer = "column<" + this->context.structName + ">(" + memberPointer + ")";
            this->context.emittedTableTypedColumnRef = true;
            if(this->context.columnRefUnderLogicalNot) {
                // Only the column-pointer form survives under a NOT, so there is no style left to
                // decide between and no decision point to offer.
                this->context.emittedColumnPointerUnderLogicalNot = true;
                return CodeGenResult{std::move(columnPointer), {}, {}, {}, {}};
            }
            const bool useColumnPtr =
                policyEquals(this->context.codeGenPolicy, "column_ref_style", "column_pointer");
            const std::string& emittedCol = useColumnPtr ? columnPointer : memberPointer;
            const std::string chosenCol = useColumnPtr ? "column_pointer" : "member_pointer";
            // options lists every variant (the chosen one included); the consumer decides how to
            // present the selection.
            return CodeGenResult{
                emittedCol,
                {DecisionPoint{this->context.nextDecisionPointId++,
                               "column_ref_style",
                               chosenCol,
                               emittedCol,
                               {Option{"member_pointer", memberPointer, "direct member pointer"},
                                Option{"column_pointer", columnPointer,
                                       "explicit mapped type (inheritance / ambiguity)"}}}},
                {},
                {},
                {}};
        } else if(auto* qualifiedRef = dynamic_cast<const QualifiedColumnRefNode*>(&astNode)) {
            std::vector<CodegenWarning> qualWarnings;
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
                        return CodeGenResult{monIt->second + "->*" + colIt->second, {}, std::move(qualWarnings), {}, {}};
                    }
                    if(monIt != this->context.withCteCpp20MonikerVarByCteKey.end()) {
                        if(!this->context.isExplicitCteColumn(tableKeyNorm, qualifiedRef->columnName)) {
                            auto baseIt = this->context.cteBaseStructByKey.find(tableKeyNorm);
                            if(baseIt != this->context.cteBaseStructByKey.end()) {
                                std::string colCpp = toCppIdentifier(qualifiedRef->columnName);
                                return CodeGenResult{monIt->second + "->*&" + baseIt->second + "::" + colCpp, {},
                                                     std::move(qualWarnings), {}, {}};
                            }
                        }
                    }
                }
                if(this->context.withCteLegacyColalias()) {
                    auto colIt = this->context.withCteLegacyColVarByPipeKey.find(pipe);
                    if(colIt != this->context.withCteLegacyColVarByPipeKey.end()) {
                        return CodeGenResult{"column<" + cteIt->second + ">(" + colIt->second + ")", {},
                                             std::move(qualWarnings), {}, {}};
                    }
                }
                {
                    auto indexedIt = this->context.withCteIndexedColVarByPipeKey.find(pipe);
                    if(indexedIt != this->context.withCteIndexedColVarByPipeKey.end()) {
                        return CodeGenResult{"column<" + cteIt->second + ">(" + indexedIt->second + ")", {},
                                             std::move(qualWarnings), {}, {}};
                    }
                }
                if(this->context.isExplicitCteColumn(tableKeyNorm, qualifiedRef->columnName)) {
                    std::string colLit =
                        identifierToCppStringLiteral(stripIdentifierQuotes(qualifiedRef->columnName));
                return CodeGenResult{"column<" + cteIt->second + ">(" + colLit + ")", {},
                                         std::move(qualWarnings), {}, {}};
                }
                auto baseIt = this->context.cteBaseStructByKey.find(tableKeyNorm);
                if(baseIt != this->context.cteBaseStructByKey.end()) {
                    std::string colCpp = toCppIdentifier(qualifiedRef->columnName);
                    return CodeGenResult{"column<" + cteIt->second + ">(&" + baseIt->second + "::" + colCpp + ")", {},
                                         std::move(qualWarnings), {}, {}};
                }
                std::string colLit = identifierToCppStringLiteral(stripIdentifierQuotes(qualifiedRef->columnName));
                return CodeGenResult{"column<" + cteIt->second + ">(" + colLit + ")", {},
                                         std::move(qualWarnings), {}, {}};
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
                return CodeGenResult{std::move(code), {}, std::move(qualWarnings), {}, {}};
            }
            auto aliasIt = this->context.fromTableAliasToStructName.find(tableKey);
            std::string structForColumn = aliasIt != this->context.fromTableAliasToStructName.end()
                                              ? aliasIt->second
                                              : this->context.structNameForTable(qualifiedRef->tableName);
            const std::string colCpp = toCppIdentifier(qualifiedRef->columnName);
            this->context.registerPrefixColumn(colCpp, this->context.syntheticColumnCppType(colCpp));
            std::string memberPointer = "&" + structForColumn + "::" + toCppIdentifier(qualifiedRef->columnName);
            std::string columnPointer = "column<" + structForColumn + ">(" + memberPointer + ")";
            if(this->context.columnRefUnderLogicalNot) {
                // Only the column-pointer form survives under a NOT, so there is no style left to
                // decide between and no decision point to offer.
                this->context.emittedColumnPointerUnderLogicalNot = true;
                return CodeGenResult{std::move(columnPointer), {}, std::move(qualWarnings), {}, {}};
            }
            const bool useQColPtr =
                policyEquals(this->context.codeGenPolicy, "column_ref_style", "column_pointer");
            const std::string& emittedQ = useQColPtr ? columnPointer : memberPointer;
            const std::string chosenQ = useQColPtr ? "column_pointer" : "member_pointer";
            // options lists every variant (the chosen one included).
            return CodeGenResult{
                emittedQ,
                {DecisionPoint{this->context.nextDecisionPointId++,
                               "column_ref_style",
                               chosenQ,
                               emittedQ,
                               {Option{"member_pointer", memberPointer, "direct member pointer"},
                                Option{"column_pointer", columnPointer,
                                       "explicit mapped type (inheritance / ambiguity)"}}}},
                std::move(qualWarnings), {}, {}};
        } else if(auto* qualifiedAsterisk = dynamic_cast<const QualifiedAsteriskNode*>(&astNode)) {
            std::vector<CodegenWarning> qualifiedAsteriskWarnings;
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
            return CodeGenResult{
                "asterisk<" + this->context.structNameForTable(qualifiedAsterisk->tableName) + ">()", {},
                std::move(qualifiedAsteriskWarnings)};
        } else if(auto* newRef = dynamic_cast<const NewRefNode*>(&astNode)) {
            auto cppName = toCppIdentifier(newRef->columnName);
            this->context.registerColumn(cppName, defaultCppTypeForSyntheticColumn(cppName));
            const std::string& subjectStruct =
                this->context.triggerSubjectStructName.value_or(this->context.structName);
            return CodeGenResult{"new_(&" + subjectStruct + "::" + cppName + ")", {}};
        } else if(auto* oldRef = dynamic_cast<const OldRefNode*>(&astNode)) {
            auto cppName = toCppIdentifier(oldRef->columnName);
            this->context.registerColumn(cppName, defaultCppTypeForSyntheticColumn(cppName));
            const std::string& subjectStruct =
                this->context.triggerSubjectStructName.value_or(this->context.structName);
            return CodeGenResult{"old(&" + subjectStruct + "::" + cppName + ")", {}};
        } else if(auto* excludedRef = dynamic_cast<const ExcludedRefNode*>(&astNode)) {
            auto cppName = toCppIdentifier(excludedRef->columnName);
            return CodeGenResult{"excluded(&" + this->context.structName + "::" + cppName + ")", {}};
        }
        auto nodeGeneratesColumnPointer = [&](const AstNode* node) -> bool {
            if(auto* qualifiedColumnRefNode = dynamic_cast<const QualifiedColumnRefNode*>(node)) {
                std::string tableKey = normalizeSqlIdentifier(qualifiedColumnRefNode->tableName);
                if(this->context.activeCteTypedefByTableKey.find(tableKey) !=
                   this->context.activeCteTypedefByTableKey.end())
                    return true;
                std::string tableName = std::string(qualifiedColumnRefNode->tableName);
                if(this->context.activeTableAliases.find(tableName) !=
                   this->context.activeTableAliases.end())
                    return true;
                if(policyEquals(this->context.codeGenPolicy, "column_ref_style", "column_pointer"))
                    return true;
                return false;
            }
            if(dynamic_cast<const ColumnRefNode*>(node)) {
                if(this->context.implicitSingleSourceCteTypedef) return true;
                if(policyEquals(this->context.codeGenPolicy, "column_ref_style", "column_pointer"))
                    return true;
            }
            return false;
        };
        if(auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&astNode)) {
            if(binaryOp->binaryOperator == BinaryOperator::isOp ||
               binaryOp->binaryOperator == BinaryOperator::isNot ||
               binaryOp->binaryOperator == BinaryOperator::isDistinctFrom ||
               binaryOp->binaryOperator == BinaryOperator::isNotDistinctFrom) {
                std::string message = "binary IS / IS NOT / IS [NOT] DISTINCT FROM "
                                      "is not supported in sqlite_orm";
                this->context.accumulatedErrors.push_back(message);
                // The error already stops the whole statement from being generated, but the
                // placeholder goes through the same funnel as every other one: what stands in the
                // code says where it came from, whichever channel carries it out.
                return unsupportedPlaceholder("unsupported IS expression", std::move(message), *binaryOp);
            }
            auto leftResult = this->coordinator.generateNode(*binaryOp->lhs);
            auto rightResult = this->coordinator.generateNode(*binaryOp->rhs);

            // A COLLATE and a unary plus generate their operand and nothing else, so an operand
            // standing under one is the operand this node really puts into the C++ expression.
            const AstNode& leftNode = generatedOperandNode(*binaryOp->lhs);
            const AstNode& rightNode = generatedOperandNode(*binaryOp->rhs);

            if(auto* leftCol = dynamic_cast<const ColumnRefNode*>(&leftNode)) {
                this->context.registerPrefixColumn(toCppIdentifier(leftCol->columnName),
                                                   this->context.inferTypeFromNode(rightNode));
            }
            if(auto* rightCol = dynamic_cast<const ColumnRefNode*>(&rightNode)) {
                this->context.registerPrefixColumn(toCppIdentifier(rightCol->columnName),
                                                   this->context.inferTypeFromNode(leftNode));
            }

            auto decisionPoints = std::move(leftResult.decisionPoints);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(rightResult.decisionPoints.begin()),
                                  std::make_move_iterator(rightResult.decisionPoints.end()));

            bool leftLeaf = isLeafNode(leftNode);
            bool rightLeaf = isLeafNode(rightNode);

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
            if(auto* leftCol = dynamic_cast<const ColumnRefNode*>(&leftNode)) {
                leftNoWrap = this->context.columnRefIsSelectAliasNoWrap(*leftCol);
            }
            if(nodeIsNoWrapRef(&leftNode)) {
                leftNoWrap = true;
            }
            bool rightNoWrap = false;
            if(auto* rightCol = dynamic_cast<const ColumnRefNode*>(&rightNode)) {
                rightNoWrap = this->context.columnRefIsSelectAliasNoWrap(*rightCol);
            }
            if(nodeIsNoWrapRef(&rightNode)) {
                rightNoWrap = true;
            }

            // The C++ term the operands make is only half the grouping: sqlite_orm serializes the
            // statement back into SQL, and there it parenthesizes an operand only when that operand
            // is itself a binary operator or condition. A predicate — IN, BETWEEN, LIKE, GLOB,
            // MATCH, IS [NOT] NULL, NOT — comes out bare, and SQLite binds those looser than every
            // arithmetic, bit and comparison operator, so `c(1) - is_null(&User::a)` serializes as
            // `1 - "a" IS NULL` and is read back as `(1 - "a") IS NULL`. A CAST to INTEGER delimits
            // the predicate in that SQL and leaves what it stands for alone: a predicate is 0, 1 or
            // NULL, and a CAST to INTEGER keeps all three, typeof included.
            const bool operandsBecomeCallArguments =
                binaryOp->binaryOperator == BinaryOperator::jsonArrow ||
                binaryOp->binaryOperator == BinaryOperator::jsonArrow2;
            const int sqlPrecedence = sqlOperatorPrecedence(binaryOp->binaryOperator);
            bool castsPredicateOperand = false;
            auto castPredicate = [&](std::string& code, const AstNode& operandNode, bool rightOperand) {
                if(operandsBecomeCallArguments) return;
                const int operandPrecedence = serializedSqlPrecedenceAsBinaryOperand(operandNode);
                // SQL reads these operators left-associatively too, so the right operand regroups at
                // equal precedence as well: `1 = (a IS NULL)` comes back as `(1 = "a") IS NULL`.
                const bool regroups = rightOperand ? operandPrecedence >= sqlPrecedence
                                                   : operandPrecedence > sqlPrecedence;
                if(!regroups) return;
                code = "cast<" + sqliteTypeToCpp("INTEGER") + ">(" + code + ")";
                castsPredicateOperand = true;
            };
            castPredicate(leftResult.code, *binaryOp->lhs, false);
            castPredicate(rightResult.code, *binaryOp->rhs, true);

            // The operator spelling puts the operands into a C++ expression, whose grouping is the
            // C++ precedence and not the SQL one this node was parsed with. An operand that would
            // regroup there gets parentheses: every operator involved is left-associative, so the
            // right one needs them at equal precedence too, which is what tells `1 - (2 - 3)` from
            // `1 - 2 - 3`. The functional spelling passes its operands as arguments and needs none.
            const int precedence = cppOperatorPrecedence(binaryOp->binaryOperator);
            auto asOperand = [&](const std::string& code, const AstNode& operandNode, bool rightOperand) {
                if(precedence == kCppPrecedencePrimary) return code;
                const int operandPrecedence = generatedCppPrecedence(operandNode, this->context.codeGenPolicy);
                const bool regroups =
                    rightOperand ? operandPrecedence >= precedence : operandPrecedence > precedence;
                return regroups ? "(" + code + ")" : code;
            };
            std::string leftOperand = asOperand(leftResult.code, leftNode, false);
            std::string rightOperand = asOperand(rightResult.code, rightNode, true);

            std::string wrappedLeft =
                (leftLeaf && !leftNoWrap && !nodeGeneratesColumnPointer(&leftNode)) ? wrap(leftResult.code) : leftOperand;
            std::string wrappedRight =
                (rightLeaf && !rightNoWrap && !nodeGeneratesColumnPointer(&rightNode)) ? wrap(rightResult.code) : rightOperand;

            auto funcName = binaryFunctionalName(binaryOp->binaryOperator);
            std::string functionalCode =
                std::string(funcName) + "(" + leftResult.code + ", " + rightResult.code + ")";

            auto op = binaryOperatorString(binaryOp->binaryOperator);
            std::string wrapLeftCode = wrappedLeft + std::string(op) + rightOperand;
            std::string wrapRightCode = leftOperand + std::string(op) + wrappedRight;
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
            // C++ spells a logical OR and a concatenation with the same token, `||`, and
            // sqlite_orm's two `operator||` overloads pick between the `or_condition_t` and the
            // `conc_t` by the operands: the OR only when one of them is a condition, the
            // concatenation only when neither is. So `SELECT 1 OR 0` spelled `c(1) or 0` runs as
            // `SELECT 1 || 0` and answers '10', and `SELECT (a = 1) || 'x'` spelled
            // `c(&User::a) == 1 || "x"` runs as `SELECT (a = 1) OR 'x'`. `or_()` and `conc()` name
            // the node they build, so the call is the only form offered where the operator
            // spelling would not be the operator that was written.
            const bool needsCallSpelling = binaryOperatorNeedsCallSpelling(*binaryOp);
            // The JSON arrows have no C++ operator spelling at all — they are generated as a
            // `json_extract()` call — so the call is the only form they offer either.
            const bool onlyCallSpellingCompiles = needsCallSpelling || operandsBecomeCallArguments;
            if(onlyCallSpellingCompiles) {
                chosenExprVal = "functional";
                emittedExpr = functionalCode;
            }

            // options lists every variant (the chosen one included); the consumer decides how to
            // present the selection. Where the operator spelling would be the wrong operator or no
            // C++ operator at all, the call is the only option offered.
            std::vector<Option> exprStyleOptions;
            if(onlyCallSpellingCompiles) {
                exprStyleOptions.push_back(Option{"functional", functionalCode, "functional style"});
            } else {
                exprStyleOptions.push_back(Option{"operator_wrap_left", wrapLeftCode, "wrap left operand"});
                exprStyleOptions.push_back(Option{"operator_wrap_right", wrapRightCode, "wrap right operand"});
                exprStyleOptions.push_back(Option{"functional", functionalCode, "functional style"});
                exprStyleOptions.push_back(Option{"operator_wrap_both", wrapBothCode, "wrap both operands", true});
            }
            decisionPoints.push_back(DecisionPoint{this->context.nextDecisionPointId++, "expr_style",
                                                   chosenExprVal, emittedExpr,
                                                   std::move(exprStyleOptions)});

            std::vector<CodegenWarning> binWarnings;
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

            // A trigger keeps its WHEN expression in an `optional_container`, which default-constructs
            // it, so the WHEN clause of a generated trigger only compiles while every sqlite_orm type
            // in it has a default constructor. `binary_operator` — what the arithmetic, bit and
            // concatenation operators produce — and the `builtin_function_t` behind the JSON arrows
            // have none; the `binary_condition` behind a comparison, an AND and an OR does.
            if(const std::string_view formWithoutDefaultConstructor =
                   binaryOperatorWithoutDefaultConstructor(binaryOp->binaryOperator);
               !formWithoutDefaultConstructor.empty()) {
                this->context.recordFormWithoutDefaultConstructor(
                    std::string(formWithoutDefaultConstructor));
            }

            std::vector<std::string> binComments;
            appendUniqueStrings(binComments, leftResult.comments);
            appendUniqueStrings(binComments, rightResult.comments);
            if(castsPredicateOperand) {
                appendUniqueString(binComments, kCommentPredicateGroupingCast);
            }
            if(needsCallSpelling) {
                appendUniqueString(binComments, kCommentOrTokenCallSpelling);
            }
            return CodeGenResult{std::move(emittedExpr), std::move(decisionPoints), std::move(binWarnings), {},
                                 std::move(binComments)};
        } else if(auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
            // A COLLATE generates its operand and nothing else, so the operand standing under one
            // is the operand this unary operator really applies to in the generated code. The sign
            // a minus folds into a literal is the exception — SQLite's parser does not read one
            // through a COLLATE either, which is why `negationFormFor` is asked about the operand
            // as written.
            const AstNode& operandNode = generatedOperandNode(*unaryOp->operand);
            const bool operandLeaf = isLeafNode(operandNode);

            // Every other sqlite_orm operator unwraps the `c(...)` its operand carries; `operator!`
            // keeps it, and the walker that collects the tables a statement reads stops at such a
            // wrapper. A column a NOT is the only mention of is then invisible to it, and
            // `storage.select(not c(&User::a))` runs as `SELECT NOT "users"."a"` with no FROM clause
            // at all — SQLite answers that with `no such column`, which sqlite_orm reports as
            // `SQL logic error`. A column reference generated here takes the column-pointer form
            // instead, which names the same column and which the walker does read.
            const bool notKeepsOperandQuoted =
                unaryOp->unaryOperator == UnaryOperator::logicalNot && operandLeaf;
            const bool outerColumnRefUnderLogicalNot = this->context.columnRefUnderLogicalNot;
            const bool outerEmittedColumnPointer = this->context.emittedColumnPointerUnderLogicalNot;
            this->context.columnRefUnderLogicalNot = notKeepsOperandQuoted;
            this->context.emittedColumnPointerUnderLogicalNot = false;
            auto operandResult = this->coordinator.generateNode(*unaryOp->operand);
            // What the operand was generated as is the emitter's own answer, not the node's kind: a
            // column that names a SELECT alias is generated as `get<Alias>()` whatever this asks for.
            const bool operandGeneratedColumnPointerUnderNot =
                this->context.emittedColumnPointerUnderLogicalNot;
            this->context.columnRefUnderLogicalNot = outerColumnRefUnderLogicalNot;
            this->context.emittedColumnPointerUnderLogicalNot = outerEmittedColumnPointer;
            auto decisionPoints = std::move(operandResult.decisionPoints);

            if(unaryOp->unaryOperator == UnaryOperator::plus) {
                return CodeGenResult{operandResult.code, std::move(decisionPoints),
                                     std::move(operandResult.warnings), std::move(operandResult.errors),
                                     std::move(operandResult.comments)};
            }

            // sqlite_orm's unary_minus_t reports a wrong result type, so `-c(2)` serializes to the
            // right SQL and still reads the row back as 0. A minus over a numeric constant is folded
            // into it the way SQLite's own parser does, which is why `-2` is the constant -2 rather
            // than a negation of 2; C++ folds the second sign of `- -3` just as well.
            const std::optional<NegationForm> negationForm =
                unaryOp->unaryOperator == UnaryOperator::minus
                    ? std::optional<NegationForm>{negationFormFor(*unaryOp->operand)}
                    : std::nullopt;
            if(negationForm == NegationForm::foldedIntoConstant) {
                // A constant that already carries a sign needs the parentheses C++ has no `--` for.
                std::string folded = isNumericLiteral(*unaryOp->operand)
                                         ? "-" + operandResult.code
                                         : "-(" + operandResult.code + ")";
                return CodeGenResult{std::move(folded), std::move(decisionPoints),
                                     std::move(operandResult.warnings), std::move(operandResult.errors),
                                     std::move(operandResult.comments)};
            }

            bool operandNoWrap = false;
            if(auto* opCol = dynamic_cast<const ColumnRefNode*>(&operandNode)) {
                operandNoWrap = this->context.columnRefIsSelectAliasNoWrap(*opCol);
                if(this->context.withCteCpp20Monikers() && this->context.implicitSingleSourceCteTypedef) {
                    operandNoWrap = true;
                }
            }
            if(auto* opQCol = dynamic_cast<const QualifiedColumnRefNode*>(&operandNode)) {
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
            // sqlite_orm classifies `negated_condition_t` — what a NOT and the `!predicate` spelling
            // of a negated BETWEEN, LIKE, GLOB and MATCH generate — as neither negatable nor an
            // operator argument, so a second NOT over one does not compile at all. A CAST to INTEGER
            // makes it an operand again and leaves what it stands for alone: the inner NOT is 0, 1 or
            // NULL, and a CAST to INTEGER keeps all three, so `NOT NOT a` and
            // `NOT CAST(NOT a AS INTEGER)` answer alike (checked against sqlite3 3.51 over integers,
            // reals, text, a blob and NULL).
            const bool castsNegatedOperand = unaryOp->unaryOperator == UnaryOperator::logicalNot &&
                                             generatesNegatedCondition(*unaryOp->operand);
            if(castsNegatedOperand) {
                operandResult.code = "cast<" + sqliteTypeToCpp("INTEGER") + ">(" + operandResult.code + ")";
                appendUniqueString(operandResult.comments, kCommentNegatedConditionCast);
            }

            // A column reference under a NOT is generated as a column pointer rather than wrapped in
            // `c(...)`; the two forms stand in the same place, so the wrapper is skipped for it.
            const bool wrapsOperandInC =
                operandLeaf && !operandNoWrap && !nodeGeneratesColumnPointer(&operandNode);
            const bool operandIsColumnPointerUnderNot =
                wrapsOperandInC && operandGeneratedColumnPointerUnderNot;
            if(operandIsColumnPointerUnderNot) {
                appendUniqueString(operandResult.comments, kCommentNotColumnPointer);
            }

            // The same wrapper hides a value from the walk that binds one: the literal of
            // `select(not c(0))` was never bound, so the statement ran with an empty parameter and
            // answered NULL where SQLite answers 1. A binary operator unwraps what it is given, and
            // `0 + x` is the numeric coercion SQLite applies to `x` in a boolean context anyway —
            // `NOT x` and `NOT (0 + x)` answer alike for every value (checked against sqlite3 3.51
            // over 33 literals: integers, reals, text that does and does not convert, blobs, NULL).
            const bool operandIsAddedToZeroUnderNot =
                notKeepsOperandQuoted && wrapsOperandInC && generatesBoundValue(operandNode);
            if(operandIsAddedToZeroUnderNot) {
                appendUniqueString(operandResult.comments, kCommentNotValueAddedToZero);
            }

            std::string operandStr;
            if(castsNegatedOperand) {
                // A CAST delimits itself, both in C++ and in the SQL sqlite_orm serializes.
                operandStr = operandResult.code;
            } else if(operandIsAddedToZeroUnderNot) {
                operandStr = "(c(0) + " + operandResult.code + ")";
            } else if(wrapsOperandInC && !operandIsColumnPointerUnderNot) {
                operandStr = wrap(operandResult.code);
            } else if(!operandLeaf && !generatesZeroMinusSubtraction(operandNode)) {
                operandStr = "(" + operandResult.code + ")";
            } else {
                operandStr = operandResult.code;
            }

            // An operand that is not a numeric constant keeps its negation as a subtraction from zero.
            // SQLite computes `0 - x` exactly like `-x` — same value and same typeof for every operand
            // kind — and sqlite_orm serializes and reads that back correctly, where its unary_minus_t
            // hands the caller 0 and throws over a column. The one operand this cannot carry is a
            // predicate: sqlite_orm leaves `a BETWEEN 1 AND 9` unparenthesized and SQLite binds it
            // looser than a binary `-`, so `0 - a BETWEEN 1 AND 9` would regroup the expression.
            bool negationAsSubtraction = false;
            if(unaryOp->unaryOperator == UnaryOperator::minus) {
                if(negationForm == NegationForm::zeroMinusSubtraction) {
                    negationAsSubtraction = true;
                    appendUniqueString(operandResult.comments, kCommentNegationAsZeroMinus);
                } else {
                    operandResult.warnings.push_back(CodegenWarning{
                        "unary minus over a predicate (" +
                            std::string(sqlPredicateLooserThanMinus(operandNode)) +
                            ") has no working sqlite_orm form; the generated negation does not "
                            "reproduce what SQLite computes and does not compile",
                        unaryOp->location, 1});
                }
                if(numericLiteralRejectsFoldedSign(*unaryOp->operand)) {
                    // The sign stays out of the C++ constant, which could not hold it, so the
                    // subtraction above is generated instead. SQLite has no value for this
                    // expression either: it refuses the statement that uses it, and only a DDL
                    // clause — which SQLite stores without compiling — gets this far.
                    const auto& tooBigLiteral =
                        dynamic_cast<const IntegerLiteralNode&>(*unaryOp->operand);
                    operandResult.warnings.push_back(CodegenWarning{
                        "hex literal too big: -" + withoutDigitSeparators(tooBigLiteral.value) +
                            "; SQLite refuses this expression wherever it is used, so the generated "
                            "subtraction from zero does not reproduce it",
                        unaryOp->location, 1});
                }
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
            if(negationAsSubtraction) {
                // `~` and `not` bind tighter than every binary operator C++ gives them, a subtraction
                // does not, so the operator spelling carries its own parentheses to stay one operand.
                operatorCode = "(c(0) - " + operandStr + ")";
                functionalCode = "sub(0, " + operandResult.code + ")";
            }

            // Neither `negated_condition_t` nor `bitwise_not_t` has a default constructor, and a
            // negation generated as a subtraction from zero is a `binary_operator`, which has none
            // either — so none of the three can stand in a trigger's WHEN clause.
            if(unaryOp->unaryOperator == UnaryOperator::logicalNot) {
                this->context.recordFormWithoutDefaultConstructor("NOT");
            } else if(unaryOp->unaryOperator == UnaryOperator::bitwiseNot) {
                this->context.recordFormWithoutDefaultConstructor("~");
            } else if(negationAsSubtraction) {
                this->context.recordFormWithoutDefaultConstructor("unary -");
            }

            std::string chosenUnaryVal = "operator";
            std::string emittedUnary = operatorCode;
            if(policyEquals(this->context.codeGenPolicy, "expr_style", "functional") &&
               unaryOp->unaryOperator != UnaryOperator::logicalNot) {
                chosenUnaryVal = "functional";
                emittedUnary = functionalCode;
            } else if(policyEquals(this->context.codeGenPolicy, "expr_style", "operator_excl") &&
                      unaryOp->unaryOperator == UnaryOperator::logicalNot) {
                chosenUnaryVal = "operator_excl";
                emittedUnary = "!" + operandStr;
            }

            // options lists every variant (the chosen one included).
            std::vector<Option> options;
            options.push_back(Option{"operator", operatorCode, "operator style"});
            if(unaryOp->unaryOperator == UnaryOperator::logicalNot) {
                // sqlite_orm negates via operator! / `not` only; there is no functional form.
                options.push_back(Option{"operator_excl", "!" + operandStr, "use ! instead of not"});
            } else {
                options.push_back(Option{"functional", functionalCode, "functional style"});
            }

            decisionPoints.push_back(
                DecisionPoint{this->context.nextDecisionPointId++, "expr_style", chosenUnaryVal, emittedUnary,
                              std::move(options)});

            return CodeGenResult{std::move(emittedUnary), std::move(decisionPoints),
                                 std::move(operandResult.warnings), std::move(operandResult.errors),
                                 std::move(operandResult.comments)};
        } else if(auto* isNullNode = dynamic_cast<const IsNullNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*isNullNode->operand);
            std::string operandCode = groupPredicateArgument(std::move(operandResult.code),
                                                             *isNullNode->operand, operandResult.comments);
            this->context.recordFormWithoutDefaultConstructor("IS NULL");
            return CodeGenResult{"is_null(" + operandCode + ")", std::move(operandResult.decisionPoints), {},
                                 {}, std::move(operandResult.comments)};
        } else if(auto* isNotNullNode = dynamic_cast<const IsNotNullNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*isNotNullNode->operand);
            std::string operandCode = groupPredicateArgument(std::move(operandResult.code),
                                                             *isNotNullNode->operand, operandResult.comments);
            this->context.recordFormWithoutDefaultConstructor("IS NOT NULL");
            return CodeGenResult{"is_not_null(" + operandCode + ")",
                                 std::move(operandResult.decisionPoints), {}, {},
                                 std::move(operandResult.comments)};
        } else if(auto* betweenNode = dynamic_cast<const BetweenNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*betweenNode->operand);
            auto lowResult = this->coordinator.generateNode(*betweenNode->low);
            auto highResult = this->coordinator.generateNode(*betweenNode->high);

            // A COLLATE and a unary plus emit their operand and nothing else, so the column this
            // is compared against is the one standing under them.
            if(auto* col = dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(*betweenNode->operand))) {
                // Both bounds are compared against the column, so both have a say in its type:
                // registering the wider one after the narrower one widens the field to hold it.
                const std::string cppName = toCppIdentifier(col->columnName);
                this->context.registerPrefixColumn(cppName, this->context.inferTypeFromNode(*betweenNode->low));
                this->context.registerPrefixColumn(cppName, this->context.inferTypeFromNode(*betweenNode->high));
            }

            auto decisionPoints = std::move(operandResult.decisionPoints);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(lowResult.decisionPoints.begin()),
                                  std::make_move_iterator(lowResult.decisionPoints.end()));
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(highResult.decisionPoints.begin()),
                                  std::make_move_iterator(highResult.decisionPoints.end()));

            std::vector<std::string> betweenComments;
            std::string operandCode = groupPredicateArgument(std::move(operandResult.code),
                                                             *betweenNode->operand, betweenComments);
            std::string lowCode =
                groupPredicateArgument(std::move(lowResult.code), *betweenNode->low, betweenComments);
            std::string highCode =
                groupPredicateArgument(std::move(highResult.code), *betweenNode->high, betweenComments);

            this->context.recordFormWithoutDefaultConstructor("BETWEEN");
            std::string betweenCode = "between(" + operandCode + ", " + lowCode + ", " + highCode + ")";
            std::string code = betweenNode->negated ? "!" + betweenCode : betweenCode;
            return CodeGenResult{code, std::move(decisionPoints), {}, {}, std::move(betweenComments)};
        } else if(auto* subqueryNode = dynamic_cast<const SubqueryNode*>(&astNode)) {
            auto sub = this->coordinator.tryCodegenSelectLikeSubquery(*subqueryNode->select);
            if(sub.code.empty()) {
                auto placeholder = unsupportedPlaceholder(
                    "(SELECT ...)", "scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen",
                    *subqueryNode);
                placeholder.decisionPoints = std::move(sub.decisionPoints);
                placeholder.warnings.insert(placeholder.warnings.end(),
                                            std::make_move_iterator(sub.warnings.begin()),
                                            std::make_move_iterator(sub.warnings.end()));
                return placeholder;
            }
            return CodeGenResult{sub.code, std::move(sub.decisionPoints), std::move(sub.warnings)};
        } else if(auto* existsNode = dynamic_cast<const ExistsNode*>(&astNode)) {
            auto sub = this->coordinator.tryCodegenSelectLikeSubquery(*existsNode->select);
            if(sub.code.empty()) {
                auto placeholder = unsupportedPlaceholder(
                    "EXISTS (SELECT ...)", "EXISTS (SELECT ...) is not mapped to sqlite_orm codegen", *existsNode);
                placeholder.decisionPoints = std::move(sub.decisionPoints);
                placeholder.warnings.insert(placeholder.warnings.end(),
                                            std::make_move_iterator(sub.warnings.begin()),
                                            std::make_move_iterator(sub.warnings.end()));
                return placeholder;
            }
            this->context.recordFormWithoutDefaultConstructor("EXISTS");
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
                    this->context.recordFormWithoutDefaultConstructor("IN");
                    std::string inFunc = inNode->negated ? "not_in" : "in";
                    std::string operandCode = groupPredicateArgument(
                        std::move(operandResult.code), *inNode->operand, operandResult.comments);
                    std::string code =
                        inFunc + "(" + operandCode + ", select(" + selectColumnCode + "))";
                    return CodeGenResult{std::move(code), std::move(operandResult.decisionPoints),
                                         std::move(operandResult.warnings), {},
                                         std::move(operandResult.comments)};
                }
                CodeGenResult carried;
                carried.decisionPoints = std::move(operandResult.decisionPoints);
                carried.warnings = std::move(operandResult.warnings);
                return unsupportedPlaceholder(operandResult.code + " IN " + inNode->tableName,
                                              "IN table-name is not supported in sqlite_orm codegen", *inNode,
                                              std::move(carried));
            }
            if(inNode->subquerySelect) {
                auto operandResult = this->coordinator.generateNode(*inNode->operand);
                auto sub = this->coordinator.tryCodegenSelectLikeSubquery(*inNode->subquerySelect);
                auto decisionPoints = std::move(operandResult.decisionPoints);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(sub.decisionPoints.begin()),
                                      std::make_move_iterator(sub.decisionPoints.end()));
                std::vector<CodegenWarning> inSubWarnings;
                inSubWarnings.insert(inSubWarnings.end(),
                                     std::make_move_iterator(operandResult.warnings.begin()),
                                     std::make_move_iterator(operandResult.warnings.end()));
                inSubWarnings.insert(inSubWarnings.end(),
                                     std::make_move_iterator(sub.warnings.begin()),
                                     std::make_move_iterator(sub.warnings.end()));
                if(sub.code.empty()) {
                    CodeGenResult carried;
                    carried.decisionPoints = std::move(decisionPoints);
                    carried.warnings = std::move(inSubWarnings);
                    return unsupportedPlaceholder("IN (SELECT ...)",
                                                  "IN (SELECT ...) is not mapped to sqlite_orm codegen", *inNode,
                                                  std::move(carried));
                }
                std::string operandCode = groupPredicateArgument(std::move(operandResult.code),
                                                                 *inNode->operand, operandResult.comments);
                this->context.recordFormWithoutDefaultConstructor("IN");
                std::string code = inNode->negated ? "not_in(" + operandCode + ", " + sub.code + ")"
                                                   : "in(" + operandCode + ", " + sub.code + ")";
                return CodeGenResult{code, std::move(decisionPoints), std::move(inSubWarnings), {},
                                     std::move(operandResult.comments)};
            }
            auto operandResult = this->coordinator.generateNode(*inNode->operand);
            auto decisionPoints = std::move(operandResult.decisionPoints);

            if(auto* col = dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(*inNode->operand))) {
                // Every value of the list is compared against the column, not just the first one,
                // so the field has to hold the widest of them.
                const std::string cppName = toCppIdentifier(col->columnName);
                for(const auto& value: inNode->values) {
                    this->context.registerPrefixColumn(cppName, this->context.inferTypeFromNode(*value));
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

            // Every value of the list is delimited by the commas around it, so only the operand
            // stands where the SQL an AND or an OR serializes into would run into the predicate.
            std::string inOperandCode = groupPredicateArgument(std::move(operandResult.code),
                                                               *inNode->operand, operandResult.comments);
            this->context.recordFormWithoutDefaultConstructor("IN");
            std::string inCode = "in(" + inOperandCode + ", {" + valuesList + "})";
            if(inNode->negated) {
                std::string notInCode = "not_in(" + inOperandCode + ", {" + valuesList + "})";
                std::string negatedInCode = "!" + inCode;
                const bool useOperator =
                    policyEquals(this->context.codeGenPolicy, "negation_style", "operator_excl");
                const std::string& chosenNegation = useOperator ? negatedInCode : notInCode;
                // options lists every variant (the chosen one included).
                decisionPoints.push_back(DecisionPoint{
                    this->context.nextDecisionPointId++, "negation_style",
                    useOperator ? "operator_excl" : "not_in", chosenNegation,
                    {Option{"not_in", notInCode, "use not_in()"},
                     Option{"operator_excl", negatedInCode, "use the ! operator"}}});
                return CodeGenResult{chosenNegation, std::move(decisionPoints), {}, {},
                                     std::move(operandResult.comments)};
            }
            return CodeGenResult{inCode, std::move(decisionPoints), {}, {},
                                 std::move(operandResult.comments)};
        } else if(auto* likeNode = dynamic_cast<const LikeNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*likeNode->operand);
            auto patternResult = this->coordinator.generateNode(*likeNode->pattern);

            if(auto* col = dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(*likeNode->operand))) {
                this->context.registerPrefixColumn(toCppIdentifier(col->columnName), "std::string");
            }

            auto decisionPoints = std::move(operandResult.decisionPoints);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(patternResult.decisionPoints.begin()),
                                  std::make_move_iterator(patternResult.decisionPoints.end()));

            std::vector<std::string> likeComments;
            std::string operandCode =
                groupPredicateArgument(std::move(operandResult.code), *likeNode->operand, likeComments);
            std::string patternCode =
                groupPredicateArgument(std::move(patternResult.code), *likeNode->pattern, likeComments);

            this->context.recordFormWithoutDefaultConstructor("LIKE");
            std::string likeCode = "like(" + operandCode + ", " + patternCode;
            if(likeNode->escape) {
                auto escapeResult = this->coordinator.generateNode(*likeNode->escape);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(escapeResult.decisionPoints.begin()),
                                      std::make_move_iterator(escapeResult.decisionPoints.end()));
                likeCode += ", " + groupPredicateArgument(std::move(escapeResult.code),
                                                          *likeNode->escape, likeComments);
            }
            likeCode += ")";

            std::string code = likeNode->negated ? "!" + likeCode : likeCode;
            return CodeGenResult{code, std::move(decisionPoints), {}, {}, std::move(likeComments)};
        } else if(auto* globNode = dynamic_cast<const GlobNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*globNode->operand);
            auto patternResult = this->coordinator.generateNode(*globNode->pattern);

            if(auto* col = dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(*globNode->operand))) {
                this->context.registerPrefixColumn(toCppIdentifier(col->columnName), "std::string");
            }

            auto decisionPoints = std::move(operandResult.decisionPoints);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(patternResult.decisionPoints.begin()),
                                  std::make_move_iterator(patternResult.decisionPoints.end()));

            std::vector<std::string> globComments;
            std::string operandCode =
                groupPredicateArgument(std::move(operandResult.code), *globNode->operand, globComments);
            std::string patternCode =
                groupPredicateArgument(std::move(patternResult.code), *globNode->pattern, globComments);

            this->context.recordFormWithoutDefaultConstructor("GLOB");
            std::string globCode = "glob(" + operandCode + ", " + patternCode + ")";
            std::string code = globNode->negated ? "!" + globCode : globCode;
            return CodeGenResult{code, std::move(decisionPoints), {}, {}, std::move(globComments)};
        } else if(auto* matchNode = dynamic_cast<const MatchNode*>(&astNode)) {
            std::vector<DecisionPoint> decisionPoints;
            std::vector<CodegenWarning> warnings;
            std::vector<std::string> matchComments;

            // `fts_table MATCH pattern` targets the hidden FTS5 "any" column of that table.
            std::string lhsCode;
            if(auto* col = dynamic_cast<const ColumnRefNode*>(matchNode->operand.get())) {
                const std::string key = normalizeSqlIdentifier(stripIdentifierQuotes(col->columnName));
                for(const auto& [fromName, mappedStructName] : this->context.fromTableAliasToStructName) {
                    if(normalizeSqlIdentifier(stripIdentifierQuotes(fromName)) == key) {
                        lhsCode = "c<" + mappedStructName + ">()->*&fts5::hidden::any";
                        warnings.push_back("MATCH against table \"" + std::string(col->columnName) +
                                           "\" maps to the hidden FTS5 'any' column; requires an FTS5 "
                                           "virtual table mapped as " +
                                           mappedStructName);
                        break;
                    }
                }
            }
            if(lhsCode.empty()) {
                auto operandResult = this->coordinator.generateNode(*matchNode->operand);
                if(auto* col = dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(*matchNode->operand))) {
                    this->context.registerPrefixColumn(toCppIdentifier(col->columnName), "std::string");
                }
                decisionPoints = std::move(operandResult.decisionPoints);
                lhsCode = groupPredicateArgument(std::move(operandResult.code), *matchNode->operand,
                                                 matchComments);
            }

            auto patternResult = this->coordinator.generateNode(*matchNode->pattern);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(patternResult.decisionPoints.begin()),
                                  std::make_move_iterator(patternResult.decisionPoints.end()));
            std::string patternCode =
                groupPredicateArgument(std::move(patternResult.code), *matchNode->pattern, matchComments);

            // `match_t` is an aggregate holding its two operands, so unlike the other predicates it
            // default-constructs with them and can stand in a trigger's WHEN clause. A negated MATCH
            // does not compile at all, which the warning below is about.
            std::string matchCode = "match(" + lhsCode + ", " + patternCode + ")";
            std::string code = matchNode->negated ? "!" + matchCode : matchCode;
            if(matchNode->negated) {
                warnings.push_back(
                    "negated MATCH does not compile against sqlite_orm dev yet: match_t is not accepted by "
                    "operator! (https://github.com/fnc12/sqlite_orm/issues/1501)");
            }
            return CodeGenResult{code, std::move(decisionPoints), std::move(warnings), {},
                                 std::move(matchComments)};
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
            auto bindParameterMessage = [&paramStr](const std::string& variable) {
                return "bind parameter " + paramStr + " -> C++ variable '" + variable +
                       "'; for prepared statements use storage.prepare() + get<N>(stmt)";
            };
            std::string cppVar;
            if(paramStr.size() > 1 && (paramStr[0] == ':' || paramStr[0] == '@' || paramStr[0] == '$')) {
                cppVar = toCppIdentifier(paramStr.substr(1));
            } else if(paramStr == "?") {
                cppVar = "bindParam" + std::to_string(++this->context.nextBindParamIndex);
            } else if(paramStr.size() > 1 && paramStr[0] == '?') {
                cppVar = "bindParam" + paramStr.substr(1);
            } else {
                // A marker with nothing behind it — a lone `:`, which SQLite refuses outright —
                // names no variable, so a placeholder stands where the value would have gone.
                return unsupportedPlaceholder(paramStr, bindParameterMessage(placeholderCode(paramStr)),
                                              *bindParam);
            }
            return CodeGenResult{cppVar, {}, {bindParameterMessage(cppVar)}};
        } else if(auto* collateNode = dynamic_cast<const CollateNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*collateNode->operand);
            operandResult.warnings.push_back("COLLATE " + collateNode->collationName +
                                              " on expressions is not directly supported in sqlite_orm codegen");
            return operandResult;
        } else if(auto* funcCall = dynamic_cast<const FunctionCallNode*>(&astNode)) {
            std::string funcName = toLowerAscii(funcCall->name);
            std::vector<DecisionPoint> decisionPoints;
            std::vector<CodegenWarning> funcWarnings;
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
                const bool customFunction = !isKnownSqlFunction(funcName);
                CustomFunctionUse customUse;
                if(customFunction) {
                    customUse.sqlName = funcCall->name;
                    customUse.structName = toStructName(funcCall->name);
                }
                std::string argList;
                for(size_t argIndex = 0; argIndex < funcCall->arguments.size(); ++argIndex) {
                    const AstNode& argNode = *funcCall->arguments.at(argIndex);
                    auto argResult = this->coordinator.generateNode(argNode);
                    decisionPoints.insert(decisionPoints.end(),
                                          std::make_move_iterator(argResult.decisionPoints.begin()),
                                          std::make_move_iterator(argResult.decisionPoints.end()));
                    funcWarnings.insert(funcWarnings.end(),
                                        std::make_move_iterator(argResult.warnings.begin()),
                                        std::make_move_iterator(argResult.warnings.end()));
                    if(argIndex > 0)
                        argList += ", ";
                    argList += argResult.code;
                    if(customFunction) {
                        customUse.argTypes.push_back(this->context.customFunctionArgType(argNode));
                        if(auto* col = dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(argNode))) {
                            customUse.argNames.push_back(toCppIdentifier(col->columnName));
                        } else {
                            customUse.argNames.push_back("arg" + std::to_string(argIndex));
                        }
                    }
                }
                if(!funcCall->star && !funcCall->arguments.empty() &&
                   sqliteScalarFirstArgTextContext(funcName)) {
                    if(auto* col =
                           dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(*funcCall->arguments.at(0)))) {
                        this->context.registerPrefixColumn(toCppIdentifier(col->columnName), "std::string");
                    }
                }
                if(customFunction) {
                    this->context.registerCustomFunction(std::move(customUse));
                    baseCode = "func<" + toStructName(funcCall->name) + ">(" + argList + ")";
                } else if(funcCall->distinct && !argList.empty()) {
                    baseCode = funcName + "(distinct(" + argList + "))";
                } else {
                    baseCode = funcName + "(" + argList + ")";
                }
            }

            // `builtin_function_t` and the `function_call` a user-defined function is generated as
            // both declare a constructor and no default one, so a trigger's WHEN clause, which
            // sqlite_orm default-constructs, holds neither. The window functions, `count(*)` and a
            // function-spelled MATCH are the exceptions — each is generated as an aggregate — and so
            // are the `filter` and `over` wrappers, which keep only what they are given:
            // `count_asterisk_t::filter()` unwraps the `where_t` and keeps its expression, so none
            // of them costs the default constructor on their own. What they carry records itself: an
            // argument and a FILTER expression through their own emitters, an OVER clause's ORDER BY
            // through `codegenOverClause`.
            if(!functionCallHasDefaultConstructor(funcName, funcCall->star)) {
                this->context.recordFormWithoutDefaultConstructor(std::string(funcCall->name) + "()");
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
                if(functionCallFormHasNoFilter(funcName)) {
                    funcWarnings.push_back(std::string(funcCall->name) +
                                           "() has no filter() in sqlite_orm: only the aggregate function calls "
                                           "and count(*) take a FILTER, so the generated code does not compile. "
                                           "SQLite refuses the same call — FILTER clause may only be used with "
                                           "aggregate window functions — but stores a trigger or a view holding it");
                }
            }
            if(funcCall->over) {
                std::string overArgs = this->codegenOverClause(*funcCall->over, decisionPoints, funcWarnings);
                baseCode += ".over(" + overArgs + ")";
            }
            return CodeGenResult{std::move(baseCode), std::move(decisionPoints), std::move(funcWarnings)};
        }
        return CodeGenResult{};
    }

    std::string ExpressionCodeGenerator::codegenWindowFrameBound(const WindowFrameBound& bound,
                                                                 std::vector<DecisionPoint>& decisionPoints,
                                                                 std::vector<CodegenWarning>& warnings) {
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
                                                                std::vector<CodegenWarning>& warnings) {
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
                                                           std::vector<CodegenWarning>& warnings) {
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
            // `order_by_t` declares a constructor and no default one, and it is the only part of a
            // window definition that does: `partition_by` and the frame boundaries are aggregates.
            this->context.recordFormWithoutDefaultConstructor("ORDER BY");
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
