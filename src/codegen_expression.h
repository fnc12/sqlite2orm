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

        /**
         *  The code of `astNode` with the spans its operands recorded; `generateExpression` adds
         *  the node's own around them.
         */
        CodeGenResult generateExpressionCode(const AstNode& astNode);

        /**
         *  Whether `node` is a column reference that is generated as a column pointer rather than
         *  as a plain member pointer.
         */
        bool nodeGeneratesColumnPointer(const AstNode* node) const;

        /**
         *  The code of one kind of node, for `generateExpressionCode`. `astNode` is the column
         *  reference itself, which a correlated reference generates once more in the enclosing
         *  query's scope.
         */
        CodeGenResult generateColumnRef(const ColumnRefNode* columnRef, const AstNode& astNode);
        CodeGenResult generateQualifiedColumnRef(const QualifiedColumnRefNode* qualifiedRef);
        CodeGenResult generateQualifiedAsterisk(const QualifiedAsteriskNode* qualifiedAsterisk);
        CodeGenResult generateBinaryOperator(const BinaryOperatorNode* binaryOp);
        CodeGenResult generateUnaryOperator(const UnaryOperatorNode* unaryOp);
        CodeGenResult generateIsNull(const IsNullNode* isNullNode);
        CodeGenResult generateIsNotNull(const IsNotNullNode* isNotNullNode);
        CodeGenResult generateBetween(const BetweenNode* betweenNode);
        CodeGenResult generateIn(const InNode* inNode);
        CodeGenResult generateLike(const LikeNode* likeNode);
        CodeGenResult generateGlob(const GlobNode* globNode);
        CodeGenResult generateMatch(const MatchNode* matchNode);
        CodeGenResult generateFunctionCall(const FunctionCallNode* funcCall);

        std::string codegenWindowFrameBound(const WindowFrameBound& bound,
                                            std::vector<DecisionPoint>& decisionPoints,
                                            std::vector<CodegenWarning>& warnings);
        std::string codegenWindowFrameSpec(const WindowFrameSpec& frame,
                                           std::vector<DecisionPoint>& decisionPoints,
                                           std::vector<CodegenWarning>& warnings);
    };

}  // namespace sqlite2orm
