#include "codegen_utils.h"

#include <sqlite2orm/utils.h>

#include <string_view>

namespace sqlite2orm {

    std::string_view binaryOperatorString(BinaryOperator binaryOperator) {
        switch (binaryOperator) {
            case BinaryOperator::logicalOr:
                return " or ";
            case BinaryOperator::logicalAnd:
                return " and ";
            case BinaryOperator::equals:
                return " == ";
            case BinaryOperator::notEquals:
                return " != ";
            case BinaryOperator::lessThan:
                return " < ";
            case BinaryOperator::lessOrEqual:
                return " <= ";
            case BinaryOperator::greaterThan:
                return " > ";
            case BinaryOperator::greaterOrEqual:
                return " >= ";
            case BinaryOperator::add:
                return " + ";
            case BinaryOperator::subtract:
                return " - ";
            case BinaryOperator::multiply:
                return " * ";
            case BinaryOperator::divide:
                return " / ";
            case BinaryOperator::modulo:
                return " % ";
            case BinaryOperator::concatenate:
                return " || ";
            case BinaryOperator::bitwiseAnd:
                return " & ";
            case BinaryOperator::bitwiseOr:
                return " | ";
            case BinaryOperator::shiftLeft:
                return " << ";
            case BinaryOperator::shiftRight:
                return " >> ";
            case BinaryOperator::isOp:
            case BinaryOperator::isNot:
            case BinaryOperator::isDistinctFrom:
            case BinaryOperator::isNotDistinctFrom:
                return {};
            case BinaryOperator::jsonArrow:
                return " -> ";
            case BinaryOperator::jsonArrow2:
                return " ->> ";
        }
        return {};
    }

    std::string_view binaryOperatorWithoutDefaultConstructor(BinaryOperator binaryOperator) {
        switch (binaryOperator) {
            case BinaryOperator::logicalOr:
            case BinaryOperator::logicalAnd:
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
                return {};
            case BinaryOperator::add:
                return "+";
            case BinaryOperator::subtract:
                return "-";
            case BinaryOperator::multiply:
                return "*";
            case BinaryOperator::divide:
                return "/";
            case BinaryOperator::modulo:
                return "%";
            case BinaryOperator::concatenate:
                return "||";
            case BinaryOperator::bitwiseAnd:
                return "&";
            case BinaryOperator::bitwiseOr:
                return "|";
            case BinaryOperator::shiftLeft:
                return "<<";
            case BinaryOperator::shiftRight:
                return ">>";
            case BinaryOperator::jsonArrow:
                return "->";
            case BinaryOperator::jsonArrow2:
                return "->>";
        }
        return {};
    }

    std::string_view jsonArrowOperatorText(BinaryOperator binaryOperator) {
        switch (binaryOperator) {
            case BinaryOperator::jsonArrow:
                return "->";
            case BinaryOperator::jsonArrow2:
                return "->>";
            default:
                return {};
        }
    }

    CodegenWarning jsonTextArrowWarning(SourceLocation location) {
        return CodegenWarning{"`->` is generated as a JSON_EXTRACT call, which answers the SQL value at the path "
                              "where `->` answers the JSON text of that value: a string comes back unquoted, and "
                              "`true`, `false` and `null` come back as 1, 0 and NULL. sqlite_orm has no form for the "
                              "operator itself",
                              location,
                              underlineLengthOf("->")};
    }

    CodegenWarning jsonArrowPathNotExpandedWarning(const BinaryOperatorNode& arrow) {
        return CodegenWarning{"the path operand of a JSON arrow is not a text or an integer literal, so the `$` path "
                              "SQLite expands it into cannot be spelled out here: the JSON_EXTRACT call the operator "
                              "is generated as takes the operand as written, and SQLite refuses a path that does not "
                              "start with `$` with `bad JSON path`",
                              arrow.location,
                              underlineLengthOf(jsonArrowOperatorText(arrow.binaryOperator))};
    }

    std::string_view binaryFunctionalName(BinaryOperator binaryOperator) {
        switch (binaryOperator) {
            case BinaryOperator::logicalOr:
                return "or_";
            case BinaryOperator::logicalAnd:
                return "and_";
            case BinaryOperator::equals:
                return "is_equal";
            case BinaryOperator::notEquals:
                return "is_not_equal";
            case BinaryOperator::lessThan:
                return "lesser_than";
            case BinaryOperator::lessOrEqual:
                return "lesser_or_equal";
            case BinaryOperator::greaterThan:
                return "greater_than";
            case BinaryOperator::greaterOrEqual:
                return "greater_or_equal";
            case BinaryOperator::add:
                return "add";
            case BinaryOperator::subtract:
                return "sub";
            case BinaryOperator::multiply:
                return "mul";
            case BinaryOperator::divide:
                return "div";
            case BinaryOperator::modulo:
                return "mod";
            case BinaryOperator::concatenate:
                return "conc";
            case BinaryOperator::bitwiseAnd:
                return "bitwise_and";
            case BinaryOperator::bitwiseOr:
                return "bitwise_or";
            case BinaryOperator::shiftLeft:
                return "bitwise_shift_left";
            case BinaryOperator::shiftRight:
                return "bitwise_shift_right";
            case BinaryOperator::isOp:
                return "is";
            case BinaryOperator::isNot:
                return "is_not";
            case BinaryOperator::isDistinctFrom:
                return "is_distinct_from";
            case BinaryOperator::isNotDistinctFrom:
                return "is_not_distinct_from";
            // Both arrows are read back through the same call, which spells the result type
            // `functionCallResultTypeArgument` names for it and looks up the path
            // `jsonArrowPathExpansion` expands their right operand into.
            case BinaryOperator::jsonArrow:
                return "json_extract";
            case BinaryOperator::jsonArrow2:
                return "json_extract";
        }
        return {};
    }

    int cppOperatorPrecedence(BinaryOperator binaryOperator) {
        switch (binaryOperator) {
            case BinaryOperator::multiply:
            case BinaryOperator::divide:
            case BinaryOperator::modulo:
                return 5;
            case BinaryOperator::add:
            case BinaryOperator::subtract:
                return 6;
            case BinaryOperator::shiftLeft:
            case BinaryOperator::shiftRight:
                return 7;
            case BinaryOperator::lessThan:
            case BinaryOperator::lessOrEqual:
            case BinaryOperator::greaterThan:
            case BinaryOperator::greaterOrEqual:
                return 9;
            case BinaryOperator::equals:
            case BinaryOperator::notEquals:
                return 10;
            case BinaryOperator::bitwiseAnd:
                return 11;
            case BinaryOperator::bitwiseOr:
                return 13;
            case BinaryOperator::logicalAnd:
                return 14;
            // `or` and `||` are the same C++ token, which is also why the operator spelling is not
            // always the operator that was written — see `binaryOperatorNeedsCallSpelling`.
            case BinaryOperator::logicalOr:
            case BinaryOperator::concatenate:
                return 15;
            // json_extract() is a call, and so is every operator of the IS family: `is()`, `is_not()`,
            // `is_distinct_from()` and `is_not_distinct_from()` are the only spellings sqlite_orm has.
            case BinaryOperator::jsonArrow:
            case BinaryOperator::jsonArrow2:
            case BinaryOperator::isOp:
            case BinaryOperator::isNot:
            case BinaryOperator::isDistinctFrom:
            case BinaryOperator::isNotDistinctFrom:
                return kCppPrecedencePrimary;
        }
        return kCppPrecedencePrimary;
    }

    const AstNode& generatedOperandNode(const AstNode& astNode) {
        if (auto* collateNode = dynamic_cast<const CollateNode*>(&astNode)) {
            // COLLATE has no sqlite_orm form, so the generated code is the operand's own.
            return generatedOperandNode(*collateNode->operand);
        }
        if (auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
            // A unary plus emits its operand and nothing else. Every other unary operator emits
            // a C++ expression of its own.
            if (unaryOp->unaryOperator == UnaryOperator::plus) {
                return generatedOperandNode(*unaryOp->operand);
            }
        }
        return astNode;
    }

    const BoolLiteralNode* isFamilyTruthKeyword(const BinaryOperatorNode& binaryOp) {
        switch (binaryOp.binaryOperator) {
            case BinaryOperator::isOp:
            case BinaryOperator::isNot:
            case BinaryOperator::isDistinctFrom:
            case BinaryOperator::isNotDistinctFrom:
                break;
            default:
                return nullptr;
        }
        // The parser keeps no node for parentheses, so a COLLATE is all there is to step through.
        const AstNode* rightNode = binaryOp.rhs.get();
        while (auto* collateNode = dynamic_cast<const CollateNode*>(rightNode)) {
            rightNode = collateNode->operand.get();
        }
        auto* keyword = dynamic_cast<const BoolLiteralNode*>(rightNode);
        if (!keyword) {
            return nullptr;
        }
        // `ON` is a boolean only as a PRAGMA value; SQLite refuses it as an operand.
        const std::string spelling = toLowerAscii(keyword->spelling);
        return spelling == "true" || spelling == "false" ? keyword : nullptr;
    }

    int sqlOperatorPrecedence(BinaryOperator binaryOperator) {
        // SQLite's own operator table, tightest first: `||` above `* / %` above `+ -` above the bit
        // operators above the ordering comparisons above the equality ones, the rank the predicates
        // share. `AND` and `OR` are looser than everything a predicate operand could regroup with.
        switch (binaryOperator) {
            case BinaryOperator::concatenate:
            case BinaryOperator::jsonArrow:
            case BinaryOperator::jsonArrow2:
                return 1;
            case BinaryOperator::multiply:
            case BinaryOperator::divide:
            case BinaryOperator::modulo:
                return 2;
            case BinaryOperator::add:
            case BinaryOperator::subtract:
                return 3;
            case BinaryOperator::shiftLeft:
            case BinaryOperator::shiftRight:
            case BinaryOperator::bitwiseAnd:
            case BinaryOperator::bitwiseOr:
                return 4;
            case BinaryOperator::lessThan:
            case BinaryOperator::lessOrEqual:
            case BinaryOperator::greaterThan:
            case BinaryOperator::greaterOrEqual:
                return 5;
            case BinaryOperator::equals:
            case BinaryOperator::notEquals:
            case BinaryOperator::isOp:
            case BinaryOperator::isNot:
            case BinaryOperator::isDistinctFrom:
            case BinaryOperator::isNotDistinctFrom:
                return kSqlPrecedencePredicate;
            case BinaryOperator::logicalAnd:
                return kSqlPrecedenceAnd;
            case BinaryOperator::logicalOr:
                return kSqlPrecedenceOr;
        }
        return kSqlPrecedencePredicate;
    }

    int serializedSqlPrecedence(const AstNode& astNode, const CodeGenPolicy* policy) {
        // A COLLATE and a unary plus emit their operand and nothing else, so the SQL this node is
        // serialized as is the one its operand is serialized as.
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if (auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
            if (unaryOp->unaryOperator == UnaryOperator::logicalNot) {
                return kSqlPrecedenceNot;
            }
            if (unaryOp->unaryOperator == UnaryOperator::minus) {
                // A negation is a constant or the parenthesized `(c(0) - …)` subtraction, both of
                // them terms. The exception is the one over a predicate, which keeps a bare unary
                // minus SQLite reads inside the predicate: `- "a" IS NULL` is `(- "a") IS NULL`.
                return negationFormFor(*unaryOp->operand) == NegationForm::unaryOverPredicate
                           ? serializedSqlPrecedence(*unaryOp->operand, policy)
                           : kSqlPrecedenceTerm;
            }
            return kSqlPrecedenceTerm;
        }
        if (auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
            // The JSON arrows are the one binary operator serialized as a call, `json_extract(…)`,
            // which reads as one term; every other one is serialized as the SQL operator it was
            // parsed as, and binds the way SQLite binds that operator.
            if (binaryOp->binaryOperator == BinaryOperator::jsonArrow ||
                binaryOp->binaryOperator == BinaryOperator::jsonArrow2) {
                return kSqlPrecedenceTerm;
            }
            return sqlOperatorPrecedence(binaryOp->binaryOperator);
        }
        // A negated predicate generated as `!pred` is a `negated_condition_t`, which sqlite_orm
        // serializes as a prefix `NOT` over the bare predicate: `NOT "b" BETWEEN 1 AND 2`.
        auto negated = [](const auto* predicate) {
            return predicate && predicate->negated;
        };
        if (negated(dynamic_cast<const BetweenNode*>(&generatedNode)) ||
            negated(dynamic_cast<const LikeNode*>(&generatedNode)) ||
            negated(dynamic_cast<const GlobNode*>(&generatedNode)) ||
            negated(dynamic_cast<const MatchNode*>(&generatedNode))) {
            return kSqlPrecedenceNot;
        }
        // A NOT IN is that only when the policy asks for `!in(…)`; `not_in(…)` serializes as the
        // `NOT IN` predicate itself.
        if (negated(dynamic_cast<const InNode*>(&generatedNode)) &&
            policyEquals(policy, "negation_style", "operator_excl")) {
            return kSqlPrecedenceNot;
        }
        // Everything else is a term of its own in the serialized SQL: a literal, a column, a call,
        // CAST, CASE, a parenthesized subquery.
        return sqlPredicateLooserThanMinus(generatedNode).empty() ? kSqlPrecedenceTerm : kSqlPrecedencePredicate;
    }

    int serializedSqlPrecedenceAsBinaryOperand(const AstNode& astNode, const CodeGenPolicy* policy) {
        // sqlite_orm's binary operator and binary condition serializer parenthesizes an operand
        // that is itself a binary operator or condition — everything a `BinaryOperatorNode`
        // generates — so however loosely SQLite binds it, it comes back as one term there.
        if (dynamic_cast<const BinaryOperatorNode*>(&generatedOperandNode(astNode))) {
            return kSqlPrecedenceTerm;
        }
        return serializedSqlPrecedence(astNode, policy);
    }

    bool trailingCollateBindsWholeExpression(const AstNode& astNode) {
        // A COLLATE and a unary plus generate their operand and nothing else, so what a trailing
        // COLLATE lands on is decided by the node under them.
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if (auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
            // The JSON arrows are the one binary operator serialized as a call, `json_extract(…)`,
            // which ends in no operand of its own; every other one ends in its right-hand side.
            return binaryOp->binaryOperator == BinaryOperator::jsonArrow ||
                   binaryOp->binaryOperator == BinaryOperator::jsonArrow2;
        }
        if (auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
            if (unaryOp->unaryOperator == UnaryOperator::logicalNot) {
                // `NOT` is the one prefix operator SQLite binds looser than COLLATE:
                // `NOT "a" COLLATE nocase` is `NOT ("a" COLLATE nocase)`.
                return false;
            }
            if (unaryOp->unaryOperator == UnaryOperator::minus) {
                switch (negationFormFor(*unaryOp->operand)) {
                    case NegationForm::foldedIntoConstant:
                        return true;
                    case NegationForm::zeroMinusSubtraction:
                        // The parentheses that subtraction reads with elsewhere are the enclosing
                        // binary serializer's, and this slot is not one: it comes out `0 - "a"`.
                        return false;
                    case NegationForm::unaryOverPredicate:
                        // The minus stays unary, so the predicate under it is what ends the SQL.
                        return trailingCollateBindsWholeExpression(*unaryOp->operand);
                }
            }
            // `~x`, which SQLite binds tighter than COLLATE.
            return true;
        }
        if (dynamic_cast<const InNode*>(&generatedNode)) {
            // An IN ends in its value list, which is no expression for a COLLATE to attach to.
            return true;
        }
        // The predicates left end in an expression of their own — the upper bound of a BETWEEN, the
        // pattern of a LIKE, the NULL of an IS NULL. Everything else is one term: a literal, a
        // column, a call, CAST, CASE.
        return sqlPredicateLooserThanMinus(generatedNode).empty();
    }

    bool predicateArgumentNeedsGroupingCast(const AstNode& astNode, const CodeGenPolicy* policy) {
        return serializedSqlPrecedence(astNode, policy) >= kSqlPrecedenceNot;
    }

    bool predicatePatternNeedsGroupingCast(const AstNode& astNode, const CodeGenPolicy* policy) {
        return serializedSqlPrecedence(astNode, policy) >= kSqlPrecedencePredicate;
    }

    int generatedCppPrecedence(const AstNode& astNode, const CodeGenPolicy* policy) {
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if (auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
            // An operator the C++ `||` token would misread is generated as a call whatever the
            // policy asks for, and a call is a primary.
            if (policyEquals(policy, "expr_style", "functional") || binaryOperatorNeedsCallSpelling(*binaryOp)) {
                return kCppPrecedencePrimary;
            }
            return cppOperatorPrecedence(binaryOp->binaryOperator);
        }
        // Every remaining node emits either a C++ unary expression, which binds tighter than any
        // binary one, or a primary: a literal, a call, an already-parenthesized subtraction.
        return kCppPrecedencePrimary;
    }

}  // namespace sqlite2orm
