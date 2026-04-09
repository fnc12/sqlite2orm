#include "codegen_select.h"
#include "codegen_context.h"
#include "codegen_utils.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    SelectCodeGenerator::SelectCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context)
        : coordinator(coordinator), context(context) {}

    CodeGenResult SelectCodeGenerator::generateCompoundSelect(const CompoundSelectNode& compoundNode) {
        auto inner = this->tryCodegenCompoundSelectSubexpression(compoundNode);
        std::vector<std::string> compoundWarnings = std::move(inner.warnings);
        if(inner.code.empty()) {
            compoundWarnings.insert(compoundWarnings.begin(),
                                    "compound SELECT (UNION / INTERSECT / EXCEPT) is not mapped to sqlite_orm "
                                    "codegen");
            return CodeGenResult{"/* compound SELECT */", std::move(inner.decisionPoints),
                                 std::move(compoundWarnings), {}, std::move(inner.comments)};
        }
        return CodeGenResult{"auto rows = storage.select(" + inner.code + ");",
                             std::move(inner.decisionPoints), std::move(compoundWarnings), {},
                             std::move(inner.comments)};
    }

    CodeGenResult SelectCodeGenerator::generateSelect(const SelectNode& selectNode) {
        std::vector<std::string> selectWarnings;
        std::vector<DecisionPoint> selectDecisionPoints;
        std::vector<std::string> selectComments;
        for(const auto& fromItem : selectNode.fromClause) {
            if(fromItem.table.derivedSelect) {
                selectWarnings.push_back("subselect in FROM is not supported in sqlite_orm codegen");
                return CodeGenResult{"/* SELECT with derived FROM */", std::move(selectDecisionPoints),
                                     std::move(selectWarnings), {}, std::move(selectComments)};
            }
        }
        this->context.fromTableAliasToStructName.clear();
        this->context.activeTableAliases.clear();
        this->context.nextAliasLetter = 0;
        auto isCteKey = [&](std::string_view tableSqlName) -> bool {
            auto key = normalizeSqlIdentifier(tableSqlName);
            return this->context.activeCteTypedefByTableKey.find(key) !=
                   this->context.activeCteTypedefByTableKey.end();
        };
        auto structForFromTable = [&](std::string_view tableSqlName) -> std::string {
            auto key = normalizeSqlIdentifier(tableSqlName);
            if(auto cteLookup = this->context.activeCteTypedefByTableKey.find(key);
               cteLookup != this->context.activeCteTypedefByTableKey.end()) {
                return cteLookup->second;
            }
            return toStructName(tableSqlName);
        };
        auto prefixStructNameForFromTable = [&](std::string_view tableSqlName) -> std::string {
            const auto key = normalizeSqlIdentifier(tableSqlName);
            if(this->context.activeCteTypedefByTableKey.find(key) !=
               this->context.activeCteTypedefByTableKey.end()) {
                return toStructName(tableSqlName);
            }
            return structForFromTable(tableSqlName);
        };
        if(!selectNode.fromClause.empty()) {
            for(const auto& fromItem : selectNode.fromClause) {
                const auto& ft = fromItem.table;
                if(ft.schemaName) {
                    selectWarnings.push_back("FROM clause schema qualifier '" + *ft.schemaName + "' for table '" +
                                             ft.tableName + "' is not represented in sqlite_orm mapping");
                }
                std::string mappedStructName = structForFromTable(ft.tableName);
                this->context.fromTableAliasToStructName[ft.tableName] = mappedStructName;
                if(ft.alias && !isCteKey(ft.tableName)) {
                    if(this->context.useCpp20TableAliasStyle()) {
                        std::string varName = toCppIdentifier(*ft.alias);
                        TableAliasInfo info{varName, mappedStructName};
                        this->context.activeTableAliases[*ft.alias] = info;
                        this->context.activeTableAliases[ft.tableName] = info;
                        this->context.cpp20TableAliasDeclarations.push_back(
                            Cpp20TableAliasDeclaration{varName, mappedStructName,
                                                       stripIdentifierQuotes(*ft.alias)});
                    } else {
                        char letter = static_cast<char>('a' + this->context.nextAliasLetter++);
                        std::string ormAlias = "alias_" + std::string(1, letter) + "<" + mappedStructName + ">";
                        TableAliasInfo info{ormAlias, mappedStructName};
                        this->context.activeTableAliases[*ft.alias] = info;
                        this->context.activeTableAliases[ft.tableName] = info;
                    }
                    this->context.fromTableAliasToStructName[*ft.alias] = mappedStructName;
                } else if(ft.alias) {
                    this->context.fromTableAliasToStructName[*ft.alias] = mappedStructName;
                }
            }
            std::string_view structNameSource = selectNode.fromClause.at(0).table.tableName;
            for(const auto& fromItem : selectNode.fromClause) {
                if(!isCteKey(fromItem.table.tableName)) {
                    structNameSource = fromItem.table.tableName;
                    break;
                }
            }
            this->context.structName = prefixStructNameForFromTable(structNameSource);
        }

        std::optional<std::string> implicitCte;
        std::optional<std::string> implicitCteTableKey;
        if(!selectNode.fromClause.empty()) {
            const auto& firstFrom = selectNode.fromClause.at(0).table;
            const auto firstKey = normalizeSqlIdentifier(firstFrom.tableName);
            if(this->context.activeCteTypedefByTableKey.find(firstKey) !=
               this->context.activeCteTypedefByTableKey.end()) {
                bool allCte = true;
                for(const auto& fromItem : selectNode.fromClause) {
                    if(!isCteKey(fromItem.table.tableName)) {
                        allCte = false;
                        break;
                    }
                }
                if(selectNode.fromClause.size() == 1u || allCte) {
                    implicitCte = this->context.activeCteTypedefByTableKey.at(firstKey);
                    implicitCteTableKey = firstKey;
                }
            }
        }
        struct ImplicitCteScope {
            CodeGeneratorContext* ctx;
            std::optional<std::string> savedTypedef;
            std::optional<std::string> savedTableKey;
            ImplicitCteScope(CodeGeneratorContext* context, std::optional<std::string> implTypedef,
                             std::optional<std::string> implTableKey)
                : ctx(context), savedTypedef(std::move(context->implicitSingleSourceCteTypedef)),
                  savedTableKey(std::move(context->implicitCteFromTableKeyNorm)) {
                ctx->implicitSingleSourceCteTypedef = std::move(implTypedef);
                ctx->implicitCteFromTableKeyNorm = std::move(implTableKey);
            }
            ~ImplicitCteScope() {
                ctx->implicitSingleSourceCteTypedef = std::move(savedTypedef);
                ctx->implicitCteFromTableKeyNorm = std::move(savedTableKey);
            }
        } implicitScope{&this->context, std::move(implicitCte), std::move(implicitCteTableKey)};

        CodeGeneratorContext selectAltBaseline = this->context;

        auto expressionCode = [&](const AstNode& node) -> std::string {
            auto result = this->coordinator.generateNode(node);
            selectWarnings.insert(selectWarnings.end(), std::make_move_iterator(result.warnings.begin()),
                                  std::make_move_iterator(result.warnings.end()));
            selectDecisionPoints.insert(selectDecisionPoints.end(),
                                        std::make_move_iterator(result.decisionPoints.begin()),
                                        std::make_move_iterator(result.decisionPoints.end()));
            appendUniqueStrings(selectComments, result.comments);
            return result.code;
        };

        bool isStar = selectNode.columns.size() == 1 && !selectNode.columns.at(0).expression;
        int apiLevelDecisionId = -1;
        if(isStar && !selectNode.fromClause.empty()) {
            apiLevelDecisionId = this->context.nextDecisionPointId++;
        }
        std::string code;
        std::string aliasPreamble;
        const bool cpp20ColumnAliases = this->context.useCpp20ColumnAliasStyle();
        if(!isStar) {
            if(hasAnyColumnAlias(selectNode.columns)) {
                if(cpp20ColumnAliases) {
                    aliasPreamble = generateCpp20ColumnAliasPreamble(selectNode.columns);
                } else {
                    aliasPreamble = generateColumnAliasPreamble(selectNode.columns);
                }
            }
            code = "auto rows = storage.select(";
            if(selectNode.distinct) {
                if(selectNode.columns.size() == 1) {
                    auto colCode = expressionCode(*selectNode.columns.at(0).expression);
                    code += "distinct(" +
                            wrapWithColumnAlias(colCode, selectNode.columns.at(0).alias, cpp20ColumnAliases) + ")";
                } else {
                    code += "distinct(columns(";
                    for(size_t i = 0; i < selectNode.columns.size(); ++i) {
                        if(i > 0) code += ", ";
                        auto colCode = expressionCode(*selectNode.columns.at(i).expression);
                        code +=
                            wrapWithColumnAlias(colCode, selectNode.columns.at(i).alias, cpp20ColumnAliases);
                    }
                    code += "))";
                }
            } else if(selectNode.columns.size() == 1) {
                auto colCode = expressionCode(*selectNode.columns.at(0).expression);
                code += wrapWithColumnAlias(colCode, selectNode.columns.at(0).alias, cpp20ColumnAliases);
            } else {
                code += "columns(";
                for(size_t i = 0; i < selectNode.columns.size(); ++i) {
                    if(i > 0) code += ", ";
                    auto colCode = expressionCode(*selectNode.columns.at(i).expression);
                    code +=
                        wrapWithColumnAlias(colCode, selectNode.columns.at(i).alias, cpp20ColumnAliases);
                }
                code += ")";
            }
        }

        this->context.activeSelectColumnAliases.clear();
        this->context.activeSelectColumnAliasCpp20Vars.clear();
        for(const auto& column : selectNode.columns) {
            if(!column.alias.empty()) {
                std::string key = toLowerAscii(stripColumnAliasQuotes(column.alias));
                this->context.activeSelectColumnAliases[key] = columnAliasTypeName(column.alias);
                this->context.activeSelectColumnAliasCpp20Vars[key] = columnAliasCpp20VarName(column.alias);
            }
        }

        std::vector<std::string> selectTrailingClauses;
        auto appendClause = [&](const std::string& clause) { selectTrailingClauses.push_back(clause); };

        auto isCteSource = [&](std::string_view tableName) -> bool {
            auto key = normalizeSqlIdentifier(tableName);
            return this->context.activeCteTypedefByTableKey.find(key) !=
                   this->context.activeCteTypedefByTableKey.end();
        };
        auto resolveJoinType = [&](const FromTableClause& ft) -> std::string {
            std::string key = ft.alias ? *ft.alias : ft.tableName;
            auto it = this->context.activeTableAliases.find(key);
            if(it != this->context.activeTableAliases.end()) {
                return it->second.ormAliasType;
            }
            return structForFromTable(ft.tableName);
        };
        auto cteUsingColumnCode = [&](std::string_view tableName, std::string_view colSql) -> std::string {
            auto tableKey = normalizeSqlIdentifier(tableName);
            auto cteIt = this->context.activeCteTypedefByTableKey.find(tableKey);
            if(cteIt != this->context.activeCteTypedefByTableKey.end()) {
                if(this->context.isExplicitCteColumn(tableKey, std::string(colSql))) {
                    return "column<" + cteIt->second + ">(" +
                           identifierToCppStringLiteral(stripIdentifierQuotes(colSql)) + ")";
                }
                auto baseIt = this->context.cteBaseStructByKey.find(tableKey);
                if(baseIt != this->context.cteBaseStructByKey.end()) {
                    return "column<" + cteIt->second + ">(&" + baseIt->second + "::" + toCppIdentifier(colSql) +
                           ")";
                }
                return "column<" + cteIt->second + ">(" +
                       identifierToCppStringLiteral(stripIdentifierQuotes(colSql)) + ")";
            }
            return "&" + structForFromTable(tableName) + "::" + toCppIdentifier(colSql);
        };
        bool firstFromIsCte =
            !selectNode.fromClause.empty() && isCteSource(selectNode.fromClause.at(0).table.tableName);
        bool emittedNonCteJoin = false;
        for(size_t joinIndex = 1; joinIndex < selectNode.fromClause.size(); ++joinIndex) {
            const auto& joinItem = selectNode.fromClause.at(joinIndex);
            if(isCteSource(joinItem.table.tableName) && joinItem.leadingJoin == JoinKind::crossJoin) {
                continue;
            }
            if(firstFromIsCte && !emittedNonCteJoin && joinItem.leadingJoin == JoinKind::crossJoin) {
                emittedNonCteJoin = true;
                continue;
            }
            emittedNonCteJoin = true;
            const auto& leftTable = selectNode.fromClause.at(joinIndex - 1).table;
            std::string rightType = resolveJoinType(joinItem.table);
            std::string rightStruct = structForFromTable(joinItem.table.tableName);
            std::string leftStruct = structForFromTable(leftTable.tableName);
            std::string joinCode;
            switch(joinItem.leadingJoin) {
            case JoinKind::crossJoin:
            case JoinKind::naturalInnerJoin:
                joinCode = std::string(joinSqliteOrmApiName(joinItem.leadingJoin)) + "<" + rightType + ">()";
                break;
            case JoinKind::naturalLeftJoin:
                selectWarnings.push_back(
                    "NATURAL LEFT JOIN is not supported in sqlite_orm; generated natural_join does not match SQL "
                    "semantics");
                joinCode = "natural_join<" + rightType + ">()";
                break;
            default: {
                std::string api(joinSqliteOrmApiName(joinItem.leadingJoin));
                if(!joinItem.usingColumnNames.empty()) {
                    bool rightIsCte = isCteSource(joinItem.table.tableName);
                    if(joinItem.usingColumnNames.size() == 1) {
                        std::string usingCol;
                        if(rightIsCte) {
                            usingCol = cteUsingColumnCode(leftTable.tableName, joinItem.usingColumnNames.at(0));
                        } else {
                            usingCol = "&" + rightStruct + "::" +
                                       toCppIdentifier(joinItem.usingColumnNames.at(0));
                        }
                        joinCode = std::string(api) + "<" + rightType + ">(using_(" + usingCol + "))";
                    } else {
                        std::string cond;
                        for(size_t ci = 0; ci < joinItem.usingColumnNames.size(); ++ci) {
                            if(ci > 0) {
                                cond += " and ";
                            }
                            auto col = toCppIdentifier(joinItem.usingColumnNames.at(ci));
                            cond += "c(&" + leftStruct + "::" + col + ") == c(&" + rightStruct + "::" + col +
                                    ")";
                        }
                        joinCode = std::string(api) + "<" + rightType + ">(on(" + cond + "))";
                    }
                } else if(joinItem.onExpression) {
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

        if(selectNode.whereClause) {
            appendClause("where(" + expressionCode(*selectNode.whereClause) + ")");
        }

        if(selectNode.groupBy) {
            std::string groupCode = "group_by(";
            for(size_t i = 0; i < selectNode.groupBy->expressions.size(); ++i) {
                if(i > 0) groupCode += ", ";
                groupCode += expressionCode(*selectNode.groupBy->expressions.at(i));
            }
            groupCode += ")";
            if(selectNode.groupBy->having) {
                groupCode += ".having(" + expressionCode(*selectNode.groupBy->having) + ")";
            }
            appendClause(groupCode);
        }

        for(const auto& namedWindow : selectNode.namedWindows) {
            if(!namedWindow.definition) {
                continue;
            }
            std::string windowArgs =
                this->coordinator.codegenOverClause(*namedWindow.definition, selectDecisionPoints, selectWarnings);
            if(!windowArgs.empty()) {
                appendClause("window(" + identifierToCppStringLiteral(namedWindow.name) + ", " + windowArgs +
                             ")");
            } else {
                selectWarnings.push_back("WINDOW `" + namedWindow.name +
                                         "`: empty or unmapped window definition omitted in sqlite_orm codegen");
            }
        }

        if(!selectNode.orderBy.empty()) {
            auto formatOrderTerm = [&](const OrderByTerm& term) -> std::string {
                std::string orderCode = "order_by(" + expressionCode(*term.expression) + ")";
                if(term.direction == SortDirection::asc) {
                    orderCode += ".asc()";
                } else if(term.direction == SortDirection::desc) {
                    orderCode += ".desc()";
                }
                if(!term.collation.empty()) {
                    std::string collLower = toLowerAscii(term.collation);
                    if(collLower == "nocase") {
                        orderCode += ".collate_nocase()";
                    } else if(collLower == "binary") {
                        orderCode += ".collate_binary()";
                    } else if(collLower == "rtrim") {
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
            if(selectNode.orderBy.size() == 1) {
                appendClause(formatOrderTerm(selectNode.orderBy.at(0)));
            } else {
                std::string multiCode = "multi_order_by(";
                for(size_t i = 0; i < selectNode.orderBy.size(); ++i) {
                    if(i > 0) multiCode += ", ";
                    multiCode += formatOrderTerm(selectNode.orderBy.at(i));
                }
                multiCode += ")";
                appendClause(multiCode);
            }
        }

        if(selectNode.limitValue >= 0) {
            std::string limitCode = "limit(" + std::to_string(selectNode.limitValue);
            if(selectNode.offsetValue >= 0) {
                limitCode += ", offset(" + std::to_string(selectNode.offsetValue) + ")";
            }
            limitCode += ")";
            appendClause(limitCode);
        }

        std::string trailingJoined;
        for(size_t ti = 0; ti < selectTrailingClauses.size(); ++ti) {
            if(ti > 0) {
                trailingJoined += ", ";
            }
            trailingJoined += selectTrailingClauses.at(ti);
        }

        if(isStar) {
            const std::string& starRowType = this->context.implicitSingleSourceCteTypedef
                                                 ? *this->context.implicitSingleSourceCteTypedef
                                                 : this->context.structName;
            std::string tail = selectTrailingClauses.empty() ? "" : (", " + trailingJoined);
            std::string codeGetAll =
                "auto rows = storage.get_all<" + starRowType + ">(" + trailingJoined + ");";
            std::string codeSelectObject =
                "auto rows = storage.select(object<" + starRowType + ">()" + tail + ");";
            std::string codeSelectAsterisk =
                "auto rows = storage.select(asterisk<" + starRowType + ">()" + tail + ");";
            std::string chosenApi = "get_all";
            code = codeGetAll;
            if(policyEquals(this->context.codeGenPolicy, "api_level", "select_object")) {
                chosenApi = "select_object";
                code = codeSelectObject;
            } else if(policyEquals(this->context.codeGenPolicy, "api_level", "select_asterisk")) {
                chosenApi = "select_asterisk";
                code = codeSelectAsterisk;
            }
            if(apiLevelDecisionId >= 0) {
                selectDecisionPoints.insert(
                    selectDecisionPoints.begin(),
                    DecisionPoint{apiLevelDecisionId, "api_level", chosenApi, code,
                                  {Alternative{"select_object", codeSelectObject,
                                               "select(object<T>(), ...) returns std::tuple of columns"},
                                   Alternative{"select_asterisk", codeSelectAsterisk,
                                               "select(asterisk<T>(), ...) returns full row objects"}}});
            }
        } else {
            if(!trailingJoined.empty()) {
                code += ", ";
                code += trailingJoined;
            }
            code += ");";
        }
        if(hasAnyColumnAlias(selectNode.columns)) {
            if(this->context.useCpp20ColumnAliasStyle()) {
                if(!aliasPreamble.empty()) {
                    code = aliasPreamble + code;
                }
                appendUniqueString(selectComments, kCommentCpp20ColumnAliases);
            } else {
                bool hasBuiltin = false;
                bool hasCustom = false;
                for(const auto& column : selectNode.columns) {
                    if(column.alias.empty()) continue;
                    if(needsCustomAliasStruct(column.alias))
                        hasCustom = true;
                    else
                        hasBuiltin = true;
                }
                if(hasCustom) {
                    code = aliasPreamble + code;
                    selectWarnings.push_back(
                        "SELECT column alias uses as<AliasTag>() with a generated sqlite_orm::alias_tag struct");
                }
                if(hasBuiltin) {
                    selectWarnings.push_back(
                        "SELECT column alias uses sqlite_orm built-in colalias_* types; "
                        "requires `using namespace sqlite_orm`");
                }
                if(!this->context.columnAliasStyleOverride) {
                    CodeGenerator altGen;
                    altGen.context() = selectAltBaseline;
                    altGen.context().columnAliasStyleOverride = "cpp20_literal";
                    auto altRes = altGen.generateNode(selectNode);
                    Alternative cpp20Alt{"cpp20_literal", altRes.code,
                                         "C++20 literal aliases (`orm_column_alias`, `_col`)"};
                    cpp20Alt.comments = std::move(altRes.comments);
                    selectDecisionPoints.push_back(
                        DecisionPoint{this->context.nextDecisionPointId++, "column_alias_style", "alias_tag",
                                      code, {std::move(cpp20Alt)}});
                }
            }
        }
        if(hasAnyColumnAlias(selectNode.columns) && this->context.useCpp20ColumnAliasStyle() &&
           !this->context.columnAliasStyleOverride) {
            CodeGenerator altGen;
            altGen.context() = selectAltBaseline;
            altGen.context().columnAliasStyleOverride = "alias_tag";
            auto altRes = altGen.generateNode(selectNode);
            selectDecisionPoints.push_back(DecisionPoint{
                this->context.nextDecisionPointId++, "column_alias_style", "cpp20_literal", code,
                {Alternative{"alias_tag", altRes.code,
                             "alias_tag / colalias_* / generated struct (default; wider compiler support)"}}});
        }
        bool hasTableAliases = !this->context.activeTableAliases.empty();
        if(!this->context.cpp20TableAliasDeclarations.empty() && !this->context.activeWithCteStyle) {
            std::string prelude;
            for(const auto& tad : this->context.cpp20TableAliasDeclarations) {
                prelude += "constexpr orm_table_alias auto " + tad.variableName + " = " +
                           identifierToCppStringLiteral(tad.sqlAlias) + "_alias.for_<" + tad.baseStructName +
                           ">();\n";
            }
            code = prelude + code;
        }
        if(hasTableAliases) {
            if(!this->context.activeWithCteStyle && !this->context.suppressTableAliasStyleDecisionPoint) {
                const std::string currentStyle =
                    policyEquals(this->context.codeGenPolicy, "table_alias_style", "cpp20") ? "cpp20"
                                                                                            : "pre_cpp20";
                auto makeAlt = [&](const char* styleValue) -> std::string {
                    CodeGenPolicy pol =
                        policyWithOverride(this->context.codeGenPolicy, "table_alias_style", styleValue);
                    CodeGenerator gen;
                    gen.codeGenPolicy = &pol;
                    gen.context().suppressTableAliasStyleDecisionPoint = true;
                    return gen.generate(static_cast<const AstNode&>(selectNode)).code;
                };
                selectDecisionPoints.push_back(DecisionPoint{
                    this->context.nextDecisionPointId++, "table_alias_style", currentStyle, code,
                    {Alternative{"pre_cpp20", makeAlt("pre_cpp20"),
                                 "alias_a<T> + alias_column<> (wider compiler support)"},
                     Alternative{"cpp20", makeAlt("cpp20"),
                                 "\"name\"_alias.for_<T>() + ->* (C++20 sqlite_orm)"}}});
            }
        }
        this->context.cpp20TableAliasDeclarations.clear();
        this->context.activeSelectColumnAliases.clear();
        this->context.activeSelectColumnAliasCpp20Vars.clear();
        return CodeGenResult{code, std::move(selectDecisionPoints), std::move(selectWarnings), {},
                             std::move(selectComments)};
    }

    CodeGenResult SelectCodeGenerator::tryCodegenSqliteSelectSubexpression(const SelectNode& selectNode) {
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

            SubselectAliasRestore(CodeGeneratorContext* context)
                : ctx(context), savedAliases(context->fromTableAliasToStructName),
                  savedTableAliases(context->activeTableAliases),
                  savedNextAliasLetter(context->nextAliasLetter),
                  savedColumnAliases(context->activeSelectColumnAliases),
                  savedColumnAliasCpp20Vars(context->activeSelectColumnAliasCpp20Vars),
                  savedStructName(context->structName),
                  savedImplicitCte(std::move(context->implicitSingleSourceCteTypedef)),
                  savedImplicitCteTableKey(std::move(context->implicitCteFromTableKeyNorm)) {}

            ~SubselectAliasRestore() {
                ctx->fromTableAliasToStructName = std::move(savedAliases);
                ctx->activeTableAliases = std::move(savedTableAliases);
                ctx->nextAliasLetter = savedNextAliasLetter;
                ctx->activeSelectColumnAliases = std::move(savedColumnAliases);
                ctx->activeSelectColumnAliasCpp20Vars = std::move(savedColumnAliasCpp20Vars);
                ctx->structName = std::move(savedStructName);
                ctx->implicitSingleSourceCteTypedef = std::move(savedImplicitCte);
                ctx->implicitCteFromTableKeyNorm = std::move(savedImplicitCteTableKey);
            }
        } restore{&this->context};

        std::vector<std::string> subWarnings;
        std::vector<DecisionPoint> subDecisionPoints;

        if(selectNode.groupBy || !selectNode.orderBy.empty()) {
            subWarnings.push_back(
                "GROUP BY / ORDER BY in subquery are not yet mapped to sqlite_orm select(...)");
            return CodeGenResult{{}, {}, std::move(subWarnings)};
        }
        if(selectNode.offsetValue >= 0 && selectNode.limitValue < 0) {
            subWarnings.push_back(
                "OFFSET without LIMIT in subquery is not yet mapped to sqlite_orm select(...)");
            return CodeGenResult{{}, {}, std::move(subWarnings)};
        }

        for(const auto& fromItem : selectNode.fromClause) {
            if(fromItem.table.derivedSelect) {
                subWarnings.push_back("subselect in FROM is not supported in sqlite_orm codegen");
                return CodeGenResult{{}, {}, std::move(subWarnings)};
            }
        }

        this->context.fromTableAliasToStructName.clear();
        this->context.activeTableAliases.clear();
        this->context.nextAliasLetter = 0;
        auto isCteKey = [&](std::string_view tableSqlName) -> bool {
            auto key = normalizeSqlIdentifier(tableSqlName);
            return this->context.activeCteTypedefByTableKey.find(key) !=
                   this->context.activeCteTypedefByTableKey.end();
        };
        auto structForFromTable = [&](std::string_view tableSqlName) -> std::string {
            auto key = normalizeSqlIdentifier(tableSqlName);
            if(auto cteLookup = this->context.activeCteTypedefByTableKey.find(key);
               cteLookup != this->context.activeCteTypedefByTableKey.end()) {
                return cteLookup->second;
            }
            return toStructName(tableSqlName);
        };
        auto prefixStructNameForFromTable = [&](std::string_view tableSqlName) -> std::string {
            const auto key = normalizeSqlIdentifier(tableSqlName);
            if(this->context.activeCteTypedefByTableKey.find(key) !=
               this->context.activeCteTypedefByTableKey.end()) {
                return toStructName(tableSqlName);
            }
            return structForFromTable(tableSqlName);
        };
        if(!selectNode.fromClause.empty()) {
            for(const auto& fromItem : selectNode.fromClause) {
                const auto& ft = fromItem.table;
                if(ft.schemaName) {
                    subWarnings.push_back("FROM clause schema qualifier '" + *ft.schemaName + "' for table '" +
                                          ft.tableName + "' is not represented in sqlite_orm mapping");
                }
                std::string mappedStructName = structForFromTable(ft.tableName);
                this->context.fromTableAliasToStructName[ft.tableName] = mappedStructName;
                if(ft.alias && !isCteKey(ft.tableName)) {
                    if(this->context.useCpp20TableAliasStyle()) {
                        std::string varName = toCppIdentifier(*ft.alias);
                        TableAliasInfo info{varName, mappedStructName};
                        this->context.activeTableAliases[*ft.alias] = info;
                        this->context.activeTableAliases[ft.tableName] = info;
                        this->context.cpp20TableAliasDeclarations.push_back(
                            Cpp20TableAliasDeclaration{varName, mappedStructName,
                                                       stripIdentifierQuotes(*ft.alias)});
                    } else {
                        char letter = static_cast<char>('a' + this->context.nextAliasLetter++);
                        std::string ormAlias = "alias_" + std::string(1, letter) + "<" + mappedStructName + ">";
                        TableAliasInfo info{ormAlias, mappedStructName};
                        this->context.activeTableAliases[*ft.alias] = info;
                        this->context.activeTableAliases[ft.tableName] = info;
                    }
                    this->context.fromTableAliasToStructName[*ft.alias] = mappedStructName;
                } else if(ft.alias) {
                    this->context.fromTableAliasToStructName[*ft.alias] = mappedStructName;
                }
            }
            std::string_view structNameSource = selectNode.fromClause.at(0).table.tableName;
            for(const auto& fromItem : selectNode.fromClause) {
                if(!isCteKey(fromItem.table.tableName)) {
                    structNameSource = fromItem.table.tableName;
                    break;
                }
            }
            this->context.structName = prefixStructNameForFromTable(structNameSource);
        }
        if(!selectNode.fromClause.empty()) {
            const auto& firstFrom = selectNode.fromClause.at(0).table;
            const auto firstKey = normalizeSqlIdentifier(firstFrom.tableName);
            if(this->context.activeCteTypedefByTableKey.find(firstKey) !=
               this->context.activeCteTypedefByTableKey.end()) {
                bool allCte = true;
                for(const auto& fromItem : selectNode.fromClause) {
                    if(!isCteKey(fromItem.table.tableName)) {
                        allCte = false;
                        break;
                    }
                }
                if(selectNode.fromClause.size() == 1u || allCte) {
                    this->context.implicitSingleSourceCteTypedef =
                        this->context.activeCteTypedefByTableKey.at(firstKey);
                    this->context.implicitCteFromTableKeyNorm = firstKey;
                }
            }
        }

        auto expressionCode = [&](const AstNode& node) -> std::string {
            auto result = this->coordinator.generateNode(node);
            subWarnings.insert(subWarnings.end(), std::make_move_iterator(result.warnings.begin()),
                               std::make_move_iterator(result.warnings.end()));
            subDecisionPoints.insert(subDecisionPoints.end(),
                                     std::make_move_iterator(result.decisionPoints.begin()),
                                     std::make_move_iterator(result.decisionPoints.end()));
            return result.code;
        };

        bool isStar = selectNode.columns.size() == 1 && !selectNode.columns.at(0).expression;
        std::string columnPart;
        if(isStar) {
            if(selectNode.fromClause.empty()) {
                subWarnings.push_back("SELECT * subexpression requires FROM for sqlite_orm asterisk<...>()");
                return CodeGenResult{{}, std::move(subDecisionPoints), std::move(subWarnings)};
            }
            const std::string& subStarRow = this->context.implicitSingleSourceCteTypedef
                                                ? *this->context.implicitSingleSourceCteTypedef
                                                : this->context.structName;
            columnPart = "asterisk<" + subStarRow + ">()";
        } else {
            if(selectNode.distinct) {
                if(selectNode.columns.size() == 1) {
                    columnPart = "distinct(" + expressionCode(*selectNode.columns.at(0).expression) + ")";
                } else {
                    columnPart = "distinct(columns(";
                    for(size_t i = 0; i < selectNode.columns.size(); ++i) {
                        if(i > 0) columnPart += ", ";
                        columnPart += expressionCode(*selectNode.columns.at(i).expression);
                    }
                    columnPart += "))";
                }
            } else if(selectNode.columns.size() == 1) {
                columnPart = expressionCode(*selectNode.columns.at(0).expression);
            } else {
                columnPart = "columns(";
                for(size_t i = 0; i < selectNode.columns.size(); ++i) {
                    if(i > 0) columnPart += ", ";
                    columnPart += expressionCode(*selectNode.columns.at(i).expression);
                }
                columnPart += ")";
            }
        }

        auto resolveJoinType = [&](const FromTableClause& ft) -> std::string {
            std::string key = ft.alias ? *ft.alias : ft.tableName;
            auto it = this->context.activeTableAliases.find(key);
            if(it != this->context.activeTableAliases.end()) {
                return it->second.ormAliasType;
            }
            return structForFromTable(ft.tableName);
        };
        auto cteUsingColumnCode = [&](std::string_view tableName, std::string_view colSql) -> std::string {
            auto tableKey = normalizeSqlIdentifier(tableName);
            auto cteIt = this->context.activeCteTypedefByTableKey.find(tableKey);
            if(cteIt != this->context.activeCteTypedefByTableKey.end()) {
                if(this->context.isExplicitCteColumn(tableKey, std::string(colSql))) {
                    return "column<" + cteIt->second + ">(" +
                           identifierToCppStringLiteral(stripIdentifierQuotes(colSql)) + ")";
                }
                auto baseIt = this->context.cteBaseStructByKey.find(tableKey);
                if(baseIt != this->context.cteBaseStructByKey.end()) {
                    return "column<" + cteIt->second + ">(&" + baseIt->second + "::" + toCppIdentifier(colSql) +
                           ")";
                }
                return "column<" + cteIt->second + ">(" +
                       identifierToCppStringLiteral(stripIdentifierQuotes(colSql)) + ")";
            }
            return "&" + structForFromTable(tableName) + "::" + toCppIdentifier(colSql);
        };
        auto isCteSource = [&](std::string_view tableName) -> bool {
            auto key = normalizeSqlIdentifier(tableName);
            return this->context.activeCteTypedefByTableKey.find(key) !=
                   this->context.activeCteTypedefByTableKey.end();
        };
        bool firstFromIsCte =
            !selectNode.fromClause.empty() && isCteSource(selectNode.fromClause.at(0).table.tableName);
        bool emittedNonCteJoin = false;
        std::vector<std::string> tailParts;
        for(size_t joinIndex = 1; joinIndex < selectNode.fromClause.size(); ++joinIndex) {
            const auto& joinItem = selectNode.fromClause.at(joinIndex);
            if(isCteSource(joinItem.table.tableName) && joinItem.leadingJoin == JoinKind::crossJoin) {
                continue;
            }
            if(firstFromIsCte && !emittedNonCteJoin && joinItem.leadingJoin == JoinKind::crossJoin) {
                emittedNonCteJoin = true;
                continue;
            }
            emittedNonCteJoin = true;
            const auto& leftTable = selectNode.fromClause.at(joinIndex - 1).table;
            std::string rightType = resolveJoinType(joinItem.table);
            std::string rightStruct = structForFromTable(joinItem.table.tableName);
            std::string leftStruct = structForFromTable(leftTable.tableName);
            std::string joinCode;
            switch(joinItem.leadingJoin) {
            case JoinKind::crossJoin:
            case JoinKind::naturalInnerJoin:
                joinCode = std::string(joinSqliteOrmApiName(joinItem.leadingJoin)) + "<" + rightType + ">()";
                break;
            case JoinKind::naturalLeftJoin:
                subWarnings.push_back(
                    "NATURAL LEFT JOIN is not supported in sqlite_orm; generated natural_join does not match SQL "
                    "semantics");
                joinCode = "natural_join<" + rightType + ">()";
                break;
            default: {
                std::string api(joinSqliteOrmApiName(joinItem.leadingJoin));
                if(!joinItem.usingColumnNames.empty()) {
                    bool rightIsCte = isCteSource(joinItem.table.tableName);
                    if(joinItem.usingColumnNames.size() == 1) {
                        std::string usingCol;
                        if(rightIsCte) {
                            usingCol = cteUsingColumnCode(leftTable.tableName, joinItem.usingColumnNames.at(0));
                        } else {
                            usingCol = "&" + rightStruct + "::" +
                                       toCppIdentifier(joinItem.usingColumnNames.at(0));
                        }
                        joinCode = std::string(api) + "<" + rightType + ">(using_(" + usingCol + "))";
                    } else {
                        std::string cond;
                        for(size_t ci = 0; ci < joinItem.usingColumnNames.size(); ++ci) {
                            if(ci > 0) {
                                cond += " and ";
                            }
                            auto col = toCppIdentifier(joinItem.usingColumnNames.at(ci));
                            cond += "c(&" + leftStruct + "::" + col + ") == c(&" + rightStruct + "::" + col +
                                    ")";
                        }
                        joinCode = std::string(api) + "<" + rightType + ">(on(" + cond + "))";
                    }
                } else if(joinItem.onExpression) {
                    joinCode = std::string(api) + "<" + rightType + ">(on(" +
                               expressionCode(*joinItem.onExpression) + "))";
                } else {
                    joinCode = std::string(api) + "<" + rightType + ">(on(true))";
                }
                break;
            }
            }
            tailParts.push_back(std::move(joinCode));
        }

        if(selectNode.whereClause) {
            tailParts.push_back("where(" + expressionCode(*selectNode.whereClause) + ")");
        }

        for(const auto& namedWindow : selectNode.namedWindows) {
            if(!namedWindow.definition) {
                continue;
            }
            std::string windowArgs =
                this->coordinator.codegenOverClause(*namedWindow.definition, subDecisionPoints, subWarnings);
            if(!windowArgs.empty()) {
                tailParts.push_back("window(" + identifierToCppStringLiteral(namedWindow.name) + ", " +
                                    windowArgs + ")");
            }
        }

        std::string code = "select(" + columnPart;
        for(const auto& part : tailParts) {
            code += ", ";
            code += part;
        }
        if(selectNode.limitValue >= 0) {
            code += ", limit(" + std::to_string(selectNode.limitValue);
            if(selectNode.offsetValue >= 0) {
                code += ", offset(" + std::to_string(selectNode.offsetValue) + ")";
            }
            code += ")";
        }
        code += ")";
        return CodeGenResult{code, std::move(subDecisionPoints), std::move(subWarnings)};
    }

    CodeGenResult
    SelectCodeGenerator::tryCodegenCompoundSelectSubexpression(const CompoundSelectNode& compoundNode) {
        if(compoundNode.selects.size() != compoundNode.operators.size() + 1) {
            return CodeGenResult{{}, {}, {"internal: compound SELECT operand count mismatch"}};
        }
        auto* firstSelect = dynamic_cast<const SelectNode*>(compoundNode.selects.at(0).get());
        if(!firstSelect) {
            return CodeGenResult{{}, {}, {"compound SELECT arm is not a SelectNode"}};
        }
        CodeGenResult accumulated = this->tryCodegenSqliteSelectSubexpression(*firstSelect);
        if(accumulated.code.empty()) {
            return accumulated;
        }
        for(size_t operatorIndex = 0; operatorIndex < compoundNode.operators.size(); ++operatorIndex) {
            auto* nextSelect =
                dynamic_cast<const SelectNode*>(compoundNode.selects.at(operatorIndex + 1).get());
            if(!nextSelect) {
                return CodeGenResult{{}, {}, {"compound SELECT arm is not a SelectNode"}};
            }
            CodeGenResult nextArm = this->tryCodegenSqliteSelectSubexpression(*nextSelect);
            accumulated.decisionPoints.insert(accumulated.decisionPoints.end(),
                                              std::make_move_iterator(nextArm.decisionPoints.begin()),
                                              std::make_move_iterator(nextArm.decisionPoints.end()));
            accumulated.warnings.insert(accumulated.warnings.end(),
                                        std::make_move_iterator(nextArm.warnings.begin()),
                                        std::make_move_iterator(nextArm.warnings.end()));
            if(nextArm.code.empty()) {
                return CodeGenResult{
                    {}, std::move(accumulated.decisionPoints), std::move(accumulated.warnings)};
            }
            accumulated.code = std::string(compoundSelectApi(compoundNode.operators.at(operatorIndex))) + "(" +
                               accumulated.code + ", " + nextArm.code + ")";
        }
        return accumulated;
    }

    CodeGenResult SelectCodeGenerator::tryCodegenSelectLikeSubquery(const AstNode& node) {
        if(auto* selectNode = dynamic_cast<const SelectNode*>(&node)) {
            return this->tryCodegenSqliteSelectSubexpression(*selectNode);
        }
        if(auto* compoundNode = dynamic_cast<const CompoundSelectNode*>(&node)) {
            return this->tryCodegenCompoundSelectSubexpression(*compoundNode);
        }
        if(auto* withQueryNode = dynamic_cast<const WithQueryNode*>(&node)) {
            auto inner = this->tryCodegenSelectLikeSubquery(*withQueryNode->statement);
            std::vector<std::string> subWarnings = std::move(inner.warnings);
            subWarnings.insert(subWarnings.begin(),
                               "nested WITH in subquery: sqlite_orm select(...) cannot embed CTEs; generated code "
                               "uses the inner SELECT only (WITH clause dropped)");
            return CodeGenResult{std::move(inner.code), std::move(inner.decisionPoints),
                                 std::move(subWarnings)};
        }
        return CodeGenResult{{}, {}, {"subquery is not a SELECT or compound SELECT for sqlite_orm codegen"}};
    }

}  // namespace sqlite2orm
