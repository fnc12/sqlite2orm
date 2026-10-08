#include "parenthesized_condition_scope.h"
#include "codegen_context.h"

namespace sqlite2orm {

    ParenthesizedConditionScope::ParenthesizedConditionScope(CodeGeneratorContext& context, const AstNode& condition) :
        context(context), enclosing(context.parenthesizedConditionNode) {
        this->context.parenthesizedConditionNode = &condition;
    }

    ParenthesizedConditionScope::~ParenthesizedConditionScope() {
        this->context.parenthesizedConditionNode = this->enclosing;
    }
}  // namespace sqlite2orm
