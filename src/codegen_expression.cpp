#include "codegen_expression.h"
#include "codegen_context.h"
#include "codegen_utils.h"
#include "expression_subquery_scope.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    ExpressionCodeGenerator::ExpressionCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context) :
        coordinator(coordinator), context(context) {}

    void ExpressionCodeGenerator::noteDdlInfinity(const AstNode& literal, std::string_view spelling) {
        if (!this->context.ddlSerializedExpression) {
            return;
        }
        this->context.ddlInfinityLiterals.push_back(
            DdlInfinityLiteral{withoutDigitSeparators(spelling), literal.sourceSpan});
    }

    CodeGenResult ExpressionCodeGenerator::generateExpression(const AstNode& astNode) {
        CodeGenResult result = this->generateExpressionCode(astNode);
        // The code of an expression is the code of the node as a whole, whatever its operands
        // recorded inside it, so its own span is the outermost one of those it carries.
        SpannedCode code = SpannedCode::takenFrom(result);
        this->context.markExpression(code, astNode);
        std::move(code).placeInto(result);
        return result;
    }

    CodeGenResult ExpressionCodeGenerator::generateExpressionCode(const AstNode& astNode) {
        if (auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&astNode)) {
            if (hexLiteralExceedsInt64(integerLiteral->value)) {
                // SQLite raises `hex literal too big` in `codeInteger()`, i.e. when it compiles
                // the expression, so `SELECT 0x10000000000000000` is refused while a DEFAULT, a
                // CHECK or a view body keeps the literal and only a use of them fails. C++ has no
                // literal for it either, so a stored clause hands it to the generator that owns
                // the clause, which leaves the clause out of the generated code.
                std::string literal = withoutDigitSeparators(integerLiteral->value);
                if (this->context.storedExpression) {
                    this->context.storedHexLiteralsTooBig.push_back(std::move(literal));
                } else {
                    this->context.accumulatedErrors.push_back("hex literal too big: " + std::move(literal));
                }
                return CodeGenResult{{}, {}};
            }
            if (numericLiteralGeneratesInfinity(integerLiteral->value)) {
                this->context.spelledInfinity = true;
                this->noteDdlInfinity(*integerLiteral, integerLiteral->value);
            }
            return CodeGenResult{integerLiteralToCpp(integerLiteral->value), {}};
        } else if (auto* realLiteral = dynamic_cast<const RealLiteralNode*>(&astNode)) {
            if (numericLiteralGeneratesInfinity(realLiteral->value)) {
                this->context.spelledInfinity = true;
                this->noteDdlInfinity(*realLiteral, realLiteral->value);
            }
            return CodeGenResult{realLiteralToCpp(realLiteral->value), {}};
        } else if (auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(&astNode)) {
            return CodeGenResult{sqlStringToCpp(stringLiteral->value), {}};
        } else if (dynamic_cast<const NullLiteralNode*>(&astNode)) {
            return CodeGenResult{"nullptr", {}};
        } else if (auto* boolLiteral = dynamic_cast<const BoolLiteralNode*>(&astNode)) {
            return CodeGenResult{boolLiteral->value ? "true" : "false", {}};
        } else if (auto* raiseNode = dynamic_cast<const RaiseNode*>(&astNode)) {
            if (raiseNode->kind == RaiseKind::ignore) {
                return CodeGenResult{"raise_ignore()", {}};
            }
            if (!raiseNode->message) {
                return CodeGenResult{"raise_ignore()", {}};
            }
            std::vector<CodegenWarning> raiseWarnings;
            const char* api = "raise_abort";
            if (raiseNode->kind == RaiseKind::rollback) {
                api = "raise_rollback";
            } else if (raiseNode->kind == RaiseKind::abort) {
                api = "raise_abort";
            } else if (raiseNode->kind == RaiseKind::fail) {
                api = "raise_fail";
            }
            if (auto* strLit = dynamic_cast<const StringLiteralNode*>(raiseNode->message.get())) {
                return CodeGenResult{std::string(api) + "(" + sqlStringToCpp(strLit->value) + ")", {}};
            }
            raiseWarnings.push_back(
                "RAISE(ROLLBACK|ABORT|FAIL, ...) message should be a SQL string literal for sqlite_orm raise_*()");
            return CodeGenResult{std::string(api) + "(\"\")", {}, std::move(raiseWarnings)};
        } else if (auto* blobLiteral = dynamic_cast<const BlobLiteralNode*>(&astNode)) {
            // A blob goes into a query as a bound parameter, where its bytes travel whole, but into
            // a DDL statement as the text `field_printer<std::vector<char>>` prints — the raw bytes
            // rather than their hex digits, inside `x'…'`. An empty blob is the one that survives
            // that (`x''` either way); every other one reaches SQLite as a different value or as an
            // unrecognized token, so a DDL clause hands it to the generator that owns the clause.
            if (this->context.ddlSerializedExpression && !blobLiteralIsEmpty(blobLiteral->value)) {
                this->context.ddlBlobLiterals.push_back(blobLiteral->value);
            }
            return CodeGenResult{blobToCpp(blobLiteral->value), {}};
        } else if (auto* currentDt = dynamic_cast<const CurrentDatetimeLiteralNode*>(&astNode)) {
            switch (currentDt->kind) {
                case CurrentDatetimeKind::time:
                    return CodeGenResult{"current_time()", {}};
                case CurrentDatetimeKind::date:
                    return CodeGenResult{"current_date()", {}};
                case CurrentDatetimeKind::timestamp:
                    return CodeGenResult{"current_timestamp()", {}};
            }
            return CodeGenResult{"current_timestamp()", {}};
        } else if (auto* columnRef = dynamic_cast<const ColumnRefNode*>(&astNode)) {
            return this->generateColumnRef(columnRef, astNode);
        } else if (auto* qualifiedRef = dynamic_cast<const QualifiedColumnRefNode*>(&astNode)) {
            return this->generateQualifiedColumnRef(qualifiedRef);
        } else if (auto* qualifiedAsterisk = dynamic_cast<const QualifiedAsteriskNode*>(&astNode)) {
            return this->generateQualifiedAsterisk(qualifiedAsterisk);
        } else if (auto* newRef = dynamic_cast<const NewRefNode*>(&astNode)) {
            const std::string& subjectStruct =
                this->context.triggerSubjectStructName.value_or(this->context.structName);
            auto cppName = this->context.structColumnMember(subjectStruct, newRef->columnName);
            this->context.registerColumn(cppName, defaultCppTypeForSyntheticColumn(cppName));
            return CodeGenResult{"new_(&" + subjectStruct + "::" + cppName + ")", {}};
        } else if (auto* oldRef = dynamic_cast<const OldRefNode*>(&astNode)) {
            const std::string& subjectStruct =
                this->context.triggerSubjectStructName.value_or(this->context.structName);
            auto cppName = this->context.structColumnMember(subjectStruct, oldRef->columnName);
            this->context.registerColumn(cppName, defaultCppTypeForSyntheticColumn(cppName));
            return CodeGenResult{"old(&" + subjectStruct + "::" + cppName + ")", {}};
        } else if (auto* excludedRef = dynamic_cast<const ExcludedRefNode*>(&astNode)) {
            auto cppName = this->context.structColumnMember(this->context.structName, excludedRef->columnName);
            return CodeGenResult{"excluded(&" + this->context.structName + "::" + cppName + ")", {}};
        }
        if (auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&astNode)) {
            return this->generateBinaryOperator(binaryOp);
        } else if (auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
            return this->generateUnaryOperator(unaryOp);
        } else if (auto* isNullNode = dynamic_cast<const IsNullNode*>(&astNode)) {
            return this->generateIsNull(isNullNode);
        } else if (auto* isNotNullNode = dynamic_cast<const IsNotNullNode*>(&astNode)) {
            return this->generateIsNotNull(isNotNullNode);
        } else if (auto* betweenNode = dynamic_cast<const BetweenNode*>(&astNode)) {
            return this->generateBetween(betweenNode);
        } else if (auto* subqueryNode = dynamic_cast<const SubqueryNode*>(&astNode)) {
            bool compoundSubquery = false;
            CodeGenResult sub;
            {
                ExpressionSubqueryScope enclosedByStatement{this->context};
                sub = selectLikeSubqueryForm(this->coordinator, this->context, *subqueryNode->select, compoundSubquery);
            }
            // A compound form is a statement, and sqlite_orm serializes it without parentheses of
            // its own: `coalesce(union_(…), 1)` comes out as `COALESCE(SELECT … UNION SELECT …, 1)`,
            // which SQLite rejects, and as a result column the compound is what the whole statement
            // becomes. The one place it reads back as the subquery it was written as is the whole
            // `where(...)` condition, which brings its own parentheses.
            if (!sub.code.empty() && compoundSubquery && &astNode != this->context.parenthesizedConditionNode) {
                auto placeholder = unsupportedPlaceholder(
                    this->context,
                    "(SELECT ...)",
                    "a compound SELECT as a scalar subquery is not mapped to sqlite_orm codegen: its "
                    "union_()/intersect()/except() form is a statement, which sqlite_orm serializes without "
                    "the parentheses this position needs",
                    *subqueryNode);
                placeholder.decisionPoints = std::move(sub.decisionPoints);
                placeholder.warnings.insert(placeholder.warnings.end(),
                                            std::make_move_iterator(sub.warnings.begin()),
                                            std::make_move_iterator(sub.warnings.end()));
                return placeholder;
            }
            if (sub.code.empty()) {
                auto placeholder =
                    unsupportedPlaceholder(this->context,
                                           "(SELECT ...)",
                                           "scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen",
                                           *subqueryNode);
                placeholder.decisionPoints = std::move(sub.decisionPoints);
                placeholder.warnings.insert(placeholder.warnings.end(),
                                            std::make_move_iterator(sub.warnings.begin()),
                                            std::make_move_iterator(sub.warnings.end()));
                return placeholder;
            }
            this->context.emittedSubqueryFormNode = &astNode;
            return CodeGenResult{sub.code, std::move(sub.decisionPoints), std::move(sub.warnings)};
        } else if (auto* existsNode = dynamic_cast<const ExistsNode*>(&astNode)) {
            bool compoundSubquery = false;
            CodeGenResult sub;
            {
                ExpressionSubqueryScope enclosedByStatement{this->context};
                sub = selectLikeSubqueryForm(this->coordinator, this->context, *existsNode->select, compoundSubquery);
            }
            // `exists()` writes its argument bare — `EXISTS SELECT … UNION SELECT …` — and SQLite
            // rejects that, so a compound leaves the statement unmapped here the way it does in a
            // value slot. `in(x, union_(…))` is the form that does parenthesize, and it is
            // generated by the IN branch below.
            if (!sub.code.empty() && compoundSubquery) {
                auto placeholder = unsupportedPlaceholder(
                    this->context,
                    "EXISTS (SELECT ...)",
                    "EXISTS over a compound SELECT is not mapped to sqlite_orm codegen: its "
                    "union_()/intersect()/except() form is a statement, which sqlite_orm serializes without "
                    "the parentheses EXISTS needs",
                    *existsNode);
                placeholder.decisionPoints = std::move(sub.decisionPoints);
                placeholder.warnings.insert(placeholder.warnings.end(),
                                            std::make_move_iterator(sub.warnings.begin()),
                                            std::make_move_iterator(sub.warnings.end()));
                return placeholder;
            }
            if (sub.code.empty()) {
                auto placeholder = unsupportedPlaceholder(this->context,
                                                          "EXISTS (SELECT ...)",
                                                          "EXISTS (SELECT ...) is not mapped to sqlite_orm codegen",
                                                          *existsNode);
                placeholder.decisionPoints = std::move(sub.decisionPoints);
                placeholder.warnings.insert(placeholder.warnings.end(),
                                            std::make_move_iterator(sub.warnings.begin()),
                                            std::make_move_iterator(sub.warnings.end()));
                return placeholder;
            }
            this->context.recordFormWithoutDefaultConstructor("EXISTS");
            return CodeGenResult{"exists(" + sub.code + ")", std::move(sub.decisionPoints), std::move(sub.warnings)};
        } else if (auto* inNode = dynamic_cast<const InNode*>(&astNode)) {
            return this->generateIn(inNode);
        } else if (auto* likeNode = dynamic_cast<const LikeNode*>(&astNode)) {
            return this->generateLike(likeNode);
        } else if (auto* globNode = dynamic_cast<const GlobNode*>(&astNode)) {
            return this->generateGlob(globNode);
        } else if (auto* matchNode = dynamic_cast<const MatchNode*>(&astNode)) {
            return this->generateMatch(matchNode);
        } else if (auto* castNode = dynamic_cast<const CastNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*castNode->operand);
            std::string cppType = castTypeToCpp(castNode->typeName);
            return spannedResult("cast<" + cppType + ">(" + SpannedCode::takenFrom(operandResult) + ")",
                                 std::move(operandResult.decisionPoints),
                                 std::move(operandResult.warnings));
        } else if (auto* caseNode = dynamic_cast<const CaseNode*>(&astNode)) {
            std::vector<DecisionPoint> decisionPoints;
            std::vector<CodegenWarning> warnings;
            // `case_<R>` reads every row of the column through the one `R`, while SQLite answers
            // the CASE with the value of whichever branch matched, so `R` is the widest type over
            // all the branch results and the ELSE. Taken from the first branch alone it truncated
            // every wider one silently: `CASE WHEN a < 0 THEN 1 ELSE 9223372036854775807 END` read
            // back as -1 through an `int`.
            std::vector<const AstNode*> resultNodes;
            resultNodes.reserve(caseNode->branches.size() + 1);
            for (const auto& branch: caseNode->branches) {
                resultNodes.push_back(branch.result.get());
            }
            resultNodes.push_back(caseNode->elseResult.get());
            const std::string returnType = this->context.inferWidestTypeFromNodes(resultNodes);
            SpannedCode code = "case_<" + returnType + ">(";
            if (caseNode->operand) {
                auto operandResult = this->coordinator.generateNode(*caseNode->operand);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(operandResult.decisionPoints.begin()),
                                      std::make_move_iterator(operandResult.decisionPoints.end()));
                appendUniqueWarnings(warnings, operandResult.warnings);
                code += SpannedCode::takenFrom(operandResult);
            }
            code += ")";
            for (auto& branch: caseNode->branches) {
                auto condResult = this->coordinator.generateNode(*branch.condition);
                auto resResult = this->coordinator.generateNode(*branch.result);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(condResult.decisionPoints.begin()),
                                      std::make_move_iterator(condResult.decisionPoints.end()));
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(resResult.decisionPoints.begin()),
                                      std::make_move_iterator(resResult.decisionPoints.end()));
                appendUniqueWarnings(warnings, condResult.warnings);
                appendUniqueWarnings(warnings, resResult.warnings);
                code += ".when(" + SpannedCode::takenFrom(condResult) + ", then(" + SpannedCode::takenFrom(resResult) +
                        "))";
            }
            if (caseNode->elseResult) {
                auto elseResult = this->coordinator.generateNode(*caseNode->elseResult);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(elseResult.decisionPoints.begin()),
                                      std::make_move_iterator(elseResult.decisionPoints.end()));
                appendUniqueWarnings(warnings, elseResult.warnings);
                code += ".else_(" + SpannedCode::takenFrom(elseResult) + ")";
            }
            code += ".end()";
            return spannedResult(std::move(code), std::move(decisionPoints), std::move(warnings));
        } else if (auto* bindParam = dynamic_cast<const BindParameterNode*>(&astNode)) {
            std::string paramStr(bindParam->value);
            auto bindParameterMessage = [&paramStr](const std::string& variable) {
                return "bind parameter " + paramStr + " -> C++ variable '" + variable +
                       "'; for prepared statements use storage.prepare() + get<N>(stmt)";
            };
            std::string cppVar;
            if (paramStr.size() > 1 && (paramStr[0] == ':' || paramStr[0] == '@' || paramStr[0] == '$')) {
                cppVar = toCppIdentifier(paramStr.substr(1));
            } else if (paramStr == "?") {
                cppVar = "bindParam" + std::to_string(++this->context.nextBindParamIndex);
            } else if (paramStr.size() > 1 && paramStr[0] == '?') {
                cppVar = "bindParam" + paramStr.substr(1);
            } else {
                // A marker with nothing behind it — a lone `:`, which SQLite refuses outright —
                // names no variable, so a placeholder stands where the value would have gone.
                return unsupportedPlaceholder(this->context, paramStr, bindParameterMessage, *bindParam);
            }
            return CodeGenResult{cppVar, {}, {bindParameterMessage(cppVar)}};
        } else if (auto* collateNode = dynamic_cast<const CollateNode*>(&astNode)) {
            auto operandResult = this->coordinator.generateNode(*collateNode->operand);
            operandResult.warnings.push_back("COLLATE " + collateNode->collationName +
                                             " on expressions is not directly supported in sqlite_orm codegen");
            return operandResult;
        } else if (auto* funcCall = dynamic_cast<const FunctionCallNode*>(&astNode)) {
            return this->generateFunctionCall(funcCall);
        }
        return CodeGenResult{};
    }

    bool ExpressionCodeGenerator::nodeGeneratesColumnPointer(const AstNode* node) const {
        if (auto* qualifiedColumnRefNode = dynamic_cast<const QualifiedColumnRefNode*>(node)) {
            std::string tableKey = normalizeSqlIdentifier(qualifiedColumnRefNode->tableName);
            if (this->context.activeCteTypedefByTableKey.find(tableKey) !=
                this->context.activeCteTypedefByTableKey.end())
                return true;
            std::string tableName = std::string(qualifiedColumnRefNode->tableName);
            if (this->context.activeTableAliases.find(tableName) != this->context.activeTableAliases.end())
                return true;
            if (policyEquals(this->context.codeGenPolicy, "column_ref_style", "column_pointer"))
                return true;
            return false;
        }
        if (dynamic_cast<const ColumnRefNode*>(node)) {
            if (this->context.implicitSingleSourceCteTypedef)
                return true;
            if (policyEquals(this->context.codeGenPolicy, "column_ref_style", "column_pointer"))
                return true;
        }
        return false;
    }

}  // namespace sqlite2orm
