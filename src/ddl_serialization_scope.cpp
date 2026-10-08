#include "ddl_serialization_scope.h"

#include <utility>

namespace sqlite2orm {

    DdlSerializationScope::DdlSerializationScope(CodeGeneratorContext& context) :
        context(context), enclosing(context.ddlSerializedExpression),
        enclosingLiterals(std::move(context.ddlInfinityLiterals)) {
        this->context.ddlSerializedExpression = true;
        this->context.ddlInfinityLiterals.clear();
    }

    DdlSerializationScope::~DdlSerializationScope() {
        this->context.ddlSerializedExpression = this->enclosing;
        this->context.ddlInfinityLiterals = std::move(this->enclosingLiterals);
    }

}  // namespace sqlite2orm
