#include <sqlite2orm/codegen.h>

#include "codegen_context.h"
#include "codegen_expression.h"
#include "codegen_select.h"
#include "codegen_dml.h"
#include "codegen_ddl.h"
#include "codegen_with.h"
#include "codegen_pragma.h"

namespace codegen_test_helpers {

    void setSuppressWithCteStyleDecisionPointForTests(sqlite2orm::CodeGenerator& generator, bool suppress) {
        generator.context().suppressWithCteStyleDecisionPoint = suppress;
    }

}  // namespace codegen_test_helpers

namespace sqlite2orm {

    CodeGenerator::CodeGenerator()
        : generatorContext(std::make_unique<CodeGeneratorContext>()),
          expressionCodeGenerator(
              std::make_unique<ExpressionCodeGenerator>(*this, *this->generatorContext)),
          selectCodeGenerator(std::make_unique<SelectCodeGenerator>(*this, *this->generatorContext)),
          dmlCodeGenerator(std::make_unique<DmlCodeGenerator>(*this, *this->generatorContext)),
          ddlCodeGenerator(std::make_unique<DdlCodeGenerator>(*this, *this->generatorContext)),
          withCodeGenerator(std::make_unique<WithCodeGenerator>(*this, *this->generatorContext)),
          pragmaCodeGenerator(std::make_unique<PragmaCodeGenerator>(*this, *this->generatorContext)) {}

    CodeGenerator::~CodeGenerator() = default;

    void CodeGenerator::syncToContext() {
        this->generatorContext->structName = this->structName;
        this->generatorContext->codeGenPolicy = this->codeGenPolicy;
    }

    CodeGeneratorContext& CodeGenerator::context() {
        return *this->generatorContext;
    }

    const CodeGeneratorContext& CodeGenerator::context() const {
        return *this->generatorContext;
    }

    CodeGenResult CodeGenerator::generate(const AstNode& astNode) {
        this->syncToContext();
        this->generatorContext->resetForGeneration();
        auto result = this->generateNode(astNode);
        if(!this->generatorContext->accumulatedErrors.empty()) {
            return CodeGenResult{{}, {}, {}, std::move(this->generatorContext->accumulatedErrors), {}};
        }
        return result;
    }

    std::string CodeGenerator::generatePrefix() const {
        return this->generatorContext->generatePrefix();
    }

    CreateTableParts CodeGenerator::createTableParts(const CreateTableNode& createTable) {
        this->syncToContext();
        return this->ddlCodeGenerator->createTableParts(createTable);
    }

    CodeGenResult CodeGenerator::generateNode(const AstNode& astNode) {
        if(dynamic_cast<const IntegerLiteralNode*>(&astNode) ||
           dynamic_cast<const RealLiteralNode*>(&astNode) ||
           dynamic_cast<const StringLiteralNode*>(&astNode) ||
           dynamic_cast<const NullLiteralNode*>(&astNode) ||
           dynamic_cast<const BoolLiteralNode*>(&astNode) ||
           dynamic_cast<const RaiseNode*>(&astNode) ||
           dynamic_cast<const BlobLiteralNode*>(&astNode) ||
           dynamic_cast<const CurrentDatetimeLiteralNode*>(&astNode) ||
           dynamic_cast<const ColumnRefNode*>(&astNode) ||
           dynamic_cast<const QualifiedColumnRefNode*>(&astNode) ||
           dynamic_cast<const QualifiedAsteriskNode*>(&astNode) ||
           dynamic_cast<const NewRefNode*>(&astNode) ||
           dynamic_cast<const OldRefNode*>(&astNode) ||
           dynamic_cast<const ExcludedRefNode*>(&astNode) ||
           dynamic_cast<const BinaryOperatorNode*>(&astNode) ||
           dynamic_cast<const UnaryOperatorNode*>(&astNode) ||
           dynamic_cast<const IsNullNode*>(&astNode) ||
           dynamic_cast<const IsNotNullNode*>(&astNode) ||
           dynamic_cast<const BetweenNode*>(&astNode) ||
           dynamic_cast<const SubqueryNode*>(&astNode) ||
           dynamic_cast<const ExistsNode*>(&astNode) ||
           dynamic_cast<const InNode*>(&astNode) ||
           dynamic_cast<const LikeNode*>(&astNode) ||
           dynamic_cast<const GlobNode*>(&astNode) ||
           dynamic_cast<const CastNode*>(&astNode) ||
           dynamic_cast<const CaseNode*>(&astNode) ||
           dynamic_cast<const BindParameterNode*>(&astNode) ||
           dynamic_cast<const CollateNode*>(&astNode) ||
           dynamic_cast<const FunctionCallNode*>(&astNode)) {
            return this->expressionCodeGenerator->generateExpression(astNode);
        }

        if(auto* insertNode = dynamic_cast<const InsertNode*>(&astNode)) {
            return this->dmlCodeGenerator->generateInsert(*insertNode);
        }
        if(auto* updateNode = dynamic_cast<const UpdateNode*>(&astNode)) {
            return this->dmlCodeGenerator->generateUpdate(*updateNode);
        }
        if(auto* deleteNode = dynamic_cast<const DeleteNode*>(&astNode)) {
            return this->dmlCodeGenerator->generateDelete(*deleteNode);
        }

        if(auto* createTable = dynamic_cast<const CreateTableNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateCreateTable(*createTable);
        }
        if(auto* createTrigger = dynamic_cast<const CreateTriggerNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateCreateTrigger(*createTrigger);
        }
        if(auto* createIndex = dynamic_cast<const CreateIndexNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateCreateIndex(*createIndex);
        }
        if(auto* transactionControl = dynamic_cast<const TransactionControlNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateTransactionControl(*transactionControl);
        }
        if(auto* vacuumStatement = dynamic_cast<const VacuumStatementNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateVacuum(*vacuumStatement);
        }
        if(auto* dropStatement = dynamic_cast<const DropStatementNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateDrop(*dropStatement);
        }
        if(auto* createVirtualTable = dynamic_cast<const CreateVirtualTableNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateCreateVirtualTable(*createVirtualTable);
        }

        if(auto* withQueryNode = dynamic_cast<const WithQueryNode*>(&astNode)) {
            return this->withCodeGenerator->generateWithQuery(*withQueryNode);
        }
        if(auto* compoundSelect = dynamic_cast<const CompoundSelectNode*>(&astNode)) {
            return this->selectCodeGenerator->generateCompoundSelect(*compoundSelect);
        }
        if(auto* selectNode = dynamic_cast<const SelectNode*>(&astNode)) {
            return this->selectCodeGenerator->generateSelect(*selectNode);
        }

        if(auto* pragmaNode = dynamic_cast<const PragmaNode*>(&astNode)) {
            return this->pragmaCodeGenerator->codegenPragmaStatement(*pragmaNode);
        }
        if(auto* createView = dynamic_cast<const CreateViewNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateCreateView(*createView);
        }

        return CodeGenResult{"/* unsupported node */", {}, {}};
    }

    CodeGenResult CodeGenerator::tryCodegenSqliteSelectSubexpression(const SelectNode& selectNode) {
        return this->selectCodeGenerator->tryCodegenSqliteSelectSubexpression(selectNode);
    }

    CodeGenResult CodeGenerator::tryCodegenCompoundSelectSubexpression(const CompoundSelectNode& compoundNode) {
        return this->selectCodeGenerator->tryCodegenCompoundSelectSubexpression(compoundNode);
    }

    CodeGenResult CodeGenerator::tryCodegenSelectLikeSubquery(const AstNode& node) {
        return this->selectCodeGenerator->tryCodegenSelectLikeSubquery(node);
    }

    CodeGenResult CodeGenerator::generateTriggerStep(const AstNode& statement,
                                                      const std::string& subjectTableStruct) {
        return this->dmlCodeGenerator->generateTriggerStep(statement, subjectTableStruct);
    }

    std::string CodeGenerator::codegenOverClause(const OverClause& overClause,
                                                  std::vector<DecisionPoint>& decisionPoints,
                                                  std::vector<std::string>& warnings) {
        return this->expressionCodeGenerator->codegenOverClause(overClause, decisionPoints, warnings);
    }

}  // namespace sqlite2orm
