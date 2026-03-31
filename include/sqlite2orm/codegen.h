#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_policy.h>

#include <map>
#include <string>
#include <vector>
#include <optional>

namespace sqlite2orm {

    struct Alternative {
        std::string value;
        std::string code;
        std::string description;
        bool hidden = false;
        /** Optional notes when this alternative is shown or chosen (e.g. build requirements); any consumer may show them. */
        std::vector<std::string> comments;

        bool operator==(const Alternative&) const = default;
    };

    struct DecisionPoint {
        int id = 0;
        std::string category;
        std::string chosenValue;
        std::string chosenCode;
        std::vector<Alternative> alternatives;

        bool operator==(const DecisionPoint&) const = default;
    };

    struct CodeGenResult {
        std::string code;
        std::vector<DecisionPoint> decisionPoints;
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
        /** Optional hints for the generated snippet (deduplicated when merging fragments). */
        std::vector<std::string> comments;

        bool operator==(const CodeGenResult&) const = default;
    };

    struct CreateTableParts {
        std::string structDeclaration;
        std::string makeTableExpression;
        std::vector<std::string> warnings;
    };

    class CodeGenerator {
      public:
        std::string structName = "User";

        /** When non-null, steers decision points (expr_style, column_ref_style, api_level, …). */
        const CodeGenPolicy* codeGenPolicy = nullptr;

        CodeGenResult generate(const AstNode& astNode);
        std::string generatePrefix() const;

        /** Struct body + `make_table(...)[.without_rowid()]` for schema header merge (no make_storage). */
        CreateTableParts createTableParts(const CreateTableNode& createTable);

      private:
        int nextDecisionPointId = 1;
        int nextBindParamIndex = 0;
        std::vector<std::string> accumulatedErrors;
        std::map<std::string, std::string> columnTypes;
        std::map<std::string, std::string> fromTableAliasToStructName;
        /** During WITH codegen: SQL table/alias (normalized) → C++ typedef name (e.g. `cte_0`). */
        std::map<std::string, std::string> activeCteTypedefByTableKey;
        /**
         * When the current SELECT has exactly one FROM source that resolves to a CTE, bare `col` references
         * emit `column<cte_N>("col")`.
         */
        std::optional<std::string> implicitSingleSourceCteTypedef;
        /** During SELECT codegen: SQL column alias name → C++ alias type name (e.g. `colalias_i`, `GradeAlias`). */
        std::map<std::string, std::string> activeSelectColumnAliases;
        /** Normalized SQL alias → C++ variable name for `constexpr orm_column_alias auto … = "…"_col`. */
        std::map<std::string, std::string> activeSelectColumnAliasCpp20Vars;
        /**
         * When set, forces column alias codegen style for this generator (used to build decision-point alternate).
         * Otherwise `CodeGenPolicy::column_alias_style` applies (`alias_tag` default, `cpp20_literal` for C++20 `_col`).
         */
        std::optional<std::string> columnAliasStyleOverride;

        bool useCpp20ColumnAliasStyle() const;
        bool columnRefIsSelectAliasNoWrap(const ColumnRefNode& ref) const;

        CodeGenResult generateNode(const AstNode& astNode);

        void registerColumn(const std::string& cppName, const std::string& cppType);
        std::string inferTypeFromNode(const AstNode& node) const;

        /** For exists(select(...)) and scalar (SELECT ...); restores alias map after. */
        CodeGenResult tryCodegenSqliteSelectSubexpression(const SelectNode& selectNode);

        /** SELECT or UNION/INTERSECT/EXCEPT chain for subquery / EXISTS / IN. */
        CodeGenResult tryCodegenSelectLikeSubquery(const AstNode& node);

        CodeGenResult tryCodegenCompoundSelectSubexpression(const CompoundSelectNode& compoundNode);

        /** Trigger body step: SELECT as select(...), DML without storage. prefix / trailing ';'. */
        CodeGenResult generateTriggerStep(const AstNode& statement, const std::string& subjectTableStruct);

        std::string codegenOverClause(const OverClause& overClause, std::vector<DecisionPoint>& decisionPoints,
                                      std::vector<std::string>& warnings);
        std::string codegenWindowFrameBound(const WindowFrameBound& bound, std::vector<DecisionPoint>& decisionPoints,
                                            std::vector<std::string>& warnings);
        std::string codegenWindowFrameSpec(const WindowFrameSpec& frame, std::vector<DecisionPoint>& decisionPoints,
                                           std::vector<std::string>& warnings);

        CodeGenResult generateWithQuery(const WithQueryNode& withQueryNode);

        CodeGenResult codegenPragmaStatement(const PragmaNode& pragmaNode);
    };

}  // namespace sqlite2orm
