#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_result.h>

#include <string>

namespace sqlite2orm {

    class CodeGenerator;
    class CodeGeneratorContext;

    class DmlCodeGenerator {
      public:
        DmlCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context);

        CodeGenResult generateInsert(const InsertNode& insertNode);
        CodeGenResult generateUpdate(const UpdateNode& updateNode);
        CodeGenResult generateDelete(const DeleteNode& deleteNode);

        CodeGenResult generateTriggerStep(const AstNode& statement, const std::string& subjectTableStruct);

      private:
        CodeGenerator& coordinator;
        CodeGeneratorContext& context;
    };

}  // namespace sqlite2orm
