#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_policy.h>
#include <sqlite2orm/codegen_result.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sqlite2orm {

    struct TableAliasInfo {
        std::string ormAliasType;
        std::string baseStructName;
    };

    struct Cpp20TableAliasDeclaration {
        std::string variableName;
        std::string baseStructName;
        std::string sqlAlias;
    };

    class CodeGeneratorContext {
      public:
        std::string structName = "User";
        const CodeGenPolicy* codeGenPolicy = nullptr;

        int nextDecisionPointId = 1;
        int nextBindParamIndex = 0;
        std::vector<std::string> accumulatedErrors;
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

        std::optional<std::string> activeWithCteStyle;
        std::map<std::string, std::string> withCteLegacyColVarByPipeKey;
        std::map<std::string, std::string> withCteCpp20MonikerVarByCteKey;
        std::map<std::string, std::string> withCteCpp20ColVarByPipeKey;
        std::vector<Cpp20TableAliasDeclaration> cpp20TableAliasDeclarations;

        bool suppressWithCteStyleDecisionPoint = false;
        bool suppressTableAliasStyleDecisionPoint = false;

        bool useCpp20ColumnAliasStyle() const;
        bool useCpp20TableAliasStyle() const;
        bool withCteLegacyColalias() const;
        bool withCteCpp20Monikers() const;
        bool columnRefIsSelectAliasNoWrap(const ColumnRefNode& ref) const;

        void registerColumn(const std::string& cppName, const std::string& cppType);
        void registerPrefixColumn(const std::string& cppName, const std::string& cppType);
        std::string syntheticColumnCppType(std::string_view cppIdentifier) const;
        std::string inferTypeFromNode(const AstNode& node) const;
        std::string generatePrefix() const;

        void resetForGeneration();
    };

}  // namespace sqlite2orm
