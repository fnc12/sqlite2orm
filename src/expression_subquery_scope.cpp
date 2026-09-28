#include "expression_subquery_scope.h"
#include "codegen_context.h"

namespace sqlite2orm {

    ExpressionSubqueryScope::ExpressionSubqueryScope(CodeGeneratorContext& context) : context(context) {
        ++this->context.selectNestingLevel;
        this->context.enclosingColumnNameScopes.push_back(this->context.columnNameScope());
    }

    ExpressionSubqueryScope::~ExpressionSubqueryScope() {
        this->context.enclosingColumnNameScopes.pop_back();
        --this->context.selectNestingLevel;
    }

}  // namespace sqlite2orm
