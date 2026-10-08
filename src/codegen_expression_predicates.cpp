#include "codegen_expression.h"
#include "codegen_context.h"
#include "codegen_utils.h"
#include "expression_subquery_scope.h"
#include "match_field_scope.h"
#include <sqlite2orm/codegen.h>
#include <algorithm>

namespace sqlite2orm {

    namespace {
        /**
         *  `code`, delimited with a CAST to INTEGER when the argument it was generated from
         *  serializes as a NOT, an AND or an OR, and the comment that explains the CAST recorded in
         *  `context`. A predicate serializer parenthesizes no argument of its own, and SQLite binds
         *  those looser than every predicate, so a bare one there would take the predicate into
         *  itself.
         */
        SpannedCode
        groupPredicateArgument(SpannedCode code, const AstNode& argumentNode, CodeGeneratorContext& context) {
            if (!predicateArgumentNeedsGroupingCast(argumentNode, context.codeGenPolicy)) {
                return code;
            }
            const bool andOr = serializedSqlPrecedence(argumentNode, context.codeGenPolicy) >= kSqlPrecedenceAnd;
            context.recordComment(
                sourceSpanComment(andOr ? kCommentAndOrPredicateArgumentCast : kCommentNotPredicateArgumentCast,
                                  argumentNode));
            return "cast<" + sqliteTypeToCpp("INTEGER") + ">(" + code + ")";
        }

        /**
         *  `code` for an argument standing after the keyword of a LIKE, GLOB or MATCH — the pattern
         *  or the ESCAPE — where SQLite regroups a predicate as well as an AND or an OR: the
         *  predicate before it is read left-associatively at the same rank. A NOT, an AND or an OR
         *  keeps the comment `groupPredicateArgument` gives it.
         */
        SpannedCode
        groupPredicatePattern(SpannedCode code, const AstNode& argumentNode, CodeGeneratorContext& context) {
            if (predicateArgumentNeedsGroupingCast(argumentNode, context.codeGenPolicy)) {
                return groupPredicateArgument(std::move(code), argumentNode, context);
            }
            if (!predicatePatternNeedsGroupingCast(argumentNode, context.codeGenPolicy)) {
                return code;
            }
            context.recordComment(sourceSpanComment(kCommentPredicatePatternCast, argumentNode));
            return "cast<" + sqliteTypeToCpp("INTEGER") + ">(" + code + ")";
        }

    }

    CodeGenResult ExpressionCodeGenerator::generateIsNull(const IsNullNode* isNullNode) {
        auto operandResult = this->coordinator.generateNode(*isNullNode->operand);
        SpannedCode operandCode =
            groupPredicateArgument(SpannedCode::takenFrom(operandResult), *isNullNode->operand, this->context);
        this->context.recordFormWithoutDefaultConstructor("IS NULL");
        return spannedResult("is_null(" + operandCode + ")",
                             std::move(operandResult.decisionPoints),
                             std::move(operandResult.warnings));
    }

    CodeGenResult ExpressionCodeGenerator::generateIsNotNull(const IsNotNullNode* isNotNullNode) {
        auto operandResult = this->coordinator.generateNode(*isNotNullNode->operand);
        SpannedCode operandCode =
            groupPredicateArgument(SpannedCode::takenFrom(operandResult), *isNotNullNode->operand, this->context);
        this->context.recordFormWithoutDefaultConstructor("IS NOT NULL");
        return spannedResult("is_not_null(" + operandCode + ")",
                             std::move(operandResult.decisionPoints),
                             std::move(operandResult.warnings));
    }

    CodeGenResult ExpressionCodeGenerator::generateBetween(const BetweenNode* betweenNode) {
        auto operandResult = this->coordinator.generateNode(*betweenNode->operand);
        auto lowResult = this->coordinator.generateNode(*betweenNode->low);
        auto highResult = this->coordinator.generateNode(*betweenNode->high);

        // A COLLATE and a unary plus emit their operand and nothing else, so the column this
        // is compared against is the one standing under them.
        if (auto* col = dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(*betweenNode->operand))) {
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

        auto warnings = std::move(operandResult.warnings);
        appendUniqueWarnings(warnings, lowResult.warnings);
        appendUniqueWarnings(warnings, highResult.warnings);

        SpannedCode operandCode =
            groupPredicateArgument(SpannedCode::takenFrom(operandResult), *betweenNode->operand, this->context);
        SpannedCode lowCode =
            groupPredicateArgument(SpannedCode::takenFrom(lowResult), *betweenNode->low, this->context);
        SpannedCode highCode =
            groupPredicateArgument(SpannedCode::takenFrom(highResult), *betweenNode->high, this->context);

        // sqlite_orm deduces one `T` from both bounds of `between(A, T, T)`, so bounds
        // generated as different C++ types do not compile — and SQLite takes the SQL either
        // way, `a BETWEEN 1 AND 3000000000` as readily as `a BETWEEN 1 AND 'x'`.
        switch (oneDeducedTypeForm({betweenNode->low.get(), betweenNode->high.get()})) {
            case OneDeducedTypeForm::asWritten:
                break;
            case OneDeducedTypeForm::widenedToInt64:
                lowCode = widenToInt64(*betweenNode->low, std::move(lowCode));
                highCode = widenToInt64(*betweenNode->high, std::move(highCode));
                this->context.recordComment(sourceSpanComment(kCommentBetweenBoundsWidened, *betweenNode));
                break;
            case OneDeducedTypeForm::noCommonType:
                warnings.push_back(sourceSpanWarning(
                    "sqlite_orm's between(A, T, T) deduces one C++ type from both bounds of a BETWEEN, and " +
                        generatedValueTypeDescription(*betweenNode->low) + " next to " +
                        generatedValueTypeDescription(*betweenNode->high) +
                        " is not one type, so the generated code does not compile",
                    *betweenNode));
                break;
        }

        this->context.recordFormWithoutDefaultConstructor("BETWEEN");
        SpannedCode betweenCode = "between(" + operandCode + ", " + lowCode + ", " + highCode + ")";
        SpannedCode code = betweenNode->negated ? "!" + betweenCode : betweenCode;
        return spannedResult(std::move(code), std::move(decisionPoints), std::move(warnings));
    }

    CodeGenResult ExpressionCodeGenerator::generateIn(const InNode* inNode) {
        // An IN whose right-hand side sqlite_orm has no form for generates a placeholder, and
        // the operand generated for it goes with the rest of the node: the comments recorded
        // from here on explain forms that are then not in the generated code at all.
        const size_t inCommentMark = this->context.commentMark();
        if (!inNode->tableName.empty()) {
            auto operandResult = this->coordinator.generateNode(*inNode->operand);
            const std::string normalizedTableKey = normalizeSqlIdentifier(inNode->tableName);
            auto cteIt = this->context.activeCteTypedefByTableKey.find(normalizedTableKey);
            if (cteIt != this->context.activeCteTypedefByTableKey.end()) {
                const std::string& cteTypedef = cteIt->second;
                auto colNamesIt = this->context.cteColumnNamesByTableKey.find(normalizedTableKey);
                std::string selectColumnCode;
                if (colNamesIt != this->context.cteColumnNamesByTableKey.end() && !colNamesIt->second.empty()) {
                    const auto& columnNames = colNamesIt->second;
                    if (this->context.withCteCpp20Monikers()) {
                        auto monikerIt = this->context.withCteCpp20MonikerVarByCteKey.find(normalizedTableKey);
                        std::string monikerVar = (monikerIt != this->context.withCteCpp20MonikerVarByCteKey.end())
                                                     ? monikerIt->second
                                                     : cteTypedef;
                        if (columnNames.size() == 1) {
                            const std::string colKey =
                                normalizedTableKey + "|" + normalizeSqlIdentifier(columnNames[0]);
                            auto colVarIt = this->context.withCteCpp20ColVarByPipeKey.find(colKey);
                            if (colVarIt != this->context.withCteCpp20ColVarByPipeKey.end()) {
                                selectColumnCode = monikerVar + "->*" + colVarIt->second;
                            } else {
                                auto baseIt = this->context.cteBaseStructByKey.find(normalizedTableKey);
                                std::string baseStruct = baseIt != this->context.cteBaseStructByKey.end()
                                                             ? baseIt->second
                                                             : this->context.structName;
                                selectColumnCode =
                                    monikerVar + "->*&" + baseStruct + "::" + toCppIdentifier(columnNames[0]);
                            }
                        } else {
                            std::string cols;
                            for (size_t columnIndex = 0; columnIndex < columnNames.size(); ++columnIndex) {
                                if (columnIndex > 0)
                                    cols += ", ";
                                const std::string colKey =
                                    normalizedTableKey + "|" + normalizeSqlIdentifier(columnNames[columnIndex]);
                                auto colVarIt = this->context.withCteCpp20ColVarByPipeKey.find(colKey);
                                if (colVarIt != this->context.withCteCpp20ColVarByPipeKey.end()) {
                                    cols += monikerVar + "->*" + colVarIt->second;
                                } else {
                                    auto baseIt = this->context.cteBaseStructByKey.find(normalizedTableKey);
                                    std::string baseStruct = baseIt != this->context.cteBaseStructByKey.end()
                                                                 ? baseIt->second
                                                                 : this->context.structName;
                                    cols += monikerVar + "->*&" + baseStruct +
                                            "::" + toCppIdentifier(columnNames[columnIndex]);
                                }
                            }
                            selectColumnCode = "columns(" + cols + ")";
                        }
                    } else {
                        if (columnNames.size() == 1) {
                            selectColumnCode =
                                "column<" + cteTypedef + ">(" + identifierToCppStringLiteral(columnNames[0]) + ")";
                        } else {
                            std::string cols;
                            for (size_t columnIndex = 0; columnIndex < columnNames.size(); ++columnIndex) {
                                if (columnIndex > 0)
                                    cols += ", ";
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
                SpannedCode operandCode =
                    groupPredicateArgument(SpannedCode::takenFrom(operandResult), *inNode->operand, this->context);
                SpannedCode code = inFunc + "(" + operandCode + ", select(" + selectColumnCode + "))";
                return spannedResult(std::move(code),
                                     std::move(operandResult.decisionPoints),
                                     std::move(operandResult.warnings));
            }
            CodeGenResult carried;
            carried.decisionPoints = std::move(operandResult.decisionPoints);
            carried.warnings = std::move(operandResult.warnings);
            this->context.discardCommentsSince(inCommentMark);
            return unsupportedPlaceholder(this->context,
                                          operandResult.code + " IN " + inNode->tableName,
                                          "IN table-name is not supported in sqlite_orm codegen",
                                          *inNode,
                                          std::move(carried));
        }
        if (inNode->subquerySelect) {
            auto operandResult = this->coordinator.generateNode(*inNode->operand);
            CodeGenResult sub;
            {
                ExpressionSubqueryScope enclosedByStatement{this->context};
                sub = this->coordinator.tryCodegenSelectLikeSubquery(*inNode->subquerySelect);
            }
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
            if (sub.code.empty()) {
                CodeGenResult carried;
                carried.decisionPoints = std::move(decisionPoints);
                carried.warnings = std::move(inSubWarnings);
                this->context.discardCommentsSince(inCommentMark);
                return unsupportedPlaceholder(this->context,
                                              "IN (SELECT ...)",
                                              "IN (SELECT ...) is not mapped to sqlite_orm codegen",
                                              *inNode,
                                              std::move(carried));
            }
            SpannedCode operandCode =
                groupPredicateArgument(SpannedCode::takenFrom(operandResult), *inNode->operand, this->context);
            this->context.recordFormWithoutDefaultConstructor("IN");
            SpannedCode code = inNode->negated ? "not_in(" + operandCode + ", " + sub.code + ")"
                                               : "in(" + operandCode + ", " + sub.code + ")";
            return spannedResult(std::move(code), std::move(decisionPoints), std::move(inSubWarnings));
        }
        auto operandResult = this->coordinator.generateNode(*inNode->operand);
        auto decisionPoints = std::move(operandResult.decisionPoints);
        auto warnings = std::move(operandResult.warnings);

        if (auto* col = dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(*inNode->operand))) {
            // Every value of the list is compared against the column, not just the first one,
            // so the field has to hold the widest of them.
            const std::string cppName = toCppIdentifier(col->columnName);
            for (const auto& value: inNode->values) {
                this->context.registerPrefixColumn(cppName, this->context.inferTypeFromNode(*value));
            }
        }

        std::vector<const AstNode*> valueNodes;
        valueNodes.reserve(inNode->values.size());
        for (const auto& value: inNode->values) {
            valueNodes.push_back(value.get());
        }
        // sqlite_orm deduces one `E` from the whole of `in(A, std::initializer_list<E>)`, so
        // values generated as different C++ types do not compile — and SQLite takes the SQL
        // either way, `x IN (1, 3000000000)` as readily as `x IN (1, 'a')`.
        const OneDeducedTypeForm valuesForm = inValuesForm(valueNodes);

        SpannedCode valuesList;
        // Merged into `warnings` once after the loop rather than value by value: every bind
        // parameter warns with a message of its own, and a merge per value would compare each
        // of them against all the ones before it.
        std::vector<CodegenWarning> valueWarnings;
        for (size_t valueIndex = 0; valueIndex < inNode->values.size(); ++valueIndex) {
            const AstNode& value = *inNode->values.at(valueIndex);
            auto valueResult = this->coordinator.generateNode(value);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(valueResult.decisionPoints.begin()),
                                  std::make_move_iterator(valueResult.decisionPoints.end()));
            valueWarnings.insert(valueWarnings.end(),
                                 std::make_move_iterator(valueResult.warnings.begin()),
                                 std::make_move_iterator(valueResult.warnings.end()));
            if (valueIndex > 0)
                valuesList += ", ";
            valuesList += valuesForm == OneDeducedTypeForm::widenedToInt64
                              ? widenToInt64(value, SpannedCode::takenFrom(valueResult))
                              : SpannedCode::takenFrom(valueResult);
        }
        appendUniqueWarnings(warnings, valueWarnings);
        switch (valuesForm) {
            case OneDeducedTypeForm::asWritten:
                break;
            case OneDeducedTypeForm::widenedToInt64:
                this->context.recordComment(sourceSpanComment(kCommentInValuesWidened, *inNode));
                break;
            case OneDeducedTypeForm::noCommonType: {
                // The warning names the pair of values that proves the list has no one type,
                // which in a longer list is not necessarily the first two of them. A reason to
                // have no common type that no pair proves would leave it without one to name,
                // and then the warning says only what it knows.
                const auto [firstValue, secondValue] = firstNoCommonTypePair(valueNodes);
                const std::string types = firstValue && secondValue
                                              ? generatedValueTypeDescription(*firstValue) + " next to " +
                                                    generatedValueTypeDescription(*secondValue) + " is not one type"
                                              : "these values are not one type";
                warnings.push_back(sourceSpanWarning(
                    "sqlite_orm's in(A, std::initializer_list<E>) deduces one C++ type from every value of an IN "
                    "list, and " +
                        types + ", so the generated code does not compile",
                    *inNode));
                break;
            }
        }

        // An empty list has no value to deduce that `E` from at all — `in(&User::x, {})` does
        // not compile — while SQLite reads `x IN ()` as a test that is simply always false.
        // An empty vector names a type instead, and sqlite_orm serializes it to the same
        // `IN ()`, binding nothing.
        const SpannedCode valuesCode =
            inNode->values.empty() ? SpannedCode("std::vector<int64_t>{}") : "{" + valuesList + "}";

        // Every value of the list is delimited by the commas around it, so only the operand
        // stands where the SQL an AND or an OR serializes into would run into the predicate.
        SpannedCode inOperandCode =
            groupPredicateArgument(SpannedCode::takenFrom(operandResult), *inNode->operand, this->context);
        this->context.recordFormWithoutDefaultConstructor("IN");
        SpannedCode inCode = "in(" + inOperandCode + ", " + valuesCode + ")";
        if (inNode->negated) {
            SpannedCode notInCode = "not_in(" + inOperandCode + ", " + valuesCode + ")";
            SpannedCode negatedInCode = "!" + inCode;
            const bool useOperator = policyEquals(this->context.codeGenPolicy, "negation_style", "operator_excl");
            const SpannedCode& chosenNegation = useOperator ? negatedInCode : notInCode;
            // options lists every variant (the chosen one included).
            decisionPoints.push_back(
                DecisionPoint{this->context.nextDecisionPointId++,
                              "negation_style",
                              useOperator ? "operator_excl" : "not_in",
                              chosenNegation.text(),
                              {Option{"not_in", notInCode.text(), "use not_in()"},
                               Option{"operator_excl", negatedInCode.text(), "use the ! operator"}}});
            return spannedResult(chosenNegation, std::move(decisionPoints), std::move(warnings));
        }
        return spannedResult(std::move(inCode), std::move(decisionPoints), std::move(warnings));
    }

    CodeGenResult ExpressionCodeGenerator::generateLike(const LikeNode* likeNode) {
        auto operandResult = this->coordinator.generateNode(*likeNode->operand);
        auto patternResult = this->coordinator.generateNode(*likeNode->pattern);

        if (auto* col = dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(*likeNode->operand))) {
            this->context.registerPrefixColumn(toCppIdentifier(col->columnName), "std::string");
        }

        auto decisionPoints = std::move(operandResult.decisionPoints);
        decisionPoints.insert(decisionPoints.end(),
                              std::make_move_iterator(patternResult.decisionPoints.begin()),
                              std::make_move_iterator(patternResult.decisionPoints.end()));
        auto warnings = std::move(operandResult.warnings);
        appendUniqueWarnings(warnings, patternResult.warnings);

        SpannedCode operandCode =
            groupPredicateArgument(SpannedCode::takenFrom(operandResult), *likeNode->operand, this->context);
        SpannedCode patternCode =
            groupPredicatePattern(SpannedCode::takenFrom(patternResult), *likeNode->pattern, this->context);

        this->context.recordFormWithoutDefaultConstructor("LIKE");
        SpannedCode likeCode = "like(" + operandCode + ", " + patternCode;
        if (likeNode->escape) {
            auto escapeResult = this->coordinator.generateNode(*likeNode->escape);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(escapeResult.decisionPoints.begin()),
                                  std::make_move_iterator(escapeResult.decisionPoints.end()));
            appendUniqueWarnings(warnings, escapeResult.warnings);
            likeCode +=
                ", " + groupPredicatePattern(SpannedCode::takenFrom(escapeResult), *likeNode->escape, this->context);
        }
        likeCode += ")";

        SpannedCode code = likeNode->negated ? "!" + likeCode : likeCode;
        return spannedResult(std::move(code), std::move(decisionPoints), std::move(warnings));
    }

    CodeGenResult ExpressionCodeGenerator::generateGlob(const GlobNode* globNode) {
        auto operandResult = this->coordinator.generateNode(*globNode->operand);
        auto patternResult = this->coordinator.generateNode(*globNode->pattern);

        if (auto* col = dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(*globNode->operand))) {
            this->context.registerPrefixColumn(toCppIdentifier(col->columnName), "std::string");
        }

        auto decisionPoints = std::move(operandResult.decisionPoints);
        decisionPoints.insert(decisionPoints.end(),
                              std::make_move_iterator(patternResult.decisionPoints.begin()),
                              std::make_move_iterator(patternResult.decisionPoints.end()));
        auto warnings = std::move(operandResult.warnings);
        appendUniqueWarnings(warnings, patternResult.warnings);

        SpannedCode operandCode =
            groupPredicateArgument(SpannedCode::takenFrom(operandResult), *globNode->operand, this->context);
        SpannedCode patternCode =
            groupPredicatePattern(SpannedCode::takenFrom(patternResult), *globNode->pattern, this->context);

        this->context.recordFormWithoutDefaultConstructor("GLOB");
        SpannedCode globCode = "glob(" + operandCode + ", " + patternCode + ")";
        SpannedCode code = globNode->negated ? "!" + globCode : globCode;
        return spannedResult(std::move(code), std::move(decisionPoints), std::move(warnings));
    }

    CodeGenResult ExpressionCodeGenerator::generateMatch(const MatchNode* matchNode) {
        ++this->context.generatedMatchCount;
        std::vector<DecisionPoint> decisionPoints;
        std::vector<CodegenWarning> warnings;

        // `fts_table MATCH pattern` targets the hidden FTS5 "any" column of that table.
        SpannedCode lhsCode;
        if (auto* col = dynamic_cast<const ColumnRefNode*>(matchNode->operand.get())) {
            const std::string key = normalizeSqlIdentifier(stripIdentifierQuotes(col->columnName));
            for (const auto& [fromName, mappedStructName]: this->context.fromTableAliasToStructName) {
                if (normalizeSqlIdentifier(stripIdentifierQuotes(fromName)) == key) {
                    const std::string hiddenColumn = "c<" + mappedStructName + ">()->*&fts5::hidden::any";
                    const auto aliasIt = this->context.activeTableAliases.find(fromName);
                    const bool aliased = aliasIt != this->context.activeTableAliases.end();
                    if (aliased && normalizeSqlIdentifier(stripIdentifierQuotes(aliasIt->second.tableName)) != key) {
                        // The name is the alias: the hidden column is named after the table
                        // only, so SQLite finds no such column and neither is one written here.
                        break;
                    }
                    // A source sqlite_orm gets with no alias written — the row of a bare `*`,
                    // a `cross_join<alias_a<Docs>>()` — is a plain `"docs"` in the SQL, and an
                    // `"a"."docs"` beside it names a source the statement does not have.
                    const bool writtenWithoutAlias =
                        aliased &&
                        std::find(this->context.sourcesWrittenWithoutAlias.begin(),
                                  this->context.sourcesWrittenWithoutAlias.end(),
                                  aliasIt->second.ormAliasType) != this->context.sourcesWrittenWithoutAlias.end();
                    if (aliased && !writtenWithoutAlias) {
                        const auto& info = aliasIt->second;
                        // The table of an aliased source is read through its alias alone, so
                        // its hidden column is the alias's column: `"a"."docs"`, not a
                        // `"docs"."docs"` naming a source the FROM does not have.
                        MatchFieldScope matchField{&this->context};
                        this->context.recordEmittedTableType(info.ormAliasType);
                        lhsCode = this->context.useCpp20TableAliasStyle()
                                      ? info.ormAliasType + "->*(" + hiddenColumn + ")"
                                      : "alias_column<" + info.ormAliasType + ">(" + hiddenColumn + ")";
                    } else {
                        lhsCode = hiddenColumn;
                    }
                    // The table name is the operand, written as the hidden column it stands for.
                    this->context.markExpression(lhsCode, *matchNode->operand);
                    warnings.push_back("MATCH against table \"" + std::string(col->columnName) +
                                       "\" maps to the hidden FTS5 'any' column; requires an FTS5 "
                                       "virtual table mapped as " +
                                       mappedStructName);
                    break;
                }
            }
        }
        if (lhsCode.empty()) {
            // Whatever this operand names, sqlite_orm will not find a FROM in it: the select
            // generator has to write that FROM out itself.
            MatchFieldScope matchField{&this->context};
            auto operandResult = this->coordinator.generateNode(*matchNode->operand);
            if (auto* col = dynamic_cast<const ColumnRefNode*>(&generatedOperandNode(*matchNode->operand))) {
                this->context.registerPrefixColumn(toCppIdentifier(col->columnName), "std::string");
            }
            decisionPoints = std::move(operandResult.decisionPoints);
            appendUniqueWarnings(warnings, operandResult.warnings);
            lhsCode = groupPredicateArgument(SpannedCode::takenFrom(operandResult), *matchNode->operand, this->context);
        }

        auto patternResult = this->coordinator.generateNode(*matchNode->pattern);
        decisionPoints.insert(decisionPoints.end(),
                              std::make_move_iterator(patternResult.decisionPoints.begin()),
                              std::make_move_iterator(patternResult.decisionPoints.end()));
        appendUniqueWarnings(warnings, patternResult.warnings);
        SpannedCode patternCode =
            groupPredicatePattern(SpannedCode::takenFrom(patternResult), *matchNode->pattern, this->context);

        // `match_t` is an aggregate holding its two operands, so unlike the other predicates it
        // default-constructs with them and can stand in a trigger's WHEN clause. A negated MATCH
        // does not compile at all, which the warning below is about.
        SpannedCode matchCode = "match(" + lhsCode + ", " + patternCode + ")";
        SpannedCode code = matchNode->negated ? "!" + matchCode : matchCode;
        if (matchNode->negated) {
            warnings.push_back("negated MATCH does not compile against sqlite_orm dev yet: match_t is not accepted by "
                               "operator! (https://github.com/fnc12/sqlite_orm/issues/1501)");
        }
        return spannedResult(std::move(code), std::move(decisionPoints), std::move(warnings));
    }

}  // namespace sqlite2orm
