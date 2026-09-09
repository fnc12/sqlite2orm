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

    void CodeGeneratorContext::registerSourceTable(std::string_view tableName,
                                                   std::vector<SourceTableColumn> columns) {
        this->sourceTableColumnsByNormalizedName[normalizeSqlIdentifier(tableName)] = std::move(columns);
    }

    const SourceTableColumn* CodeGeneratorContext::findSourceTableColumn(std::string_view tableName,
                                                                         std::string_view columnName) const {
        const auto tableIterator =
            this->sourceTableColumnsByNormalizedName.find(normalizeSqlIdentifier(tableName));
        if(tableIterator == this->sourceTableColumnsByNormalizedName.end()) {
            return nullptr;
        }
        const std::string normalizedColumn = normalizeSqlIdentifier(columnName);
        for(const SourceTableColumn& sourceTableColumn : tableIterator->second) {
            if(normalizeSqlIdentifier(sourceTableColumn.sqlName) == normalizedColumn) {
                return &sourceTableColumn;
            }
        }
        return nullptr;
    }

    std::string CodeGeneratorContext::customFunctionArgType(const AstNode& argument) const {
        if(auto* columnRef = dynamic_cast<const ColumnRefNode*>(&argument)) {
            // Schema type wins when the column belongs to a known CREATE TABLE in the batch.
            const std::string normalizedColumn = normalizeSqlIdentifier(columnRef->columnName);
            for(const auto& [tableKey, columns] : this->sourceTableColumnsByNormalizedName) {
                (void)tableKey;
                for(const SourceTableColumn& column : columns) {
                    if(normalizeSqlIdentifier(column.sqlName) == normalizedColumn) {
                        return column.cppType;
                    }
                }
            }
            const std::string cppName = toCppIdentifier(columnRef->columnName);
            const auto known = this->columnTypes.find(cppName);
            if(known != this->columnTypes.end()) {
                return known->second;
            }
            return this->syntheticColumnCppType(cppName);  // name heuristic (name → std::string, else int)
        }
        return this->inferTypeFromNode(argument);
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

    std::string CodeGeneratorContext::statementVariableName(std::string_view baseName) {
        const std::string base(baseName);
        auto resolved = this->statementVariableNames.find(base);
        if(resolved != this->statementVariableNames.end()) {
            return resolved->second;
        }
        const int use = ++this->batchVariableUses[base];
        std::string name = use == 1 ? base : base + std::to_string(use);
        this->statementVariableNames.emplace(base, name);
        return name;
    }

    void CodeGeneratorContext::registerCustomFunction(CustomFunctionUse use) {
        for(const auto& existing : this->customFunctions) {
            if(existing.structName == use.structName) {
                return;
            }
        }
        this->customFunctions.push_back(std::move(use));
    }

    void CodeGeneratorContext::resetForGeneration() {
        this->accumulatedErrors.clear();
        this->customFunctions.clear();
    }

}  // namespace sqlite2orm
