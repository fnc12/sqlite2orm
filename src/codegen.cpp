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

    CodeGenerator::CodeGenerator() :
        generatorContext(std::make_unique<CodeGeneratorContext>()),
        expressionCodeGenerator(std::make_unique<ExpressionCodeGenerator>(*this, *this->generatorContext)),
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
        /** What a statement left out because a placeholder stands inside its code says. */
        constexpr std::string_view kStatementNotGeneratedWarning =
            "a construct in this statement is not mapped to sqlite_orm, so the statement is not generated";

        std::string escapeForCppStringLiteral(std::string_view text) {
            std::string result;
            for (char character: text) {
                if (character == '\\' || character == '"') {
                    result += '\\';
                }
                result += character;
            }
            return result;
        }

        std::string renderCustomFunctionStruct(const CustomFunctionUse& fn, std::string_view style) {
            std::string params;
            for (size_t i = 0; i < fn.argTypes.size(); ++i) {
                if (i > 0) {
                    params += ", ";
                }
                params += fn.argTypes[i] + " " + fn.argNames[i];
            }
            std::string out = "struct " + fn.structName + " {\n";
            if (style == "aggregate") {
                out += "    // TODO: accumulate one input row at a time\n";
                out += "    void step(" + params + ") {}\n";
                out += "    // TODO: return the aggregate result\n";
                out += "    " + fn.returnType + " fin() const { return {}; }\n";
            } else if (style == "func_only") {
                out +=
                    "    // Provided by a loaded SQLite extension — declared for func<>() only, not registered here.\n";
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
            if (style == "aggregate") {
                return "storage.create_aggregate_function<" + fn.structName + ">();";
            }
            if (style == "func_only") {
                return {};
            }
            return "storage.create_scalar_function<" + fn.structName + ">();";
        }

        /** Structs (+ registrations) for every custom function, using `styleFor(fn)` per function. */
        template<class StyleFor>
        std::string customFunctionsPreamble(const std::vector<CustomFunctionUse>& fns, StyleFor styleFor) {
            std::string structs;
            std::string registrations;
            for (const CustomFunctionUse& fn: fns) {
                const std::string style = styleFor(fn);
                structs += renderCustomFunctionStruct(fn, style) + "\n";
                const std::string registration = customFunctionRegistration(fn, style);
                if (!registration.empty()) {
                    registrations += registration + "\n";
                }
            }
            std::string preamble = structs;
            if (!registrations.empty()) {
                preamble += "\n" + registrations;
            }
            return preamble;
        }

        std::string chosenCustomFunctionStyle(const CodeGenPolicy* policy) {
            if (policyEquals(policy, "custom_function_style", "aggregate")) {
                return "aggregate";
            }
            if (policyEquals(policy, "custom_function_style", "func_only")) {
                return "func_only";
            }
            return "scalar";
        }
    }

    void CodeGenerator::injectCustomFunctions(CodeGenResult& result) {
        const auto& fns = this->generatorContext->customFunctions;
        if (fns.empty()) {
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
        for (const CustomFunctionUse& fn: fns) {
            auto optionCode = [&](std::string_view style) {
                auto styleFor = [&](const CustomFunctionUse& other) -> std::string {
                    return other.structName == fn.structName ? std::string(style) : chosen;
                };
                return customFunctionsPreamble(fns, styleFor) + "\n" + statementCode;
            };
            std::vector<Option> options = {
                Option{"scalar",
                       optionCode("scalar"),
                       "user-defined scalar function (create_scalar_function + stub body)"},
                Option{"aggregate",
                       optionCode("aggregate"),
                       "user-defined aggregate function (create_aggregate_function + step/fin stubs)"},
                Option{"func_only",
                       optionCode("func_only"),
                       "call only, for a function from a loaded extension (no registration)"},
            };
            result.decisionPoints.push_back(DecisionPoint{this->generatorContext->nextDecisionPointId++,
                                                          "custom_function",
                                                          chosen,
                                                          result.code,
                                                          std::move(options)});
        }
    }

    CodeGenResult CodeGenerator::generate(const AstNode& astNode) {
        // Marked before anything runs, so the take at the end of the statement erases this
        // generation and nothing below it. `resetForGeneration` empties the log, which makes the
        // mark zero today; taking since it rather than from zero is what keeps that an
        // implementation detail if a generation is ever nested inside another one's mark.
        const size_t commentMark = this->generatorContext->commentMark();
        this->syncToContext();
        this->generatorContext->resetForGeneration();
        // Taken after the reset, which empties the journal: what this statement placeheld is what
        // is recorded from here on.
        const size_t placeholderMark = this->generatorContext->placeholderMark();
        auto result = this->generateNode(astNode);
        if (!this->generatorContext->accumulatedErrors.empty()) {
            return CodeGenResult{{}, {}, {}, std::move(this->generatorContext->accumulatedErrors), {}};
        }
        if (!result.code.empty() && this->generatorContext->placeheldInExpressionSince(placeholderMark)) {
            // A placeholder standing in an expression slot is a comment where C++ expects an
            // expression — `storage.select(as<XAlias>(/* (SELECT ...) */))` is not code anyone can
            // build — so the statement is left out whole instead, the way a statement the pipeline
            // could not carry through already is. The warnings naming the construct and underlining
            // it stay: they are what says a statement was read and not generated.
            this->generatorContext->discardCommentsSince(commentMark);
            CodeGenResult dropped;
            dropped.warnings = std::move(result.warnings);
            dropped.warnings.push_back(std::string(kStatementNotGeneratedWarning));
            return dropped;
        }
        // A comment belongs to the statement whose body the expression was generated in, and this is
        // that statement. `generateNode` has already reported everything recorded since the mark, so
        // the append here adds nothing: the take is here for the erase, which leaves the context
        // clean for the next statement of the batch. `createTableParts` and `createViewParts` take
        // theirs when they run, so a CREATE TABLE and a CREATE VIEW carry them in the result and
        // leave none here either.
        appendUniqueStrings(result.comments, this->generatorContext->takeCommentsSince(commentMark));
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

    CodeGenResult CodeGenerator::generateStoredExpression(const AstNode& astNode) {
        this->generatorContext->storedHexLiteralsTooBig.clear();
        const bool wasStoredExpression = this->generatorContext->storedExpression;
        this->generatorContext->storedExpression = true;
        CodeGenResult result = this->generateNode(astNode);
        this->generatorContext->storedExpression = wasStoredExpression;
        return result;
    }

    CodeGenResult CodeGenerator::generateNode(const AstNode& astNode) {
        const size_t mark = this->generatorContext->commentMark();
        const size_t placeholderMark = this->generatorContext->placeholderMark();
        return this->withRecordedComments(this->dispatchNode(astNode), mark, placeholderMark);
    }

    CodeGenResult CodeGenerator::dispatchNode(const AstNode& astNode) {
        if (dynamic_cast<const IntegerLiteralNode*>(&astNode) || dynamic_cast<const RealLiteralNode*>(&astNode) ||
            dynamic_cast<const StringLiteralNode*>(&astNode) || dynamic_cast<const NullLiteralNode*>(&astNode) ||
            dynamic_cast<const BoolLiteralNode*>(&astNode) || dynamic_cast<const RaiseNode*>(&astNode) ||
            dynamic_cast<const BlobLiteralNode*>(&astNode) ||
            dynamic_cast<const CurrentDatetimeLiteralNode*>(&astNode) || dynamic_cast<const ColumnRefNode*>(&astNode) ||
            dynamic_cast<const QualifiedColumnRefNode*>(&astNode) ||
            dynamic_cast<const QualifiedAsteriskNode*>(&astNode) || dynamic_cast<const NewRefNode*>(&astNode) ||
            dynamic_cast<const OldRefNode*>(&astNode) || dynamic_cast<const ExcludedRefNode*>(&astNode) ||
            dynamic_cast<const BinaryOperatorNode*>(&astNode) || dynamic_cast<const UnaryOperatorNode*>(&astNode) ||
            dynamic_cast<const IsNullNode*>(&astNode) || dynamic_cast<const IsNotNullNode*>(&astNode) ||
            dynamic_cast<const BetweenNode*>(&astNode) || dynamic_cast<const SubqueryNode*>(&astNode) ||
            dynamic_cast<const ExistsNode*>(&astNode) || dynamic_cast<const InNode*>(&astNode) ||
            dynamic_cast<const LikeNode*>(&astNode) || dynamic_cast<const GlobNode*>(&astNode) ||
            dynamic_cast<const MatchNode*>(&astNode) || dynamic_cast<const CastNode*>(&astNode) ||
            dynamic_cast<const CaseNode*>(&astNode) || dynamic_cast<const BindParameterNode*>(&astNode) ||
            dynamic_cast<const CollateNode*>(&astNode) || dynamic_cast<const FunctionCallNode*>(&astNode)) {
            return this->expressionCodeGenerator->generateExpression(astNode);
        }

        if (auto* insertNode = dynamic_cast<const InsertNode*>(&astNode)) {
            return this->dmlCodeGenerator->generateInsert(*insertNode);
        }
        if (auto* updateNode = dynamic_cast<const UpdateNode*>(&astNode)) {
            return this->dmlCodeGenerator->generateUpdate(*updateNode);
        }
        if (auto* deleteNode = dynamic_cast<const DeleteNode*>(&astNode)) {
            return this->dmlCodeGenerator->generateDelete(*deleteNode);
        }

        if (auto* createTable = dynamic_cast<const CreateTableNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateCreateTable(*createTable);
        }
        if (auto* createTrigger = dynamic_cast<const CreateTriggerNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateCreateTrigger(*createTrigger);
        }
        if (auto* createIndex = dynamic_cast<const CreateIndexNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateCreateIndex(*createIndex);
        }
        if (auto* transactionControl = dynamic_cast<const TransactionControlNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateTransactionControl(*transactionControl);
        }
        if (auto* savepointNode = dynamic_cast<const SavepointNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateSavepoint(*savepointNode);
        }
        if (auto* releaseNode = dynamic_cast<const ReleaseNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateRelease(*releaseNode);
        }
        if (auto* vacuumStatement = dynamic_cast<const VacuumStatementNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateVacuum(*vacuumStatement);
        }
        if (auto* dropStatement = dynamic_cast<const DropStatementNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateDrop(*dropStatement);
        }
        if (auto* createVirtualTable = dynamic_cast<const CreateVirtualTableNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateCreateVirtualTable(*createVirtualTable);
        }

        if (auto* withQueryNode = dynamic_cast<const WithQueryNode*>(&astNode)) {
            return this->withCodeGenerator->generateWithQuery(*withQueryNode);
        }
        if (auto* compoundSelect = dynamic_cast<const CompoundSelectNode*>(&astNode)) {
            return this->selectCodeGenerator->generateCompoundSelect(*compoundSelect);
        }
        if (auto* selectNode = dynamic_cast<const SelectNode*>(&astNode)) {
            return this->selectCodeGenerator->generateSelect(*selectNode);
        }

        if (auto* pragmaNode = dynamic_cast<const PragmaNode*>(&astNode)) {
            return this->pragmaCodeGenerator->codegenPragmaStatement(*pragmaNode);
        }
        if (auto* createView = dynamic_cast<const CreateViewNode*>(&astNode)) {
            return this->ddlCodeGenerator->generateCreateView(*createView);
        }

        // ALTER TABLE, ANALYZE, ATTACH, DETACH, EXPLAIN and REINDEX end up here: sqlite_orm has no
        // form for any of them, so the statement generates a placeholder and says so. None of them
        // reaches codegen through `processSql`, though — the validator refuses them first, and the
        // consumer is shown that error instead — so this placeholder and its underline are what a
        // generator asking another for a node sees. The same goes for the derived-FROM and the
        // `IN <table>` placeholders.
        return unsupportedStatementPlaceholder(*this->generatorContext,
                                               "unsupported node",
                                               "this statement is not mapped to sqlite_orm codegen",
                                               astNode);
    }

    CodeGenResult CodeGenerator::tryCodegenSqliteSelectSubexpression(const SelectNode& selectNode) {
        const size_t mark = this->generatorContext->commentMark();
        const size_t placeholderMark = this->generatorContext->placeholderMark();
        return this->withRecordedComments(this->selectCodeGenerator->tryCodegenSqliteSelectSubexpression(selectNode),
                                          mark,
                                          placeholderMark);
    }

    CodeGenResult CodeGenerator::tryCodegenCompoundSelectSubexpression(const CompoundSelectNode& compoundNode) {
        const size_t mark = this->generatorContext->commentMark();
        const size_t placeholderMark = this->generatorContext->placeholderMark();
        return this->withRecordedComments(
            this->selectCodeGenerator->tryCodegenCompoundSelectSubexpression(compoundNode),
            mark,
            placeholderMark);
    }

    CodeGenResult CodeGenerator::tryCodegenSelectLikeSubquery(const AstNode& node) {
        const size_t mark = this->generatorContext->commentMark();
        const size_t placeholderMark = this->generatorContext->placeholderMark();
        return this->withRecordedComments(this->selectCodeGenerator->tryCodegenSelectLikeSubquery(node),
                                          mark,
                                          placeholderMark);
    }

    CodeGenResult CodeGenerator::generateTriggerStep(const AstNode& statement, const std::string& subjectTableStruct) {
        const size_t mark = this->generatorContext->commentMark();
        const size_t placeholderMark = this->generatorContext->placeholderMark();
        return this->withRecordedComments(this->dmlCodeGenerator->generateTriggerStep(statement, subjectTableStruct),
                                          mark,
                                          placeholderMark);
    }

    CodeGenResult CodeGenerator::withRecordedComments(CodeGenResult result, size_t mark, size_t placeholderMark) {
        if (result.code.empty()) {
            // Nothing was generated, so there is no form left for a comment recorded here to
            // explain: a node that answers with no code has its comments dropped along with the
            // code the generation of it threw away. An index sqlite_orm has no form for is the
            // whole statement doing this — it generates its expression, drops the statement and
            // would otherwise explain a `0 - expr` the consumer never gets.
            this->generatorContext->discardCommentsSince(mark);
            this->generatorContext->discardPlaceholdersSince(placeholderMark);
            return result;
        }
        appendUniqueStrings(result.comments, this->generatorContext->commentsRecordedSince(mark));
        return result;
    }

    std::string CodeGenerator::codegenOverClause(const OverClause& overClause,
                                                 std::vector<DecisionPoint>& decisionPoints,
                                                 std::vector<CodegenWarning>& warnings) {
        return this->expressionCodeGenerator->codegenOverClause(overClause, decisionPoints, warnings);
    }

}  // namespace sqlite2orm
