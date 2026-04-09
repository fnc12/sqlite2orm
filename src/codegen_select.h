#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_result.h>

namespace sqlite2orm {

    class CodeGenerator;
    class CodeGeneratorContext;

    class SelectCodeGenerator {
      public:
        SelectCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context);

        CodeGenResult generateSelect(const SelectNode& selectNode);
        CodeGenResult generateCompoundSelect(const CompoundSelectNode& compoundNode);

        CodeGenResult tryCodegenSqliteSelectSubexpression(const SelectNode& selectNode);
        CodeGenResult tryCodegenCompoundSelectSubexpression(const CompoundSelectNode& compoundNode);
        CodeGenResult tryCodegenSelectLikeSubquery(const AstNode& node);

      private:
        CodeGenerator& coordinator;
        CodeGeneratorContext& context;
    };

}  // namespace sqlite2orm
