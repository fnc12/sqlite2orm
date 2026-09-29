#include "correlated_column_scope.h"

#include <utility>

namespace sqlite2orm {

    CorrelatedColumnScope::CorrelatedColumnScope(CodeGeneratorContext& context, ColumnNameScope enclosingScope) :
        context(context), saved(std::move(enclosingScope)) {
        this->exchange(this->saved);
    }

    CorrelatedColumnScope::~CorrelatedColumnScope() {
        this->exchange(this->saved);
    }

    void CorrelatedColumnScope::exchange(ColumnNameScope& scope) {
        std::swap(this->context.structName, scope.structName);
        std::swap(this->context.implicitSourceAlias, scope.implicitSourceAlias);
        std::swap(this->context.implicitSingleSourceCteTypedef, scope.implicitSingleSourceCteTypedef);
        std::swap(this->context.implicitCteFromTableKeyNorm, scope.implicitCteFromTableKeyNorm);
        std::swap(this->context.selectSourceTables, scope.sourceTables);
    }

}  // namespace sqlite2orm
