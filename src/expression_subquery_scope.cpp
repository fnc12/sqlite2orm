#include "expression_subquery_scope.h"
#include "codegen_context.h"

namespace sqlite2orm {

    ExpressionSubqueryScope::ExpressionSubqueryScope(CodeGeneratorContext& context) : context(context) {
        ++this->context.selectNestingLevel;
    }

    ExpressionSubqueryScope::~ExpressionSubqueryScope() {
        --this->context.selectNestingLevel;
    }

}  // namespace sqlite2orm
