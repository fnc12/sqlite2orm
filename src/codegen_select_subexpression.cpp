#include "codegen_select.h"
#include "codegen_context.h"
#include "codegen_select_from.h"
#include "codegen_utils.h"
#include "emitted_table_type_scope.h"
#include "parenthesized_condition_scope.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

#include <map>
#include <utility>

namespace sqlite2orm {

    namespace {

        /** The result-column aliases of `selectNode`, as `selectResultColumnAliases` holds them. */
        std::map<std::string, const AstNode*> resultColumnAliases(const SelectNode& selectNode) {
            std::map<std::string, const AstNode*> aliases;
            for (const auto& column: selectNode.columns) {
                if (!column.alias.empty() && column.expression) {
                    aliases.emplace(toLowerAscii(stripColumnAliasQuotes(column.alias)), column.expression.get());
                }
            }
            return aliases;
        }

    }  // namespace

    CodeGenResult SelectCodeGenerator::tryCodegenSqliteSelectSubexpression(
        const SelectNode& selectNode,
        const std::vector<ResultColumnWidening>& widenedResultColumns,
        bool cteBodySelect) {
        struct SubselectAliasRestore {
            CodeGeneratorContext* ctx;
            std::map<std::string, std::string> savedAliases;
            std::map<std::string, TableAliasInfo> savedTableAliases;
            int savedNextAliasLetter;
            std::map<std::string, std::string> savedColumnAliases;
            std::map<std::string, std::string> savedColumnAliasCpp20Vars;
            std::string savedStructName;
            std::optional<std::string> savedImplicitCte;
            std::optional<std::string> savedImplicitCteTableKey;
            std::optional<TableAliasInfo> savedImplicitSourceAlias;
            bool savedCanNameInferredFromSources;
            std::vector<std::string> savedPlainRowSourceTypes;
            bool savedCountedAliasedSource;
            std::optional<std::vector<std::string>> savedSelectSourceTables;
            std::map<std::string, const AstNode*> savedResultColumnAliases;

            SubselectAliasRestore(CodeGeneratorContext* context) :
                ctx(context), savedAliases(context->fromTableAliasToStructName),
                savedTableAliases(context->activeTableAliases), savedNextAliasLetter(context->nextAliasLetter),
                savedColumnAliases(context->activeSelectColumnAliases),
                savedColumnAliasCpp20Vars(context->activeSelectColumnAliasCpp20Vars),
                savedStructName(context->structName),
                // Taken with `exchange`, not `move`: moving out of an engaged `std::optional`
                // leaves it engaged over an emptied string, and a subquery whose own FROM names no
                // CTE then read that leftover as its implicit CTE source and spelled its columns
                // `column<>("b")` — a form sqlite_orm has no overload for. The subquery sets these
                // from its own FROM clause below, so it has to start out with neither.
                savedImplicitCte(std::exchange(context->implicitSingleSourceCteTypedef, std::nullopt)),
                savedImplicitCteTableKey(std::exchange(context->implicitCteFromTableKeyNorm, std::nullopt)),
                savedImplicitSourceAlias(std::exchange(context->implicitSourceAlias, std::nullopt)),
                savedCanNameInferredFromSources(context->canNameInferredFromSources),
                savedPlainRowSourceTypes(context->sourcesWrittenWithoutAlias),
                savedCountedAliasedSource(context->countedAliasedSource),
                savedSelectSourceTables(context->selectSourceTables),
                // The result list of the subquery sees no alias, its own or one of the query around it.
                savedResultColumnAliases(std::exchange(context->selectResultColumnAliases, {})) {}

            ~SubselectAliasRestore() {
                ctx->fromTableAliasToStructName = std::move(savedAliases);
                ctx->activeTableAliases = std::move(savedTableAliases);
                ctx->nextAliasLetter = savedNextAliasLetter;
                ctx->activeSelectColumnAliases = std::move(savedColumnAliases);
                ctx->activeSelectColumnAliasCpp20Vars = std::move(savedColumnAliasCpp20Vars);
                ctx->structName = std::move(savedStructName);
                ctx->implicitSingleSourceCteTypedef = std::move(savedImplicitCte);
                ctx->implicitCteFromTableKeyNorm = std::move(savedImplicitCteTableKey);
                ctx->implicitSourceAlias = std::move(savedImplicitSourceAlias);
                ctx->canNameInferredFromSources = savedCanNameInferredFromSources;
                ctx->sourcesWrittenWithoutAlias = std::move(savedPlainRowSourceTypes);
                ctx->countedAliasedSource = savedCountedAliasedSource;
                ctx->selectSourceTables = std::move(savedSelectSourceTables);
                ctx->selectResultColumnAliases = std::move(savedResultColumnAliases);
            }
        } restore{&this->context};
        EmittedTableTypeScope emittedTableTypes{&this->context};

        std::vector<CodegenWarning> subWarnings;
        std::vector<DecisionPoint> subDecisionPoints;

        if (selectNode.groupBy) {
            subWarnings.push_back("GROUP BY in subquery is not yet mapped to sqlite_orm select(...)");
            return CodeGenResult{{}, {}, std::move(subWarnings)};
        }
        if (selectNode.having) {
            subWarnings.push_back("HAVING without GROUP BY in subquery is not mapped to sqlite_orm select(...)");
            return CodeGenResult{{}, {}, std::move(subWarnings)};
        }
        if (selectNode.offsetValue && !selectNode.limitValue) {
            subWarnings.push_back("OFFSET without LIMIT in subquery is not yet mapped to sqlite_orm select(...)");
            return CodeGenResult{{}, {}, std::move(subWarnings)};
        }

        for (const auto& fromItem: selectNode.fromClause) {
            if (fromItem.table.derivedSelect) {
                subWarnings.push_back("subselect in FROM is not supported in sqlite_orm codegen");
                return CodeGenResult{{}, {}, std::move(subWarnings)};
            }
        }
        for (const auto& fromItem: selectNode.fromClause) {
            if (auto hintWarning = tableIndexHintWarning(fromItem.table.indexHint)) {
                subWarnings.push_back(std::move(*hintWarning));
            }
        }

        this->context.selectSourceTables = fromSourceTables(selectNode, this->context);
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
                    subWarnings.push_back("FROM clause schema qualifier '" + *ft.schemaName + "' for table '" +
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
                    this->context.implicitSingleSourceCteTypedef =
                        this->context.activeCteTypedefByTableKey.at(firstKey);
                    this->context.implicitCteFromTableKeyNorm = firstKey;
                }
            }
        }

        auto expressionCode = [&](const AstNode& node) -> std::string {
            auto result = this->coordinator.generateNode(node);
            subWarnings.insert(subWarnings.end(),
                               std::make_move_iterator(result.warnings.begin()),
                               std::make_move_iterator(result.warnings.end()));
            subDecisionPoints.insert(subDecisionPoints.end(),
                                     std::make_move_iterator(result.decisionPoints.begin()),
                                     std::make_move_iterator(result.decisionPoints.end()));
            return result.code;
        };

        auto colExprWithBinding = [&](size_t colIndex, const std::string& expr) -> std::string {
            if (colIndex < this->context.pendingAnchorCteBindings.size() &&
                !this->context.pendingAnchorCteBindings[colIndex].empty()) {
                return expr + " >>= " + this->context.pendingAnchorCteBindings[colIndex];
            }
            return expr;
        };

        // A result column the caller reads back is widened here, the way the ordinary SELECT
        // widens its own: the compound this select is an arm of decided the widening for every arm
        // at once, so that the arms keep the one common type sqlite_orm reads them back through.
        auto colExprAsResultColumn = [&](size_t colIndex, const std::string& expr) -> std::string {
            if (colIndex >= widenedResultColumns.size()) {
                return expr;
            }
            const ResultColumnWidening& widening = widenedResultColumns[colIndex];
            std::string widened = expr;
            if (widening.integerCast) {
                widened = "cast<int64_t>(" + widened + ")";
                this->context.recordComment(
                    sourceSpanComment(kCommentBitwiseResultCast, *selectNode.columns.at(colIndex).expression));
            }
            if (widening.asOptional) {
                widened = "as_optional(" + widened + ")";
            }
            return widened;
        };
        // A CTE column is read back through sqlite_orm's `extract_colref_expressions`, which is
        // deleted for a `select_t`, so a subquery standing as a WHOLE column of a CTE has no form
        // there. It is that slot alone: the same subquery in the CTE's WHERE, or under a call or an
        // operator, compiles, and the emitter's answer about which node its code came out for is
        // what tells the two apart. The placeholder stands in an expression slot, so the statement
        // carrying it is left out whole rather than handed out as a header that does not build.
        auto cteColumnOrPlaceholder = [&](const AstNode& columnExpression, std::string code) -> std::string {
            if (!cteBodySelect || code.empty() || this->context.emittedSubqueryFormNode != &columnExpression) {
                return code;
            }
            CodeGenResult placeholder =
                unsupportedPlaceholder(this->context,
                                       "(SELECT ...)",
                                       "a subquery as a whole column of a CTE is not mapped to sqlite_orm codegen: "
                                       "sqlite_orm reads the columns of a CTE with extract_colref_expressions(), "
                                       "which declares no overload for a select(...)",
                                       columnExpression);
            subWarnings.insert(subWarnings.end(),
                               std::make_move_iterator(placeholder.warnings.begin()),
                               std::make_move_iterator(placeholder.warnings.end()));
            return placeholder.code;
        };
        auto resultColumnCode = [&](size_t colIndex) -> std::string {
            const AstNode& columnExpression = *selectNode.columns.at(colIndex).expression;
            return colExprWithBinding(
                colIndex,
                colExprAsResultColumn(colIndex,
                                      cteColumnOrPlaceholder(columnExpression, expressionCode(columnExpression))));
        };

        bool isStar = selectNode.columns.size() == 1 && !selectNode.columns.at(0).expression;
        // A subquery answers for its own sources the way the outer select does, and settles them
        // at the same point, before the first clause is generated.
        const SelectFromSources fromSources = selectFromSources(this->context, selectNode.fromClause);
        if (isStar) {
            // Same rule as the outer star: the row comes from the plain struct, so no reference of
            // this select may name the alias.
            this->context.implicitSourceAlias.reset();
        }
        this->context.canNameInferredFromSources =
            fromSources.leadingAnyAliased && !fromSources.hasCteSource && !isStar;
        // The same count and the same emissions the outer select settles, for the same reason.
        const size_t sourcesNamedByFromClause =
            this->context.canNameInferredFromSources && fromSources.leadingTypes.size() > 1u
                ? fromSources.leadingTypes.size()
                : 0u;
        const auto fromEmissions = fromItemEmissions(this->context, selectNode.fromClause, sourcesNamedByFromClause);
        this->context.sourcesWrittenWithoutAlias =
            sourcesWrittenWithoutAlias(selectNode.fromClause, fromSources, fromEmissions, isStar);
        std::string columnPart;
        if (isStar) {
            if (selectNode.fromClause.empty()) {
                subWarnings.push_back("SELECT * subexpression requires FROM for sqlite_orm asterisk<...>()");
                return CodeGenResult{{}, std::move(subDecisionPoints), std::move(subWarnings)};
            }
            const std::string& subStarRow = this->context.implicitSingleSourceCteTypedef
                                                ? *this->context.implicitSingleSourceCteTypedef
                                                : this->context.structName;
            columnPart = "asterisk<" + subStarRow + ">()";
        } else {
            // `asterisk<T>()` is the form for a result list that is a `*` and nothing else, so a
            // `*` standing next to other columns leaves the subquery unmapped; the caller places
            // the placeholder and underlines the subquery it stands for.
            for (const auto& column: selectNode.columns) {
                if (!column.expression) {
                    subWarnings.push_back(
                        sourceSpanWarning("a `*` result column next to other result columns is not mapped "
                                          "to sqlite_orm select(...)",
                                          selectNode));
                    return CodeGenResult{{}, std::move(subDecisionPoints), std::move(subWarnings)};
                }
            }
            if (selectNode.distinct) {
                // A trigger's WHEN clause is default-constructed by sqlite_orm, so a subquery
                // standing in one only compiles while every clause it carries has a default
                // constructor. `distinct_t`, `where_t`, `order_by_t` and a join that carries an
                // `on(...)` or a `using_(...)` declare a constructor and no default one; `from_t`,
                // `limit_t`, a cross or natural join and a named window hold nothing and do.
                this->context.recordFormWithoutDefaultConstructor("DISTINCT");
                if (selectNode.columns.size() == 1) {
                    columnPart = "distinct(" + resultColumnCode(0) + ")";
                } else {
                    columnPart = "distinct(columns(";
                    for (size_t i = 0; i < selectNode.columns.size(); ++i) {
                        if (i > 0)
                            columnPart += ", ";
                        columnPart += resultColumnCode(i);
                    }
                    columnPart += "))";
                }
            } else if (selectNode.columns.size() == 1) {
                columnPart = resultColumnCode(0);
            } else {
                columnPart = "columns(";
                for (size_t i = 0; i < selectNode.columns.size(); ++i) {
                    if (i > 0)
                        columnPart += ", ";
                    columnPart += resultColumnCode(i);
                }
                columnPart += ")";
            }
        }
        this->context.pendingAnchorCteBindings.clear();
        // Every clause after the result list resolves a name against its aliases first.
        this->context.selectResultColumnAliases = resultColumnAliases(selectNode);

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
        auto isCteSource = [&](std::string_view tableName) -> bool {
            auto key = normalizeSqlIdentifier(tableName);
            return this->context.activeCteTypedefByTableKey.find(key) != this->context.activeCteTypedefByTableKey.end();
        };
        std::vector<std::string> tailParts;
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
            std::string joinCode;
            switch (joinKind) {
                case JoinKind::crossJoin:
                case JoinKind::naturalInnerJoin:
                    joinCode = std::string(joinSqliteOrmApiName(joinKind)) + "<" + rightType + ">()";
                    break;
                case JoinKind::naturalLeftJoin:
                    subWarnings.push_back(
                        "NATURAL LEFT JOIN is not supported in sqlite_orm; generated natural_join does not match SQL "
                        "semantics");
                    joinCode = "natural_join<" + rightType + ">()";
                    break;
                default: {
                    std::string api(joinSqliteOrmApiName(joinKind));
                    if (generatedJoinLosesCrossJoinBarrier(joinItem)) {
                        subWarnings.push_back(constrainedCrossJoinWarning(joinItem, rightType));
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
            if (joinKind != JoinKind::crossJoin && joinKind != JoinKind::naturalInnerJoin &&
                joinKind != JoinKind::naturalLeftJoin) {
                this->context.recordFormWithoutDefaultConstructor("JOIN");
            }
            tailParts.push_back(std::move(joinCode));
        }

        if (selectNode.whereClause) {
            this->context.recordFormWithoutDefaultConstructor("WHERE");
            const ParenthesizedConditionScope conditionScope{this->context, *selectNode.whereClause};
            tailParts.push_back("where(" + expressionCode(*selectNode.whereClause) + ")");
        }

        for (const auto& namedWindow: selectNode.namedWindows) {
            if (!namedWindow.definition) {
                continue;
            }
            std::string windowArgs =
                this->coordinator.codegenOverClause(*namedWindow.definition, subDecisionPoints, subWarnings);
            if (!windowArgs.empty()) {
                tailParts.push_back("window(" + identifierToCppStringLiteral(namedWindow.name) + ", " + windowArgs +
                                    ")");
            }
        }

        // SQLite resolves the ORDER BY, the LIMIT and the OFFSET of a subquery against its own FROM
        // and result aliases alone ("no such column: t.Id" for `ORDER BY t.Id` over an enclosing
        // `t`), so no name there is a correlated reference, nor one of a subquery written there.
        struct EnclosingQueriesHidden {
            CodeGeneratorContext* ctx;
            std::vector<ColumnNameScope> saved;
            explicit EnclosingQueriesHidden(CodeGeneratorContext* context) :
                ctx(context), saved(std::exchange(context->enclosingColumnNameScopes, {})) {}
            ~EnclosingQueriesHidden() {
                ctx->enclosingColumnNameScopes = std::move(saved);
            }
        } enclosingQueriesHidden{&this->context};
        if (!selectNode.orderBy.empty()) {
            this->context.recordFormWithoutDefaultConstructor("ORDER BY");
            auto formatSubOrderTerm = [&](const OrderByTerm& term) -> std::string {
                std::string orderCode = "order_by(" + expressionCode(*term.expression) + ")";
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
                    }
                }
                if (term.nulls == NullsOrdering::first) {
                    orderCode += ".nulls_first()";
                } else if (term.nulls == NullsOrdering::last) {
                    orderCode += ".nulls_last()";
                }
                return orderCode;
            };
            if (selectNode.orderBy.size() == 1) {
                tailParts.push_back(formatSubOrderTerm(selectNode.orderBy.at(0)));
            } else {
                std::string multiCode = "multi_order_by(";
                for (size_t i = 0; i < selectNode.orderBy.size(); ++i) {
                    if (i > 0)
                        multiCode += ", ";
                    multiCode += formatSubOrderTerm(selectNode.orderBy.at(i));
                }
                multiCode += ")";
                tailParts.push_back(multiCode);
            }
        }

        if (selectNode.limitValue) {
            std::string limitPart = "limit(" + expressionCode(*selectNode.limitValue);
            if (selectNode.offsetValue) {
                limitPart += ", offset(" + expressionCode(*selectNode.offsetValue) + ")";
            }
            limitPart += ")";
            tailParts.push_back(std::move(limitPart));
        }

        // A placeholder rather than an empty result: the slots a subquery stands in answer an empty
        // one differently, and a CTE body answers it with the outer statement generated without
        // the CTE, where this select is one sqlite_orm has a form for, only not the one it means.
        if (inferredFromWouldBeInvented(selectNode, this->context)) {
            CodeGenResult carried;
            carried.decisionPoints = std::move(subDecisionPoints);
            carried.warnings = std::move(subWarnings);
            return unsupportedPlaceholder(this->context,
                                          "(SELECT ...)",
                                          std::string(kWarningSelectWithoutFromNamesTable),
                                          selectNode,
                                          std::move(carried));
        }
        // The same single point, with the same criterion, for a subquery: a correlated reference
        // to the enclosing table widens the FROM sqlite_orm infers here just like any other
        // mention, and an alias of its own is lost there just the same.
        const std::string& subStarRowType = this->context.implicitSingleSourceCteTypedef
                                                ? *this->context.implicitSingleSourceCteTypedef
                                                : this->context.structName;
        const std::string explicitFrom =
            selectExplicitFrom(this->context,
                               selectNode,
                               fromSources,
                               sourcesNamedByFromClause,
                               isStar ? std::string_view(subStarRowType) : std::string_view());

        std::string code = "select(" + columnPart;
        // Same reservation as the outer star: where `asterisk<T>()` does not name the source the
        // FROM would, pinning it down would leave the star reading a table the select no longer
        // names, so the subquery keeps the form it had.
        const bool starDisagreesWithFrom = isStar && !namesInferredSource(fromSources, subStarRowType);
        if (!explicitFrom.empty() && !starDisagreesWithFrom) {
            code += ", " + explicitFrom;
        }
        for (const auto& part: tailParts) {
            code += ", ";
            code += part;
        }
        code += ")";
        return CodeGenResult{code, std::move(subDecisionPoints), std::move(subWarnings)};
    }

}  // namespace sqlite2orm
