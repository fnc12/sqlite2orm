#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_policy.h>
#include <sqlite2orm/codegen_result.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sqlite2orm {

    struct TableAliasInfo {
        std::string ormAliasType;
        std::string baseStructName;
    };

    /** A column of a known CREATE TABLE (or view), used to infer view struct field types. */
    struct SourceTableColumn {
        std::string sqlName;
        std::string cppType;
        bool nullable = false;
    };

    struct Cpp20TableAliasDeclaration {
        std::string variableName;
        std::string baseStructName;
        std::string sqlAlias;
    };

    /** A user-defined / extension function referenced in a statement, turned into a func<>() call. */
    struct CustomFunctionUse {
        std::string sqlName;                 // original SQL name, e.g. "morton_encode"
        std::string structName;              // C++ struct name, e.g. "MortonEncode"
        std::vector<std::string> argTypes;   // best-effort C++ type per argument
        std::vector<std::string> argNames;   // parameter names (column name when known, else argN)
        std::string returnType = "int";      // best-effort result type

        bool operator==(const CustomFunctionUse&) const = default;
    };

    class CodeGeneratorContext {
      public:
        std::string structName = "User";
        const CodeGenPolicy* codeGenPolicy = nullptr;

        int nextDecisionPointId = 1;
        int nextBindParamIndex = 0;
        std::vector<std::string> accumulatedErrors;
        /**
         *  Set while generating an expression the statement only stores the text of: a column
         *  DEFAULT, a CHECK, a view or a trigger body. SQLite raises `hex literal too big` in
         *  `codeInteger()`, when it compiles an expression, so such a statement is accepted with
         *  a hex literal no compiled expression may hold — and that C++ cannot spell either.
         */
        bool storedExpression = false;
        /**
         *  The hex literals past the int64 range met since the last `generateStoredExpression()`,
         *  as SQLite names them. The generator that owns the clause leaves it out of the generated
         *  code with a warning instead of failing the whole statement.
         */
        std::vector<std::string> storedHexLiteralsTooBig;
        /**
         *  The tables of the current batch that cannot be mapped at all — a STORED generated
         *  column holding such a hex literal leaves the whole table out, because a column that
         *  lost its `as(...)` would be an ordinary column. sqlite_orm cannot reference a type it
         *  does not map, so a foreign key naming one of these is left out too. Normalized names,
         *  filled by whoever assembles a whole schema.
         */
        std::set<std::string> ungeneratableTables;
        /**
         *  The `ungeneratableTables` entries the statement being generated has turned into a
         *  struct name. Every table reference reaches its struct through `structNameForTable()`,
         *  so however deeply a reference is nested — a subquery in a WHERE, a trigger WHEN clause,
         *  a CTE — it is recorded here, and the caller leaves the whole statement out.
         */
        std::set<std::string> referencedUngeneratableTables;
        std::map<std::string, std::string> columnTypes;
        std::map<std::string, std::string> fromTableAliasToStructName;
        std::map<std::string, TableAliasInfo> activeTableAliases;
        int nextAliasLetter = 0;
        std::map<std::string, std::string> activeCteTypedefByTableKey;
        std::map<std::string, std::string> cteBaseStructByKey;
        /** CTE key (normalized) → explicit SQL column names from the CTE definition. */
        std::map<std::string, std::vector<std::string>> cteColumnNamesByTableKey;
        std::optional<std::string> implicitSingleSourceCteTypedef;
        std::optional<std::string> implicitCteFromTableKeyNorm;
        std::map<std::string, std::string> activeSelectColumnAliases;
        std::map<std::string, std::string> activeSelectColumnAliasCpp20Vars;
        std::optional<std::string> columnAliasStyleOverride;

        /** Normalized table/view name → columns; filled from CREATE TABLE statements seen in this batch. */
        std::map<std::string, std::vector<SourceTableColumn>> sourceTableColumnsByNormalizedName;

        /** Struct of the table a trigger is ON; OLD/NEW refs bind to it, not to the DML target table. */
        std::optional<std::string> triggerSubjectStructName;

        std::optional<std::string> activeWithCteStyle;
        std::map<std::string, std::string> withCteLegacyColVarByPipeKey;
        std::map<std::string, std::string> withCteCpp20MonikerVarByCteKey;
        std::map<std::string, std::string> withCteCpp20ColVarByPipeKey;
        std::map<std::string, std::string> withCteIndexedColVarByPipeKey;
        std::vector<std::string> pendingAnchorCteBindings;
        std::vector<Cpp20TableAliasDeclaration> cpp20TableAliasDeclarations;

        bool suppressWithCteStyleDecisionPoint = false;
        bool suppressTableAliasStyleDecisionPoint = false;
        /**
         *  Set while generating the outer SELECT of a WITH: a bare `SELECT *` must use the
         *  `select(asterisk<T>())` form (which nests into `storage.with(...)`), not `storage.get_all<T>()`
         *  (a storage method that cannot be a `with()` argument). Consumed once by the outer select.
         */
        bool withOuterSelect = false;

        /** User-defined / extension functions used in the current statement (deduplicated by struct name). */
        std::vector<CustomFunctionUse> customFunctions;

        /** Records a custom function use if its struct name is not already present. */
        void registerCustomFunction(CustomFunctionUse use);

        /**
         *  How many statements of the current batch already declared each result variable
         *  (`rows`, `vtab`, …). `processMultiSql` carries it from one statement to the next so
         *  that every statement gets its own name.
         */
        std::map<std::string, int> batchVariableUses;
        /**
         *  Result variable names resolved for the statement being generated. Cached so that the
         *  main code and every regenerated alternative of one statement agree on the name.
         */
        std::map<std::string, std::string> statementVariableNames;

        /** `rows` for the first statement declaring it in the batch, then `rows2`, `rows3`, … */
        std::string statementVariableName(std::string_view baseName);

        bool useCpp20ColumnAliasStyle() const;
        bool useCpp20TableAliasStyle() const;
        bool withCteLegacyColalias() const;
        bool withCteCpp20Monikers() const;
        bool columnRefIsSelectAliasNoWrap(const ColumnRefNode& ref) const;
        bool isExplicitCteColumn(std::string_view cteKeyNorm, std::string_view columnName) const;

        void registerSourceTable(std::string_view tableName, std::vector<SourceTableColumn> columns);

        /** Records that `tableName` is left out of the generated storage. */
        void markUngeneratableTable(std::string_view tableName);

        /** Whether `tableName` names a table of this batch that is left out of the generated storage. */
        bool isUngeneratableTable(std::string_view tableName) const;

        /** Struct name of a referenced table, recording the reference when the table is not generated. */
        std::string structNameForTable(std::string_view tableName);

        const SourceTableColumn* findSourceTableColumn(std::string_view tableName,
                                                       std::string_view columnName) const;

        /** Best-effort C++ type for a custom-function argument: schema type when known, else the name heuristic. */
        std::string customFunctionArgType(const AstNode& argument) const;

        void registerColumn(const std::string& cppName, const std::string& cppType);
        void registerPrefixColumn(const std::string& cppName, const std::string& cppType);
        std::string syntheticColumnCppType(std::string_view cppIdentifier) const;
        std::string inferTypeFromNode(const AstNode& node) const;
        std::string generatePrefix() const;

        void resetForGeneration();
    };

}  // namespace sqlite2orm
