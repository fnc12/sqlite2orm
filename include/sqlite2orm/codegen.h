#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_policy.h>

#include <map>
#include <string>
#include <vector>
#include <optional>

namespace sqlite2orm {
    class CodeGenerator;
}

namespace codegen_test_helpers {
    void setSuppressWithCteStyleDecisionPointForTests(sqlite2orm::CodeGenerator& generator, bool suppress);
}

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
        friend void codegen_test_helpers::setSuppressWithCteStyleDecisionPointForTests(CodeGenerator&, bool);

        struct TableAliasInfo {
            std::string ormAliasType;    // e.g. "alias_a<Org>"
            std::string baseStructName;  // e.g. "Org"
        };

        int nextDecisionPointId = 1;
        int nextBindParamIndex = 0;
        std::vector<std::string> accumulatedErrors;
        std::map<std::string, std::string> columnTypes;
        std::map<std::string, std::string> fromTableAliasToStructName;
        std::map<std::string, TableAliasInfo> activeTableAliases;
        int nextAliasLetter = 0;
        /** During WITH codegen: SQL table/alias (normalized) → C++ typedef name (e.g. `cte_0`). */
        std::map<std::string, std::string> activeCteTypedefByTableKey;
        /** During WITH codegen: CTE key (normalized) → base struct name (e.g. `Org`) for member-pointer column refs. */
        std::map<std::string, std::string> cteBaseStructByKey;
        /**
         * When the current SELECT has exactly one FROM source that resolves to a CTE, bare `col` references
         * emit `column<cte_N>("col")`.
         */
        std::optional<std::string> implicitSingleSourceCteTypedef;
        /** Normalized SQL name of that single CTE `FROM` source (for `with_cte_style` lookups). */
        std::optional<std::string> implicitCteFromTableKeyNorm;
        /** During SELECT codegen: SQL column alias name → C++ alias type name (e.g. `colalias_i`, `GradeAlias`). */
        std::map<std::string, std::string> activeSelectColumnAliases;
        /** Normalized SQL alias → C++ variable name for `constexpr orm_column_alias auto … = "…"_col`. */
        std::map<std::string, std::string> activeSelectColumnAliasCpp20Vars;
        /**
         * When set, forces column alias codegen style for this generator (used to build decision-point alternate).
         * Otherwise `CodeGenPolicy::column_alias_style` applies (`alias_tag` default, `cpp20_literal` for C++20 `_col`).
         */
        std::optional<std::string> columnAliasStyleOverride;

        /** During `generateWithQuery`: `indexed_typedef` | `legacy_colalias` | `cpp20_monikers`. */
        std::optional<std::string> activeWithCteStyle;
        std::map<std::string, std::string> withCteLegacyColVarByPipeKey;
        std::map<std::string, std::string> withCteCpp20MonikerVarByCteKey;
        std::map<std::string, std::string> withCteCpp20ColVarByPipeKey;
        /** When true, `with_cte_style` DecisionPoint is not emitted (used when regenerating for alternatives). */
        bool suppressWithCteStyleDecisionPoint = false;

        bool useCpp20ColumnAliasStyle() const;
        bool withCteLegacyColalias() const;
        bool withCteCpp20Monikers() const;
        bool columnRefIsSelectAliasNoWrap(const ColumnRefNode& ref) const;

        CodeGenResult generateNode(const AstNode& astNode);

        void registerColumn(const std::string& cppName, const std::string& cppType);
        /** Registers a column for `generatePrefix()`; skipped for single-source CTE rows (type is `cte_N`). */
        void registerPrefixColumn(const std::string& cppName, const std::string& cppType);
        std::string syntheticColumnCppType(std::string_view cppIdentifier) const;
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
