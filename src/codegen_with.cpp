#include "codegen_with.h"
#include "codegen_context.h"
#include "codegen_utils.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    WithCodeGenerator::WithCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context)
        : coordinator(coordinator), context(context) {}

    CodeGenResult WithCodeGenerator::generateWithQuery(const WithQueryNode& withQueryNode) {
        std::vector<std::string> warnings;
        std::vector<DecisionPoint> allDecisionPoints;

        struct ClearActiveCteMap {
            CodeGeneratorContext* context;
            ~ClearActiveCteMap() {
                context->activeCteTypedefByTableKey.clear();
                context->cteBaseStructByKey.clear();
                context->activeWithCteStyle.reset();
                context->withCteLegacyColVarByPipeKey.clear();
                context->withCteCpp20MonikerVarByCteKey.clear();
                context->withCteCpp20ColVarByPipeKey.clear();
                context->cpp20TableAliasDeclarations.clear();
            }
        } clearGuard{&this->context};

        this->context.activeCteTypedefByTableKey.clear();

        const auto& ctes = withQueryNode.clause.tables;

        std::string withStyle = "indexed_typedef";
        if(policyEquals(this->context.codeGenPolicy, "with_cte_style", "legacy_colalias")) {
            withStyle = "legacy_colalias";
        } else if(policyEquals(this->context.codeGenPolicy, "with_cte_style", "cpp20_monikers")) {
            withStyle = "cpp20_monikers";
        }

        if(withStyle == "legacy_colalias") {
            const bool badForLegacy = ctes.size() != 1u || ctes.front().columnNames.empty();
            if(badForLegacy) {
                warnings.push_back(
                    "WITH: legacy_colalias needs one CTE with an explicit column list; using indexed_typedef");
                withStyle = "indexed_typedef";
            }
        } else if(withStyle == "cpp20_monikers" && ctes.size() != 1u) {
            warnings.push_back(
                "WITH: cpp20_monikers currently supports a single CTE; using indexed_typedef");
            withStyle = "indexed_typedef";
        }

        this->context.activeWithCteStyle = withStyle;

        std::vector<std::string> cteTypedefIds(ctes.size());
        for(size_t cteIndex = 0; cteIndex < ctes.size(); ++cteIndex) {
            if(withStyle == "legacy_colalias") {
                std::string base = toCppIdentifier(ctes[cteIndex].cteName);
                std::string id = base;
                for(size_t counter = 2;; ++counter) {
                    bool clash = false;
                    for(size_t priorCteIndex = 0; priorCteIndex < cteIndex; ++priorCteIndex) {
                        if(cteTypedefIds[priorCteIndex] == id) {
                            clash = true;
                            break;
                        }
                    }
                    if(!clash) {
                        break;
                    }
                    id = base + "_" + std::to_string(counter);
                }
                cteTypedefIds[cteIndex] = id;
            } else {
                cteTypedefIds[cteIndex] = "cte_" + std::to_string(cteIndex);
            }
        }

        for(size_t cteIndex = 0; cteIndex < ctes.size(); ++cteIndex) {
            this->context.activeCteTypedefByTableKey[normalizeSqlIdentifier(ctes[cteIndex].cteName)] =
                cteTypedefIds[cteIndex];
        }

        this->context.withCteLegacyColVarByPipeKey.clear();
        this->context.withCteCpp20MonikerVarByCteKey.clear();
        this->context.withCteCpp20ColVarByPipeKey.clear();

        if(withStyle == "legacy_colalias") {
            for(size_t cteIndex = 0; cteIndex < ctes.size(); ++cteIndex) {
                const std::string normalizedCteTableKey = normalizeSqlIdentifier(ctes[cteIndex].cteName);
                for(size_t columnNameIndex = 0; columnNameIndex < ctes[cteIndex].columnNames.size();
                    ++columnNameIndex) {
                    const std::string colK = normalizeSqlIdentifier(ctes[cteIndex].columnNames[columnNameIndex]);
                    const std::string var = toCppIdentifier(ctes[cteIndex].cteName) + "_" +
                                            toCppIdentifier(ctes[cteIndex].columnNames[columnNameIndex]);
                    this->context.withCteLegacyColVarByPipeKey[normalizedCteTableKey + "|" + colK] = var;
                }
            }
        } else if(withStyle == "cpp20_monikers") {
            for(size_t cteIndex = 0; cteIndex < ctes.size(); ++cteIndex) {
                const std::string normalizedCteTableKey = normalizeSqlIdentifier(ctes[cteIndex].cteName);
                std::string base = toCppIdentifier(ctes[cteIndex].cteName) + "_cte";
                std::string mon = base;
                for(size_t counter = 2;; ++counter) {
                    bool clash = false;
                    for(const auto& kv : this->context.withCteCpp20MonikerVarByCteKey) {
                        if(kv.second == mon) {
                            clash = true;
                            break;
                        }
                    }
                    if(!clash) {
                        break;
                    }
                    mon = base + "_" + std::to_string(counter);
                }
                this->context.withCteCpp20MonikerVarByCteKey[normalizedCteTableKey] = mon;
                for(size_t columnNameIndex = 0; columnNameIndex < ctes[cteIndex].columnNames.size();
                    ++columnNameIndex) {
                    const std::string colK = normalizeSqlIdentifier(ctes[cteIndex].columnNames[columnNameIndex]);
                    const std::string cvar = toCppIdentifier(ctes[cteIndex].cteName) + "__" +
                                             toCppIdentifier(ctes[cteIndex].columnNames[columnNameIndex]);
                    this->context.withCteCpp20ColVarByPipeKey[normalizedCteTableKey + "|" + colK] = cvar;
                }
            }
        }

        this->context.cteBaseStructByKey.clear();
        for(size_t cteIndex = 0; cteIndex < ctes.size(); ++cteIndex) {
            const std::string cteKey = normalizeSqlIdentifier(ctes[cteIndex].cteName);
            const AstNode* bodyAst = ctes[cteIndex].query.get();
            const SelectNode* anchorSelect = dynamic_cast<const SelectNode*>(bodyAst);
            if(!anchorSelect) {
                if(auto* compound = dynamic_cast<const CompoundSelectNode*>(bodyAst)) {
                    if(!compound->selects.empty()) {
                        anchorSelect = dynamic_cast<const SelectNode*>(compound->selects[0].get());
                    }
                }
            }
            if(anchorSelect && !anchorSelect->fromClause.empty()) {
                const auto& firstFrom = anchorSelect->fromClause[0].table;
                auto fromKey = normalizeSqlIdentifier(firstFrom.tableName);
                if(this->context.activeCteTypedefByTableKey.find(fromKey) ==
                   this->context.activeCteTypedefByTableKey.end()) {
                    this->context.cteBaseStructByKey[cteKey] = toStructName(firstFrom.tableName);
                }
            }
        }

        std::vector<std::string> innerCodes;
        innerCodes.reserve(ctes.size());
        for(const auto& cte : ctes) {
            auto part = this->coordinator.tryCodegenSelectLikeSubquery(*cte.query);
            allDecisionPoints.insert(allDecisionPoints.end(),
                std::make_move_iterator(part.decisionPoints.begin()),
                std::make_move_iterator(part.decisionPoints.end()));
            warnings.insert(warnings.end(), std::make_move_iterator(part.warnings.begin()),
                             std::make_move_iterator(part.warnings.end()));
            if(part.code.empty()) {
                warnings.push_back(
                    "WITH: a CTE SELECT is not mapped to a sqlite_orm select(...) subexpression; emitted outer "
                    "statement without storage.with()");
                this->context.activeCteTypedefByTableKey.clear();
                auto inner = this->coordinator.generateNode(*withQueryNode.statement);
                warnings.insert(warnings.end(), std::make_move_iterator(inner.warnings.begin()),
                                 std::make_move_iterator(inner.warnings.end()));
                allDecisionPoints.insert(allDecisionPoints.end(),
                    std::make_move_iterator(inner.decisionPoints.begin()),
                    std::make_move_iterator(inner.decisionPoints.end()));
                return CodeGenResult{inner.code, std::move(allDecisionPoints), std::move(warnings)};
            }
            innerCodes.push_back(std::move(part.code));
        }

        std::string prelude;
        if(withStyle == "cpp20_monikers") {
            prelude = "using namespace sqlite_orm::literals;\n";
            for(size_t cteIndex = 0; cteIndex < ctes.size(); ++cteIndex) {
                const std::string normalizedCteTableKey = normalizeSqlIdentifier(ctes[cteIndex].cteName);
                const std::string& monVar = this->context.withCteCpp20MonikerVarByCteKey[normalizedCteTableKey];
                prelude += "constexpr orm_cte_moniker auto " + monVar + " = " +
                           identifierToCppStringLiteral(stripIdentifierQuotes(ctes[cteIndex].cteName)) +
                           "_cte;\n";
            }
            for(size_t cteIndex = 0; cteIndex < ctes.size(); ++cteIndex) {
                const std::string normalizedCteTableKey = normalizeSqlIdentifier(ctes[cteIndex].cteName);
                for(size_t columnNameIndex = 0; columnNameIndex < ctes[cteIndex].columnNames.size();
                    ++columnNameIndex) {
                    const std::string colK = normalizeSqlIdentifier(ctes[cteIndex].columnNames[columnNameIndex]);
                    const std::string pipe = normalizedCteTableKey + "|" + colK;
                    const std::string& cvar = this->context.withCteCpp20ColVarByPipeKey[pipe];
                    const std::string columnSqlName =
                        stripIdentifierQuotes(ctes[cteIndex].columnNames[columnNameIndex]);
                    prelude += "constexpr orm_column_alias auto " + cvar + " = " +
                               identifierToCppStringLiteral(columnSqlName) + "_col;\n";
                }
            }
            for(const auto& tad : this->context.cpp20TableAliasDeclarations) {
                prelude += "constexpr orm_table_alias auto " + tad.variableName + " = " +
                           identifierToCppStringLiteral(tad.sqlAlias) + "_alias.for_<" + tad.baseStructName +
                           ">();\n";
            }
            this->context.cpp20TableAliasDeclarations.clear();
            warnings.push_back(
                "WITH: cpp20_monikers requires C++20, SQLITE_ORM_WITH_CPP20_ALIASES, and matching sqlite_orm");
        } else if(withStyle == "legacy_colalias") {
            prelude = "using namespace sqlite_orm::literals;\n";
            for(size_t cteIndex = 0; cteIndex < ctes.size(); ++cteIndex) {
                prelude += "using " + cteTypedefIds[cteIndex] + " = decltype(" + std::to_string(cteIndex + 1) +
                           "_ctealias);\n";
            }
            for(size_t cteIndex = 0; cteIndex < ctes.size(); ++cteIndex) {
                const std::string normalizedCteTableKey = normalizeSqlIdentifier(ctes[cteIndex].cteName);
                for(size_t columnNameIndex = 0; columnNameIndex < ctes[cteIndex].columnNames.size();
                    ++columnNameIndex) {
                    const std::string colK = normalizeSqlIdentifier(ctes[cteIndex].columnNames[columnNameIndex]);
                    const std::string var =
                        this->context.withCteLegacyColVarByPipeKey[normalizedCteTableKey + "|" + colK];
                    prelude += "constexpr auto " + var + " = " + colaliasBuiltinSlot(columnNameIndex) + ";\n";
                }
            }
        } else {
            prelude = "using namespace sqlite_orm::literals;\n";
            for(size_t cteIndex = 0; cteIndex < ctes.size(); ++cteIndex) {
                prelude += "using " + cteTypedefIds[cteIndex] + " = decltype(" + std::to_string(cteIndex + 1) +
                           "_ctealias);\n";
            }
        }

        auto buildCteExpression = [&](size_t cteIndex) -> std::string {
            std::string built;
            if(withStyle == "cpp20_monikers") {
                const std::string normalizedCteTableKey = normalizeSqlIdentifier(ctes[cteIndex].cteName);
                built = this->context.withCteCpp20MonikerVarByCteKey[normalizedCteTableKey] + "()";
            } else if(withStyle == "legacy_colalias") {
                built = "cte<" + cteTypedefIds[cteIndex] + ">()";
            } else {
                built = "cte<" + cteTypedefIds[cteIndex] + ">";
                if(!ctes[cteIndex].columnNames.empty()) {
                    built += "(";
                    for(size_t cn = 0; cn < ctes[cteIndex].columnNames.size(); ++cn) {
                        if(cn > 0) {
                            built += ", ";
                        }
                        built += identifierToCppStringLiteral(ctes[cteIndex].columnNames[cn]);
                    }
                    built += ")";
                } else {
                    built += "()";
                }
            }
            std::string asMethod = ".as(";
            if(ctes[cteIndex].materialization == CteMaterialization::materialized) {
                asMethod = ".as<sqlite_orm::materialized()>(";
                warnings.push_back(
                    "WITH: AS MATERIALIZED uses sqlite_orm::materialized() — requires C++20 and "
                    "SQLITE_ORM_WITH_CPP20_ALIASES in the consuming project");
            } else if(ctes[cteIndex].materialization == CteMaterialization::notMaterialized) {
                asMethod = ".as<sqlite_orm::not_materialized()>(";
                warnings.push_back(
                    "WITH: AS NOT MATERIALIZED uses sqlite_orm::not_materialized() — requires C++20 and "
                    "SQLITE_ORM_WITH_CPP20_ALIASES in the consuming project");
            }
            built += asMethod + innerCodes[cteIndex] + ")";
            return built;
        };

        std::string cteArgument;
        if(ctes.size() == 1) {
            cteArgument = buildCteExpression(0);
        } else {
            cteArgument = "std::make_tuple(";
            for(size_t cteIndex = 0; cteIndex < ctes.size(); ++cteIndex) {
                if(cteIndex > 0) {
                    cteArgument += ", ";
                }
                cteArgument += buildCteExpression(cteIndex);
            }
            cteArgument += ")";
        }

        const char* withApi = withQueryNode.clause.recursive ? "with_recursive" : "with";

        const auto* outerSelect = dynamic_cast<const SelectNode*>(withQueryNode.statement.get());
        const auto* outerCompound = dynamic_cast<const CompoundSelectNode*>(withQueryNode.statement.get());
        const auto* outerInsert = dynamic_cast<const InsertNode*>(withQueryNode.statement.get());
        const auto* outerUpdate = dynamic_cast<const UpdateNode*>(withQueryNode.statement.get());
        const auto* outerDelete = dynamic_cast<const DeleteNode*>(withQueryNode.statement.get());

        if(outerSelect || outerCompound) {
            auto outerResult = this->coordinator.generateNode(*withQueryNode.statement);
            warnings.insert(warnings.end(), std::make_move_iterator(outerResult.warnings.begin()),
                             std::make_move_iterator(outerResult.warnings.end()));
            allDecisionPoints.insert(allDecisionPoints.end(),
                std::make_move_iterator(outerResult.decisionPoints.begin()),
                std::make_move_iterator(outerResult.decisionPoints.end()));

            auto outerArgOpt = extractStorageSelectArgument(outerResult.code);
            if(!outerArgOpt) {
                warnings.push_back(
                    "WITH: outer SELECT is not in the expected `auto rows = storage.select(...);` form; emitted "
                    "as plain outer codegen");
                this->context.activeCteTypedefByTableKey.clear();
                return CodeGenResult{outerResult.code, std::move(allDecisionPoints), std::move(warnings)};
            }

            std::string code = prelude + "auto rows = storage." + std::string(withApi) + "(" + cteArgument +
                               ", " + *outerArgOpt + ");";

            if(!this->context.suppressWithCteStyleDecisionPoint && ctes.size() == 1u) {
                const bool hasColumnList = !ctes[0].columnNames.empty();
                const int dpId = this->context.nextDecisionPointId++;
                auto altCode = [this, &withQueryNode](const char* styleValue) {
                    CodeGenPolicy pol =
                        policyWithOverride(this->context.codeGenPolicy, "with_cte_style", styleValue);
                    CodeGenerator gen;
                    gen.codeGenPolicy = &pol;
                    gen.context().suppressWithCteStyleDecisionPoint = true;
                    return gen.generate(static_cast<const AstNode&>(withQueryNode)).code;
                };
                std::vector<Alternative> alts;
                alts.push_back(Alternative{"indexed_typedef", altCode("indexed_typedef"),
                                           "using cte_N + column<cte_N>(\"col\") (default sqlite2orm style)"});
                if(hasColumnList) {
                    alts.push_back(
                        Alternative{"legacy_colalias", altCode("legacy_colalias"),
                                    "using typedef from SQL CTE name + colalias_i… + column<T>(var)"});
                }
                alts.push_back(Alternative{
                    "cpp20_monikers", altCode("cpp20_monikers"),
                    "constexpr orm_cte_moniker / orm_table_alias + operator->* (C++20 sqlite_orm)"});
                allDecisionPoints.push_back(
                    DecisionPoint{dpId, "with_cte_style", withStyle, code, std::move(alts)});
            }

            warnings.push_back(
                "WITH: requires SQLite ≥ 3.8.3, sqlite_orm built with SQLITE_ORM_WITH_CTE, and `using namespace "
                "sqlite_orm::literals` scope for `_ctealias`");

            return CodeGenResult{std::move(code), std::move(allDecisionPoints), std::move(warnings)};
        }

        if(outerInsert || outerUpdate || outerDelete) {
            auto outerResult = this->coordinator.generateNode(*withQueryNode.statement);
            warnings.insert(warnings.end(), std::make_move_iterator(outerResult.warnings.begin()),
                             std::make_move_iterator(outerResult.warnings.end()));
            allDecisionPoints.insert(allDecisionPoints.end(),
                std::make_move_iterator(outerResult.decisionPoints.begin()),
                std::make_move_iterator(outerResult.decisionPoints.end()));
            std::string stripped = stripStoragePrefixAndTrailingSemicolon(outerResult.code);
            if(stripped.empty()) {
                warnings.push_back(
                    "WITH … DML: outer statement codegen could not be wrapped in storage.with(); emitted plain "
                    "DML");
                this->context.activeCteTypedefByTableKey.clear();
                return CodeGenResult{outerResult.code, std::move(allDecisionPoints), std::move(warnings)};
            }
            std::string code =
                prelude + "storage." + std::string(withApi) + "(" + cteArgument + ", " + stripped + ");";
            warnings.push_back(
                "WITH … DML: second argument omits the `storage.` prefix (sqlite_orm::with / with_recursive)");
            warnings.push_back(
                "WITH: requires SQLite ≥ 3.8.3, sqlite_orm built with SQLITE_ORM_WITH_CTE, and `using namespace "
                "sqlite_orm::literals` scope for `_ctealias`");
            return CodeGenResult{std::move(code), std::move(allDecisionPoints), std::move(warnings)};
        }

        warnings.push_back(
            "WITH …: outer statement kind is not wrapped with storage.with() in sqlite2orm codegen; emitted as "
            "plain statement");
        this->context.activeCteTypedefByTableKey.clear();
        auto inner = this->coordinator.generateNode(*withQueryNode.statement);
        warnings.insert(warnings.end(), std::make_move_iterator(inner.warnings.begin()),
                         std::make_move_iterator(inner.warnings.end()));
        allDecisionPoints.insert(allDecisionPoints.end(),
            std::make_move_iterator(inner.decisionPoints.begin()),
            std::make_move_iterator(inner.decisionPoints.end()));
        return CodeGenResult{inner.code, std::move(allDecisionPoints), std::move(warnings)};
    }

}  // namespace sqlite2orm
