#include <sqlite2orm/codegen.h>

#include "codegen_context.h"
#include "codegen_utils.h"
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

    namespace {
        std::string escapeForCppStringLiteral(std::string_view text) {
            std::string result;
            for(char character : text) {
                if(character == '\\' || character == '"') {
                    result += '\\';
                }
                result += character;
            }
            return result;
        }

        std::string renderCustomFunctionStruct(const CustomFunctionUse& fn, std::string_view style) {
            std::string params;
            for(size_t i = 0; i < fn.argTypes.size(); ++i) {
                if(i > 0) {
                    params += ", ";
                }
                params += fn.argTypes[i] + " " + fn.argNames[i];
            }
            std::string out = "struct " + fn.structName + " {\n";
            if(style == "aggregate") {
                out += "    // TODO: accumulate one input row at a time\n";
                out += "    void step(" + params + ") {}\n";
                out += "    // TODO: return the aggregate result\n";
                out += "    " + fn.returnType + " fin() const { return {}; }\n";
            } else if(style == "func_only") {
                out += "    // Provided by a loaded SQLite extension — declared for func<>() only, not registered here.\n";
                out += "    " + fn.returnType + " operator()(" + params + ") const;\n";
            } else {
                out += "    // TODO: implement this user-defined scalar function\n";
                out += "    " + fn.returnType + " operator()(" + params + ") const { return {}; }\n";
            }
            out += "    static const char *name() { return \"" + escapeForCppStringLiteral(fn.sqlName) + "\"; }\n";
            out += "};";
            return out;
        }

        std::string customFunctionRegistration(const CustomFunctionUse& fn, std::string_view style) {
            if(style == "aggregate") {
                return "storage.create_aggregate_function<" + fn.structName + ">();";
            }
            if(style == "func_only") {
                return {};
            }
            return "storage.create_scalar_function<" + fn.structName + ">();";
        }

        /** Structs (+ registrations) for every custom function, using `styleFor(fn)` per function. */
        template<class StyleFor>
        std::string customFunctionsPreamble(const std::vector<CustomFunctionUse>& fns, StyleFor styleFor) {
            std::string structs;
            std::string registrations;
            for(const CustomFunctionUse& fn : fns) {
                const std::string style = styleFor(fn);
                structs += renderCustomFunctionStruct(fn, style) + "\n";
                const std::string registration = customFunctionRegistration(fn, style);
                if(!registration.empty()) {
                    registrations += registration + "\n";
                }
            }
            std::string preamble = structs;
            if(!registrations.empty()) {
                preamble += "\n" + registrations;
            }
            return preamble;
        }

        std::string chosenCustomFunctionStyle(const CodeGenPolicy* policy) {
            if(policyEquals(policy, "custom_function_style", "aggregate")) {
                return "aggregate";
            }
            if(policyEquals(policy, "custom_function_style", "func_only")) {
                return "func_only";
            }
            return "scalar";
        }
    }

    void CodeGenerator::injectCustomFunctions(CodeGenResult& result) {
        const auto& fns = this->generatorContext->customFunctions;
        if(fns.empty()) {
            return;
        }
        const CodeGenPolicy* policy = this->generatorContext->codeGenPolicy;
        const std::string chosen = chosenCustomFunctionStyle(policy);
        const std::string statementCode = result.code;

        auto allChosen = [&](const CustomFunctionUse&) {
            return chosen;
        };
        result.code = customFunctionsPreamble(fns, allChosen) + "\n" + statementCode;

        // One decision point per function: scalar (impl) / aggregate (impl) / func_only (extension).
        for(const CustomFunctionUse& fn : fns) {
            auto optionCode = [&](std::string_view style) {
                auto styleFor = [&](const CustomFunctionUse& other) -> std::string {
                    return other.structName == fn.structName ? std::string(style) : chosen;
                };
                return customFunctionsPreamble(fns, styleFor) + "\n" + statementCode;
            };
            std::vector<Option> options = {
                Option{"scalar", optionCode("scalar"),
                       "user-defined scalar function (create_scalar_function + stub body)"},
                Option{"aggregate", optionCode("aggregate"),
                       "user-defined aggregate function (create_aggregate_function + step/fin stubs)"},
                Option{"func_only", optionCode("func_only"),
                       "call only, for a function from a loaded extension (no registration)"},
            };
            result.decisionPoints.push_back(DecisionPoint{this->generatorContext->nextDecisionPointId++,
                                                          "custom_function", chosen, result.code,
                                                          std::move(options)});
        }
    }

    CodeGenResult CodeGenerator::generate(const AstNode& astNode) {
        this->syncToContext();
        this->generatorContext->resetForGeneration();
        auto result = this->generateNode(astNode);
        if(!this->generatorContext->accumulatedErrors.empty()) {
            return CodeGenResult{{}, {}, {}, std::move(this->generatorContext->accumulatedErrors), {}};
        }
        this->injectCustomFunctions(result);
        return result;
    }

    std::string CodeGenerator::generatePrefix() const {
        return this->generatorContext->generatePrefix();
    }

    CreateTableParts CodeGenerator::createTableParts(const CreateTableNode& createTable) {
        this->syncToContext();
        return this->ddlCodeGenerator->createTableParts(createTable);
    }

    CreateViewParts CodeGenerator::createViewParts(const CreateViewNode& node) {
        this->syncToContext();
        return this->ddlCodeGenerator->createViewParts(node);
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
           dynamic_cast<const MatchNode*>(&astNode) ||
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
        if(auto* savepointNode = dynamic_cast<const SavepointNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateSavepoint(*savepointNode);
        }
        if(auto* releaseNode = dynamic_cast<const ReleaseNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateRelease(*releaseNode);
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
