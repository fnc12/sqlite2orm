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
        /**
         *  `cteBodySelect` says this select is the body of a CTE, which the caller knows and the
         *  select cannot see: a CTE column is read back through sqlite_orm's
         *  `extract_colref_expressions`, which is deleted for a `select_t`, so a subquery standing
         *  as a whole column of one has no form there. Like the widening, it belongs to this select
         *  and not to the selects nested in it, so it arrives as an argument rather than as a mark
         *  on the context, and a compound hands it to each of its arms.
         */
        CodeGenResult tryCodegenSqliteSelectSubexpression(const SelectNode& selectNode,
                                                          const std::vector<bool>& widenedResultColumns = {},
                                                          bool cteBodySelect = false);
        CodeGenResult tryCodegenCompoundSelectSubexpression(const CompoundSelectNode& compoundNode,
                                                            const std::vector<bool>& widenedResultColumns = {},
                                                            bool cteBodySelect = false);
        CodeGenResult tryCodegenSelectLikeSubquery(const AstNode& node, bool cteBodySelect = false);

      private:
        CodeGenerator& coordinator;
        CodeGeneratorContext& context;
    };

}  // namespace sqlite2orm
