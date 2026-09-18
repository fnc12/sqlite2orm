#include "codegen_context.h"

#include "codegen_utils.h"

#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    bool CodeGeneratorContext::useCpp20ColumnAliasStyle() const {
        if(this->columnAliasStyleOverride) {
            // Internal override used to render the C++20 alternative for the options list; ungated.
            return *this->columnAliasStyleOverride == "cpp20_literal";
        }
        return cpp20Allowed(this->codeGenPolicy) &&
               policyEquals(this->codeGenPolicy, "column_alias_style", "cpp20_literal");
    }

    bool CodeGeneratorContext::useCpp20TableAliasStyle() const {
        if(this->withCteCpp20Monikers()) {
            return true;
        }
        return cpp20Allowed(this->codeGenPolicy) &&
               policyEquals(this->codeGenPolicy, "table_alias_style", "cpp20");
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
        // The argument a COLLATE or a unary plus stands over is the one the call is handed.
        const AstNode& valueNode = generatedOperandNode(argument);
        if(auto* columnRef = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
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
        return this->inferTypeFromNode(valueNode);
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
        // A COLLATE picks a collating sequence for a comparison and leaves the value it is written
        // over alone, so the value — and the field that holds it — is the one under it.
        const AstNode& valueNode = generatedOperandNode(node);
        if(dynamic_cast<const StringLiteralNode*>(&valueNode)) return "std::string";
        if(auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&valueNode)) {
            // An integer literal an int64 cannot hold is a REAL for SQLite, so is the column.
            if(integerLiteralExceedsInt64(integerLiteral->value)) {
                return "double";
            }
            // `int` stays the field for the small literal the comparison usually carries, but one
            // that only an int64 holds would be truncated by it, so it widens the field instead.
            return integerLiteralExceedsInt32(integerLiteral->value) ? "int64_t" : "int";
        }
        if(dynamic_cast<const RealLiteralNode*>(&valueNode)) return "double";
        if(dynamic_cast<const BoolLiteralNode*>(&valueNode)) return "bool";
        if(auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&valueNode)) {
            if(unaryOperator->unaryOperator == UnaryOperator::bitwiseNot) {
                // `~` is a 64-bit complement in SQLite, and it takes a value out of the int32
                // range as readily as it brings one back in: `~2147483648` is -2147483649. The
                // field follows the operation rather than the operand, the way it already does
                // for a view column in `ViewFieldTypeInferrer`.
                return "int64_t";
            }
            if(unaryOperator->operand && (unaryOperator->unaryOperator == UnaryOperator::minus ||
                                          unaryOperator->unaryOperator == UnaryOperator::plus)) {
                // The signs standing in front of a literal belong to the value they spell with
                // it, so the width follows all of them together rather than the magnitude alone:
                // `0xFFFFFFFF80000000` is the -2147483648 an `int` holds, `-0xFFFFFFFF80000000`
                // is the 2147483648 it does not, and `-(-2147483648)` is that value again.
                std::size_t foldedSigns = 0;
                auto* signedLiteral =
                    dynamic_cast<const IntegerLiteralNode*>(withoutFoldedSigns(valueNode, foldedSigns));
                if(signedLiteral) {
                    // A value past the int64 range is a REAL for SQLite, whichever side of the
                    // range the signs leave it on.
                    if(isIntegerLiteralPastIntegerFieldRange(valueNode)) {
                        return "double";
                    }
                    return integerLiteralExceedsInt32(signedLiteral->value, foldedSigns % 2 != 0) ? "int64_t"
                                                                                                 : "int";
                }
                // A sign does not otherwise change the width a value needs, so `-(x + 1)` is the
                // int64_t its operand is. C++ types a constant the same way: `2147483648` is
                // already wider than an `int` there, and the minus applies to that wider type.
                return this->inferTypeFromNode(*unaryOperator->operand);
            }
        }
        if(auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&valueNode)) {
            // SQLite computes arithmetic and bit operations over 64-bit integers, so the result
            // leaves the int32 range even where both operands sit inside it: `2147483647 + 1` is
            // 2147483648. The same widening `ViewFieldTypeInferrer` applies to a view column.
            switch(binaryOperator->binaryOperator) {
            case BinaryOperator::add:
            case BinaryOperator::subtract:
            case BinaryOperator::multiply:
            case BinaryOperator::divide:
            case BinaryOperator::modulo: {
                const std::string lhsType =
                    binaryOperator->lhs ? this->inferTypeFromNode(*binaryOperator->lhs) : std::string();
                const std::string rhsType =
                    binaryOperator->rhs ? this->inferTypeFromNode(*binaryOperator->rhs) : std::string();
                return lhsType == "double" || rhsType == "double" ? "double" : "int64_t";
            }
            case BinaryOperator::bitwiseAnd:
            case BinaryOperator::bitwiseOr:
            case BinaryOperator::shiftLeft:
            case BinaryOperator::shiftRight: return "int64_t";
            default: break;
            }
        }
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

    void CodeGeneratorContext::markUngeneratableTable(std::string_view tableName) {
        this->ungeneratableTables.insert(normalizeSqlIdentifier(tableName));
    }

    void CodeGeneratorContext::markUngeneratableView(std::string_view viewName) {
        this->markUngeneratableTable(viewName);
        this->ungeneratableViews.insert(normalizeSqlIdentifier(viewName));
    }

    std::string_view CodeGeneratorContext::referencedUngeneratableKind() const {
        for(const std::string& referencedName : this->referencedUngeneratableTables) {
            if(this->ungeneratableViews.find(referencedName) == this->ungeneratableViews.end()) {
                return "table";
            }
        }
        return "view";
    }

    bool CodeGeneratorContext::isUngeneratableTable(std::string_view tableName) const {
        return this->ungeneratableTables.find(normalizeSqlIdentifier(tableName)) != this->ungeneratableTables.end();
    }

    std::string CodeGeneratorContext::structNameForTable(std::string_view tableName) {
        if(this->isUngeneratableTable(tableName)) {
            this->referencedUngeneratableTables.insert(normalizeSqlIdentifier(tableName));
        }
        return toStructName(tableName);
    }

    void CodeGeneratorContext::resetForGeneration() {
        this->accumulatedErrors.clear();
        this->storedExpression = false;
        this->storedHexLiteralsTooBig.clear();
        this->customFunctions.clear();
    }

}  // namespace sqlite2orm
