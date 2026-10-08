#include "codegen_select.h"
#include "codegen_context.h"
#include "codegen_select_from.h"
#include "codegen_utils.h"
#include "emitted_table_type_scope.h"
#include "parenthesized_condition_scope.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

#include <algorithm>
#include <functional>
#include <utility>

namespace sqlite2orm {

    SelectCodeGenerator::SelectCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context) :
        coordinator(coordinator), context(context) {}

    CodeGenResult SelectCodeGenerator::generateCompoundSelect(const CompoundSelectNode& compoundNode) {
        // The rows of this compound are what the caller reads back, so its result columns are
        // widened here, at the one point that knows they are — and not in the generator the arms
        // share with a subquery, a view body, a CTE and an INSERT ... SELECT, whose columns go to
        // SQL itself and are read back by nobody.
        auto inner = this->coordinator.tryCodegenCompoundSelectSubexpression(
            compoundNode,
            compoundSelectResultWidening(compoundNode, this->context));
        std::vector<CodegenWarning> compoundWarnings = std::move(inner.warnings);
        if (inner.code.empty()) {
            auto placeholder = unsupportedStatementPlaceholder(this->context,
                                                               "compound SELECT",
                                                               "compound SELECT (UNION / INTERSECT / EXCEPT) is not "
                                                               "mapped to sqlite_orm codegen",
                                                               compoundNode);
            placeholder.decisionPoints = std::move(inner.decisionPoints);
            placeholder.warnings.insert(placeholder.warnings.end(),
                                        std::make_move_iterator(compoundWarnings.begin()),
                                        std::make_move_iterator(compoundWarnings.end()));
            return placeholder;
        }
        return CodeGenResult{"auto " + this->context.statementVariableName("rows") + " = storage.select(" + inner.code +
                                 ");",
                             std::move(inner.decisionPoints),
                             std::move(compoundWarnings)};
    }

    CodeGenResult SelectCodeGenerator::generateSelect(const SelectNode& selectNode) {
        std::vector<CodegenWarning> selectWarnings;
        std::vector<DecisionPoint> selectDecisionPoints;
        // Consume the WITH-outer flag here so it applies only to this (top-level) select, not to any
        // nested subselect generated while producing it.
        const bool forceOuterAsterisk = this->context.withOuterSelect;
        this->context.withOuterSelect = false;
        EmittedTableTypeScope emittedTableTypes{&this->context};
        // The FROM of this select is what a subquery written in it resolves a correlated name
        // against; a statement generated after it reads no FROM of it. The aliases of this select
        // are `activeSelectColumnAliases`, and no subquery around it lends it its own.
        struct SelectSourceTablesScope {
            CodeGeneratorContext* ctx;
            std::optional<std::vector<std::string>> saved;
            std::map<std::string, const AstNode*> savedResultColumnAliases;
            SelectSourceTablesScope(CodeGeneratorContext* context, std::vector<std::string> sourceTables) :
                ctx(context), saved(std::exchange(context->selectSourceTables, std::move(sourceTables))),
                savedResultColumnAliases(std::exchange(context->selectResultColumnAliases, {})) {}
            ~SelectSourceTablesScope() {
                ctx->selectSourceTables = std::move(saved);
                ctx->selectResultColumnAliases = std::move(savedResultColumnAliases);
            }
        } selectSourceTables{&this->context, fromSourceTables(selectNode, this->context)};
        for (const auto& fromItem: selectNode.fromClause) {
            if (fromItem.table.derivedSelect) {
                CodeGenResult carried;
                carried.decisionPoints = std::move(selectDecisionPoints);
                carried.warnings = std::move(selectWarnings);
                return unsupportedStatementPlaceholder(this->context,
                                                       "SELECT with derived FROM",
                                                       "subselect in FROM is not supported in sqlite_orm codegen",
                                                       *fromItem.table.derivedSelect,
                                                       std::move(carried));
            }
        }
        for (const auto& fromItem: selectNode.fromClause) {
            if (auto hintWarning = tableIndexHintWarning(fromItem.table.indexHint)) {
                selectWarnings.push_back(std::move(*hintWarning));
            }
        }
        // sqlite_orm spells a HAVING condition only as a tail of the GROUP BY clause
        // (`group_by(...).having(...)`), and there is no faithful stand-in for the form SQLite also
        // takes, HAVING with no GROUP BY: `group_by()` with no term is not SQL, and grouping by a
        // constant is a different query — over an empty table `SELECT count(*) FROM t HAVING
        // count(*) >= 0` answers one row on 3.51.0 where `... GROUP BY NULL HAVING ...` answers
        // none. So the statement is not generated, rather than generated as the query without the
        // condition, which would answer rows the SQL does not.
        if (selectNode.having && !selectNode.groupBy) {
            CodeGenResult carried;
            carried.decisionPoints = std::move(selectDecisionPoints);
            carried.warnings = std::move(selectWarnings);
            return unsupportedStatementPlaceholder(this->context,
                                                   "SELECT with HAVING and no GROUP BY",
                                                   "HAVING without GROUP BY is not mapped to sqlite_orm codegen; "
                                                   "sqlite_orm spells a HAVING condition only as "
                                                   "group_by(...).having(...)",
                                                   *selectNode.having,
                                                   std::move(carried));
        }
        this->context.fromTableAliasToStructName.clear();
        this->context.activeTableAliases.clear();
        this->context.implicitSourceAlias.reset();
        this->context.canNameInferredFromSources = false;
        this->context.sourcesWrittenWithoutAlias.clear();
        this->context.countedAliasedSource = false;
        this->context.nextAliasLetter = 0;
        auto isCteKey = [&](std::string_view tableSqlName) -> bool {
            auto key = normalizeSqlIdentifier(tableSqlName);
            return this->context.activeCteTypedefByTableKey.find(key) != this->context.activeCteTypedefByTableKey.end();
        };
        auto structForFromTable = [&](std::string_view tableSqlName) -> std::string {
            auto key = normalizeSqlIdentifier(tableSqlName);
            if (auto cteLookup = this->context.activeCteTypedefByTableKey.find(key);
                cteLookup != this->context.activeCteTypedefByTableKey.end()) {
                return cteLookup->second;
            }
            return this->context.structNameForTable(tableSqlName);
        };
        auto prefixStructNameForFromTable = [&](std::string_view tableSqlName) -> std::string {
            const auto key = normalizeSqlIdentifier(tableSqlName);
            if (this->context.activeCteTypedefByTableKey.find(key) != this->context.activeCteTypedefByTableKey.end()) {
                return toStructName(tableSqlName);
            }
            return structForFromTable(tableSqlName);
        };
        if (!selectNode.fromClause.empty()) {
            for (const auto& fromItem: selectNode.fromClause) {
                const auto& ft = fromItem.table;
                if (ft.schemaName) {
                    selectWarnings.push_back("FROM clause schema qualifier '" + *ft.schemaName + "' for table '" +
                                             ft.tableName + "' is not represented in sqlite_orm mapping");
                }
                std::string mappedStructName = structForFromTable(ft.tableName);
                this->context.fromTableAliasToStructName[ft.tableName] = mappedStructName;
                if (ft.alias && !isCteKey(ft.tableName)) {
                    if (this->context.useCpp20TableAliasStyle()) {
                        std::string varName = toCppIdentifier(*ft.alias);
                        TableAliasInfo info{varName, mappedStructName, ft.tableName};
                        this->context.activeTableAliases[*ft.alias] = info;
                        this->context.activeTableAliases[ft.tableName] = info;
                        this->context.declareCpp20TableAlias(
                            Cpp20TableAliasDeclaration{varName, mappedStructName, stripIdentifierQuotes(*ft.alias)});
                    } else {
                        char letter = static_cast<char>('a' + this->context.nextAliasLetter++);
                        std::string ormAlias = "alias_" + std::string(1, letter) + "<" + mappedStructName + ">";
                        TableAliasInfo info{ormAlias, mappedStructName, ft.tableName};
                        this->context.activeTableAliases[*ft.alias] = info;
                        this->context.activeTableAliases[ft.tableName] = info;
                    }
                    this->context.fromTableAliasToStructName[*ft.alias] = mappedStructName;
                } else if (ft.alias) {
                    this->context.fromTableAliasToStructName[*ft.alias] = mappedStructName;
                }
            }
            const FromTableClause* structNameTable = &selectNode.fromClause.at(0).table;
            for (const auto& fromItem: selectNode.fromClause) {
                if (!isCteKey(fromItem.table.tableName)) {
                    structNameTable = &fromItem.table;
                    break;
                }
            }
            this->context.structName = prefixStructNameForFromTable(structNameTable->tableName);
            // The same source under its alias, for the references that name no table: they belong
            // to this source, and an aliased one answers to its alias only. The alias is looked up
            // by the alias itself — a self-join has two sources of one table, and the table name
            // reaches whichever of them was registered last.
            if (structNameTable->alias) {
                if (auto aliasIt = this->context.activeTableAliases.find(*structNameTable->alias);
                    aliasIt != this->context.activeTableAliases.end()) {
                    this->context.implicitSourceAlias = aliasIt->second;
                }
            }
        }

        std::optional<std::string> implicitCte;
        std::optional<std::string> implicitCteTableKey;
        if (!selectNode.fromClause.empty()) {
            const auto& firstFrom = selectNode.fromClause.at(0).table;
            const auto firstKey = normalizeSqlIdentifier(firstFrom.tableName);
            if (this->context.activeCteTypedefByTableKey.find(firstKey) !=
                this->context.activeCteTypedefByTableKey.end()) {
                bool allCte = true;
                for (const auto& fromItem: selectNode.fromClause) {
                    if (!isCteKey(fromItem.table.tableName)) {
                        allCte = false;
                        break;
                    }
                }
                if (selectNode.fromClause.size() == 1u || allCte) {
                    implicitCte = this->context.activeCteTypedefByTableKey.at(firstKey);
                    implicitCteTableKey = firstKey;
                }
            }
        }
        struct ImplicitCteScope {
            CodeGeneratorContext* ctx;
            std::optional<std::string> savedTypedef;
            std::optional<std::string> savedTableKey;
            ImplicitCteScope(CodeGeneratorContext* context,
                             std::optional<std::string> implTypedef,
                             std::optional<std::string> implTableKey) :
                ctx(context), savedTypedef(std::move(context->implicitSingleSourceCteTypedef)),
                savedTableKey(std::move(context->implicitCteFromTableKeyNorm)) {
                ctx->implicitSingleSourceCteTypedef = std::move(implTypedef);
                ctx->implicitCteFromTableKeyNorm = std::move(implTableKey);
            }
            ~ImplicitCteScope() {
                ctx->implicitSingleSourceCteTypedef = std::move(savedTypedef);
                ctx->implicitCteFromTableKeyNorm = std::move(savedTableKey);
            }
        } implicitScope{&this->context, std::move(implicitCte), std::move(implicitCteTableKey)};

        // Resolved before the baseline snapshot so options regenerate with the same name.
        const std::string rowsVariable = this->context.statementVariableName("rows");
        CodeGeneratorContext selectAltBaseline = this->context;

        auto expressionCode = [&](const AstNode& node) -> SpannedCode {
            auto result = this->coordinator.generateNode(node);
            selectWarnings.insert(selectWarnings.end(),
                                  std::make_move_iterator(result.warnings.begin()),
                                  std::make_move_iterator(result.warnings.end()));
            selectDecisionPoints.insert(selectDecisionPoints.end(),
                                        std::make_move_iterator(result.decisionPoints.begin()),
                                        std::make_move_iterator(result.decisionPoints.end()));
            return SpannedCode::takenFrom(result);
        };

        bool isStar = selectNode.columns.size() == 1 && !selectNode.columns.at(0).expression;
        // The sources of this select, settled before a single clause is generated: the `count(*)`
        // branch asks whether the FROM is going to be written out before it may name an alias, and
        // the join loop below asks how many of the FROM items that clause stands for.
        const SelectFromSources fromSources = selectFromSources(this->context, selectNode.fromClause);
        if (isStar) {
            // Every reference a select generates has to name the same recordset its row is read
            // from, and a bare `*` reads the plain struct: `asterisk<T>()` and `get_all<T>()` name
            // no alias (card 1868205961584313865). An `alias_column<alias_a<T>>` beside one of them
            // is a second source to sqlite_orm, and the alias it names is declared by nothing.
            this->context.implicitSourceAlias.reset();
        }
        this->context.canNameInferredFromSources =
            fromSources.leadingAnyAliased && !fromSources.hasCteSource && !isStar;
        // How many of the FROM items the `from<...>()` will stand for, which the join loop has to
        // know while the clause itself is only written once every clause that can name a source has
        // been generated. A comma or CROSS JOIN run of more than one aliased source is always written
        // out: it reaches sqlite_orm through `cross_join<alias_b<T>>()`, which drops the alias.
        const size_t sourcesNamedByFromClause =
            this->context.canNameInferredFromSources && fromSources.leadingTypes.size() > 1u
                ? fromSources.leadingTypes.size()
                : 0u;
        const auto fromEmissions = fromItemEmissions(this->context, selectNode.fromClause, sourcesNamedByFromClause);
        this->context.sourcesWrittenWithoutAlias =
            sourcesWrittenWithoutAlias(selectNode.fromClause, fromSources, fromEmissions, isStar);
        int apiLevelDecisionId = -1;
        // No api_level choice for a WITH outer: it is fixed to the `select(asterisk<T>())` form.
        if (isStar && !selectNode.fromClause.empty() && !forceOuterAsterisk) {
            apiLevelDecisionId = this->context.nextDecisionPointId++;
        }
        SpannedCode code;
        std::string aliasPreamble;
        const bool cpp20ColumnAliases = this->context.useCpp20ColumnAliasStyle();
        if (!isStar) {
            if (hasAnyColumnAlias(selectNode.columns)) {
                if (cpp20ColumnAliases) {
                    aliasPreamble = generateCpp20ColumnAliasPreamble(selectNode.columns);
                } else {
                    aliasPreamble = generateColumnAliasPreamble(selectNode.columns);
                }
            }
            // A result column is what the caller reads back, so it is here — and not in the
            // expression generator, whose code also serves a WHERE or an ORDER BY — that an
            // expression sqlite_orm types too narrowly is widened: `as_optional` for a value that
            // can be NULL, `cast<int64_t>` for a bitwise one that can leave the int32 range, and a
            // warning for the arithmetic operators, whose `double` no CAST can widen without
            // truncating the REAL they answer with. Warnings dedupe by message, so several columns
            // computed with the same operator report once, anchored at the first of them.
            auto resultColumnCode = [&](const SelectColumn& column) -> SpannedCode {
                if (!column.expression) {
                    // A bare `*` standing next to other result columns: SQLite runs it, and the
                    // parser leaves it as a column with no expression of its own, so the whole
                    // SELECT is what the warning underlines.
                    auto starPlaceholder =
                        unsupportedPlaceholder(this->context,
                                               "* among other result columns",
                                               "a `*` result column next to other result columns is not "
                                               "mapped to sqlite_orm codegen",
                                               selectNode);
                    appendUniqueWarnings(selectWarnings, starPlaceholder.warnings);
                    return starPlaceholder.code;
                }
                auto colCode = expressionCode(*column.expression);
                if (selectResultNeedsIntegerCast(*column.expression)) {
                    colCode = "cast<int64_t>(" + colCode + ")";
                    this->context.recordComment(sourceSpanComment(kCommentBitwiseResultCast, *column.expression));
                }
                if (selectResultNeedsAsOptional(*column.expression, this->context)) {
                    colCode = "as_optional(" + colCode + ")";
                }
                if (auto warning = selectResultDoublePrecisionWarning(*column.expression)) {
                    appendUniqueWarnings(selectWarnings, {std::move(*warning)});
                }
                if (auto warning = selectResultJsonExtractTypeWarning(*column.expression)) {
                    appendUniqueWarnings(selectWarnings, {std::move(*warning)});
                }
                if (auto warning = selectResultCommonArgumentTypeWarning(*column.expression, this->context)) {
                    appendUniqueWarnings(selectWarnings, {std::move(*warning)});
                }
                return wrapWithColumnAlias(colCode, column.alias, cpp20ColumnAliases);
            };
            code = "auto " + rowsVariable + " = storage.select(";
            if (selectNode.distinct) {
                if (selectNode.columns.size() == 1) {
                    code += "distinct(" + resultColumnCode(selectNode.columns.at(0)) + ")";
                } else {
                    code += "distinct(columns(";
                    for (size_t i = 0; i < selectNode.columns.size(); ++i) {
                        if (i > 0)
                            code += ", ";
                        code += resultColumnCode(selectNode.columns.at(i));
                    }
                    code += "))";
                }
            } else if (selectNode.columns.size() == 1) {
                code += resultColumnCode(selectNode.columns.at(0));
            } else {
                code += "columns(";
                for (size_t i = 0; i < selectNode.columns.size(); ++i) {
                    if (i > 0)
                        code += ", ";
                    code += resultColumnCode(selectNode.columns.at(i));
                }
                code += ")";
            }
        }

        this->context.activeSelectColumnAliases.clear();
        this->context.activeSelectColumnAliasCpp20Vars.clear();
        for (const auto& column: selectNode.columns) {
            if (!column.alias.empty()) {
                std::string key = toLowerAscii(stripColumnAliasQuotes(column.alias));
                this->context.activeSelectColumnAliases[key] = columnAliasTypeName(column.alias);
                this->context.activeSelectColumnAliasCpp20Vars[key] = columnAliasCpp20VarName(column.alias);
            }
        }

        std::vector<SpannedCode> selectTrailingClauses;
        auto appendClause = [&](SpannedCode clause) {
            selectTrailingClauses.push_back(std::move(clause));
        };

        auto isCteSource = [&](std::string_view tableName) -> bool {
            auto key = normalizeSqlIdentifier(tableName);
            return this->context.activeCteTypedefByTableKey.find(key) != this->context.activeCteTypedefByTableKey.end();
        };
        auto resolveJoinType = [&](const FromTableClause& ft) -> std::string {
            std::string key = ft.alias ? *ft.alias : ft.tableName;
            auto it = this->context.activeTableAliases.find(key);
            if (it != this->context.activeTableAliases.end()) {
                return it->second.ormAliasType;
            }
            return structForFromTable(ft.tableName);
        };
        auto cteUsingColumnCode = [&](std::string_view tableName, std::string_view colSql) -> std::string {
            auto tableKey = normalizeSqlIdentifier(tableName);
            auto cteIt = this->context.activeCteTypedefByTableKey.find(tableKey);
            if (cteIt != this->context.activeCteTypedefByTableKey.end()) {
                std::string colKey = normalizeSqlIdentifier(colSql);
                std::string pipe = tableKey + "|" + colKey;
                auto indexedIt = this->context.withCteIndexedColVarByPipeKey.find(pipe);
                if (indexedIt != this->context.withCteIndexedColVarByPipeKey.end()) {
                    return "column<" + cteIt->second + ">(" + indexedIt->second + ")";
                }
                auto legacyIt = this->context.withCteLegacyColVarByPipeKey.find(pipe);
                if (legacyIt != this->context.withCteLegacyColVarByPipeKey.end()) {
                    return "column<" + cteIt->second + ">(" + legacyIt->second + ")";
                }
                auto cpp20It = this->context.withCteCpp20ColVarByPipeKey.find(pipe);
                if (cpp20It != this->context.withCteCpp20ColVarByPipeKey.end()) {
                    return "column<" + cteIt->second + ">(" + cpp20It->second + ")";
                }
                if (this->context.isExplicitCteColumn(tableKey, std::string(colSql))) {
                    return "column<" + cteIt->second + ">(" +
                           identifierToCppStringLiteral(stripIdentifierQuotes(colSql)) + ")";
                }
                auto baseIt = this->context.cteBaseStructByKey.find(tableKey);
                if (baseIt != this->context.cteBaseStructByKey.end()) {
                    return "column<" + cteIt->second + ">(&" + baseIt->second +
                           "::" + this->context.structColumnMember(baseIt->second, colSql) + ")";
                }
                return "column<" + cteIt->second + ">(" + identifierToCppStringLiteral(stripIdentifierQuotes(colSql)) +
                       ")";
            }
            const std::string tableStruct = structForFromTable(tableName);
            return "&" + tableStruct + "::" + this->context.structColumnMember(tableStruct, colSql);
        };
        for (size_t joinIndex = 1; joinIndex < selectNode.fromClause.size(); ++joinIndex) {
            const auto& joinItem = selectNode.fromClause.at(joinIndex);
            const JoinKind joinKind = generatedJoinKind(joinItem);
            if (fromEmissions.at(joinIndex) != FromItemEmission::joinClause) {
                // The `from<...>()` written below stands for this item, or sqlite_orm infers it.
                continue;
            }
            const auto& leftTable = selectNode.fromClause.at(joinIndex - 1).table;
            std::string rightType = resolveJoinType(joinItem.table);
            std::string rightStruct = structForFromTable(joinItem.table.tableName);
            std::string leftStruct = structForFromTable(leftTable.tableName);
            SpannedCode joinCode;
            switch (joinKind) {
                case JoinKind::crossJoin:
                case JoinKind::naturalInnerJoin:
                    joinCode = std::string(joinSqliteOrmApiName(joinKind)) + "<" + rightType + ">()";
                    break;
                case JoinKind::naturalLeftJoin:
                    selectWarnings.push_back(
                        "NATURAL LEFT JOIN is not supported in sqlite_orm; generated natural_join does not match SQL "
                        "semantics");
                    joinCode = "natural_join<" + rightType + ">()";
                    break;
                default: {
                    std::string api(joinSqliteOrmApiName(joinKind));
                    if (generatedJoinLosesCrossJoinBarrier(joinItem)) {
                        selectWarnings.push_back(constrainedCrossJoinWarning(joinItem, rightType));
                    }
                    if (!joinItem.usingColumnNames.empty()) {
                        bool rightIsCte = isCteSource(joinItem.table.tableName);
                        if (joinItem.usingColumnNames.size() == 1) {
                            std::string usingCol;
                            if (rightIsCte) {
                                usingCol = cteUsingColumnCode(leftTable.tableName, joinItem.usingColumnNames.at(0));
                            } else {
                                usingCol =
                                    "&" + rightStruct + "::" +
                                    this->context.structColumnMember(rightStruct, joinItem.usingColumnNames.at(0));
                            }
                            joinCode = std::string(api) + "<" + rightType + ">(using_(" + usingCol + "))";
                        } else {
                            std::string cond;
                            for (size_t ci = 0; ci < joinItem.usingColumnNames.size(); ++ci) {
                                if (ci > 0) {
                                    cond += " and ";
                                }
                                const std::string& usingName = joinItem.usingColumnNames.at(ci);
                                cond += "c(&" + leftStruct +
                                        "::" + this->context.structColumnMember(leftStruct, usingName) + ") == c(&" +
                                        rightStruct + "::" + this->context.structColumnMember(rightStruct, usingName) +
                                        ")";
                            }
                            joinCode = std::string(api) + "<" + rightType + ">(on(" + cond + "))";
                        }
                    } else if (joinItem.onExpression) {
                        joinCode = std::string(api) + "<" + rightType + ">(on(" +
                                   expressionCode(*joinItem.onExpression) + "))";
                    } else {
                        joinCode = std::string(api) + "<" + rightType + ">(on(true))";
                    }
                    break;
                }
            }
            appendClause(joinCode);
        }

        if (selectNode.whereClause) {
            const ParenthesizedConditionScope conditionScope{this->context, *selectNode.whereClause};
            appendClause("where(" + expressionCode(*selectNode.whereClause) + ")");
        }

        if (selectNode.groupBy) {
            SpannedCode groupCode = "group_by(";
            for (size_t i = 0; i < selectNode.groupBy->expressions.size(); ++i) {
                if (i > 0)
                    groupCode += ", ";
                groupCode += expressionCode(*selectNode.groupBy->expressions.at(i));
            }
            groupCode += ")";
            if (selectNode.having) {
                groupCode += ".having(" + expressionCode(*selectNode.having) + ")";
            }
            appendClause(groupCode);
        }

        for (const auto& namedWindow: selectNode.namedWindows) {
            if (!namedWindow.definition) {
                continue;
            }
            std::string windowArgs =
                this->coordinator.codegenOverClause(*namedWindow.definition, selectDecisionPoints, selectWarnings);
            if (!windowArgs.empty()) {
                appendClause("window(" + identifierToCppStringLiteral(namedWindow.name) + ", " + windowArgs + ")");
            } else {
                selectWarnings.push_back("WINDOW `" + namedWindow.name +
                                         "`: empty or unmapped window definition omitted in sqlite_orm codegen");
            }
        }

        if (!selectNode.orderBy.empty()) {
            auto formatOrderTerm = [&](const OrderByTerm& term) -> SpannedCode {
                SpannedCode orderCode = "order_by(" + expressionCode(*term.expression) + ")";
                if (term.direction == SortDirection::asc) {
                    orderCode += ".asc()";
                } else if (term.direction == SortDirection::desc) {
                    orderCode += ".desc()";
                }
                if (!term.collation.empty()) {
                    std::string collLower = toLowerAscii(term.collation);
                    if (collLower == "nocase") {
                        orderCode += ".collate_nocase()";
                    } else if (collLower == "binary") {
                        orderCode += ".collate_binary()";
                    } else if (collLower == "rtrim") {
                        orderCode += ".collate_rtrim()";
                    } else {
                        orderCode += ".collate(" + identifierToCppStringLiteral(term.collation) + ")";
                        selectWarnings.push_back(
                            "COLLATE " + term.collation +
                            " in ORDER BY is not a built-in collation; generated .collate(...) uses literal name");
                    }
                }
                return orderCode;
            };
            if (selectNode.orderBy.size() == 1) {
                appendClause(formatOrderTerm(selectNode.orderBy.at(0)));
            } else {
                SpannedCode multiCode = "multi_order_by(";
                for (size_t i = 0; i < selectNode.orderBy.size(); ++i) {
                    if (i > 0)
                        multiCode += ", ";
                    multiCode += formatOrderTerm(selectNode.orderBy.at(i));
                }
                multiCode += ")";
                appendClause(multiCode);
            }
        }

        if (selectNode.limitValue) {
            SpannedCode limitCode = "limit(" + expressionCode(*selectNode.limitValue);
            if (selectNode.offsetValue) {
                limitCode += ", offset(" + expressionCode(*selectNode.offsetValue) + ")";
            }
            limitCode += ")";
            appendClause(limitCode);
        }

        if (inferredFromWouldBeInvented(selectNode, this->context)) {
            CodeGenResult carried;
            carried.decisionPoints = std::move(selectDecisionPoints);
            carried.warnings = std::move(selectWarnings);
            return unsupportedStatementPlaceholder(this->context,
                                                   "SELECT without FROM",
                                                   std::string(kWarningSelectWithoutFromNamesTable),
                                                   selectNode,
                                                   std::move(carried));
        }
        const std::string& starRowType = this->context.implicitSingleSourceCteTypedef
                                             ? *this->context.implicitSingleSourceCteTypedef
                                             : this->context.structName;
        const std::string explicitFrom =
            selectExplicitFrom(this->context,
                               selectNode,
                               fromSources,
                               sourcesNamedByFromClause,
                               isStar ? std::string_view(starRowType) : std::string_view());

        // What `storage.get_all<T>(...)` takes: the clauses themselves, with no FROM among them.
        SpannedCode trailingJoined;
        for (size_t ti = 0; ti < selectTrailingClauses.size(); ++ti) {
            if (ti > 0) {
                trailingJoined += ", ";
            }
            trailingJoined += selectTrailingClauses.at(ti);
        }
        // What `storage.select(...)` takes: the same clauses behind the FROM, where one is needed.
        SpannedCode selectArgsJoined = trailingJoined;
        if (!explicitFrom.empty()) {
            selectArgsJoined =
                trailingJoined.empty() ? SpannedCode(explicitFrom) : (explicitFrom + ", " + trailingJoined);
        }

        if (isStar) {
            // `asterisk<T>()` names the row the star reads, and an aliased source is not named by
            // it today (card 1868205961584313865). Where the two disagree the FROM would pin the
            // select to a source the star does not read — `SELECT "users".* FROM "users" "a"`,
            // which SQLite refuses — so the star keeps the form it had.
            const std::string starFrom = namesInferredSource(fromSources, starRowType) ? explicitFrom : std::string();
            SpannedCode starArgs = trailingJoined;
            if (!starFrom.empty()) {
                starArgs = trailingJoined.empty() ? SpannedCode(starFrom) : (starFrom + ", " + trailingJoined);
            }
            SpannedCode tail = starArgs.empty() ? SpannedCode() : (", " + starArgs);
            SpannedCode codeGetAll =
                "auto " + rowsVariable + " = storage.get_all<" + starRowType + ">(" + trailingJoined + ");";
            SpannedCode codeSelectObject =
                "auto " + rowsVariable + " = storage.select(object<" + starRowType + ">()" + tail + ");";
            SpannedCode codeSelectAsterisk =
                "auto " + rowsVariable + " = storage.select(asterisk<" + starRowType + ">()" + tail + ");";
            std::string chosenApi = "get_all";
            code = codeGetAll;
            if (policyEquals(this->context.codeGenPolicy, "api_level", "select_object")) {
                chosenApi = "select_object";
                code = codeSelectObject;
            } else if (policyEquals(this->context.codeGenPolicy, "api_level", "select_asterisk")) {
                chosenApi = "select_asterisk";
                code = codeSelectAsterisk;
            }
            // A WITH outer cannot use storage.get_all<T>() (not a with() argument); use asterisk instead.
            if (forceOuterAsterisk && chosenApi == "get_all") {
                chosenApi = "select_asterisk";
                code = codeSelectAsterisk;
            }
            if (apiLevelDecisionId >= 0) {
                // options lists every variant (the chosen one included, default get_all first).
                std::vector<Option> apiOptions = {
                    Option{"get_all", codeGetAll.text(), "get_all<T>(...) returns full row objects"},
                    Option{"select_object",
                           codeSelectObject.text(),
                           "select(object<T>(), ...) returns std::tuple of columns"},
                    Option{"select_asterisk",
                           codeSelectAsterisk.text(),
                           "select(asterisk<T>(), ...) returns full row objects"},
                };
                selectDecisionPoints.insert(
                    selectDecisionPoints.begin(),
                    DecisionPoint{apiLevelDecisionId, "api_level", chosenApi, code.text(), std::move(apiOptions)});
            }
        } else {
            if (!selectArgsJoined.empty()) {
                code += ", ";
                code += selectArgsJoined;
            }
            code += ");";
        }
        if (hasAnyColumnAlias(selectNode.columns)) {
            if (this->context.useCpp20ColumnAliasStyle()) {
                if (!aliasPreamble.empty()) {
                    code = aliasPreamble + code;
                }
                // Every alias of the select is generated in this style, and the first of them is
                // where a reader meets it, so that alias is what the hint underlines.
                this->context.recordComment(
                    sourceSpanComment(kCommentCpp20ColumnAliases, firstColumnAliasSpan(selectNode.columns)));
            } else {
                bool hasBuiltin = false;
                bool hasCustom = false;
                for (const auto& column: selectNode.columns) {
                    if (column.alias.empty())
                        continue;
                    if (needsCustomAliasStruct(column.alias))
                        hasCustom = true;
                    else
                        hasBuiltin = true;
                }
                if (hasCustom) {
                    code = aliasPreamble + code;
                    selectWarnings.push_back(
                        "SELECT column alias uses as<AliasTag>() with a generated sqlite_orm::alias_tag struct");
                }
                if (hasBuiltin) {
                    selectWarnings.push_back("SELECT column alias uses sqlite_orm built-in colalias_* types; "
                                             "requires `using namespace sqlite_orm`");
                }
                if (!this->context.columnAliasStyleOverride) {
                    // options lists every applicable variant (the chosen one included).
                    std::vector<Option> aliasOptions = {
                        Option{"alias_tag",
                               code.text(),
                               "alias_tag / colalias_* / generated struct (default; wider compiler support)"}};
                    if (cpp20Allowed(this->context.codeGenPolicy)) {
                        CodeGenerator altGen;
                        altGen.context() = selectAltBaseline;
                        altGen.context().columnAliasStyleOverride = "cpp20_literal";
                        auto altRes = altGen.generateNode(selectNode);
                        Option cpp20Alt{"cpp20_literal",
                                        altRes.code,
                                        "C++20 literal aliases (`orm_column_alias`, `_col`)"};
                        // What regenerating the select recorded, and not what the statement around
                        // it had recorded before: `generateNode` reports the comments of the node it
                        // was handed, so the baseline's own are left where they are.
                        cpp20Alt.comments = std::move(altRes.comments);
                        cpp20Alt.minCppStandard = 20;
                        aliasOptions.push_back(std::move(cpp20Alt));
                    }
                    selectDecisionPoints.push_back(DecisionPoint{this->context.nextDecisionPointId++,
                                                                 "column_alias_style",
                                                                 "alias_tag",
                                                                 code.text(),
                                                                 std::move(aliasOptions)});
                }
            }
        }
        if (hasAnyColumnAlias(selectNode.columns) && this->context.useCpp20ColumnAliasStyle() &&
            !this->context.columnAliasStyleOverride) {
            CodeGenerator altGen;
            altGen.context() = selectAltBaseline;
            altGen.context().columnAliasStyleOverride = "alias_tag";
            auto altRes = altGen.generateNode(selectNode);
            // options lists every variant (the chosen one included).
            Option cpp20LiteralChosen{"cpp20_literal",
                                      code.text(),
                                      "C++20 literal aliases (`orm_column_alias`, `_col`)"};
            cpp20LiteralChosen.minCppStandard = 20;
            selectDecisionPoints.push_back(
                DecisionPoint{this->context.nextDecisionPointId++,
                              "column_alias_style",
                              "cpp20_literal",
                              code.text(),
                              {Option{"alias_tag",
                                      altRes.code,
                                      "alias_tag / colalias_* / generated struct (default; wider compiler support)"},
                               std::move(cpp20LiteralChosen)}});
        }
        bool hasTableAliases = !this->context.activeTableAliases.empty();
        if (!this->context.cpp20TableAliasDeclarations.empty() && !this->context.activeWithCteStyle) {
            std::string prelude;
            for (const auto& tad: this->context.cpp20TableAliasDeclarations) {
                prelude += "constexpr orm_table_alias auto " + tad.variableName + " = " +
                           identifierToCppStringLiteral(tad.sqlAlias) + "_alias.for_<" + tad.baseStructName + ">();\n";
            }
            code = prelude + code;
        }
        if (hasTableAliases) {
            if (!this->context.activeWithCteStyle && !this->context.suppressTableAliasStyleDecisionPoint) {
                const bool allowCpp20 = cpp20Allowed(this->context.codeGenPolicy);
                const std::string currentStyle =
                    (allowCpp20 && policyEquals(this->context.codeGenPolicy, "table_alias_style", "cpp20"))
                        ? "cpp20"
                        : "pre_cpp20";
                auto makeAlt = [&](const char* styleValue) -> std::string {
                    CodeGenPolicy pol =
                        policyWithOverride(this->context.codeGenPolicy, "table_alias_style", styleValue);
                    CodeGenerator gen;
                    gen.codeGenPolicy = &pol;
                    gen.context().suppressTableAliasStyleDecisionPoint = true;
                    gen.context().statementVariableNames = this->context.statementVariableNames;
                    return gen.generate(static_cast<const AstNode&>(selectNode)).code;
                };
                // options lists every applicable variant (the chosen one included).
                std::vector<Option> tableAliasOptions = {
                    Option{"pre_cpp20", makeAlt("pre_cpp20"), "alias_a<T> + alias_column<> (wider compiler support)"},
                };
                if (allowCpp20) {
                    Option cpp20TableAlias{"cpp20",
                                           makeAlt("cpp20"),
                                           "\"name\"_alias.for_<T>() + ->* (C++20 sqlite_orm)"};
                    cpp20TableAlias.minCppStandard = 20;
                    tableAliasOptions.push_back(std::move(cpp20TableAlias));
                }
                selectDecisionPoints.push_back(DecisionPoint{this->context.nextDecisionPointId++,
                                                             "table_alias_style",
                                                             currentStyle,
                                                             code.text(),
                                                             std::move(tableAliasOptions)});
            }
        }
        // Inside WITH the declarations are left to the WITH generator, which puts them in its prelude.
        if (!this->context.activeWithCteStyle) {
            this->context.cpp20TableAliasDeclarations.clear();
        }
        this->context.activeSelectColumnAliases.clear();
        this->context.activeSelectColumnAliasCpp20Vars.clear();
        return spannedResult(std::move(code), std::move(selectDecisionPoints), std::move(selectWarnings));
    }

    CodeGenResult SelectCodeGenerator::tryCodegenCompoundSelectSubexpression(
        const CompoundSelectNode& compoundNode,
        const std::vector<ResultColumnWidening>& widenedResultColumns,
        bool cteBodySelect) {
        if (compoundNode.selects.size() != compoundNode.operators.size() + 1) {
            return CodeGenResult{{}, {}, {"internal: compound SELECT operand count mismatch"}};
        }
        auto* firstSelect = dynamic_cast<const SelectNode*>(compoundNode.selects.at(0).get());
        if (!firstSelect) {
            return CodeGenResult{{}, {}, {"compound SELECT arm is not a SelectNode"}};
        }
        // sqlite_orm takes a chain of one operator as the arms of a single variadic call —
        // `union_(a, b, c)` serializes `a UNION b UNION c`. It has no nested form: every arm must be
        // a `select_t` (a compound operator marks each arm `highest_level`, a member only
        // `select_t` has), so `union_(union_(a, b), c)` does not compile. A chain that mixes
        // operators needs exactly that nesting, since SQLite groups it from the left, and so it is
        // not generated.
        const auto& operators = compoundNode.operators;
        if (auto mismatch = std::adjacent_find(operators.begin(), operators.end(), std::not_equal_to<>{});
            mismatch != operators.end()) {
            // The underline goes to the arm the first differing operator joins.
            const auto& joinedArm = *compoundNode.selects.at(std::distance(operators.begin(), mismatch) + 2);
            return CodeGenResult{{},
                                 {},
                                 {sourceSpanWarning("compound SELECT that mixes UNION / UNION ALL / INTERSECT / "
                                                    "EXCEPT is not mapped to sqlite_orm codegen: sqlite_orm takes "
                                                    "one operator per call and does not nest a compound as an arm "
                                                    "of another",
                                                    joinedArm)}};
        }
        CodeGenResult accumulated =
            this->coordinator.tryCodegenSqliteSelectSubexpression(*firstSelect, widenedResultColumns, cteBodySelect);
        if (accumulated.code.empty()) {
            return accumulated;
        }
        std::string armsCode = std::move(accumulated.code);
        for (size_t armIndex = 1; armIndex < compoundNode.selects.size(); ++armIndex) {
            auto* nextSelect = dynamic_cast<const SelectNode*>(compoundNode.selects.at(armIndex).get());
            if (!nextSelect) {
                return CodeGenResult{{}, {}, {"compound SELECT arm is not a SelectNode"}};
            }
            CodeGenResult nextArm =
                this->coordinator.tryCodegenSqliteSelectSubexpression(*nextSelect, widenedResultColumns, cteBodySelect);
            accumulated.decisionPoints.insert(accumulated.decisionPoints.end(),
                                              std::make_move_iterator(nextArm.decisionPoints.begin()),
                                              std::make_move_iterator(nextArm.decisionPoints.end()));
            accumulated.warnings.insert(accumulated.warnings.end(),
                                        std::make_move_iterator(nextArm.warnings.begin()),
                                        std::make_move_iterator(nextArm.warnings.end()));
            if (nextArm.code.empty()) {
                return CodeGenResult{{}, std::move(accumulated.decisionPoints), std::move(accumulated.warnings)};
            }
            armsCode += ", " + nextArm.code;
        }
        this->context.recordFormWithoutDefaultConstructor("a compound SELECT");
        this->context.emittedCompoundSelectForm = true;
        accumulated.code = std::string(compoundSelectApi(operators.front())) + "(" + armsCode + ")";
        // The arms are joined as plain text, so whatever spans the first one carried no longer
        // name the code they did.
        accumulated.expressionSpans.clear();
        return accumulated;
    }

    CodeGenResult SelectCodeGenerator::tryCodegenSelectLikeSubquery(const AstNode& node, bool cteBodySelect) {
        if (auto* selectNode = dynamic_cast<const SelectNode*>(&node)) {
            return this->coordinator.tryCodegenSqliteSelectSubexpression(*selectNode, {}, cteBodySelect);
        }
        if (auto* compoundNode = dynamic_cast<const CompoundSelectNode*>(&node)) {
            return this->coordinator.tryCodegenCompoundSelectSubexpression(*compoundNode, {}, cteBodySelect);
        }
        if (dynamic_cast<const WithQueryNode*>(&node)) {
            // Dropping the WITH clause would leave the inner SELECT reading each CTE's name as a
            // table: a struct no schema declares, or a table of the same name the CTE shadows.
            return CodeGenResult{{},
                                 {},
                                 {"nested WITH in subquery: sqlite_orm select(...) cannot embed CTEs, so the SELECT "
                                  "is not mapped"}};
        }
        return CodeGenResult{{}, {}, {"subquery is not a SELECT or compound SELECT for sqlite_orm codegen"}};
    }
}  // namespace sqlite2orm
