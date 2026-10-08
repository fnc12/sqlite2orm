#include "codegen_expression.h"
#include "codegen_context.h"
#include "codegen_utils.h"
#include "correlated_column_scope.h"
#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    CodeGenResult ExpressionCodeGenerator::generateColumnRef(const ColumnRefNode* columnRef, const AstNode& astNode) {
        {
            std::string normalized = toLowerAscii(stripIdentifierQuotes(columnRef->columnName));
            auto aliasIt = this->context.activeSelectColumnAliases.find(normalized);
            if (aliasIt != this->context.activeSelectColumnAliases.end()) {
                if (this->context.useCpp20ColumnAliasStyle()) {
                    auto varIt = this->context.activeSelectColumnAliasCpp20Vars.find(normalized);
                    if (varIt != this->context.activeSelectColumnAliasCpp20Vars.end()) {
                        return CodeGenResult{varIt->second, {}};
                    }
                }
                return CodeGenResult{"get<" + aliasIt->second + ">()", {}};
            }
        }
        // A double-quoted name a CHECK or a generated column of the table being generated
        // writes, and that table declares no column of, is no column reference at all: SQLite
        // reads it as a string literal and takes the statement. A qualified one it does not
        // ("no such column: t.zz"), which is why this stands under the bare form alone.
        if (const auto text = this->context.clauseColumnAsStringLiteral(columnRef->columnName)) {
            return CodeGenResult{cppStringLiteral(*text), {}};
        }
        // A name the subquery's own FROM declares no column of and one of its result columns
        // is aliased as stands for that column's expression, which SQLite reads in its place
        // — `(SELECT uid AS a FROM u WHERE a > 1)` compares `uid`. The expression is resolved
        // where the result list is, which sees no alias.
        if (const AstNode* aliased = this->context.resultColumnAliasExpression(columnRef->columnName)) {
            struct ResultListScope {
                CodeGeneratorContext& ctx;
                std::map<std::string, const AstNode*> saved;
                explicit ResultListScope(CodeGeneratorContext& context) :
                    ctx(context), saved(std::exchange(context.selectResultColumnAliases, {})) {}
                ~ResultListScope() {
                    ctx.selectResultColumnAliases = std::move(saved);
                }
            } resultList{this->context};
            return this->generateExpression(*aliased);
        }
        // A name the subquery's own FROM declares no column of is a correlated reference to the
        // enclosing query SQLite resolves it in, and it is written the way it would be there.
        if (const ColumnNameScope* correlated = this->context.correlatedColumnNameScope(columnRef->columnName)) {
            const CorrelatedColumnScope enclosingQuery{this->context, *correlated};
            return this->generateExpression(astNode);
        }
        // Inside a CREATE TABLE — a CHECK, a generated column or a DEFAULT — the name is resolved
        // against the columns that table declares, so a spelling SQLite reads as the same column is
        // written as the member the declaration produced. Anywhere else it is resolved against the
        // table behind the struct the form below names the column a member of.
        auto cppName = this->context.clauseColumnMember(columnRef->columnName);
        auto memberOf = [&](std::string_view structForReference) {
            if (this->context.constraintColumnsTableIsBeingGenerated()) {
                return cppName;
            }
            return this->context.structColumnMember(structForReference, columnRef->columnName);
        };
        this->context.registerPrefixColumn(cppName, this->context.syntheticColumnCppType(cppName));
        if (this->context.implicitSingleSourceCteTypedef) {
            // Every form below this point names that CTE, whichever of them the column takes.
            this->context.recordEmittedTableType(*this->context.implicitSingleSourceCteTypedef);
        }
        if (this->context.implicitSingleSourceCteTypedef && this->context.implicitCteFromTableKeyNorm) {
            const std::string colKey = normalizeSqlIdentifier(columnRef->columnName);
            const std::string pipe = *this->context.implicitCteFromTableKeyNorm + "|" + colKey;
            if (this->context.withCteCpp20Monikers()) {
                auto monIt =
                    this->context.withCteCpp20MonikerVarByCteKey.find(*this->context.implicitCteFromTableKeyNorm);
                auto colIt = this->context.withCteCpp20ColVarByPipeKey.find(pipe);
                if (monIt != this->context.withCteCpp20MonikerVarByCteKey.end() &&
                    colIt != this->context.withCteCpp20ColVarByPipeKey.end()) {
                    return CodeGenResult{monIt->second + "->*" + colIt->second, {}, {}, {}};
                }
                if (monIt != this->context.withCteCpp20MonikerVarByCteKey.end()) {
                    if (!this->context.isExplicitCteColumn(*this->context.implicitCteFromTableKeyNorm,
                                                           columnRef->columnName)) {
                        auto baseIt = this->context.cteBaseStructByKey.find(*this->context.implicitCteFromTableKeyNorm);
                        if (baseIt != this->context.cteBaseStructByKey.end()) {
                            return CodeGenResult{monIt->second + "->*&" + baseIt->second +
                                                     "::" + memberOf(baseIt->second),
                                                 {},
                                                 {},
                                                 {}};
                        }
                    }
                }
            }
            if (this->context.withCteLegacyColalias()) {
                auto colIt = this->context.withCteLegacyColVarByPipeKey.find(pipe);
                if (colIt != this->context.withCteLegacyColVarByPipeKey.end()) {
                    std::string cteCol =
                        "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" + colIt->second + ")";
                    return CodeGenResult{std::move(cteCol), {}, {}, {}};
                }
            }
            {
                auto indexedIt = this->context.withCteIndexedColVarByPipeKey.find(pipe);
                if (indexedIt != this->context.withCteIndexedColVarByPipeKey.end()) {
                    std::string cteCol =
                        "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" + indexedIt->second + ")";
                    return CodeGenResult{std::move(cteCol), {}, {}, {}};
                }
            }
        }
        if (this->context.implicitSingleSourceCteTypedef) {
            if (this->context.implicitCteFromTableKeyNorm) {
                const std::string fallbackPipe =
                    *this->context.implicitCteFromTableKeyNorm + "|" + normalizeSqlIdentifier(columnRef->columnName);
                auto indexedIt = this->context.withCteIndexedColVarByPipeKey.find(fallbackPipe);
                if (indexedIt != this->context.withCteIndexedColVarByPipeKey.end()) {
                    std::string cteCol =
                        "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" + indexedIt->second + ")";
                    return CodeGenResult{std::move(cteCol), {}, {}, {}};
                }
                if (this->context.isExplicitCteColumn(*this->context.implicitCteFromTableKeyNorm,
                                                      columnRef->columnName)) {
                    std::string colLit = identifierToCppStringLiteral(stripIdentifierQuotes(columnRef->columnName));
                    std::string cteCol =
                        "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" + colLit + ")";
                    return CodeGenResult{std::move(cteCol), {}, {}, {}};
                }
                auto baseIt = this->context.cteBaseStructByKey.find(*this->context.implicitCteFromTableKeyNorm);
                if (baseIt != this->context.cteBaseStructByKey.end()) {
                    std::string cteCol = "column<" + *this->context.implicitSingleSourceCteTypedef + ">(&" +
                                         baseIt->second + "::" + memberOf(baseIt->second) + ")";
                    return CodeGenResult{std::move(cteCol), {}, {}, {}};
                }
            }
            std::string colLit = identifierToCppStringLiteral(stripIdentifierQuotes(columnRef->columnName));
            std::string cteCol = "column<" + *this->context.implicitSingleSourceCteTypedef + ">(" + colLit + ")";
            return CodeGenResult{std::move(cteCol), {}, {}, {}};
        }
        if (this->context.implicitSourceAlias) {
            // The source this column belongs to is aliased in the FROM, so the column is one
            // of the alias, not of the plain table — the same form a column the SQL qualified
            // with that alias takes, and for the same reason: sqlite_orm reads `&T::x` as a
            // second, unaliased source and infers a FROM naming both.
            const auto& info = *this->context.implicitSourceAlias;
            this->context.recordEmittedTableType(info.ormAliasType);
            const std::string aliasedMember = memberOf(info.baseStructName);
            std::string aliasedCode =
                this->context.useCpp20TableAliasStyle()
                    ? info.ormAliasType + "->*&" + info.baseStructName + "::" + aliasedMember
                    : "alias_column<" + info.ormAliasType + ">(&" + info.baseStructName + "::" + aliasedMember + ")";
            return CodeGenResult{std::move(aliasedCode), {}, {}, {}, {}};
        }
        std::string memberPointer = "&" + this->context.structName + "::" + memberOf(this->context.structName);
        std::string columnPointer = "column<" + this->context.structName + ">(" + memberPointer + ")";
        this->context.emittedTableTypedColumnRef = true;
        this->context.recordEmittedTableType(this->context.structName);
        if (this->context.columnRefUnderLogicalNot) {
            // Only the column-pointer form survives under a NOT, so there is no style left to
            // decide between and no decision point to offer.
            this->context.emittedColumnPointerUnderLogicalNot = true;
            return CodeGenResult{std::move(columnPointer), {}, {}, {}, {}};
        }
        const bool useColumnPtr = policyEquals(this->context.codeGenPolicy, "column_ref_style", "column_pointer");
        const std::string& emittedCol = useColumnPtr ? columnPointer : memberPointer;
        const std::string chosenCol = useColumnPtr ? "column_pointer" : "member_pointer";
        // options lists every variant (the chosen one included); the consumer decides how to
        // present the selection.
        return CodeGenResult{
            emittedCol,
            {DecisionPoint{
                this->context.nextDecisionPointId++,
                "column_ref_style",
                chosenCol,
                emittedCol,
                {Option{"member_pointer", memberPointer, "direct member pointer"},
                 Option{"column_pointer", columnPointer, "explicit mapped type (inheritance / ambiguity)"}}}},
            {},
            {},
            {}};
    }

    CodeGenResult ExpressionCodeGenerator::generateQualifiedColumnRef(const QualifiedColumnRefNode* qualifiedRef) {
        std::vector<CodegenWarning> qualWarnings;
        if (qualifiedRef->schemaName) {
            qualWarnings.push_back(
                "schema-qualified column " + std::string(*qualifiedRef->schemaName) + "." +
                std::string(qualifiedRef->tableName) + "." + std::string(qualifiedRef->columnName) +
                " is not represented in sqlite_orm mapping; generated column reference uses table `" +
                std::string(qualifiedRef->tableName) + "` only");
        }
        std::string tableKeyNorm = normalizeSqlIdentifier(qualifiedRef->tableName);
        auto cteIt = this->context.activeCteTypedefByTableKey.find(tableKeyNorm);
        if (cteIt != this->context.activeCteTypedefByTableKey.end()) {
            // Every form below this point names that CTE, whichever of them the column takes.
            this->context.recordEmittedTableType(cteIt->second);
            const std::string colKey = normalizeSqlIdentifier(qualifiedRef->columnName);
            const std::string pipe = tableKeyNorm + "|" + colKey;
            if (this->context.withCteCpp20Monikers()) {
                auto monIt = this->context.withCteCpp20MonikerVarByCteKey.find(tableKeyNorm);
                auto colIt = this->context.withCteCpp20ColVarByPipeKey.find(pipe);
                if (monIt != this->context.withCteCpp20MonikerVarByCteKey.end() &&
                    colIt != this->context.withCteCpp20ColVarByPipeKey.end()) {
                    return CodeGenResult{monIt->second + "->*" + colIt->second, {}, std::move(qualWarnings), {}, {}};
                }
                if (monIt != this->context.withCteCpp20MonikerVarByCteKey.end()) {
                    if (!this->context.isExplicitCteColumn(tableKeyNorm, qualifiedRef->columnName)) {
                        auto baseIt = this->context.cteBaseStructByKey.find(tableKeyNorm);
                        if (baseIt != this->context.cteBaseStructByKey.end()) {
                            std::string colCpp =
                                this->context.structColumnMember(baseIt->second, qualifiedRef->columnName);
                            return CodeGenResult{monIt->second + "->*&" + baseIt->second + "::" + colCpp,
                                                 {},
                                                 std::move(qualWarnings),
                                                 {},
                                                 {}};
                        }
                    }
                }
            }
            if (this->context.withCteLegacyColalias()) {
                auto colIt = this->context.withCteLegacyColVarByPipeKey.find(pipe);
                if (colIt != this->context.withCteLegacyColVarByPipeKey.end()) {
                    return CodeGenResult{"column<" + cteIt->second + ">(" + colIt->second + ")",
                                         {},
                                         std::move(qualWarnings),
                                         {},
                                         {}};
                }
            }
            {
                auto indexedIt = this->context.withCteIndexedColVarByPipeKey.find(pipe);
                if (indexedIt != this->context.withCteIndexedColVarByPipeKey.end()) {
                    return CodeGenResult{"column<" + cteIt->second + ">(" + indexedIt->second + ")",
                                         {},
                                         std::move(qualWarnings),
                                         {},
                                         {}};
                }
            }
            if (this->context.isExplicitCteColumn(tableKeyNorm, qualifiedRef->columnName)) {
                std::string colLit = identifierToCppStringLiteral(stripIdentifierQuotes(qualifiedRef->columnName));
                return CodeGenResult{"column<" + cteIt->second + ">(" + colLit + ")",
                                     {},
                                     std::move(qualWarnings),
                                     {},
                                     {}};
            }
            auto baseIt = this->context.cteBaseStructByKey.find(tableKeyNorm);
            if (baseIt != this->context.cteBaseStructByKey.end()) {
                std::string colCpp = this->context.structColumnMember(baseIt->second, qualifiedRef->columnName);
                return CodeGenResult{"column<" + cteIt->second + ">(&" + baseIt->second + "::" + colCpp + ")",
                                     {},
                                     std::move(qualWarnings),
                                     {},
                                     {}};
            }
            std::string colLit = identifierToCppStringLiteral(stripIdentifierQuotes(qualifiedRef->columnName));
            return CodeGenResult{"column<" + cteIt->second + ">(" + colLit + ")", {}, std::move(qualWarnings), {}, {}};
        }
        std::string tableKey = std::string(qualifiedRef->tableName);
        auto tableAliasIt = this->context.activeTableAliases.find(tableKey);
        if (tableAliasIt != this->context.activeTableAliases.end()) {
            const auto& info = tableAliasIt->second;
            const std::string colCpp = this->context.structColumnMember(info.baseStructName, qualifiedRef->columnName);
            this->context.registerPrefixColumn(colCpp, this->context.syntheticColumnCppType(colCpp));
            this->context.recordEmittedTableType(info.ormAliasType);
            std::string code;
            if (this->context.useCpp20TableAliasStyle()) {
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
        // A CHECK may qualify the column with the table it is declared on, and that is the one
        // qualifier the table being generated answers for. A qualifier naming another table is
        // that table's column — SQLite refuses such a CREATE TABLE ("no such column: o.k") — so
        // it is written as that table declares it. Outside a CREATE TABLE the name is resolved
        // against the table behind the struct the qualifier maps to, alias or not.
        std::string colCpp;
        if (this->context.constraintColumnsAreOfTable(qualifiedRef->tableName)) {
            colCpp = this->context.clauseColumnMember(qualifiedRef->columnName);
        } else if (this->context.constraintColumnsTableIsBeingGenerated()) {
            colCpp = this->context.sourceColumnMember(qualifiedRef->tableName, qualifiedRef->columnName);
        } else {
            colCpp = this->context.structColumnMember(structForColumn, qualifiedRef->columnName);
        }
        this->context.registerPrefixColumn(colCpp, this->context.syntheticColumnCppType(colCpp));
        this->context.recordEmittedTableType(structForColumn);
        std::string memberPointer = "&" + structForColumn + "::" + colCpp;
        std::string columnPointer = "column<" + structForColumn + ">(" + memberPointer + ")";
        if (this->context.columnRefUnderLogicalNot) {
            // Only the column-pointer form survives under a NOT, so there is no style left to
            // decide between and no decision point to offer.
            this->context.emittedColumnPointerUnderLogicalNot = true;
            return CodeGenResult{std::move(columnPointer), {}, std::move(qualWarnings), {}, {}};
        }
        const bool useQColPtr = policyEquals(this->context.codeGenPolicy, "column_ref_style", "column_pointer");
        const std::string& emittedQ = useQColPtr ? columnPointer : memberPointer;
        const std::string chosenQ = useQColPtr ? "column_pointer" : "member_pointer";
        // options lists every variant (the chosen one included).
        return CodeGenResult{
            emittedQ,
            {DecisionPoint{
                this->context.nextDecisionPointId++,
                "column_ref_style",
                chosenQ,
                emittedQ,
                {Option{"member_pointer", memberPointer, "direct member pointer"},
                 Option{"column_pointer", columnPointer, "explicit mapped type (inheritance / ambiguity)"}}}},
            std::move(qualWarnings),
            {},
            {}};
    }

    CodeGenResult ExpressionCodeGenerator::generateQualifiedAsterisk(const QualifiedAsteriskNode* qualifiedAsterisk) {
        std::vector<CodegenWarning> qualifiedAsteriskWarnings;
        if (qualifiedAsterisk->schemaName) {
            qualifiedAsteriskWarnings.push_back("schema-qualified SELECT result column " +
                                                *qualifiedAsterisk->schemaName + "." + qualifiedAsterisk->tableName +
                                                ".* is not represented in sqlite_orm; generated code uses asterisk<" +
                                                toStructName(qualifiedAsterisk->tableName) + ">() (table type only)");
        }
        auto tableAliasIt = this->context.activeTableAliases.find(qualifiedAsterisk->tableName);
        if (tableAliasIt != this->context.activeTableAliases.end()) {
            this->context.recordEmittedTableType(tableAliasIt->second.ormAliasType);
            return CodeGenResult{"asterisk<" + tableAliasIt->second.ormAliasType + ">()",
                                 {},
                                 std::move(qualifiedAsteriskWarnings)};
        }
        std::string asteriskStruct = this->context.structNameForTable(qualifiedAsterisk->tableName);
        this->context.recordEmittedTableType(asteriskStruct);
        return CodeGenResult{"asterisk<" + asteriskStruct + ">()", {}, std::move(qualifiedAsteriskWarnings)};
    }

}  // namespace sqlite2orm
