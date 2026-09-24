#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_result.h>

#include <string>
#include <string_view>
#include <vector>

namespace sqlite2orm {

    class CodeGenerator;
    class CodeGeneratorContext;

    class ExpressionCodeGenerator {
      public:
        ExpressionCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context);

        CodeGenResult generateExpression(const AstNode& astNode);

        std::string codegenOverClause(const OverClause& overClause,
                                      std::vector<DecisionPoint>& decisionPoints,
                                      std::vector<CodegenWarning>& warnings);

      private:
        CodeGenerator& coordinator;
        CodeGeneratorContext& context;

        /**
         *  Records that the literal `spelling` denotes, written at `literal`, was generated as an
         *  infinity, while the expression being generated is one sqlite_orm writes into the DDL of
         *  a schema object. Nothing is recorded for the expression of a query: a query binds its
         *  values, so an infinity reaches SQLite there as the number it is. The generator owning
         *  the clause turns what is recorded into a warning of its own.
         */
        void noteDdlInfinity(const AstNode& literal, std::string_view spelling);

        std::string codegenWindowFrameBound(const WindowFrameBound& bound,
                                            std::vector<DecisionPoint>& decisionPoints,
                                            std::vector<CodegenWarning>& warnings);
        std::string codegenWindowFrameSpec(const WindowFrameSpec& frame,
                                           std::vector<DecisionPoint>& decisionPoints,
                                           std::vector<CodegenWarning>& warnings);
    };

}  // namespace sqlite2orm
