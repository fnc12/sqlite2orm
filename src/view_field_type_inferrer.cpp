#include "view_field_type_inferrer.h"
#include "codegen_context.h"
#include "codegen_forms.h"
#include "codegen_utils.h"
#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    namespace {

        /**
         *  Whether a CASE branch result is an integer literal, under the signs and the COLLATE
         *  SQLite folds into the value it spells.
         */
        bool isIntegerLiteralResult(const AstNode& result) {
            std::size_t foldedSigns = 0;
            const AstNode* value = withoutFoldedSigns(generatedOperandNode(result), foldedSigns);
            return dynamic_cast<const IntegerLiteralNode*>(value) != nullptr;
        }

        /**
         *  The one field type two inferred view-column types are both read through, over the
         *  vocabulary this file infers with — the widening order of `widerInferredCppType`, the
         *  one the `case_<R>` of the same column is typed by, with the `std::vector<char>` a BLOB
         *  comes out as over every other type. A field holds a NULL as soon as either side can
         *  be one.
         */
        InferredFieldType widerViewFieldType(const InferredFieldType& left, const InferredFieldType& right) {
            const bool nullable = left.nullable || right.nullable;
            if (left.cppType == right.cppType) {
                return InferredFieldType{left.cppType, nullable};
            }
            return InferredFieldType{widerInferredCppType(left.cppType, right.cppType), nullable};
        }

    }  // namespace

    ViewFieldTypeInferrer::ViewFieldTypeInferrer(const CodeGeneratorContext& context, const SelectNode& selectNode) :
        context(context), scope(selectNode, context) {}

    const SourceTableColumn* ViewFieldTypeInferrer::resolveQualified(std::string_view tableOrAlias,
                                                                     std::string_view columnName) const {
        return this->scope.findQualified(tableOrAlias, columnName);
    }

    const SourceTableColumn* ViewFieldTypeInferrer::resolveUnqualified(std::string_view columnName) const {
        return this->scope.findUnqualified(columnName);
    }

    std::optional<InferredFieldType>
    ViewFieldTypeInferrer::inferFunctionCall(const FunctionCallNode& functionCall) const {
        const std::string lower = toLowerAscii(functionCall.name);
        auto firstArgument = [&]() -> std::optional<InferredFieldType> {
            if (functionCall.arguments.empty() || !functionCall.arguments.front()) {
                return std::nullopt;
            }
            return this->infer(*functionCall.arguments.front());
        };
        const SqliteOrmFunctionForm* form = sqliteOrmFunctionForm(lower);
        if (form == nullptr) {
            return std::nullopt;
        }
        switch (form->resultType) {
            case FunctionResultType::integer:
                return InferredFieldType{"int"};
            case FunctionResultType::bigInteger:
                return InferredFieldType{"int64_t"};
            case FunctionResultType::real:
                return InferredFieldType{"double"};
            case FunctionResultType::text:
                return InferredFieldType{"std::string"};
            case FunctionResultType::blob:
                return InferredFieldType{"std::vector<char>"};
            case FunctionResultType::unknown:
                return std::nullopt;
            case FunctionResultType::firstArgument:
            case FunctionResultType::secondArgument:
                break;
        }
        // A call generated with its result type spelled out — COALESCE, IFNULL, NULLIF or
        // IIF over arguments with no common C++ type — is read back as exactly that type,
        // so the field follows it rather than the argument the call is otherwise typed
        // as. The two answers live in one `make_view<V>(select(…))` and cannot be allowed
        // to disagree, and the same function answers both. A column argument is resolved
        // here against the FROM clause of the view's own SELECT: that select is generated
        // as a subquery, which leaves the context with none of the scope it named, so
        // asking the context would answer nothing for every column of a view body. That
        // resolution is `SelectScopeColumns`, shared with the one other model standing
        // outside the scope it asks about — a result column deciding about a scalar
        // subquery nested in it.
        auto spelledOr = [&](std::optional<InferredFieldType> deduced) -> std::optional<InferredFieldType> {
            const std::string spelledType = functionCallSpelledResultType(functionCall, this->scope.resolver());
            if (spelledType.empty()) {
                return deduced;
            }
            return InferredFieldType{spelledType, deduced && deduced->nullable};
        };
        if (form->resultType == FunctionResultType::firstArgument) {
            return spelledOr(firstArgument());
        }
        if (functionCall.arguments.size() >= 2 && functionCall.arguments.at(1)) {
            return spelledOr(this->infer(*functionCall.arguments.at(1)));
        }
        return std::nullopt;
    }

    std::optional<InferredFieldType> ViewFieldTypeInferrer::infer(const AstNode& node) const {
        if (auto* columnRef = dynamic_cast<const ColumnRefNode*>(&node)) {
            if (const SourceTableColumn* column = this->resolveUnqualified(columnRef->columnName)) {
                return InferredFieldType{column->cppType, column->nullable};
            }
            return std::nullopt;
        }
        if (auto* qualifiedRef = dynamic_cast<const QualifiedColumnRefNode*>(&node)) {
            if (const SourceTableColumn* column =
                    this->resolveQualified(qualifiedRef->tableName, qualifiedRef->columnName)) {
                return InferredFieldType{column->cppType, column->nullable};
            }
            return std::nullopt;
        }
        if (auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&node)) {
            // An integer literal an int64 cannot hold is a REAL for SQLite, so the field
            // holding it has to be a double as well.
            return InferredFieldType{integerLiteralExceedsInt64(integerLiteral->value) ? "double" : "int64_t"};
        }
        if (dynamic_cast<const RealLiteralNode*>(&node))
            return InferredFieldType{"double"};
        if (dynamic_cast<const StringLiteralNode*>(&node))
            return InferredFieldType{"std::string"};
        if (dynamic_cast<const BoolLiteralNode*>(&node))
            return InferredFieldType{"bool"};
        if (dynamic_cast<const BlobLiteralNode*>(&node))
            return InferredFieldType{"std::vector<char>"};
        if (dynamic_cast<const CurrentDatetimeLiteralNode*>(&node)) {
            return InferredFieldType{"std::string"};
        }
        if (auto* cast = dynamic_cast<const CastNode*>(&node)) {
            return InferredFieldType{castTypeToCpp(cast->typeName)};
        }
        if (auto* collate = dynamic_cast<const CollateNode*>(&node)) {
            if (collate->operand) {
                return this->infer(*collate->operand);
            }
            return std::nullopt;
        }
        if (auto* functionCall = dynamic_cast<const FunctionCallNode*>(&node)) {
            return this->inferFunctionCall(*functionCall);
        }
        if (auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&node)) {
            switch (unaryOperator->unaryOperator) {
                case UnaryOperator::minus:
                case UnaryOperator::plus:
                    if (unaryOperator->operand) {
                        return this->infer(*unaryOperator->operand);
                    }
                    return std::nullopt;
                case UnaryOperator::bitwiseNot:
                    return InferredFieldType{"int64_t"};
                case UnaryOperator::logicalNot:
                    return InferredFieldType{"bool"};
            }
            return std::nullopt;
        }
        if (auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&node)) {
            switch (binaryOperator->binaryOperator) {
                case BinaryOperator::concatenate:
                case BinaryOperator::jsonArrow:
                case BinaryOperator::jsonArrow2:
                    return InferredFieldType{"std::string"};
                case BinaryOperator::add:
                case BinaryOperator::subtract:
                case BinaryOperator::multiply:
                case BinaryOperator::divide:
                case BinaryOperator::modulo: {
                    auto lhsType = binaryOperator->lhs ? this->infer(*binaryOperator->lhs) : std::nullopt;
                    auto rhsType = binaryOperator->rhs ? this->infer(*binaryOperator->rhs) : std::nullopt;
                    if ((lhsType && lhsType->cppType == "double") || (rhsType && rhsType->cppType == "double")) {
                        return InferredFieldType{"double"};
                    }
                    if (lhsType || rhsType) {
                        return InferredFieldType{"int64_t"};
                    }
                    return std::nullopt;
                }
                case BinaryOperator::bitwiseAnd:
                case BinaryOperator::bitwiseOr:
                case BinaryOperator::shiftLeft:
                case BinaryOperator::shiftRight:
                    return InferredFieldType{"int64_t"};
                default:
                    return InferredFieldType{"bool"};
            }
        }
        if (dynamic_cast<const IsNullNode*>(&node) || dynamic_cast<const IsNotNullNode*>(&node) ||
            dynamic_cast<const BetweenNode*>(&node) || dynamic_cast<const InNode*>(&node) ||
            dynamic_cast<const ExistsNode*>(&node) || dynamic_cast<const LikeNode*>(&node) ||
            dynamic_cast<const GlobNode*>(&node) || dynamic_cast<const MatchNode*>(&node)) {
            return InferredFieldType{"bool"};
        }
        if (auto* caseNode = dynamic_cast<const CaseNode*>(&node)) {
            // A CASE has no type of its own in SQLite: it answers with the value of
            // whichever branch matched, and every row of the column reaches the field
            // through the one type declared for it. Taken from the first branch alone, the
            // field truncated every wider branch — and it disagreed with the `case_<R>`
            // generated for that very column, which widens over all of them, so a column
            // read as text landed in an `int64_t` field of the same `make_view`. The
            // operand of a simple CASE is compared against rather than answered with, so
            // it is not one of the values either.
            std::optional<InferredFieldType> widest;
            // A CASE no branch matches and no ELSE answers is NULL — `CASE WHEN 0 THEN 1
            // END` is one — and so is a branch that spells a NULL out. Such a branch says
            // what the field holds without naming a type of its own.
            bool nullable = !caseNode->elseResult;
            std::vector<const AstNode*> results;
            results.reserve(caseNode->branches.size() + 1);
            for (const CaseBranch& branch: caseNode->branches) {
                results.push_back(branch.result.get());
            }
            if (caseNode->elseResult) {
                results.push_back(caseNode->elseResult.get());
            }
            for (const AstNode* result: results) {
                if (!result) {
                    return std::nullopt;
                }
                if (dynamic_cast<const NullLiteralNode*>(result)) {
                    nullable = true;
                    continue;
                }
                std::optional<InferredFieldType> inferred = this->infer(*result);
                if (!inferred) {
                    return std::nullopt;
                }
                if (inferred->cppType == "int64_t" && isIntegerLiteralResult(*result)) {
                    // An integer literal is a value, and the fold asks how wide the values
                    // beside it are: `CASE WHEN a < 0 THEN 1 ELSE 1.5 END` is answered by
                    // the `double` that holds both 1 and 1.5 exactly, and only a literal
                    // past the int32 range makes the branch one a `double` may drop. So a
                    // literal contributes the width the expression side reads it with —
                    // the very rule the `case_<R>` of this column is typed by, which keeps
                    // the two answers from parting over a value both of them can see. A
                    // view column that is nothing but a literal keeps the `int64_t` of an
                    // integer column, the way it always has: nothing stands beside it to
                    // compare widths with.
                    inferred->cppType = this->context.inferTypeFromNode(*result);
                }
                widest = widest ? widerViewFieldType(*widest, *inferred) : *inferred;
            }
            if (!widest) {
                // Every branch spells a NULL out, so there is no value to name a type
                // after: `CASE WHEN a THEN NULL END` leaves the column uninferred.
                return std::nullopt;
            }
            widest->nullable = widest->nullable || nullable;
            return widest;
        }
        return std::nullopt;
    }

}  // namespace sqlite2orm
