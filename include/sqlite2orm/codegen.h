#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_policy.h>
#include <sqlite2orm/codegen_result.h>

#include <memory>
#include <string>
#include <vector>

namespace sqlite2orm {

    class CodeGenerator;
    class CodeGeneratorContext;
    class ExpressionCodeGenerator;
    class SelectCodeGenerator;
    class DmlCodeGenerator;
    class DdlCodeGenerator;
    class WithCodeGenerator;
    class PragmaCodeGenerator;

}  // namespace sqlite2orm

namespace codegen_test_helpers {
    void setSuppressWithCteStyleDecisionPointForTests(sqlite2orm::CodeGenerator& generator, bool suppress);
}

namespace sqlite2orm {

    class CodeGenerator {
      public:
        CodeGenerator();
        ~CodeGenerator();

        std::string structName = "User";

        /** When non-null, steers decision points (expr_style, column_ref_style, api_level, …). */
        const CodeGenPolicy* codeGenPolicy = nullptr;

        CodeGenResult generate(const AstNode& astNode);
        std::string generatePrefix() const;

        /** Struct body + `make_table(...)[.without_rowid()]` for schema header merge (no make_storage). */
        CreateTableParts createTableParts(const CreateTableNode& createTable);

        CodeGenResult generateNode(const AstNode& astNode);

        CodeGenResult tryCodegenSqliteSelectSubexpression(const SelectNode& selectNode);
        CodeGenResult tryCodegenCompoundSelectSubexpression(const CompoundSelectNode& compoundNode);
        CodeGenResult tryCodegenSelectLikeSubquery(const AstNode& node);

        CodeGenResult generateTriggerStep(const AstNode& statement, const std::string& subjectTableStruct);

        std::string codegenOverClause(const OverClause& overClause, std::vector<DecisionPoint>& decisionPoints,
                                      std::vector<std::string>& warnings);

        CodeGeneratorContext& context();
        const CodeGeneratorContext& context() const;

      private:
        friend void codegen_test_helpers::setSuppressWithCteStyleDecisionPointForTests(CodeGenerator&, bool);

        void syncToContext();

        std::unique_ptr<CodeGeneratorContext> generatorContext;
        std::unique_ptr<ExpressionCodeGenerator> expressionCodeGenerator;
        std::unique_ptr<SelectCodeGenerator> selectCodeGenerator;
        std::unique_ptr<DmlCodeGenerator> dmlCodeGenerator;
        std::unique_ptr<DdlCodeGenerator> ddlCodeGenerator;
        std::unique_ptr<WithCodeGenerator> withCodeGenerator;
        std::unique_ptr<PragmaCodeGenerator> pragmaCodeGenerator;
    };

}  // namespace sqlite2orm
