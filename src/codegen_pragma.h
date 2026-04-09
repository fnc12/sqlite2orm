#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_result.h>

namespace sqlite2orm {

    class CodeGenerator;
    class CodeGeneratorContext;

    class PragmaCodeGenerator {
      public:
        PragmaCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context);

        CodeGenResult codegenPragmaStatement(const PragmaNode& node);

      private:
        CodeGenerator& coordinator;
        CodeGeneratorContext& context;
    };

}  // namespace sqlite2orm
