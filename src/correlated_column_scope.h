#pragma once

#include "codegen_context.h"

namespace sqlite2orm {

    /**
     *  Stands the context in the enclosing query a correlated column reference resolves in, while
     *  that reference is generated. SQLite reads a name the subquery's own FROM does not declare as
     *  a column of the enclosing query's source, so the reference takes exactly the form it would
     *  take written in that query — a member of its struct, of its alias or of its CTE — and not a
     *  member of the subquery's struct, which has no such field. What the subquery resolves names
     *  against is restored on the way out.
     */
    class CorrelatedColumnScope {
      public:
        CorrelatedColumnScope(CodeGeneratorContext& context, ColumnNameScope enclosingScope);
        ~CorrelatedColumnScope();

        CorrelatedColumnScope(const CorrelatedColumnScope&) = delete;
        CorrelatedColumnScope& operator=(const CorrelatedColumnScope&) = delete;

      private:
        void exchange(ColumnNameScope& scope);

        CodeGeneratorContext& context;
        ColumnNameScope saved;
    };

}  // namespace sqlite2orm
