#include "codegen_utils.h"
#include "codegen_literal_digits.h"
#include "codegen_context.h"
#include "codegen_forms.h"

#include <sqlite2orm/utils.h>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <utility>

namespace sqlite2orm {

    namespace {
        /**
         *  Whether the call generates one of the types sqlite_orm keeps out of its operand traits:
         *  a window function, each generated as an aggregate of its own, and a MATCH in its
         *  function spelling, generated as `match_t`. A `filter()` and an `over()` wrap whatever
         *  they are called on into a `filtered_aggregate_function_t` and an `over_t`, which are
         *  out of them too.
         */
        bool functionCallGeneratesUnrecognizedOperand(const FunctionCallNode& functionCall) {
            if (functionCall.over != nullptr || functionCall.filterWhere != nullptr) {
                return true;
            }
            const SqliteOrmFunctionForm* form = sqliteOrmFunctionForm(toLowerAscii(functionCall.name));
            return form != nullptr &&
                   (form->kind == SqliteOrmFormKind::windowFunction || form->kind == SqliteOrmFormKind::matchFunction);
        }

        /**
         *  Whether the call generates a built-in function the way the headers spell it on every
         *  compiler. sqlite_orm declares the built-ins twice: under `SQLITE_ORM_WITH_CPP20_ALIASES`
         *  — consteval, concepts and class-type template arguments, which g++ has and Apple clang
         *  lacks `consteval` for — a call builds a `builtin_function_call`, and everywhere else it
         *  builds the legacy `builtin_function_t` / `built_in_aggregate_function_t`. Only the
         *  former is an operator argument; the legacy ones are arithmetic operands and nothing
         *  more. A `func<…>()` call, a `count(*)` and the argument-less `count()` are the same on
         *  both paths and are operator arguments on both.
         */
        bool functionCallGeneratesBuiltinFunction(const FunctionCallNode& functionCall) {
            const std::optional<SqliteOrmFunctionForm> form = resolveFunctionCallForm(functionCall, true);
            return form && (form->kind == SqliteOrmFormKind::builtinScalar ||
                            form->kind == SqliteOrmFormKind::builtinAggregate);
        }
    }

    std::string_view functionCallResultTypeArgument(std::string_view lowerFunctionName) {
        if (lowerFunctionName == "json_extract" || lowerFunctionName == "json_quote") {
            return "<std::string>";
        }
        return {};
    }

    bool isNumericLiteral(const AstNode& astNode) {
        return dynamic_cast<const IntegerLiteralNode*>(&astNode) || dynamic_cast<const RealLiteralNode*>(&astNode);
    }

    bool numericLiteralRejectsFoldedSign(const AstNode& astNode) {
        auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&astNode);
        return integerLiteral && hexLiteralIsInt64Min(integerLiteral->value);
    }

    std::string_view sqlPredicateLooserThanMinus(const AstNode& astNode) {
        // sqlite_orm parenthesizes the operands of a binary operator it serializes, and everything
        // else an expression can be — a literal, a column, a function call, CAST, CASE, a subquery,
        // `~x` — is a term SQLite reads as one unit. These predicates are the exception: they come
        // out bare and bind looser than `-`, so `0 - a BETWEEN 1 AND 9` would read as
        // `(0 - a) BETWEEN 1 AND 9` rather than as the negation it stands for. A COLLATE over one
        // changes nothing here: it is dropped, and the predicate is what the operand generates.
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if (dynamic_cast<const InNode*>(&generatedNode)) {
            return "IN";
        }
        if (dynamic_cast<const BetweenNode*>(&generatedNode)) {
            return "BETWEEN";
        }
        if (dynamic_cast<const LikeNode*>(&generatedNode)) {
            return "LIKE";
        }
        if (dynamic_cast<const GlobNode*>(&generatedNode)) {
            return "GLOB";
        }
        if (dynamic_cast<const MatchNode*>(&generatedNode)) {
            return "MATCH";
        }
        if (dynamic_cast<const IsNullNode*>(&generatedNode)) {
            return "IS NULL";
        }
        if (dynamic_cast<const IsNotNullNode*>(&generatedNode)) {
            return "IS NOT NULL";
        }
        if (auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
            if (unaryOp->unaryOperator == UnaryOperator::logicalNot) {
                return "NOT";
            }
        }
        return {};
    }

    bool generatesSqliteOrmCondition(const AstNode& astNode) {
        // A COLLATE and a unary plus generate their operand and nothing else, so the sqlite_orm
        // node standing here is the one the operand generates.
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if (auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
            switch (binaryOperator->binaryOperator) {
                case BinaryOperator::equals:
                case BinaryOperator::notEquals:
                case BinaryOperator::lessThan:
                case BinaryOperator::lessOrEqual:
                case BinaryOperator::greaterThan:
                case BinaryOperator::greaterOrEqual:
                case BinaryOperator::isOp:
                case BinaryOperator::isNot:
                case BinaryOperator::isDistinctFrom:
                case BinaryOperator::isNotDistinctFrom:
                case BinaryOperator::logicalAnd:
                    // A comparison and an IS are a `binary_condition`, `&&` an `and_condition_t`.
                    return true;
                case BinaryOperator::logicalOr:
                    // An OR is an `or_condition_t` either way: `or_()` builds one whatever its operands
                    // are, and the `or` spelling is only generated when one of them is a condition.
                    return true;
                default:
                    // The arithmetic, bitwise and concatenation operators build a `binary_operator`,
                    // and the JSON arrows a `json_extract()` call.
                    return false;
            }
        }
        if (auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
            // `not x` is a `negated_condition_t`; a negation generates a subtraction from zero and
            // `~x` a `bitwise_not_t`, neither of them a condition.
            return unaryOperator->unaryOperator == UnaryOperator::logicalNot;
        }
        // MATCH is the one predicate missing here on purpose: `match_t` derives from nothing, so
        // sqlite_orm does not count it as a condition either.
        return dynamic_cast<const InNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const BetweenNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const LikeNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const GlobNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const IsNullNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const IsNotNullNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const ExistsNode*>(&generatedNode) != nullptr;
    }

    bool generatesSqliteOrmOperatorArgument(const AstNode& astNode) {
        // A COLLATE and a unary plus generate their operand and nothing else, so the sqlite_orm
        // node standing here is the one the operand generates.
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if (auto* functionCall = dynamic_cast<const FunctionCallNode*>(&generatedNode)) {
            // A built-in is one only where the headers take their C++20 path, and the generated
            // code has to compile on the compilers that do not.
            return !functionCallGeneratesUnrecognizedOperand(*functionCall) &&
                   !functionCallGeneratesBuiltinFunction(*functionCall);
        }
        // The JSON arrows are generated as a `json_extract()` call, a built-in function like any
        // other, and every other operator builds a `binary_operator` or a `binary_condition`.
        return dynamic_cast<const CastNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const CaseNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const NewRefNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const OldRefNode*>(&generatedNode) != nullptr ||
               dynamic_cast<const ExcludedRefNode*>(&generatedNode) != nullptr;
    }

    bool generatesSqliteOrmOperandOrBindable(const AstNode& astNode) {
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if (auto* functionCall = dynamic_cast<const FunctionCallNode*>(&generatedNode)) {
            return !functionCallGeneratesUnrecognizedOperand(*functionCall);
        }
        // `match_t` derives from nothing, and the CURRENT_* literals are generated as
        // `current_date()` / `current_time()` / `current_timestamp()`, types of their own that no
        // operand trait names. Everything else the expression generator emits is a member
        // pointer, a bindable value, an arithmetic or bitwise operator, a concatenation, a
        // condition, an operator argument, a scalar subquery or a compound operator.
        return dynamic_cast<const MatchNode*>(&generatedNode) == nullptr &&
               dynamic_cast<const CurrentDatetimeLiteralNode*>(&generatedNode) == nullptr;
    }

    bool binaryOperatorNeedsCallSpelling(const BinaryOperatorNode& binaryOperatorNode) {
        switch (binaryOperatorNode.binaryOperator) {
            case BinaryOperator::logicalOr:
                // `operator||` builds the `or_condition_t` only when one of its operands is a condition.
                if (!generatesSqliteOrmCondition(*binaryOperatorNode.lhs) &&
                    !generatesSqliteOrmCondition(*binaryOperatorNode.rhs)) {
                    return true;
                }
                break;
            case BinaryOperator::concatenate: {
                // …and the `conc_t` only when neither of them is. `||` binds tighter than every other
                // SQL operator, so a predicate operand is delimited with a CAST already — it is the
                // grouping the serialized SQL needs — and a `cast_t` is not a condition any more. What
                // is left is the conditions sqlite_orm serializes as one term of their own: the
                // comparisons, AND, OR and EXISTS.
                auto operandStaysCondition = [](const AstNode& operand) {
                    return generatesSqliteOrmCondition(operand) &&
                           serializedSqlPrecedenceAsBinaryOperand(operand) == kSqlPrecedenceTerm;
                };
                if (operandStaysCondition(*binaryOperatorNode.lhs) || operandStaysCondition(*binaryOperatorNode.rhs)) {
                    return true;
                }
                break;
            }
            default:
                return false;
        }
        // An operand spelled as a call of the same operator takes the rest of the chain with it, so
        // that one chain is spelled one way throughout: `1 OR 0 OR a = 1` comes out as
        // `or_(or_(1, 0), c(&User::a) == 1)` rather than as `or_(1, 0) or c(&User::a) == 1`.
        auto operandNeedsCallSpelling = [&](const AstNode& operand) {
            auto* binaryOperand = dynamic_cast<const BinaryOperatorNode*>(&generatedOperandNode(operand));
            return binaryOperand != nullptr && binaryOperand->binaryOperator == binaryOperatorNode.binaryOperator &&
                   binaryOperatorNeedsCallSpelling(*binaryOperand);
        };
        return operandNeedsCallSpelling(*binaryOperatorNode.lhs) || operandNeedsCallSpelling(*binaryOperatorNode.rhs);
    }

    NegationForm negationFormFor(const AstNode& operandAsWritten) {
        // SQLite's parser folds the sign into the literal the minus stands directly over — through
        // parentheses and through a unary plus, but NOT through a COLLATE: `-(0x8000000000000000)`
        // and `-+0x8000000000000000` are both the `hex literal too big` it refuses, while
        // `-(0x8000000000000000 COLLATE BINARY)` is a negation it computes (9.22337203685478e+18,
        // checked against sqlite3 3.51). So the literal a sign is folded into is the operand with
        // the pluses taken off, and a COLLATE over one keeps the subtraction form.
        const AstNode& operand = withoutUnaryPluses(operandAsWritten);
        if (isNumericLiteral(operand)) {
            return numericLiteralRejectsFoldedSign(operand) ? NegationForm::zeroMinusSubtraction
                                                            : NegationForm::foldedIntoConstant;
        }
        if (generatesFoldedNegation(operand)) {
            // The operand is itself a constant with a sign already folded in, so C++ folds this
            // sign too: `- - -3` is the constant `-(-(-3))`.
            return NegationForm::foldedIntoConstant;
        }
        return sqlPredicateLooserThanMinus(operand).empty() ? NegationForm::zeroMinusSubtraction
                                                            : NegationForm::unaryOverPredicate;
    }

    namespace {
        /** The form a node's own negation takes, for a node that is not a negation at all. */
        std::optional<NegationForm> formOfNegationNode(const AstNode& astNode) {
            auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&generatedOperandNode(astNode));
            if (!unaryOp || unaryOp->unaryOperator != UnaryOperator::minus) {
                return std::nullopt;
            }
            return negationFormFor(*unaryOp->operand);
        }
    }

    bool generatesFoldedNegation(const AstNode& astNode) {
        return formOfNegationNode(astNode) == NegationForm::foldedIntoConstant;
    }

    bool generatesZeroMinusSubtraction(const AstNode& astNode) {
        return formOfNegationNode(astNode) == NegationForm::zeroMinusSubtraction;
    }

    bool generatesBoundValue(const AstNode& astNode) {
        const AstNode& generatedNode = generatedOperandNode(astNode);
        return generatesFoldedNegation(generatedNode) || dynamic_cast<const IntegerLiteralNode*>(&generatedNode) ||
               dynamic_cast<const RealLiteralNode*>(&generatedNode) ||
               dynamic_cast<const StringLiteralNode*>(&generatedNode) ||
               dynamic_cast<const NullLiteralNode*>(&generatedNode) ||
               dynamic_cast<const BoolLiteralNode*>(&generatedNode) ||
               dynamic_cast<const BlobLiteralNode*>(&generatedNode);
    }

    namespace {

        /** The C++ type `integerLiteralToCpp` spells an integer literal with. */
        GeneratedValueCppType integerLiteralCppType(std::string_view integerLiteral) {
            if (isHexadecimalIntegerLiteral(integerLiteral)) {
                // The two spans C++ would make unsigned are spelled `static_cast<int64_t>(…)`.
                // Of the rest, everything past `0xFFFFFFFF` — nine significant digits on — is a
                // `long` there, and an eight-digit literal that stayed signed fits an `int`.
                if (hexLiteralIsUnsignedInCpp(integerLiteral)) {
                    return GeneratedValueCppType::integer64Cast;
                }
                return significantDigits(integerLiteral.substr(2)).size() <= 8
                           ? GeneratedValueCppType::integer32
                           : GeneratedValueCppType::integer64Literal;
            }
            const std::string digits = significantDigits(integerLiteral);
            // A decimal literal past the int64 range is a REAL for SQLite and is spelled with a
            // `.0`, so it is a `double` here as well.
            if (integerLiteralExceedsInt64(digits)) {
                return GeneratedValueCppType::real;
            }
            return integerLiteralExceedsInt32(digits) ? GeneratedValueCppType::integer64Literal
                                                      : GeneratedValueCppType::integer32;
        }

        bool isIntegerCppType(GeneratedValueCppType type) {
            // A `bool` is one of these: SQLite's TRUE is the integer 1, and the cast that widens
            // the constant carries that value unchanged.
            return type == GeneratedValueCppType::integer32 || type == GeneratedValueCppType::integer64Literal ||
                   type == GeneratedValueCppType::integer64Cast || type == GeneratedValueCppType::boolean;
        }

        /** True for a node generated as a `bindParamN` variable, whose type the caller declares. */
        bool generatesBindParameter(const AstNode& astNode) {
            return dynamic_cast<const BindParameterNode*>(&generatedOperandNode(astNode)) != nullptr;
        }

        /** True for a node generated as a pointer to a member — `&User::a`, or the `column<T>()` of it. */
        bool generatesColumnPointer(const AstNode& astNode) {
            const AstNode& generatedNode = generatedOperandNode(astNode);
            return dynamic_cast<const ColumnRefNode*>(&generatedNode) ||
                   dynamic_cast<const QualifiedColumnRefNode*>(&generatedNode) ||
                   dynamic_cast<const NewRefNode*>(&generatedNode) || dynamic_cast<const OldRefNode*>(&generatedNode) ||
                   dynamic_cast<const ExcludedRefNode*>(&generatedNode);
        }

        /**
         *  Everything `oneDeducedTypeForm` reads off one member of a group, and nothing that tells
         *  two members it reads the same way apart. Two members of one kind are interchangeable in
         *  every group either of them belongs to.
         */
        struct OneTypeGroupMemberKind {
            std::optional<GeneratedValueCppType> type;
            bool bindParameter;
            bool columnPointer;

            bool operator==(const OneTypeGroupMemberKind&) const = default;
        };

        OneTypeGroupMemberKind oneTypeGroupMemberKind(const AstNode& node) {
            return {generatedValueCppType(node), generatesBindParameter(node), generatesColumnPointer(node)};
        }

    }  // namespace

    std::optional<GeneratedValueCppType> generatedValueCppType(const AstNode& astNode) {
        if (!generatesBoundValue(astNode)) {
            return std::nullopt;
        }
        // A sign SQLite folds into a literal is spelled in front of the C++ constant and leaves
        // the type of that constant alone: `-2147483648` is the 64-bit `2147483648` negated,
        // not an `int`, so the width follows the magnitude the literal is written with.
        std::size_t foldedSigns = 0;
        const AstNode* value = withoutFoldedSigns(generatedOperandNode(astNode), foldedSigns);
        if (auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(value)) {
            return integerLiteralCppType(integerLiteral->value);
        }
        if (dynamic_cast<const RealLiteralNode*>(value)) {
            return GeneratedValueCppType::real;
        }
        if (dynamic_cast<const StringLiteralNode*>(value)) {
            return GeneratedValueCppType::text;
        }
        if (dynamic_cast<const BoolLiteralNode*>(value)) {
            return GeneratedValueCppType::boolean;
        }
        if (dynamic_cast<const BlobLiteralNode*>(value)) {
            return GeneratedValueCppType::blob;
        }
        if (dynamic_cast<const NullLiteralNode*>(value)) {
            return GeneratedValueCppType::null;
        }
        return std::nullopt;
    }

    std::optional<std::string> unboundLiteralCode(const AstNode& astNode, const std::string& code) {
        std::string_view cppType;
        if (dynamic_cast<const IntegerLiteralNode*>(&astNode) &&
            generatedValueCppType(astNode) == GeneratedValueCppType::integer32) {
            cppType = "int";
        } else if (dynamic_cast<const BoolLiteralNode*>(&astNode)) {
            cppType = "bool";
        } else {
            return std::nullopt;
        }
        return "c(internal::literal_holder<" + std::string(cppType) + ">{" + code + "})";
    }

    OneDeducedTypeForm oneDeducedTypeForm(const std::vector<const AstNode*>& nodes) {
        bool everyTypedNodeInteger = true;
        bool everyTypedNodeOneType = true;
        std::optional<GeneratedValueCppType> firstType;
        bool bindParameterSeen = false;
        bool columnPointerSeen = false;
        bool expressionNodeSeen = false;
        for (const AstNode* node: nodes) {
            const std::optional<GeneratedValueCppType> type = generatedValueCppType(*node);
            if (type) {
                if (!firstType) {
                    firstType = type;
                } else if (*firstType != *type) {
                    everyTypedNodeOneType = false;
                }
                if (!isIntegerCppType(*type)) {
                    everyTypedNodeInteger = false;
                }
            } else if (generatesBindParameter(*node)) {
                // The bind parameter is typed by the caller, who declares `bindParamN` himself, so
                // nothing said about the other members rules its type out.
                bindParameterSeen = true;
            } else if (generatesColumnPointer(*node)) {
                columnPointerSeen = true;
            } else {
                expressionNodeSeen = true;
            }
        }
        // A member that names something sqlite_orm serializes — a column pointer, an expression
        // node — shares a type with no constant beside it.
        if (firstType && (columnPointerSeen || expressionNodeSeen)) {
            return OneDeducedTypeForm::noCommonType;
        }
        if (!everyTypedNodeOneType) {
            // Integer constants of different types are the one case with a type to widen to:
            // every INTEGER SQLite carries fits an int64, and a value bound as one is the value
            // bound as an `int` or as a `bool` — same storage class, same number. What is left
            // unwidened beside them is a bind parameter, whose type this does not decide.
            return everyTypedNodeInteger ? OneDeducedTypeForm::widenedToInt64 : OneDeducedTypeForm::noCommonType;
        }
        if (firstType || bindParameterSeen) {
            return OneDeducedTypeForm::asWritten;
        }
        // Nothing here is typed. A pointer to a member is never one of the node types sqlite_orm
        // builds from an expression, so those two never meet. Members of one of those kinds, on
        // the other hand, are typed by the schema and by what they are built over, and neither is
        // known here — `abs(&User::a)` and `abs(&User::b)` are the same type when the two columns
        // are.
        return columnPointerSeen && expressionNodeSeen ? OneDeducedTypeForm::noCommonType
                                                       : OneDeducedTypeForm::asWritten;
    }

    OneDeducedTypeForm inValuesForm(const std::vector<const AstNode*>& values) {
        const OneDeducedTypeForm form = oneDeducedTypeForm(values);
        if (form != OneDeducedTypeForm::asWritten) {
            return form;
        }
        // A value this cannot type is a bind parameter, and only a bind parameter: had the list
        // held a column pointer or an expression node beside a typed value, the gate above would
        // have answered noCommonType already. A bind parameter no more rules the `std::vector<bool>`
        // out than it rules the widening of two integer widths out — `a IN (TRUE, ?)` as written
        // has no working declaration at all (`bool bindParam1` is the vector, `int64_t bindParam1`
        // is a second type), while the widened list meets the `int64_t` the caller declares.
        bool booleanValueSeen = false;
        for (const AstNode* value: values) {
            const std::optional<GeneratedValueCppType> type = generatedValueCppType(*value);
            if (!type) {
                continue;
            }
            if (*type != GeneratedValueCppType::boolean) {
                return form;
            }
            booleanValueSeen = true;
        }
        return booleanValueSeen ? OneDeducedTypeForm::widenedToInt64 : form;
    }

    SpannedCode widenToInt64(const AstNode& node, SpannedCode code) {
        const std::optional<GeneratedValueCppType> type = generatedValueCppType(node);
        // A member this cannot type is a bind parameter the caller declares himself, and a cast
        // over one would change the value it binds rather than only its type — `bindParam1` held
        // as a `double` would reach SQLite truncated. It is left as written, and the widening of
        // the constants beside it is what a declaration of `int64_t` meets.
        if (!type || *type == GeneratedValueCppType::integer64Cast) {
            return code;
        }
        return "static_cast<int64_t>(" + code + ")";
    }

    std::pair<const AstNode*, const AstNode*> firstNoCommonTypePair(const std::vector<const AstNode*>& nodes) {
        // An IN list holds as many values as a statement is long, and a value that meets every
        // other one in a type — a bind parameter above all — is passed over by the search rather
        // than ending it, so a look at every pair of them is a walk over the whole list per value.
        // Members of one kind stand in for each other: were a third member of a kind half of the
        // pair, the first member of that kind would be half of a pair with the same partner and an
        // earlier one, which is the pair this answers instead. So the first two members of each
        // kind hold every pair there is to find, and a group has as many kinds as there are C++
        // types here — the search below is over a handful of members whatever the list's length.
        std::vector<std::pair<OneTypeGroupMemberKind, int>> memberCountByKind;
        std::vector<const AstNode*> candidates;
        for (const AstNode* node: nodes) {
            const OneTypeGroupMemberKind kind = oneTypeGroupMemberKind(*node);
            const auto counted =
                std::find_if(memberCountByKind.begin(), memberCountByKind.end(), [&kind](const auto& countedKind) {
                    return countedKind.first == kind;
                });
            if (counted == memberCountByKind.end()) {
                memberCountByKind.emplace_back(kind, 1);
            } else if (counted->second == 2) {
                continue;
            } else {
                ++counted->second;
            }
            candidates.push_back(node);
        }
        for (size_t firstIndex = 0; firstIndex + 1 < candidates.size(); ++firstIndex) {
            for (size_t secondIndex = firstIndex + 1; secondIndex < candidates.size(); ++secondIndex) {
                if (oneDeducedTypeForm({candidates[firstIndex], candidates[secondIndex]}) ==
                    OneDeducedTypeForm::noCommonType) {
                    return {candidates[firstIndex], candidates[secondIndex]};
                }
            }
        }
        return {nullptr, nullptr};
    }

    std::string generatedValueTypeDescription(const AstNode& node) {
        const std::optional<GeneratedValueCppType> type = generatedValueCppType(node);
        if (!type) {
            return generatesColumnPointer(node) ? "a pointer to a column" : "a sqlite_orm expression";
        }
        switch (*type) {
            case GeneratedValueCppType::integer32:
                return "an `int`";
            case GeneratedValueCppType::integer64Literal:
                return "a 64-bit integer constant";
            case GeneratedValueCppType::integer64Cast:
                return "an `int64_t`";
            case GeneratedValueCppType::boolean:
                return "a `bool`";
            case GeneratedValueCppType::real:
                return "a `double`";
            case GeneratedValueCppType::text:
                return "a `const char*`";
            case GeneratedValueCppType::blob:
                return "a `std::vector<char>`";
            case GeneratedValueCppType::null:
                return "a `std::nullptr_t`";
        }
        return "a sqlite_orm expression";
    }

    namespace {

        /**
         *  The kind of C++ type a call argument is read back as, at the granularity
         *  `std::common_type` reduces over. Every arithmetic type converts to every other one; a
         *  `std::string` converts to a `std::nullptr_t` and back, its `const char*` constructor
         *  taking one; a `std::vector<char>` converts to nothing else at all. Checked against the
         *  pinned sqlite_orm headers over every pair of constants and field types codegen writes.
         */
        enum class ArgumentTypeKind { arithmetic, text, blob, null };

        /** The C++ type the generated code hands sqlite_orm for one argument of a call. */
        struct ArgumentCppType {
            ArgumentTypeKind kind = ArgumentTypeKind::arithmetic;
            /**
             *  The type a nullable column's field holds inside its `std::optional<…>`, and empty
             *  for every other argument. Two `std::optional`s of different types have no common
             *  type — each converts to the other, so neither is the common one — while an
             *  `std::optional` next to a plain type has the `std::optional` for a common type.
             */
            std::string optionalFieldType;
            /**
             *  The arithmetic C++ type the value has — `bool`, `int`, `int64_t` or `double`, the
             *  inner type where the argument is a nullable column — and empty for a text, a BLOB
             *  or a NULL. Arithmetic types reduce to one whatever wrapper they arrive in, so this
             *  is what a call whose arguments have no common type can still be read back as.
             */
            std::string arithmeticType;
            /** How a warning names this type, e.g. "an `int`". */
            std::string description;
        };

        /**
         *  The kind of a C++ type a generated struct declares a field as. The spellings answered
         *  for are the closed set `sqliteTypeToCpp` and `CodeGeneratorContext::inferTypeFromNode`
         *  produce; any other one is a type this file does not know and says nothing about.
         */
        std::optional<ArgumentTypeKind> cppFieldTypeKind(std::string_view cppType) {
            if (cppType == "bool" || cppType == "int" || cppType == "int64_t" || cppType == "double") {
                return ArgumentTypeKind::arithmetic;
            }
            if (cppType == "std::string") {
                return ArgumentTypeKind::text;
            }
            if (cppType == "std::vector<char>") {
                return ArgumentTypeKind::blob;
            }
            return std::nullopt;
        }

        /** The arithmetic C++ type a generated constant holds, and empty for every other one. */
        std::string_view constantArithmeticType(GeneratedValueCppType valueType) {
            switch (valueType) {
                case GeneratedValueCppType::integer32:
                    return "int";
                case GeneratedValueCppType::integer64Literal:
                case GeneratedValueCppType::integer64Cast:
                    return "int64_t";
                case GeneratedValueCppType::boolean:
                    return "bool";
                case GeneratedValueCppType::real:
                    return "double";
                case GeneratedValueCppType::text:
                case GeneratedValueCppType::blob:
                case GeneratedValueCppType::null:
                    return {};
            }
            return {};
        }

        std::optional<ArgumentTypeKind> constantValueKind(GeneratedValueCppType valueType) {
            switch (valueType) {
                case GeneratedValueCppType::integer32:
                case GeneratedValueCppType::integer64Literal:
                case GeneratedValueCppType::integer64Cast:
                case GeneratedValueCppType::boolean:
                case GeneratedValueCppType::real:
                    return ArgumentTypeKind::arithmetic;
                case GeneratedValueCppType::text:
                    return ArgumentTypeKind::text;
                case GeneratedValueCppType::blob:
                    return ArgumentTypeKind::blob;
                case GeneratedValueCppType::null:
                    return ArgumentTypeKind::null;
            }
            return std::nullopt;
        }

        /**
         *  The C++ type a call argument is generated as, for the two kinds of argument the
         *  generated code names the type of: a constant, spelled out in the call itself, and a
         *  column, read back as the field the generated struct declares for it. Every other
         *  argument — a nested call, an operator, a bind parameter, a subquery — is typed by
         *  sqlite_orm out of what it is built over, which is not answered here, and a call holding
         *  one is left as it is generated.
         */
        std::optional<ArgumentCppType> generatedArgumentCppType(const AstNode& argument,
                                                                const ReferencedColumnResolver& resolveColumn) {
            if (const std::optional<GeneratedValueCppType> valueType = generatedValueCppType(argument)) {
                const std::optional<ArgumentTypeKind> kind = constantValueKind(*valueType);
                if (!kind) {
                    return std::nullopt;
                }
                return ArgumentCppType{*kind,
                                       {},
                                       std::string(constantArithmeticType(*valueType)),
                                       generatedValueTypeDescription(argument)};
            }
            const SourceTableColumn* column = resolveColumn(argument);
            if (!column) {
                return std::nullopt;
            }
            const std::optional<ArgumentTypeKind> kind = cppFieldTypeKind(column->cppType);
            if (!kind) {
                return std::nullopt;
            }
            const std::string fieldType = column->nullable ? "std::optional<" + column->cppType + ">" : column->cppType;
            return ArgumentCppType{*kind,
                                   column->nullable ? column->cppType : std::string(),
                                   *kind == ArgumentTypeKind::arithmetic ? column->cppType : std::string(),
                                   "a column typed `" + fieldType + "`"};
        }

        /** Whether `std::common_type` reduces these two argument types to one. */
        bool argumentTypesHaveACommonType(const ArgumentCppType& first, const ArgumentCppType& second) {
            if (first.kind != second.kind) {
                // A `std::string` is constructible from a `std::nullptr_t` through its
                // `const char*` constructor, which makes the two one type; nothing else here
                // converts across kinds.
                return (first.kind == ArgumentTypeKind::text && second.kind == ArgumentTypeKind::null) ||
                       (first.kind == ArgumentTypeKind::null && second.kind == ArgumentTypeKind::text);
            }
            if (first.optionalFieldType.empty() || second.optionalFieldType.empty() ||
                first.optionalFieldType == second.optionalFieldType) {
                return true;
            }
            // Two `std::optional`s of different types convert to each other both ways, which
            // leaves neither of them the common type — except where one is an
            // `std::optional<bool>`: a `bool` is constructible from any `std::optional` through
            // its explicit `operator bool`, and that rules the converting constructor INTO the
            // `std::optional<bool>` out, leaving the one direction. Checked against the pinned
            // headers: `coalesce(std::optional<int>, std::optional<bool>)` compiles,
            // `coalesce(std::optional<int>, std::optional<int64_t>)` does not.
            return first.optionalFieldType == "bool" || second.optionalFieldType == "bool";
        }

        /**
         *  The arguments sqlite_orm reduces to one type when it deduces the result type of a call
         *  of `lowerFunctionName`, and an empty list for every other name. Read off the
         *  declarations in the pinned headers: COALESCE is `common_argument_type<>` — all of them —
         *  IFNULL and NULLIF `common_argument_type<0, 1>`, and IIF and its spelling IF
         *  `common_argument_type<1, 2>`, the two branches and not the condition. IIF is declared in
         *  its three-argument form alone, so SQLite's two-argument and n-ary forms name no indexes
         *  here; the arity check in `functionCallFormRefusal` is what reports them.
         */
        std::vector<size_t> commonArgumentTypeIndexes(std::string_view lowerFunctionName, size_t argumentCount) {
            if (lowerFunctionName == "coalesce") {
                std::vector<size_t> indexes(argumentCount);
                for (size_t index = 0; index < argumentCount; ++index) {
                    indexes[index] = index;
                }
                return indexes;
            }
            if ((lowerFunctionName == "ifnull" || lowerFunctionName == "nullif") && argumentCount == 2) {
                return {0, 1};
            }
            if ((lowerFunctionName == "iif" || lowerFunctionName == "if") && argumentCount == 3) {
                return {1, 2};
            }
            return {};
        }

        /** The first pair of a call's reduced arguments that has no common C++ type. */
        struct CommonArgumentTypeClash {
            ArgumentCppType first;
            ArgumentCppType second;
            /** Whether a BLOB takes part anywhere in the reduced arguments. */
            bool holdsBlob = false;
            /**
             *  The one number every reduced argument that carries a value fits in, where there is
             *  one, and empty where a text, a BLOB, an argument of an unknown type or a pair with
             *  no number above it — an `int64_t` next to a `double` — takes part. A NULL carries
             *  no value type — SQLite's answer for such a call is always another argument — so it
             *  does not take part in this, and neither does the `std::optional` wrapper a nullable
             *  column arrives in: the value read back is the number either way. Where this is set
             *  the call is read back as that very number, and nothing about it is worth reporting.
             */
            std::string narrowedResultType;
        };

        std::optional<CommonArgumentTypeClash> commonArgumentTypeClash(const FunctionCallNode& functionCall,
                                                                       const ReferencedColumnResolver& resolveColumn) {
            if (functionCall.star || functionCall.over || functionCall.filterWhere) {
                // A star names no arguments to reduce at all, and a call written with an OVER or a
                // FILTER comes out wrapped in an `over_t` or a `filtered_aggregate_function_t`
                // rather than as the plain call this types. SQLite refuses both over these four
                // names anyway: `coalesce(1, 2) OVER ()` is `coalesce() may not be used as a
                // window function` and `coalesce(1, 2) FILTER (WHERE 1)` is `FILTER may not be
                // used with non-aggregate coalesce()` (3.51).
                return std::nullopt;
            }
            const std::string lowerName = toLowerAscii(functionCall.name);
            const std::vector<size_t> indexes = commonArgumentTypeIndexes(lowerName, functionCall.arguments.size());
            std::vector<ArgumentCppType> types;
            for (size_t index: indexes) {
                const AstNodePointer& argument = functionCall.arguments.at(index);
                if (!argument) {
                    return std::nullopt;
                }
                std::optional<ArgumentCppType> type = generatedArgumentCppType(*argument, resolveColumn);
                if (!type) {
                    // One argument left untyped is enough to leave the whole call alone: the type
                    // it brings can be the one every other argument reduces to.
                    return std::nullopt;
                }
                types.push_back(std::move(*type));
            }
            // The first pair without a common type is the one the report names; a COALESCE can
            // be written with more arguments than two, and every pair of them has to reduce.
            std::optional<CommonArgumentTypeClash> clash;
            for (size_t first = 0; first < types.size() && !clash; ++first) {
                for (size_t second = first + 1; second < types.size(); ++second) {
                    if (!argumentTypesHaveACommonType(types[first], types[second])) {
                        clash = CommonArgumentTypeClash{types[first], types[second]};
                        break;
                    }
                }
            }
            if (!clash) {
                return std::nullopt;
            }
            for (const ArgumentCppType& type: types) {
                clash->holdsBlob = clash->holdsBlob || type.kind == ArgumentTypeKind::blob;
            }
            // Where every argument that carries a value carries a number they all fit in, the call
            // answers one of those numbers, and that is what the row holds and what the code reads
            // it back as. Arguments that are all NULL fold to nothing, and they have a common type
            // anyway.
            // The widening is the one a CASE folds its branches with: an `int64_t` next to a
            // `double` has no number over it, and neither does a text or a BLOB.
            std::string widest;
            for (const ArgumentCppType& type: types) {
                std::string_view valueType;
                switch (type.kind) {
                    case ArgumentTypeKind::null:
                        // A NULL is no value of its own: SQLite answers with one of the others.
                        continue;
                    case ArgumentTypeKind::arithmetic:
                        valueType = type.arithmeticType;
                        break;
                    case ArgumentTypeKind::text:
                        valueType = "std::string";
                        break;
                    case ArgumentTypeKind::blob:
                        valueType = "std::vector<char>";
                        break;
                }
                widest = widest.empty() ? std::string(valueType) : widerInferredCppType(widest, valueType);
            }
            if (widest == "bool" || widest == "int" || widest == "int64_t" || widest == "double") {
                clash->narrowedResultType = std::move(widest);
            }
            return clash;
        }

        /**
         *  The type such a call spells its result as: the number every argument of it carries
         *  where they all carry one, bytes where a BLOB takes part, and text otherwise.
         */
        std::string_view clashResultType(const CommonArgumentTypeClash& clash) {
            if (!clash.narrowedResultType.empty()) {
                return clash.narrowedResultType;
            }
            // `std::string` is read through `sqlite3_column_text`, which stops at the first NUL
            // byte a BLOB holds, while `std::vector<char>` carries every byte of one — and reads a
            // text or a number back as its bytes just as well.
            return clash.holdsBlob ? "std::vector<char>" : "std::string";
        }

    }  // namespace

    std::string functionCallSpelledResultType(const FunctionCallNode& functionCall,
                                              const ReferencedColumnResolver& resolveColumn) {
        if (const std::string_view byName = functionCallResultTypeArgument(toLowerAscii(functionCall.name));
            !byName.empty()) {
            // `"<std::string>"` without the brackets the caller of the other form wants.
            return std::string(byName.substr(1, byName.size() - 2));
        }
        const std::optional<CommonArgumentTypeClash> clash = commonArgumentTypeClash(functionCall, resolveColumn);
        if (!clash) {
            return {};
        }
        return std::string(clashResultType(*clash));
    }

    std::string functionCallSpelledResultType(const FunctionCallNode& functionCall,
                                              const CodeGeneratorContext& context) {
        return functionCallSpelledResultType(functionCall, [&context](const AstNode& argument) {
            return context.findReferencedColumn(argument);
        });
    }

    std::string functionCallResultTypeArgument(const FunctionCallNode& functionCall,
                                               const CodeGeneratorContext& context) {
        const std::string spelledType = functionCallSpelledResultType(functionCall, context);
        if (spelledType.empty()) {
            return {};
        }
        return "<" + spelledType + ">";
    }

    bool generatesNegatedCondition(const AstNode& astNode) {
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if (auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
            return unaryOp->unaryOperator == UnaryOperator::logicalNot;
        }
        if (auto* between = dynamic_cast<const BetweenNode*>(&generatedNode)) {
            return between->negated;
        }
        if (auto* like = dynamic_cast<const LikeNode*>(&generatedNode)) {
            return like->negated;
        }
        if (auto* glob = dynamic_cast<const GlobNode*>(&generatedNode)) {
            return glob->negated;
        }
        if (auto* match = dynamic_cast<const MatchNode*>(&generatedNode)) {
            return match->negated;
        }
        return false;
    }

    bool generatesConcatenation(const AstNode& astNode) {
        auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&generatedOperandNode(astNode));
        return binaryOperator != nullptr && binaryOperator->binaryOperator == BinaryOperator::concatenate;
    }

    bool isLeafNode(const AstNode& astNode) {
        const AstNode& generatedNode = generatedOperandNode(astNode);
        return generatesFoldedNegation(generatedNode) || dynamic_cast<const IntegerLiteralNode*>(&generatedNode) ||
               dynamic_cast<const RealLiteralNode*>(&generatedNode) ||
               dynamic_cast<const StringLiteralNode*>(&generatedNode) ||
               dynamic_cast<const NullLiteralNode*>(&generatedNode) ||
               dynamic_cast<const BoolLiteralNode*>(&generatedNode) ||
               dynamic_cast<const BlobLiteralNode*>(&generatedNode) ||
               dynamic_cast<const CurrentDatetimeLiteralNode*>(&generatedNode) ||
               dynamic_cast<const ColumnRefNode*>(&generatedNode) ||
               dynamic_cast<const QualifiedColumnRefNode*>(&generatedNode) ||
               dynamic_cast<const NewRefNode*>(&generatedNode) || dynamic_cast<const OldRefNode*>(&generatedNode) ||
               dynamic_cast<const ExcludedRefNode*>(&generatedNode) || dynamic_cast<const RaiseNode*>(&generatedNode);
    }

    SpannedCode wrap(SpannedCode code) {
        return "c(" + code + ")";
    }

    std::optional<CodegenWarning> commonArgumentTypeWarning(const AstNode& astNode,
                                                            std::string_view subject,
                                                            const ReferencedColumnResolver& resolveColumn) {
        // A COLLATE and a unary plus emit their operand and nothing else, so the call the value is
        // read back through is the one the operand under them comes out as.
        auto* functionCall = dynamic_cast<const FunctionCallNode*>(&generatedOperandNode(astNode));
        if (!functionCall) {
            return std::nullopt;
        }
        const std::optional<CommonArgumentTypeClash> clash = commonArgumentTypeClash(*functionCall, resolveColumn);
        if (!clash || !clash->narrowedResultType.empty()) {
            // A call whose arguments all carry a number is read back as that number, which is
            // what a call with a common type would have answered too; there is nothing lost to
            // report.
            return std::nullopt;
        }
        const std::string lowerName = toLowerAscii(functionCall->name);
        const std::string resultType(clashResultType(*clash));
        std::string message(subject);
        message += " computed with `";
        message += lowerName;
        message += "` comes back as ";
        message += clash->holdsBlob ? "bytes" : "text";
        message += ": sqlite_orm types the call as the common C++ type of its arguments, and ";
        message += clash->first.description;
        message += " next to ";
        message += clash->second.description;
        message += " has none, so the call is generated as `";
        message += lowerName;
        message += "<" + resultType + ">(…)` — a number comes back as ";
        message += clash->holdsBlob ? "the bytes of its digits" : "its digits";
        message += ". Spell the result type the call answers with where it is known";
        // The call is located at its name, and the name is what the message repeats.
        return CodegenWarning{std::move(message), functionCall->location, underlineLengthOf(functionCall->name)};
    }

    std::optional<CodegenWarning> selectResultCommonArgumentTypeWarning(const AstNode& astNode,
                                                                        const CodeGeneratorContext& context) {
        return commonArgumentTypeWarning(astNode, "result column", [&context](const AstNode& argument) {
            return context.findReferencedColumn(argument);
        });
    }

}  // namespace sqlite2orm
