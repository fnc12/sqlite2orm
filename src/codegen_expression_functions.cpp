#include "codegen_expression.h"
#include "codegen_context.h"
#include "codegen_forms.h"
#include "codegen_utils.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>
#include <sqlite2orm/validator.h>

namespace sqlite2orm {

    CodeGenResult ExpressionCodeGenerator::generateFunctionCall(const FunctionCallNode* funcCall) {
        std::string funcName = toLowerAscii(funcCall->name);
        if (const SqliteOrmFunctionForm* form = sqliteOrmFunctionForm(funcName);
            form != nullptr && form->kind == SqliteOrmFormKind::matchFunction) {
            ++this->context.generatedMatchCount;
        }
        std::vector<DecisionPoint> decisionPoints;
        std::vector<CodegenWarning> funcWarnings;
        SpannedCode baseCode;
        // Whether the call comes out a `count_asterisk_t`, the one star form sqlite_orm holds
        // a row type and a `filter()` for; a `count(*)` over no FROM clause and every other
        // star are written as `name()` instead.
        bool generatedAsCountAsterisk = false;
        // A name the generator knows nothing about is written as a `func<…>()` call over a
        // stub. A star names no arguments to write one from, so it never takes that road.
        const bool customFunction = !funcCall->star && !isKnownSqlFunction(funcName);

        if (funcCall->star) {
            if (funcName == "count" && !this->context.fromTableAliasToStructName.empty()) {
                // An aliased source is only named here where the select writes its FROM out:
                // `count<alias_a<T>>()` carries no table into an inferred FROM, while
                // `count<T>()` carries the plain table — the one the alias replaced.
                const bool countsAliasedSource = this->context.implicitSourceAlias &&
                                                 this->context.canNameInferredFromSources &&
                                                 !this->context.implicitSingleSourceCteTypedef;
                const std::string& countRowType =
                    this->context.implicitSingleSourceCteTypedef ? *this->context.implicitSingleSourceCteTypedef
                    : countsAliasedSource                        ? this->context.implicitSourceAlias->ormAliasType
                                                                 : this->context.structName;
                if (countsAliasedSource) {
                    this->context.countedAliasedSource = true;
                }
                baseCode = "count<" + countRowType + ">()";
                this->context.recordEmittedTableType(countRowType);
                generatedAsCountAsterisk = true;
            } else {
                baseCode = sqliteOrmCallSpelling(funcName) + "()";
            }
        } else {
            CustomFunctionUse customUse;
            if (customFunction) {
                customUse.sqlName = funcCall->name;
                customUse.structName = toStructName(funcCall->name);
            }
            SpannedCode argList;
            for (size_t argIndex = 0; argIndex < funcCall->arguments.size(); ++argIndex) {
                const AstNode& argNode = *funcCall->arguments.at(argIndex);
                auto argResult = this->coordinator.generateNode(argNode);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(argResult.decisionPoints.begin()),
                                      std::make_move_iterator(argResult.decisionPoints.end()));
                funcWarnings.insert(funcWarnings.end(),
                                    std::make_move_iterator(argResult.warnings.begin()),
                                    std::make_move_iterator(argResult.warnings.end()));
                if (argIndex > 0)
                    argList += ", ";
                argList += SpannedCode::takenFrom(argResult);
                if (customFunction) {
                    customUse.argTypes.push_back(this->context.customFunctionArgType(argNode));
                    if (auto* col = dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(argNode))) {
                        customUse.argNames.push_back(toCppIdentifier(col->columnName));
                    } else {
                        customUse.argNames.push_back("arg" + std::to_string(argIndex));
                    }
                }
            }
            if (!funcCall->star && !funcCall->arguments.empty() && sqliteScalarFirstArgTextContext(funcName)) {
                if (auto* col = dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(*funcCall->arguments.at(0)))) {
                    this->context.registerPrefixColumn(toCppIdentifier(col->columnName), "std::string");
                }
            }
            if (customFunction) {
                this->context.registerCustomFunction(std::move(customUse));
                baseCode = "func<" + toStructName(funcCall->name) + ">(" + argList + ")";
            } else {
                // A builtin whose result type sqlite_orm cannot deduce is generated with the
                // one it is read back through spelled out; every other call names none. The
                // name itself is the library's spelling of it, which is the SQL name but for
                // the three the library spells otherwise, qualified where the C library
                // declares the same name.
                const std::string callName =
                    sqliteOrmCallSpelling(funcName) + functionCallResultTypeArgument(*funcCall, this->context);
                if (funcCall->distinct && !argList.empty()) {
                    baseCode = callName + "(distinct(" + argList + "))";
                } else {
                    baseCode = callName + "(" + argList + ")";
                }
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
        if (!functionCallHasDefaultConstructor(funcName, funcCall->star)) {
            this->context.recordFormWithoutDefaultConstructor(std::string(funcCall->name) + "()");
        }

        if (funcCall->filterWhere) {
            auto filterResult = this->coordinator.generateNode(*funcCall->filterWhere);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(filterResult.decisionPoints.begin()),
                                  std::make_move_iterator(filterResult.decisionPoints.end()));
            funcWarnings.insert(funcWarnings.end(),
                                std::make_move_iterator(filterResult.warnings.begin()),
                                std::make_move_iterator(filterResult.warnings.end()));
            baseCode += ".filter(where(" + SpannedCode::takenFrom(filterResult) + "))";
        }
        if (funcCall->over) {
            std::string overArgs = this->codegenOverClause(*funcCall->over, decisionPoints, funcWarnings);
            baseCode += ".over(" + overArgs + ")";
        }

        // The gate every generated call passes: whether sqlite_orm has a form for the call as
        // written is the registry's answer, and a call it has none for is placeheld rather
        // than emitted at a guess for a compiler to refuse. It runs here, once the arguments,
        // the FILTER and the OVER have been generated, so that whatever they collected is
        // carried along with the placeholder.
        if (auto refusal = functionCallFormRefusal(*funcCall, generatedAsCountAsterisk, customFunction)) {
            return unsupportedPlaceholder(this->context,
                                          funcCall->name + "(...)",
                                          std::move(*refusal),
                                          *funcCall,
                                          CodeGenResult{{}, std::move(decisionPoints), std::move(funcWarnings)});
        }
        return spannedResult(std::move(baseCode), std::move(decisionPoints), std::move(funcWarnings));
    }

    std::string ExpressionCodeGenerator::codegenWindowFrameBound(const WindowFrameBound& bound,
                                                                 std::vector<DecisionPoint>& decisionPoints,
                                                                 std::vector<CodegenWarning>& warnings) {
        switch (bound.kind) {
            case WindowFrameBoundKind::unboundedPreceding:
                return "unbounded_preceding()";
            case WindowFrameBoundKind::currentRow:
                return "current_row()";
            case WindowFrameBoundKind::unboundedFollowing:
                return "unbounded_following()";
            case WindowFrameBoundKind::exprPreceding:
            case WindowFrameBoundKind::exprFollowing: {
                if (!bound.expr) {
                    return {};
                }
                auto expressionResult = this->coordinator.generateNode(*bound.expr);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(expressionResult.decisionPoints.begin()),
                                      std::make_move_iterator(expressionResult.decisionPoints.end()));
                warnings.insert(warnings.end(),
                                std::make_move_iterator(expressionResult.warnings.begin()),
                                std::make_move_iterator(expressionResult.warnings.end()));
                if (bound.kind == WindowFrameBoundKind::exprPreceding) {
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
        switch (frame.unit) {
            case WindowFrameUnit::rows:
                // Qualified because a select statement's result is declared `auto rows = ...`, and
                // that name is in scope in its own initializer: a bare `rows(...)` inside it names
                // the variable, not the frame, and fails with "use of 'rows' before deduction".
                frameApi = "sqlite_orm::rows";
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
        if (start.empty() || end.empty()) {
            return {};
        }
        std::string core = std::string(frameApi) + "(" + start + ", " + end + ")";
        switch (frame.exclude) {
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
        if (overClause.namedWindow) {
            return "window_ref(" + identifierToCppStringLiteral(*overClause.namedWindow) + ")";
        }
        std::vector<std::string> parts;
        if (!overClause.partitionBy.empty()) {
            std::string inner;
            for (size_t partitionByIndex = 0; partitionByIndex < overClause.partitionBy.size(); ++partitionByIndex) {
                if (partitionByIndex > 0) {
                    inner += ", ";
                }
                auto partitionResult = this->coordinator.generateNode(*overClause.partitionBy.at(partitionByIndex));
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(partitionResult.decisionPoints.begin()),
                                      std::make_move_iterator(partitionResult.decisionPoints.end()));
                warnings.insert(warnings.end(),
                                std::make_move_iterator(partitionResult.warnings.begin()),
                                std::make_move_iterator(partitionResult.warnings.end()));
                inner += partitionResult.code;
            }
            parts.push_back("partition_by(" + inner + ")");
        }
        if (!overClause.orderBy.empty()) {
            // `order_by_t` declares a constructor and no default one, and it is the only part of a
            // window definition that does: `partition_by` and the frame boundaries are aggregates.
            this->context.recordFormWithoutDefaultConstructor("ORDER BY");
            auto formatOrderTerm = [&](const OrderByTerm& term) -> std::string {
                auto expressionResult = this->coordinator.generateNode(*term.expression);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(expressionResult.decisionPoints.begin()),
                                      std::make_move_iterator(expressionResult.decisionPoints.end()));
                warnings.insert(warnings.end(),
                                std::make_move_iterator(expressionResult.warnings.begin()),
                                std::make_move_iterator(expressionResult.warnings.end()));
                std::string termCode = "order_by(" + expressionResult.code + ")";
                if (term.direction == SortDirection::asc) {
                    termCode += ".asc()";
                } else if (term.direction == SortDirection::desc) {
                    termCode += ".desc()";
                }
                if (!term.collation.empty()) {
                    std::string collLower = toLowerAscii(term.collation);
                    if (collLower == "nocase") {
                        termCode += ".collate_nocase()";
                    } else if (collLower == "binary") {
                        termCode += ".collate_binary()";
                    } else if (collLower == "rtrim") {
                        termCode += ".collate_rtrim()";
                    } else {
                        termCode += ".collate(" + identifierToCppStringLiteral(term.collation) + ")";
                        warnings.push_back("COLLATE " + term.collation +
                                           " in window ORDER BY is not a built-in collation; generated .collate(...) "
                                           "uses literal name");
                    }
                }
                return termCode;
            };
            if (overClause.orderBy.size() == 1) {
                parts.push_back(formatOrderTerm(overClause.orderBy.at(0)));
            } else {
                std::string multi = "multi_order_by(";
                for (size_t orderIndex = 0; orderIndex < overClause.orderBy.size(); ++orderIndex) {
                    if (orderIndex > 0) {
                        multi += ", ";
                    }
                    multi += formatOrderTerm(overClause.orderBy.at(orderIndex));
                }
                multi += ")";
                parts.push_back(std::move(multi));
            }
        }
        if (overClause.frame) {
            std::string frameCode = this->codegenWindowFrameSpec(*overClause.frame, decisionPoints, warnings);
            if (!frameCode.empty()) {
                parts.push_back(std::move(frameCode));
            }
        }
        if (parts.empty()) {
            return {};
        }
        std::string joined = parts.at(0);
        for (size_t partIndex = 1; partIndex < parts.size(); ++partIndex) {
            joined += ", ";
            joined += parts.at(partIndex);
        }
        return joined;
    }

}  // namespace sqlite2orm
