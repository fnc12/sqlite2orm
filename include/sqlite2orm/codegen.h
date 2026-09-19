#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_policy.h>
#include <sqlite2orm/codegen_result.h>

#include <cstddef>
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

        /** Annotated struct + `make_view<T>(select(...))` for schema header merge (no make_storage). */
        CreateViewParts createViewParts(const CreateViewNode& node);

        CodeGenResult generateNode(const AstNode& astNode);

        /**
         *  Generates an expression the statement only stores the text of, as a column DEFAULT, a
         *  CHECK or a view body does. The caller then checks `context().storedHexLiteralsTooBig`
         *  and leaves its clause out with a warning when it is not empty: SQLite keeps a hex
         *  literal past the int64 range there, and C++ has no literal for it.
         */
        CodeGenResult generateStoredExpression(const AstNode& astNode);

        CodeGenResult tryCodegenSqliteSelectSubexpression(const SelectNode& selectNode);
        CodeGenResult tryCodegenCompoundSelectSubexpression(const CompoundSelectNode& compoundNode);
        CodeGenResult tryCodegenSelectLikeSubquery(const AstNode& node);

        CodeGenResult generateTriggerStep(const AstNode& statement, const std::string& subjectTableStruct);

        std::string codegenOverClause(const OverClause& overClause,
                                      std::vector<DecisionPoint>& decisionPoints,
                                      std::vector<CodegenWarning>& warnings);

        CodeGeneratorContext& context();
        const CodeGeneratorContext& context() const;

      private:
        friend void codegen_test_helpers::setSuppressWithCteStyleDecisionPointForTests(CodeGenerator&, bool);

        void syncToContext();

        /** `generateNode` without the bookkeeping that reports what the node recorded. */
        CodeGenResult dispatchNode(const AstNode& astNode);

        /**
         *  `result` with the comments the context recorded past `mark` appended: the comments the
         *  call that produced it recorded, and none of the ones its statement recorded around it.
         *  A `result` with no code generated nothing for a comment to explain, so the ones recorded
         *  past `mark` are dropped instead — they belong to the fragment that was thrown away.
         */
        CodeGenResult withRecordedComments(CodeGenResult result, size_t mark);

        /** Prepend user-defined/extension function structs (+ registration) and add their decision points. */
        void injectCustomFunctions(CodeGenResult& result);

        std::unique_ptr<CodeGeneratorContext> generatorContext;
        std::unique_ptr<ExpressionCodeGenerator> expressionCodeGenerator;
        std::unique_ptr<SelectCodeGenerator> selectCodeGenerator;
        std::unique_ptr<DmlCodeGenerator> dmlCodeGenerator;
        std::unique_ptr<DdlCodeGenerator> ddlCodeGenerator;
        std::unique_ptr<WithCodeGenerator> withCodeGenerator;
        std::unique_ptr<PragmaCodeGenerator> pragmaCodeGenerator;
    };

}  // namespace sqlite2orm
