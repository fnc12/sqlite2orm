#pragma once

namespace sqlite2orm {

    class CodeGeneratorContext;

    /**
     *  Counts the statement an expression subquery is written in as a query enclosing it while the
     *  subquery is generated. A select with no FROM is a correlated subquery wherever any statement
     *  encloses it, not only another select: `UPDATE t SET b = (SELECT upper(b))` reads `b` of the
     *  row being updated just as `SELECT (SELECT b) FROM t` reads it of the row being selected.
     *  The select scopes alone count only selects, so a subquery of a DELETE, an UPDATE or a trigger
     *  step stood where a top-level select does. The previous level is restored, so the count never
     *  outlives the subquery it was taken for.
     *
     *  What a name is resolved against in the enclosing statement is kept for the same stretch, so
     *  a name the subquery's own FROM does not declare can be resolved there, the way SQLite reads
     *  a correlated reference.
     */
    class ExpressionSubqueryScope {
      public:
        explicit ExpressionSubqueryScope(CodeGeneratorContext& context);
        ~ExpressionSubqueryScope();

        ExpressionSubqueryScope(const ExpressionSubqueryScope&) = delete;
        ExpressionSubqueryScope& operator=(const ExpressionSubqueryScope&) = delete;

      private:
        CodeGeneratorContext& context;
    };

}  // namespace sqlite2orm
