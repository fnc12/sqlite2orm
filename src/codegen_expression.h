#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_result.h>

#include <string>
#include <vector>

namespace sqlite2orm {

    class CodeGenerator;
    class CodeGeneratorContext;

    class ExpressionCodeGenerator {
      public:
        ExpressionCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context);

        CodeGenResult generateExpression(const AstNode& astNode);

        std::string codegenOverClause(const OverClause& overClause, std::vector<DecisionPoint>& decisionPoints,
                                      std::vector<std::string>& warnings);

      private:
        CodeGenerator& coordinator;
        CodeGeneratorContext& context;

        std::string codegenWindowFrameBound(const WindowFrameBound& bound, std::vector<DecisionPoint>& decisionPoints,
                                            std::vector<std::string>& warnings);
        std::string codegenWindowFrameSpec(const WindowFrameSpec& frame, std::vector<DecisionPoint>& decisionPoints,
                                           std::vector<std::string>& warnings);
    };

}  // namespace sqlite2orm
