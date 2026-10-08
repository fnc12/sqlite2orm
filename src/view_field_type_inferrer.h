#pragma once

#include "inferred_field_type.h"
#include "select_scope_columns.h"
#include <sqlite2orm/ast.h>

#include <optional>
#include <string_view>

namespace sqlite2orm {

    class CodeGeneratorContext;

    /** Resolves view SELECT output expressions to C++ field types using tables known to the context. */
    struct ViewFieldTypeInferrer {
        const CodeGeneratorContext& context;
        /** The sources the view's own SELECT names, which is the scope its expressions read. */
        SelectScopeColumns scope;

        explicit ViewFieldTypeInferrer(const CodeGeneratorContext& context, const SelectNode& selectNode);

        const SourceTableColumn* resolveQualified(std::string_view tableOrAlias, std::string_view columnName) const;
        const SourceTableColumn* resolveUnqualified(std::string_view columnName) const;
        std::optional<InferredFieldType> inferFunctionCall(const FunctionCallNode& functionCall) const;
        std::optional<InferredFieldType> infer(const AstNode& node) const;
    };

}  // namespace sqlite2orm
