#include "select_scope_columns.h"

#include "codegen_context.h"

namespace sqlite2orm {

    SelectScopeColumns::SelectScopeColumns(const SelectNode& selectNode, const CodeGeneratorContext& context) :
        context(context) {
        bool firstItem = true;
        for (const FromClauseItem& fromItem: selectNode.fromClause) {
            // A derived table and a table-valued function name no table of the schema, and a CTE
            // is read back through the typedef `column<cte_0>(…)` names rather than through a
            // field of a source struct, so none of the three answers a column here.
            const bool named = !fromItem.table.derivedSelect && fromItem.table.tableFunctionArgs.empty();
            const std::string sourceName = named ? stripIdentifierQuotes(fromItem.table.tableName) : std::string();
            const std::string sourceKey = normalizeSqlIdentifier(sourceName);
            const bool opaque = !named || context.activeCteTypedefByTableKey.find(sourceKey) !=
                                              context.activeCteTypedefByTableKey.end();
            if (fromItem.table.alias) {
                const std::string aliasKey = normalizeSqlIdentifier(*fromItem.table.alias);
                if (opaque) {
                    this->opaqueNames.insert(aliasKey);
                } else {
                    this->sourceByAlias[aliasKey] = sourceName;
                }
            }
            if (opaque) {
                if (!sourceKey.empty()) {
                    this->opaqueNames.insert(sourceKey);
                }
            } else {
                this->sources.push_back(sourceName);
                if (firstItem) {
                    this->implicitSource = sourceName;
                }
            }
            firstItem = false;
        }
    }

    const SourceTableColumn* SelectScopeColumns::findQualified(std::string_view tableOrAlias,
                                                               std::string_view columnName) const {
        const std::string key = normalizeSqlIdentifier(tableOrAlias);
        if (this->opaqueNames.find(key) != this->opaqueNames.end()) {
            return nullptr;
        }
        const auto aliasIterator = this->sourceByAlias.find(key);
        const std::string_view sourceName =
            aliasIterator != this->sourceByAlias.end() ? std::string_view(aliasIterator->second) : tableOrAlias;
        return this->context.findSourceTableColumn(sourceName, columnName);
    }

    const SourceTableColumn* SelectScopeColumns::findUnqualified(std::string_view columnName) const {
        for (const std::string& sourceName: this->sources) {
            if (const SourceTableColumn* column = this->context.findSourceTableColumn(sourceName, columnName)) {
                return column;
            }
        }
        return nullptr;
    }

    const SourceTableColumn* SelectScopeColumns::resolve(const AstNode& node) const {
        // A COLLATE or a unary plus generates its operand and nothing else, so the reference the
        // value is read back through is the one under them.
        const AstNode& valueNode = generatedOperandNode(node);
        if (auto* qualifiedRef = dynamic_cast<const QualifiedColumnRefNode*>(&valueNode)) {
            return this->findQualified(qualifiedRef->tableName, qualifiedRef->columnName);
        }
        if (auto* columnRef = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
            if (!this->implicitSource) {
                return nullptr;
            }
            return this->context.findSourceTableColumn(*this->implicitSource, columnRef->columnName);
        }
        return nullptr;
    }

    const std::vector<std::string>& SelectScopeColumns::sourceNames() const {
        return this->sources;
    }

    std::string_view SelectScopeColumns::sourceForAlias(std::string_view alias) const {
        const auto aliasIterator = this->sourceByAlias.find(normalizeSqlIdentifier(alias));
        return aliasIterator != this->sourceByAlias.end() ? std::string_view(aliasIterator->second) : alias;
    }

    ReferencedColumnResolver SelectScopeColumns::resolver() const {
        return [this](const AstNode& node) {
            return this->resolve(node);
        };
    }

}  // namespace sqlite2orm
