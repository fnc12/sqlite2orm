#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_result.h>

#include <string>
#include <vector>

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
        /**
         *  The column names an `INSERT INTO t VALUES (...)` has to spell out because the object
         *  form cannot carry one of its values: every value passes through a struct field there,
         *  and an `int64_t` field converts a value SQLite keeps a REAL. Empty when the object form
         *  carries the whole statement, which is every ordinary one.
         */
        std::vector<std::string> columnListForcedByFieldTypes(const InsertNode& insertNode,
                                                              std::vector<CodegenWarning>& warnings) const;

        CodeGenerator& coordinator;
        CodeGeneratorContext& context;
    };

}  // namespace sqlite2orm
