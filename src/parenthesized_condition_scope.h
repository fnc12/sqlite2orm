#pragma once

#include <sqlite2orm/ast.h>

namespace sqlite2orm {

    class CodeGeneratorContext;

    /**
     *  Marks `condition` as the node a `where(...)` is about to be built around while it is
     *  generated. `where_t` is the one clause that serializes its argument inside parentheses, so a
     *  compound subquery standing there — and only there — comes out as the SQL it was read from.
     *  The mark is the node itself and not a flag: `where(a > (SELECT … UNION …))` generates the
     *  same subquery one level down, where the parentheses are not written, and pointer identity is
     *  what tells the two apart. The previous mark is restored rather than cleared, so the mark
     *  never outlives the clause it was taken for.
     */
    class ParenthesizedConditionScope {
      public:
        ParenthesizedConditionScope(CodeGeneratorContext& context, const AstNode& condition);
        ~ParenthesizedConditionScope();

        ParenthesizedConditionScope(const ParenthesizedConditionScope&) = delete;
        ParenthesizedConditionScope& operator=(const ParenthesizedConditionScope&) = delete;

      private:
        CodeGeneratorContext& context;
        const AstNode* enclosing;
    };

}  // namespace sqlite2orm
