#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_result.h>

#include <vector>

namespace sqlite2orm {

    class CodeGenerator;
    class CodeGeneratorContext;

    class SelectCodeGenerator {
      public:
        SelectCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context);

        CodeGenResult generateSelect(const SelectNode& selectNode);
        CodeGenResult generateCompoundSelect(const CompoundSelectNode& compoundNode);

        /**
         *  `widenedResultColumns` names the result columns this select generates as
         *  `as_optional(...)`, one entry per column position: a compound SELECT statement's arm
         *  hands its columns to the caller and needs the widening every result column does, while
         *  the subqueries this same generator serves — a view body, a CTE, an IN, an
         *  INSERT ... SELECT — hand theirs to SQL and never widen. The decision is the compound's
         *  to make for all of its arms at once, which is why it arrives from outside.
         */
        CodeGenResult tryCodegenSqliteSelectSubexpression(const SelectNode& selectNode,
                                                          const std::vector<bool>& widenedResultColumns = {});
        CodeGenResult tryCodegenCompoundSelectSubexpression(const CompoundSelectNode& compoundNode,
                                                            const std::vector<bool>& widenedResultColumns = {});
        CodeGenResult tryCodegenSelectLikeSubquery(const AstNode& node);

      private:
        CodeGenerator& coordinator;
        CodeGeneratorContext& context;
    };

}  // namespace sqlite2orm
