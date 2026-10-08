#include "codegen_expression.h"
#include "codegen_context.h"
#include "codegen_utils.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    CodeGenResult ExpressionCodeGenerator::generateBinaryOperator(const BinaryOperatorNode* binaryOp) {
        const int matchesBeforeLeft = this->context.generatedMatchCount;
        auto leftResult = this->coordinator.generateNode(*binaryOp->lhs);
        const int matchesBeforeRight = this->context.generatedMatchCount;
        auto rightResult = this->coordinator.generateNode(*binaryOp->rhs);
        SpannedCode leftCode = SpannedCode::takenFrom(leftResult);
        SpannedCode rightCode = SpannedCode::takenFrom(rightResult);
        const bool leftHoldsMatch = matchesBeforeRight != matchesBeforeLeft;
        const bool rightHoldsMatch = this->context.generatedMatchCount != matchesBeforeRight;

        // sqlite_orm binds every literal into the prepared statement, and an OR over a MATCH
        // runs only where SQLite folds the other operand away: `body MATCH 'x' OR 1` is always
        // true and never calls MATCH, while `body MATCH ? OR ?` leaves MATCH standing outside
        // the FTS index and fails at the first step with `unable to use function MATCH in the
        // requested context`. A `literal_holder` is serialized into the SQL as written, so the
        // statement prepared is the one written. It is no operand `or_()` recognizes, and
        // `c()` hands it right back; the call spelling is the only one that stays an OR.
        // A constant holds no MATCH, so at most one of the two operands is kept.
        const AstNode* keptLiteralOperand = nullptr;
        if (binaryOp->binaryOperator == BinaryOperator::logicalOr) {
            if (rightHoldsMatch) {
                if (auto literal = unboundLiteralCode(*binaryOp->lhs, leftCode.text())) {
                    leftCode = std::move(*literal);
                    this->context.markExpression(leftCode, *binaryOp->lhs);
                    keptLiteralOperand = binaryOp->lhs.get();
                }
            }
            if (leftHoldsMatch) {
                if (auto literal = unboundLiteralCode(*binaryOp->rhs, rightCode.text())) {
                    rightCode = std::move(*literal);
                    this->context.markExpression(rightCode, *binaryOp->rhs);
                    keptLiteralOperand = binaryOp->rhs.get();
                }
            }
        }
        const bool orOperandKeptLiteral = keptLiteralOperand != nullptr;

        // A COLLATE and a unary plus generate their operand and nothing else, so an operand
        // standing under one is the operand this node really puts into the C++ expression.
        const AstNode& leftNode = generatedOperandNode(*binaryOp->lhs);
        const AstNode& rightNode = generatedOperandNode(*binaryOp->rhs);

        if (auto* leftCol = dynamic_cast<const ColumnRefNode*>(&leftNode)) {
            this->context.registerPrefixColumn(toCppIdentifier(leftCol->columnName),
                                               this->context.inferTypeFromNode(rightNode));
        }
        if (auto* rightCol = dynamic_cast<const ColumnRefNode*>(&rightNode)) {
            this->context.registerPrefixColumn(toCppIdentifier(rightCol->columnName),
                                               this->context.inferTypeFromNode(leftNode));
        }

        auto decisionPoints = std::move(leftResult.decisionPoints);
        decisionPoints.insert(decisionPoints.end(),
                              std::make_move_iterator(rightResult.decisionPoints.begin()),
                              std::make_move_iterator(rightResult.decisionPoints.end()));

        bool leftLeaf = isLeafNode(leftNode);
        bool rightLeaf = isLeafNode(rightNode);

        auto nodeIsNoWrapRef = [&](const AstNode* node) -> bool {
            if (auto* qr = dynamic_cast<const QualifiedColumnRefNode*>(node)) {
                if (this->context.activeTableAliases.find(std::string(qr->tableName)) !=
                    this->context.activeTableAliases.end())
                    return true;
                if (this->context.withCteCpp20Monikers()) {
                    auto key = normalizeSqlIdentifier(qr->tableName);
                    if (this->context.activeCteTypedefByTableKey.find(key) !=
                        this->context.activeCteTypedefByTableKey.end())
                        return true;
                }
            }
            if (dynamic_cast<const ColumnRefNode*>(node)) {
                if (this->context.withCteCpp20Monikers() && this->context.implicitSingleSourceCteTypedef)
                    return true;
            }
            return false;
        };
        bool leftNoWrap = false;
        if (auto* leftCol = dynamic_cast<const ColumnRefNode*>(&leftNode)) {
            leftNoWrap = this->context.columnRefIsSelectAliasNoWrap(*leftCol);
        }
        if (nodeIsNoWrapRef(&leftNode)) {
            leftNoWrap = true;
        }
        bool rightNoWrap = false;
        if (auto* rightCol = dynamic_cast<const ColumnRefNode*>(&rightNode)) {
            rightNoWrap = this->context.columnRefIsSelectAliasNoWrap(*rightCol);
        }
        if (nodeIsNoWrapRef(&rightNode)) {
            rightNoWrap = true;
        }

        // The C++ term the operands make is only half the grouping: sqlite_orm serializes the
        // statement back into SQL, and there it parenthesizes an operand only when that operand
        // is itself a binary operator or condition. A predicate — IN, BETWEEN, LIKE, GLOB,
        // MATCH, IS [NOT] NULL, NOT — comes out bare, and SQLite binds those looser than every
        // arithmetic, bit and comparison operator, so `c(1) - is_null(&User::a)` serializes as
        // `1 - "a" IS NULL` and is read back as `(1 - "a") IS NULL`. A CAST to INTEGER delimits
        // the predicate in that SQL and leaves what it stands for alone: a predicate is 0, 1 or
        // NULL, and a CAST to INTEGER keeps all three, typeof included.
        const bool operandsBecomeCallArguments = binaryOp->binaryOperator == BinaryOperator::jsonArrow ||
                                                 binaryOp->binaryOperator == BinaryOperator::jsonArrow2;
        const int sqlPrecedence = sqlOperatorPrecedence(binaryOp->binaryOperator);
        // The operand the CAST went around, for the hint below to underline: with both of them
        // cast the hint is one and stands at the first, as a warning met twice does.
        const AstNode* castPredicateOperand = nullptr;
        // Which operands the CAST went around: a `cast_t` is an operator argument whatever it holds.
        bool leftCastPredicate = false;
        bool rightCastPredicate = false;
        auto castPredicate = [&](SpannedCode& code, const AstNode& operandNode, bool rightOperand) {
            if (operandsBecomeCallArguments)
                return;
            const int operandPrecedence =
                serializedSqlPrecedenceAsBinaryOperand(operandNode, this->context.codeGenPolicy);
            // SQL reads these operators left-associatively too, so the right operand regroups at
            // equal precedence as well: `1 = (a IS NULL)` comes back as `(1 = "a") IS NULL`.
            const bool regroups = rightOperand ? operandPrecedence >= sqlPrecedence : operandPrecedence > sqlPrecedence;
            if (!regroups)
                return;
            code = "cast<" + sqliteTypeToCpp("INTEGER") + ">(" + code + ")";
            (rightOperand ? rightCastPredicate : leftCastPredicate) = true;
            if (!castPredicateOperand) {
                castPredicateOperand = &operandNode;
            }
        };
        // A TRUE or FALSE on the right of an IS tests the truth of the left operand, which
        // `is(left, true)` would compare with 1 instead. `and_(left, true)` answers that truth
        // as 1, 0 or NULL, and a condition is an operand sqlite_orm serializes parenthesized,
        // so it needs no CAST to keep its grouping.
        const BoolLiteralNode* truthKeyword = isFamilyTruthKeyword(*binaryOp);
        if (truthKeyword) {
            SpannedCode truthOperand = generatesSqliteOrmOperandOrBindable(leftNode) ? leftCode : wrap(leftCode);
            leftCode = "and_(" + truthOperand + ", true)";
        } else {
            castPredicate(leftCode, *binaryOp->lhs, false);
        }
        castPredicate(rightCode, *binaryOp->rhs, true);

        // sqlite_orm reads the operand types to decide what an AND or an OR may build at all.
        // `operator&&` is declared only where one operand is a condition or an operator
        // argument, so `a + 1 AND a + 2` finds no overload; `or_()` and `and_()` assert that
        // both arguments are operands it recognizes, and a MATCH, a CURRENT_* literal, a
        // window call and a FILTERed aggregate are none of them. `c()` is the escape hatch
        // the assertion names, and it costs nothing: all three unwrap the
        // `quoted_expression_t` right back to the expression it holds, so the condition built
        // and the SQL it serializes to are the ones the bare operand would have given.
        const bool isAndOr = binaryOp->binaryOperator == BinaryOperator::logicalAnd ||
                             binaryOp->binaryOperator == BinaryOperator::logicalOr;
        // The concatenation `operator||` is declared only where one operand is a concatenation
        // or an operator argument, so `upper(&User::t) || "x"` and `(c(&User::a) + 1) || "x"`
        // find no overload, and a built-in call finds one only on compilers the headers take
        // their C++20 path on. `c()` is unwrapped there just the same.
        const bool isConcatenation = binaryOp->binaryOperator == BinaryOperator::concatenate;
        auto generatesOperatorArgument = [&](const AstNode& operandNode, bool noWrap) {
            // A column reference generates an operator argument as a column pointer and as a
            // C++20 alias moniker — an `alias_holder` — and a plain member pointer otherwise,
            // which is none: only the generator knows which of the three it emits.
            return generatesSqliteOrmOperatorArgument(operandNode) || noWrap ||
                   nodeGeneratesColumnPointer(&operandNode);
        };
        const bool andOrHasRecognizedOperand =
            generatesSqliteOrmCondition(leftNode) || generatesSqliteOrmCondition(rightNode) ||
            generatesOperatorArgument(leftNode, leftNoWrap) || generatesOperatorArgument(rightNode, rightNoWrap);
        const bool concatenationHasRecognizedOperand =
            generatesConcatenation(leftNode) || generatesConcatenation(rightNode) || leftCastPredicate ||
            rightCastPredicate || generatesOperatorArgument(leftNode, leftNoWrap) ||
            generatesOperatorArgument(rightNode, rightNoWrap);
        // The operator spelling needs one recognized operand, and each of its variants quotes
        // a different side, so the side a variant quotes is the one that carries it.
        const bool operatorSpellingQuotesOperand =
            (isAndOr && !andOrHasRecognizedOperand) || (isConcatenation && !concatenationHasRecognizedOperand);
        // The call spelling needs both of its arguments recognized, each on its own.
        const bool quoteLeftCallArgument = isAndOr && !generatesSqliteOrmOperandOrBindable(leftNode);
        const bool quoteRightCallArgument = isAndOr && !generatesSqliteOrmOperandOrBindable(rightNode);

        // The operator spelling puts the operands into a C++ expression, whose grouping is the
        // C++ precedence and not the SQL one this node was parsed with. An operand that would
        // regroup there gets parentheses: every operator involved is left-associative, so the
        // right one needs them at equal precedence too, which is what tells `1 - (2 - 3)` from
        // `1 - 2 - 3`. The functional spelling passes its operands as arguments and needs none.
        const int precedence = cppOperatorPrecedence(binaryOp->binaryOperator);
        auto asOperand = [&](const SpannedCode& code, const AstNode& operandNode, bool rightOperand) -> SpannedCode {
            if (precedence == kCppPrecedencePrimary)
                return code;
            const int operandPrecedence = generatedCppPrecedence(operandNode, this->context.codeGenPolicy);
            const bool regroups = rightOperand ? operandPrecedence >= precedence : operandPrecedence > precedence;
            return regroups ? "(" + code + ")" : code;
        };
        SpannedCode leftOperand = asOperand(leftCode, leftNode, false);
        SpannedCode rightOperand = asOperand(rightCode, rightNode, true);

        // A scalar subquery over a single SELECT generates a `select_t`, and sqlite_orm's
        // operators recognize it no more than a bare value: `select(count<User>()) > 0` finds no
        // `operator>`. The wrapping variants spell it `c(…)` on their side just as they do a
        // leaf — `c()` hands the `select_t` right back to the operator, and the subquery is
        // serialized parenthesized as written. A compound one stays as it is: its `union_t`
        // comes out without parentheses of its own, so `c(union_(…)) > 1` would compile into
        // `(SELECT … UNION SELECT 2 > 1)` — the comparison moved into the last arm.
        auto generatesSelect = [](const AstNode& operandNode) {
            auto* subquery = dynamic_cast<const SubqueryNode*>(&operandNode);
            return subquery && dynamic_cast<const SelectNode*>(subquery->select.get()) != nullptr;
        };
        const bool leftWrapped = leftLeaf || generatesSelect(leftNode);
        const bool rightWrapped = rightLeaf || generatesSelect(rightNode);
        SpannedCode wrappedLeft =
            (operatorSpellingQuotesOperand || (leftWrapped && !leftNoWrap && !nodeGeneratesColumnPointer(&leftNode)))
                ? wrap(leftCode)
                : leftOperand;
        SpannedCode wrappedRight =
            (operatorSpellingQuotesOperand || (rightWrapped && !rightNoWrap && !nodeGeneratesColumnPointer(&rightNode)))
                ? wrap(rightCode)
                : rightOperand;

        // The JSON arrows expand the abbreviated path they are written with — a bare label
        // into `$."label"`, an array index into `$[N]` — and the JSON_EXTRACT call they are
        // generated as does not, so the expanded path is what the call is handed. An operand
        // that cannot be expanded here goes in as written and is reported below.
        SpannedCode rightArgument = rightCode;
        std::optional<std::string> arrowPath;
        if (operandsBecomeCallArguments) {
            arrowPath = jsonArrowPathExpansion(*binaryOp->rhs);
            if (arrowPath) {
                rightArgument = cppStringLiteral(*arrowPath);
                this->context.markExpression(rightArgument, *binaryOp->rhs);
            }
        }

        auto funcName = binaryFunctionalName(binaryOp->binaryOperator);
        SpannedCode functionalCode = std::string(funcName) + std::string(functionCallResultTypeArgument(funcName)) +
                                     "(" + (quoteLeftCallArgument ? wrap(leftCode) : leftCode) + ", " +
                                     (quoteRightCallArgument ? wrap(rightArgument) : rightArgument) + ")";

        auto op = binaryOperatorString(binaryOp->binaryOperator);
        SpannedCode wrapLeftCode = wrappedLeft + op + rightOperand;
        SpannedCode wrapRightCode = leftOperand + op + wrappedRight;
        SpannedCode wrapBothCode = wrappedLeft + op + wrappedRight;

        std::string chosenExprVal = "operator_wrap_left";
        SpannedCode emittedExpr = wrapLeftCode;
        if (policyEquals(this->context.codeGenPolicy, "expr_style", "operator_wrap_right")) {
            chosenExprVal = "operator_wrap_right";
            emittedExpr = wrapRightCode;
        } else if (policyEquals(this->context.codeGenPolicy, "expr_style", "functional")) {
            chosenExprVal = "functional";
            emittedExpr = functionalCode;
        } else if (policyEquals(this->context.codeGenPolicy, "expr_style", "operator_wrap_both")) {
            chosenExprVal = "operator_wrap_both";
            emittedExpr = wrapBothCode;
        }
        // C++ spells a logical OR and a concatenation with the same token, `||`, and
        // sqlite_orm's two `operator||` overloads pick between the `or_condition_t` and the
        // `conc_t` by the operands: the OR only when one of them is a condition, the
        // concatenation only when neither is. So `SELECT 1 OR 0` spelled `c(1) or 0` runs as
        // `SELECT 1 || 0` and answers '10', and `SELECT (a = 1) || 'x'` spelled
        // `c(&User::a) == 1 || "x"` runs as `SELECT (a = 1) OR 'x'`. `or_()` and `conc()` name
        // the node they build, so the call is the only form offered where the operator
        // spelling would not be the operator that was written.
        const bool needsCallSpelling = binaryOperatorNeedsCallSpelling(*binaryOp);
        // The JSON arrows have no C++ operator spelling at all — they are generated as a
        // `json_extract()` call — so the call is the only form they offer either, and neither
        // does an OR with an operand kept literal.
        // The IS family has no C++ operator either: sqlite_orm spells it `is()`, `is_not()`,
        // `is_distinct_from()` and `is_not_distinct_from()` only.
        const bool hasNoOperatorSpelling = binaryOperatorString(binaryOp->binaryOperator).empty();
        const bool onlyCallSpellingCompiles =
            needsCallSpelling || operandsBecomeCallArguments || orOperandKeptLiteral || hasNoOperatorSpelling;
        if (onlyCallSpellingCompiles) {
            chosenExprVal = "functional";
            emittedExpr = functionalCode;
        }

        // options lists every variant (the chosen one included); the consumer decides how to
        // present the selection. Where the operator spelling would be the wrong operator or no
        // C++ operator at all, the call is the only option offered.
        std::vector<Option> exprStyleOptions;
        if (onlyCallSpellingCompiles) {
            exprStyleOptions.push_back(Option{"functional", functionalCode.text(), "functional style"});
        } else {
            exprStyleOptions.push_back(Option{"operator_wrap_left", wrapLeftCode.text(), "wrap left operand"});
            exprStyleOptions.push_back(Option{"operator_wrap_right", wrapRightCode.text(), "wrap right operand"});
            exprStyleOptions.push_back(Option{"functional", functionalCode.text(), "functional style"});
            exprStyleOptions.push_back(Option{"operator_wrap_both", wrapBothCode.text(), "wrap both operands", true});
        }
        decisionPoints.push_back(DecisionPoint{this->context.nextDecisionPointId++,
                                               "expr_style",
                                               chosenExprVal,
                                               emittedExpr.text(),
                                               std::move(exprStyleOptions)});

        std::vector<CodegenWarning> binWarnings;
        binWarnings.insert(binWarnings.end(),
                           std::make_move_iterator(leftResult.warnings.begin()),
                           std::make_move_iterator(leftResult.warnings.end()));
        binWarnings.insert(binWarnings.end(),
                           std::make_move_iterator(rightResult.warnings.begin()),
                           std::make_move_iterator(rightResult.warnings.end()));

        // A path operand only SQLite can expand leaves the call looking up whatever the
        // operand answers, which is the path the operator was written with rather than the
        // one it stands for. A NULL path is the one operand that needs no expansion: SQLite
        // answers NULL for it, and so does the generated call, which takes a NULL the same way.
        if (operandsBecomeCallArguments && !arrowPath &&
            !dynamic_cast<const NullLiteralNode*>(&generatedOperandNode(*binaryOp->rhs))) {
            binWarnings.push_back(jsonArrowPathNotExpandedWarning(*binaryOp));
        }

        // `->>` over an expanded path is the JSON_EXTRACT call it is generated as, value for
        // value and type for type; `->` is the JSON text of what that call answers, which
        // sqlite_orm has no form for. The result type such a call is generated with is only
        // read back where the call stands for a result column, and is reported there
        // (`selectResultJsonExtractTypeWarning`).
        if (binaryOp->binaryOperator == BinaryOperator::jsonArrow) {
            binWarnings.push_back(jsonTextArrowWarning(binaryOp->location));
        }

        // Only a comparison reads the affinity of its operands, so only there does dropping a
        // unary plus over a column change what SQLite answers. The IS family is one: a TEXT
        // column `t` holding '1' answers `t IS 1` with 1 and `+t IS 1` with 0. The operands are
        // reported in the order they are written in.
        switch (binaryOp->binaryOperator) {
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
                // A truth test reads no affinity: `+t IS TRUE` and `t IS TRUE` agree.
                if (truthKeyword) {
                    break;
                }
                for (const AstNode* operand: {binaryOp->lhs.get(), binaryOp->rhs.get()}) {
                    if (auto warning = comparisonUnaryPlusAffinityWarning(*operand)) {
                        binWarnings.push_back(std::move(*warning));
                    }
                }
                break;
            default:
                break;
        }

        // A trigger keeps its WHEN expression in an `optional_container`, which default-constructs
        // it, so the WHEN clause of a generated trigger only compiles while every sqlite_orm type
        // in it has a default constructor. `binary_operator` — what the arithmetic, bit and
        // concatenation operators produce — and the `builtin_function_t` behind the JSON arrows
        // have none; the `binary_condition` behind a comparison, an AND and an OR does.
        if (const std::string_view formWithoutDefaultConstructor =
                binaryOperatorWithoutDefaultConstructor(binaryOp->binaryOperator);
            !formWithoutDefaultConstructor.empty()) {
            this->context.recordFormWithoutDefaultConstructor(std::string(formWithoutDefaultConstructor));
        }

        if (castPredicateOperand) {
            this->context.recordComment(sourceSpanComment(kCommentPredicateGroupingCast, *castPredicateOperand));
        }
        if (binaryOp->binaryOperator == BinaryOperator::isDistinctFrom ||
            binaryOp->binaryOperator == BinaryOperator::isNotDistinctFrom) {
            // The spelling is the whole expression's, operator and operands together.
            this->context.recordComment(sourceSpanComment(kCommentDistinctFromSqliteVersion, *binaryOp));
        }
        if (truthKeyword) {
            // The spelling is the whole expression's, operator and operands together.
            this->context.recordComment(sourceSpanComment(kCommentIsTruthTest, *binaryOp));
        }
        if (keptLiteralOperand) {
            this->context.recordComment(sourceSpanComment(kCommentOrMatchLiteralKept, *keptLiteralOperand));
        }
        if (needsCallSpelling) {
            // The spelling is the whole expression's, operator and operands together.
            this->context.recordComment(sourceSpanComment(kCommentOrTokenCallSpelling, *binaryOp));
        }
        // Only the spelling that is emitted carries a quoted operand into the code; the
        // variants that are merely offered speak for themselves. A leaf operand — a literal or
        // a column reference — is spelled `c(…)` by the wrapping variants whatever the operator
        // is, so what the comment marks is the quoting an operand's own form forces on top of
        // that. The operand it answers with is the one the comment underlines, and where a
        // spelling wraps both it is the left one: the comment is a single one, and it stands at
        // the first operand it is true of.
        auto quotedUnrecognizedOperand = [&](std::string_view exprStyle) -> const AstNode* {
            if (exprStyle == "functional") {
                if (quoteLeftCallArgument)
                    return &leftNode;
                return quoteRightCallArgument ? &rightNode : nullptr;
            }
            if (!operatorSpellingQuotesOperand) {
                return nullptr;
            }
            if (exprStyle == "operator_wrap_right") {
                return rightLeaf ? nullptr : &rightNode;
            }
            if (exprStyle == "operator_wrap_both") {
                if (!leftLeaf)
                    return &leftNode;
                return rightLeaf ? nullptr : &rightNode;
            }
            return leftLeaf ? nullptr : &leftNode;
        };
        if (const AstNode* quotedOperand = quotedUnrecognizedOperand(chosenExprVal)) {
            this->context.recordComment(
                sourceSpanComment(isConcatenation ? kCommentConcatenationQuotedOperand : kCommentAndOrQuotedOperand,
                                  *quotedOperand));
        }
        return spannedResult(std::move(emittedExpr), std::move(decisionPoints), std::move(binWarnings));
    }

    CodeGenResult ExpressionCodeGenerator::generateUnaryOperator(const UnaryOperatorNode* unaryOp) {
        if (unaryOp->unaryOperator == UnaryOperator::plus) {
            // A unary plus generates its operand and nothing else, so it has to be transparent
            // for what the surrounding generation asks of that operand as well: an operator
            // over `+a` is an operator over `a`. Generating the operand in a context the plus
            // had reset is how `NOT +a` came out as `not c(&User::a)` while `NOT a` came out
            // as `not column<User>(&User::a)` — the form the table walker reads — and the
            // generated select then ran with no FROM clause at all.
            return this->coordinator.generateNode(*unaryOp->operand);
        }
        // A COLLATE generates its operand and nothing else, so the operand standing under one
        // is the operand this unary operator really applies to in the generated code. The sign
        // a minus folds into a literal is the exception — SQLite's parser does not read one
        // through a COLLATE either — which is why `negationFormFor` is asked about the operand
        // as written and steps through the pluses alone.
        const AstNode& operandNode = generatedOperandNode(*unaryOp->operand);
        const bool operandLeaf = isLeafNode(operandNode);

        // Every other sqlite_orm operator unwraps the `c(...)` its operand carries; `operator!`
        // keeps it, and the walker that collects the tables a statement reads stops at such a
        // wrapper. A column a NOT is the only mention of is then invisible to it, and
        // `storage.select(not c(&User::a))` runs as `SELECT NOT "users"."a"` with no FROM clause
        // at all — SQLite answers that with `no such column`, which sqlite_orm reports as
        // `SQL logic error`. A column reference generated here takes the column-pointer form
        // instead, which names the same column and which the walker does read.
        const bool notKeepsOperandQuoted = unaryOp->unaryOperator == UnaryOperator::logicalNot && operandLeaf;
        const bool outerColumnRefUnderLogicalNot = this->context.columnRefUnderLogicalNot;
        const bool outerEmittedColumnPointer = this->context.emittedColumnPointerUnderLogicalNot;
        this->context.columnRefUnderLogicalNot = notKeepsOperandQuoted;
        this->context.emittedColumnPointerUnderLogicalNot = false;
        auto operandResult = this->coordinator.generateNode(*unaryOp->operand);
        SpannedCode operandCode = SpannedCode::takenFrom(operandResult);
        // What the operand was generated as is the emitter's own answer, not the node's kind: a
        // column that names a SELECT alias is generated as `get<Alias>()` whatever this asks for.
        const bool operandGeneratedColumnPointerUnderNot = this->context.emittedColumnPointerUnderLogicalNot;
        this->context.columnRefUnderLogicalNot = outerColumnRefUnderLogicalNot;
        this->context.emittedColumnPointerUnderLogicalNot = outerEmittedColumnPointer;
        auto decisionPoints = std::move(operandResult.decisionPoints);

        // sqlite_orm's unary_minus_t reports a wrong result type, so `-c(2)` serializes to the
        // right SQL and still reads the row back as 0. A minus over a numeric constant is folded
        // into it the way SQLite's own parser does, which is why `-2` is the constant -2 rather
        // than a negation of 2; C++ folds the second sign of `- -3` just as well.
        const std::optional<NegationForm> negationForm =
            unaryOp->unaryOperator == UnaryOperator::minus
                ? std::optional<NegationForm>{negationFormFor(*unaryOp->operand)}
                : std::nullopt;
        if (negationForm == NegationForm::foldedIntoConstant) {
            // A constant that already carries a sign needs the parentheses C++ has no `--` for.
            SpannedCode folded =
                isNumericLiteral(withoutUnaryPluses(*unaryOp->operand)) ? "-" + operandCode : "-(" + operandCode + ")";
            return spannedResult(std::move(folded),
                                 std::move(decisionPoints),
                                 std::move(operandResult.warnings),
                                 std::move(operandResult.errors));
        }

        bool operandNoWrap = false;
        if (auto* opCol = dynamic_cast<const ColumnRefNode*>(&operandNode)) {
            operandNoWrap = this->context.columnRefIsSelectAliasNoWrap(*opCol);
            if (this->context.withCteCpp20Monikers() && this->context.implicitSingleSourceCteTypedef) {
                operandNoWrap = true;
            }
        }
        if (auto* opQCol = dynamic_cast<const QualifiedColumnRefNode*>(&operandNode)) {
            if (this->context.activeTableAliases.find(std::string(opQCol->tableName)) !=
                this->context.activeTableAliases.end()) {
                operandNoWrap = true;
            }
            if (this->context.withCteCpp20Monikers()) {
                auto key = normalizeSqlIdentifier(opQCol->tableName);
                if (this->context.activeCteTypedefByTableKey.find(key) !=
                    this->context.activeCteTypedefByTableKey.end()) {
                    operandNoWrap = true;
                }
            }
        }
        // sqlite_orm classifies `negated_condition_t` — what a NOT and the `!predicate` spelling
        // of a negated BETWEEN, LIKE, GLOB and MATCH generate — as neither negatable nor an
        // operator argument, so a second NOT over one does not compile at all. A CAST to INTEGER
        // makes it an operand again and leaves what it stands for alone: the inner NOT is 0, 1 or
        // NULL, and a CAST to INTEGER keeps all three, so `NOT NOT a` and
        // `NOT CAST(NOT a AS INTEGER)` answer alike (checked against sqlite3 3.51 over integers,
        // reals, text, a blob and NULL).
        const bool castsNegatedOperand =
            unaryOp->unaryOperator == UnaryOperator::logicalNot && generatesNegatedCondition(*unaryOp->operand);
        if (castsNegatedOperand) {
            operandCode = "cast<" + sqliteTypeToCpp("INTEGER") + ">(" + operandCode + ")";
            this->context.recordComment(sourceSpanComment(kCommentNegatedConditionCast, *unaryOp->operand));
        }

        // A concatenation needs the same CAST for the same reason — a `conc_t` is
        // `binary_operator<L, R, conc_string>` and nothing else, so it is neither negatable nor
        // an operator argument — but it has to be a CAST to REAL rather than to INTEGER. A
        // concatenation answers TEXT (or NULL), and SQLite reads the truth of a text value
        // through its real value, not its integer one: `NOT ('0' || '.5')` is 0, where
        // `NOT CAST('0' || '.5' AS INTEGER)` is 1. A CAST to REAL parses the text exactly as
        // that truth test does, so the two agree for every value.
        const bool castsConcatenatedOperand =
            unaryOp->unaryOperator == UnaryOperator::logicalNot && generatesConcatenation(*unaryOp->operand);
        if (castsConcatenatedOperand) {
            operandCode = "cast<" + sqliteTypeToCpp("REAL") + ">(" + operandCode + ")";
            this->context.recordComment(sourceSpanComment(kCommentConcatenationCast, *unaryOp->operand));
        }

        // A column reference under a NOT is generated as a column pointer rather than wrapped in
        // `c(...)`; the two forms stand in the same place, so the wrapper is skipped for it.
        const bool wrapsOperandInC = operandLeaf && !operandNoWrap && !nodeGeneratesColumnPointer(&operandNode);
        const bool operandIsColumnPointerUnderNot = wrapsOperandInC && operandGeneratedColumnPointerUnderNot;
        if (operandIsColumnPointerUnderNot) {
            // The column is what takes the other form, so the column is what the hint
            // underlines — the operand as it stands under the NOT, a COLLATE stepped through.
            this->context.recordComment(sourceSpanComment(kCommentNotColumnPointer, operandNode));
        }

        // The same wrapper hides a value from the walk that binds one: the literal of
        // `select(not c(0))` was never bound, so the statement ran with an empty parameter and
        // answered NULL where SQLite answers 1. A binary operator unwraps what it is given, and
        // `0 + x` is the numeric coercion SQLite applies to `x` in a boolean context anyway —
        // `NOT x` and `NOT (0 + x)` answer alike for every value (checked against sqlite3 3.51
        // over 33 literals: integers, reals, text that does and does not convert, blobs, NULL).
        const bool operandIsAddedToZeroUnderNot =
            notKeepsOperandQuoted && wrapsOperandInC && generatesBoundValue(operandNode);
        if (operandIsAddedToZeroUnderNot) {
            this->context.recordComment(sourceSpanComment(kCommentNotValueAddedToZero, operandNode));
        }

        SpannedCode operandStr;
        if (castsNegatedOperand || castsConcatenatedOperand) {
            // A CAST delimits itself, both in C++ and in the SQL sqlite_orm serializes.
            operandStr = operandCode;
        } else if (operandIsAddedToZeroUnderNot) {
            operandStr = "(c(0) + " + operandCode + ")";
        } else if (wrapsOperandInC && !operandIsColumnPointerUnderNot) {
            operandStr = wrap(operandCode);
        } else if (!operandLeaf && !generatesZeroMinusSubtraction(operandNode)) {
            operandStr = "(" + operandCode + ")";
        } else {
            operandStr = operandCode;
        }

        // An operand that is not a numeric constant keeps its negation as a subtraction from zero.
        // SQLite computes `0 - x` exactly like `-x` — same value and same typeof for every operand
        // kind — and sqlite_orm serializes and reads that back correctly, where its unary_minus_t
        // hands the caller 0 and throws over a column. The one operand this cannot carry is a
        // predicate: sqlite_orm leaves `a BETWEEN 1 AND 9` unparenthesized and SQLite binds it
        // looser than a binary `-`, so `0 - a BETWEEN 1 AND 9` would regroup the expression.
        bool negationAsSubtraction = false;
        if (unaryOp->unaryOperator == UnaryOperator::minus) {
            if (negationForm == NegationForm::zeroMinusSubtraction) {
                negationAsSubtraction = true;
                // The negation as a whole is what `0 - expr` is generated for, sign included.
                this->context.recordComment(sourceSpanComment(kCommentNegationAsZeroMinus, *unaryOp));
            } else {
                operandResult.warnings.push_back(CodegenWarning{
                    "unary minus over a predicate (" + std::string(sqlPredicateLooserThanMinus(operandNode)) +
                        ") has no working sqlite_orm form; the generated negation does not "
                        "reproduce what SQLite computes and does not compile",
                    unaryOp->location,
                    1});
            }
            if (numericLiteralRejectsFoldedSign(*unaryOp->operand)) {
                // The sign stays out of the C++ constant, which could not hold it, so the
                // subtraction above is generated instead. SQLite has no value for this
                // expression either: it refuses the statement that uses it, and only a DDL
                // clause — which SQLite stores without compiling — gets this far.
                const auto& tooBigLiteral = dynamic_cast<const IntegerLiteralNode&>(*unaryOp->operand);
                operandResult.warnings.push_back(
                    CodegenWarning{"hex literal too big: -" + withoutDigitSeparators(tooBigLiteral.value) +
                                       "; SQLite refuses this expression wherever it is used, so the generated "
                                       "subtraction from zero does not reproduce it",
                                   unaryOp->location,
                                   1});
            }
        }

        std::string_view unaryFuncName;
        std::string_view opStr;
        if (unaryOp->unaryOperator == UnaryOperator::minus) {
            unaryFuncName = "minus";
            opStr = "-";
        } else if (unaryOp->unaryOperator == UnaryOperator::bitwiseNot) {
            unaryFuncName = "bitwise_not";
            opStr = "~";
        } else {
            unaryFuncName = "not_";
            opStr = "not ";
        }
        SpannedCode operatorCode = opStr + operandStr;
        SpannedCode functionalCode = std::string(unaryFuncName) + "(" + operandCode + ")";
        if (negationAsSubtraction) {
            // `~` and `not` bind tighter than every binary operator C++ gives them, a subtraction
            // does not, so the operator spelling carries its own parentheses to stay one operand.
            operatorCode = "(c(0) - " + operandStr + ")";
            functionalCode = "sub(0, " + operandCode + ")";
        }

        // Neither `negated_condition_t` nor `bitwise_not_t` has a default constructor, and a
        // negation generated as a subtraction from zero is a `binary_operator`, which has none
        // either — so none of the three can stand in a trigger's WHEN clause.
        if (unaryOp->unaryOperator == UnaryOperator::logicalNot) {
            this->context.recordFormWithoutDefaultConstructor("NOT");
        } else if (unaryOp->unaryOperator == UnaryOperator::bitwiseNot) {
            this->context.recordFormWithoutDefaultConstructor("~");
        } else if (negationAsSubtraction) {
            this->context.recordFormWithoutDefaultConstructor("unary -");
        }

        std::string chosenUnaryVal = "operator";
        SpannedCode emittedUnary = operatorCode;
        if (policyEquals(this->context.codeGenPolicy, "expr_style", "functional") &&
            unaryOp->unaryOperator != UnaryOperator::logicalNot) {
            chosenUnaryVal = "functional";
            emittedUnary = functionalCode;
        } else if (policyEquals(this->context.codeGenPolicy, "expr_style", "operator_excl") &&
                   unaryOp->unaryOperator == UnaryOperator::logicalNot) {
            chosenUnaryVal = "operator_excl";
            emittedUnary = "!" + operandStr;
        }

        // options lists every variant (the chosen one included).
        std::vector<Option> options;
        options.push_back(Option{"operator", operatorCode.text(), "operator style"});
        if (unaryOp->unaryOperator == UnaryOperator::logicalNot) {
            // sqlite_orm negates via operator! / `not` only; there is no functional form.
            options.push_back(Option{"operator_excl", "!" + operandStr.text(), "use ! instead of not"});
        } else {
            options.push_back(Option{"functional", functionalCode.text(), "functional style"});
        }

        decisionPoints.push_back(DecisionPoint{this->context.nextDecisionPointId++,
                                               "expr_style",
                                               chosenUnaryVal,
                                               emittedUnary.text(),
                                               std::move(options)});

        return spannedResult(std::move(emittedUnary),
                             std::move(decisionPoints),
                             std::move(operandResult.warnings),
                             std::move(operandResult.errors));
    }

}  // namespace sqlite2orm
