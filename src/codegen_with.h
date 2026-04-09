#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_result.h>

namespace sqlite2orm {

    class CodeGenerator;
    class CodeGeneratorContext;

    class WithCodeGenerator {
      public:
        WithCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context);

        CodeGenResult generateWithQuery(const WithQueryNode& withQueryNode);

      private:
        CodeGenerator& coordinator;
        CodeGeneratorContext& context;
    };

}  // namespace sqlite2orm
