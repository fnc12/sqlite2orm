#include "codegen_context.h"

#include "codegen_utils.h"

#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    bool CodeGeneratorContext::useCpp20ColumnAliasStyle() const {
        if(this->columnAliasStyleOverride) {
            return *this->columnAliasStyleOverride == "cpp20_literal";
        }
        return policyEquals(this->codeGenPolicy, "column_alias_style", "cpp20_literal");
    }

    bool CodeGeneratorContext::useCpp20TableAliasStyle() const {
        if(this->withCteCpp20Monikers()) {
            return true;
        }
        return policyEquals(this->codeGenPolicy, "table_alias_style", "cpp20");
    }

    bool CodeGeneratorContext::withCteLegacyColalias() const {
        return this->activeWithCteStyle && *this->activeWithCteStyle == "legacy_colalias";
    }

    bool CodeGeneratorContext::withCteCpp20Monikers() const {
        return this->activeWithCteStyle && *this->activeWithCteStyle == "cpp20_monikers";
    }

    bool CodeGeneratorContext::columnRefIsSelectAliasNoWrap(const ColumnRefNode& ref) const {
        if(!this->useCpp20ColumnAliasStyle()) {
            return false;
        }
        std::string normalized = toLowerAscii(stripIdentifierQuotes(ref.columnName));
        return this->activeSelectColumnAliasCpp20Vars.find(normalized) !=
               this->activeSelectColumnAliasCpp20Vars.end();
    }

    bool CodeGeneratorContext::isExplicitCteColumn(std::string_view cteKeyNorm, std::string_view columnName) const {
        auto it = this->cteColumnNamesByTableKey.find(std::string(cteKeyNorm));
        if(it == this->cteColumnNamesByTableKey.end()) {
            return false;
        }
        std::string normalizedCol = normalizeSqlIdentifier(columnName);
        for(const auto& colName : it->second) {
            if(normalizeSqlIdentifier(colName) == normalizedCol) {
                return true;
            }
        }
        return false;
    }

    void CodeGeneratorContext::registerColumn(const std::string& cppName, const std::string& cppType) {
        auto [it, inserted] = this->columnTypes.try_emplace(cppName, cppType);
        if(!inserted && it->second == "int" && cppType != "int") {
            it->second = cppType;
        }
    }

    void CodeGeneratorContext::registerPrefixColumn(const std::string& cppName, const std::string& cppType) {
        if(this->implicitSingleSourceCteTypedef) {
            return;
        }
        this->registerColumn(cppName, cppType);
    }

    std::string CodeGeneratorContext::syntheticColumnCppType(std::string_view cppIdentifier) const {
        return defaultCppTypeForSyntheticColumn(cppIdentifier);
    }

    std::string CodeGeneratorContext::inferTypeFromNode(const AstNode& node) const {
        if(dynamic_cast<const StringLiteralNode*>(&node)) return "std::string";
        if(dynamic_cast<const IntegerLiteralNode*>(&node)) return "int";
        if(dynamic_cast<const RealLiteralNode*>(&node)) return "double";
        if(dynamic_cast<const BoolLiteralNode*>(&node)) return "bool";
        return "int";
    }

    std::string CodeGeneratorContext::generatePrefix() const {
        if(this->columnTypes.empty()) {
            return "";
        }
        std::string result = "struct " + this->structName + " {\n";
        for(const auto& [name, type] : this->columnTypes) {
            result += "    " + type + " " + name + defaultInitializer(type) + ";\n";
        }
        result += "};";
        return result;
    }

    void CodeGeneratorContext::resetForGeneration() {
        this->accumulatedErrors.clear();
    }

}  // namespace sqlite2orm
