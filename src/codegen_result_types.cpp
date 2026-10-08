#include "codegen_utils.h"
#include "codegen_literal_digits.h"
#include "codegen_context.h"
#include "codegen_forms.h"
#include "select_scope_columns.h"

#include <sqlite2orm/utils.h>
#include <sqlite2orm/validator.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <utility>

namespace sqlite2orm {

    namespace {

        /** Whether `functionLower` is one of `names`. */
        bool isOneOfFunctions(std::string_view functionLower, std::initializer_list<std::string_view> names) {
            return std::find(names.begin(), names.end(), functionLower) != names.end();
        }

        /**
         *  Whether SQLite answers a call of this built-in function with a value even when an
         *  argument is NULL. Checked against libsqlite3 3.45.1 over a NULL argument: `hex(NULL)` is
         *  the empty text, `quote(NULL)` the text 'NULL', `typeof(NULL)` 'null', `char(NULL)` the
         *  empty text, `json_object('k', NULL)` '{"k":null}', `json_quote(NULL)` 'null',
         *  `randomblob(NULL)` and `zeroblob(NULL)` a blob, `soundex(NULL)` the text '?000' (in a
         *  build with SQLITE_SOUNDEX, as Debian's is); the ones taking no argument have nothing
         *  to propagate. The aggregates here answer a value over an empty rowset too: `count` 0,
         *  `total` 0.0, `json_group_array` '[]' and `json_group_object` '{}'. The names 3.45.1 has
         *  no function for were checked against sqlite3 3.51.0: `concat(NULL)` is the empty text
         *  and `unistr_quote(NULL)` the text 'NULL'.
         */
        bool sqliteFunctionNeverAnswersNull(std::string_view functionLower) {
            return isOneOfFunctions(functionLower,
                                    {"changes",
                                     "char",
                                     "concat",
                                     "count",
                                     "hex",
                                     "json_group_array",
                                     "json_group_object",
                                     "json_object",
                                     "json_quote",
                                     "last_insert_rowid",
                                     "pi",
                                     "quote",
                                     "random",
                                     "randomblob",
                                     "soundex",
                                     "sqlite_source_id",
                                     "sqlite_version",
                                     "total",
                                     "total_changes",
                                     "typeof",
                                     "unistr_quote",
                                     "zeroblob"});
        }

        /**
         *  Whether the only way SQLite answers a call of this built-in function with NULL is a NULL
         *  argument. Every other known function answers NULL over arguments the SQL spells out —
         *  `nullif(1, 1)`, `date('bogus')`, `unicode('')`, `sign('abc')`, `substr(x'', 1)`,
         *  `printf('')`, `json_extract('{}', '$.a')`, `sqrt(-1)`, `ln(0)`, `sin('a')` — or, being an
         *  aggregate, over an empty rowset: `avg`, `group_concat`, `max`, `min` and `sum`. This is
         *  the list SQLite answered a value for over every non-NULL argument of a 11.7M expression
         *  corpus run through libsqlite3 3.45.1, the version this project links, which unlike the
         *  `sqlite3` CLI in the image carries the math functions. The names 3.45.1 has no function
         *  for — `concat_ws`, `json_pretty`, `unistr`, `if` — and the ones the corpus did not
         *  reach were checked by hand against sqlite3 3.51.0: `octet_length(x'')` is 0,
         *  `json_error_position('x')` 1, `sqlite_compileoption_used('')` 0, `concat_ws(1, NULL)`
         *  the empty text, and a malformed argument to `json_pretty` or `unistr` is an error rather
         *  than a NULL. `iif` and its spelling `if` belong here for their three-argument form
         *  alone, which is why the name is not enough to answer with — see
         *  `iifNotInItsThreeArgumentForm`.
         */
        bool sqliteFunctionOnlyPropagatesANullArgument(std::string_view functionLower) {
            return isOneOfFunctions(functionLower,
                                    {"abs",
                                     "coalesce",
                                     "concat_ws",
                                     "glob",
                                     "if",
                                     "ifnull",
                                     "iif",
                                     "instr",
                                     "json",
                                     "json_array",
                                     "json_error_position",
                                     "json_patch",
                                     "json_pretty",
                                     "json_valid",
                                     "length",
                                     "like",
                                     "likelihood",
                                     "likely",
                                     "lower",
                                     "ltrim",
                                     "octet_length",
                                     "replace",
                                     "round",
                                     "rtrim",
                                     "sqlite_compileoption_used",
                                     "trim",
                                     "unistr",
                                     "unlikely",
                                     "upper"});
        }

        /**
         *  Whether `functionCall` is an `iif` in any arity but three. SQLite 3.48 added
         *  `iif(X, Y)` as a spelling of `iif(X, Y, NULL)`, so it answers NULL whenever X is false
         *  whatever the two arguments hold — `SELECT iif(0, 1)` is NULL — while the three-argument
         *  form merely propagates a NULL. sqlite_orm declares the three-argument form alone, as the
         *  common type of the second and third argument, so the short one is not typed nullably
         *  either; that it has no overload at all is what an arity check would report, and this file
         *  does not run one.
         */
        bool iifNotInItsThreeArgumentForm(const FunctionCallNode& functionCall, std::string_view functionLower) {
            return (functionLower == "iif" || functionLower == "if") && functionCall.arguments.size() != 3;
        }

        /** Whether any of `nodes`, the absent ones skipped, may be NULL. */
        bool anyOperandMayBeNull(std::initializer_list<const AstNode*> nodes) {
            for (const AstNode* node: nodes) {
                if (node && expressionMayBeNull(*node)) {
                    return true;
                }
            }
            return false;
        }

        /** Whether any argument of `functionCall` may be NULL. */
        bool anyFunctionArgumentMayBeNull(const FunctionCallNode& functionCall) {
            for (const AstNodePointer& argument: functionCall.arguments) {
                if (argument && expressionMayBeNull(*argument)) {
                    return true;
                }
            }
            return false;
        }

        /**
         *  Whether SQLite can answer a call of `functionCall` with NULL. Only the two lists above
         *  rule it out; every other built-in answers NULL over arguments that hold none, the same
         *  way an expression this file does not know does. The two directions do not cost the same:
         *  a call widened for nothing carries an `std::optional` that is always engaged, while a
         *  call left narrow hands the caller 0 or the empty string where the row held NULL.
         */
        bool functionCallMayBeNull(const FunctionCallNode& functionCall) {
            const std::string functionLower = toLowerAscii(functionCall.name);
            if (!isKnownSqlFunction(functionLower)) {
                // A user-defined function is called through the generated struct's `operator()`,
                // and its declared return type is what the row carries back, never a NULL.
                return false;
            }
            if (sqliteFunctionNeverAnswersNull(functionLower)) {
                return false;
            }
            if (iifNotInItsThreeArgumentForm(functionCall, functionLower)) {
                return true;
            }
            if (!sqliteFunctionOnlyPropagatesANullArgument(functionLower)) {
                return true;
            }
            return anyFunctionArgumentMayBeNull(functionCall);
        }

        /**
         *  Whether sqlite_orm already types a call of this built-in function nullably. This asks
         *  what the generated C++ carries back, not what SQLite can answer — that one is
         *  `expressionMayBeNull` — so a name can belong to both lists. `abs`, `max`, `median`,
         *  `min`, the three `percentile` aggregates, `sqlite_compileoption_get`, `sqlite_offset`
         *  and `sum` are declared `std::unique_ptr`, and `coalesce`, `ifnull`, `nullif`, `iif` and
         *  `if` (their three-argument form, the only one sqlite_orm declares), `likely`, `unlikely` and
         *  `likelihood` are declared as the result of an argument, so the call is nullable exactly
         *  when sqlite_orm types that argument nullably — a nullable column makes it an
         *  `std::optional`. Widening one of those would nest a second nullable around the first.
         *  This answers over the name alone, and that is also the hole it leaves: `length(a)` and
         *  `CAST(a AS INT)` are typed `int` however NULL the row is, so `iif(1, length(a), 2)` and
         *  `likely(length(a))` are typed `int` too and read a NULL back as 0. Asking the argument
         *  rather than the name is a rule of its own; a known hole, carded separately.
         *  `coalesce`, `ifnull`, `nullif` and `iif` drop out of the list where the call spells its
         *  own result type: what sqlite_orm deduces from the arguments is then not what the row is
         *  read back into — see `functionCallResultTypeArgument`.
         */
        bool generatedFunctionResultIsAlreadyNullable(const FunctionCallNode& functionCall,
                                                      const ReferencedColumnResolver& resolveColumn) {
            const std::string functionLower = toLowerAscii(functionCall.name);
            if (iifNotInItsThreeArgumentForm(functionCall, functionLower)) {
                return false;
            }
            if (!functionCallSpelledResultType(functionCall, resolveColumn).empty()) {
                // A call that spells its result type is read back as exactly that type, and none of
                // the types spelled holds a NULL — neither the `std::string` or `std::vector<char>`
                // of the fallbacks nor the `bool`, `int`, `int64_t` or `double` a call whose
                // arguments all carry a number spells — so the widening this answers for is back on.
                return false;
            }
            return isOneOfFunctions(functionLower,
                                    {"abs",
                                     "coalesce",
                                     "if",
                                     "ifnull",
                                     "iif",
                                     "likelihood",
                                     "likely",
                                     "max",
                                     "median",
                                     "min",
                                     "nullif",
                                     "percentile",
                                     "percentile_cont",
                                     "percentile_disc",
                                     "sqlite_compileoption_get",
                                     "sqlite_offset",
                                     "sum",
                                     "unlikely"});
        }

        /**
         *  Whether `+`, `-` or `*` over the operands of `binaryOperator` can answer NULL although
         *  neither operand is one. SQLite computes in doubles as soon as a REAL takes part and has
         *  no storage class for the NaN that `Inf - Inf`, `Inf + -Inf` and `0 * Inf` run into, so
         *  it stores that one as NULL: `SELECT typeof(0 * (1e300 * 1e300))` is null. Every other
         *  double a computation reaches — an overflow to an infinity of its own, an INTEGER past
         *  the int64 range — stays a REAL. Defined further down, where the bounds it reads live.
         */
        bool arithmeticMayOverflowToNull(const BinaryOperatorNode& binaryOperator);

    }  // namespace

    bool expressionMayBeNull(const AstNode& astNode) {
        if (dynamic_cast<const IntegerLiteralNode*>(&astNode) || dynamic_cast<const RealLiteralNode*>(&astNode) ||
            dynamic_cast<const StringLiteralNode*>(&astNode) || dynamic_cast<const BoolLiteralNode*>(&astNode) ||
            dynamic_cast<const BlobLiteralNode*>(&astNode) ||
            dynamic_cast<const CurrentDatetimeLiteralNode*>(&astNode)) {
            return false;
        }
        if (dynamic_cast<const IsNullNode*>(&astNode) || dynamic_cast<const IsNotNullNode*>(&astNode) ||
            dynamic_cast<const ExistsNode*>(&astNode)) {
            // SQLite answers these over a NULL operand too; they are the tests for one.
            return false;
        }
        if (auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&astNode)) {
            switch (binaryOp->binaryOperator) {
                case BinaryOperator::isOp:
                case BinaryOperator::isNot:
                case BinaryOperator::isDistinctFrom:
                case BinaryOperator::isNotDistinctFrom:
                    // `NULL IS 1` is 0, not NULL: these compare NULL rather than propagate it.
                    return false;
                case BinaryOperator::divide:
                case BinaryOperator::modulo:
                    // `1 / 0` and `1 % 0` are NULL in SQLite, however plain the operands are.
                    return true;
                case BinaryOperator::jsonArrow:
                case BinaryOperator::jsonArrow2:
                    // A path the JSON does not hold answers NULL over operands that are none:
                    // `'{"a":1}' -> '$.zz'` and `'{"a":1}' ->> '$.zz'` are both NULL. The
                    // JSON_EXTRACT call both are generated as answers NULL for a JSON null at
                    // the path as well.
                    return true;
                case BinaryOperator::add:
                case BinaryOperator::subtract:
                case BinaryOperator::multiply:
                    // A NaN is the one value SQLite has no storage class for, so it stores one as
                    // NULL: `1e300 * 1e300 - 1e300 * 1e300` and `0 * (1e300 * 1e300)` are NULL over
                    // operands that are none.
                    return expressionMayBeNull(*binaryOp->lhs) || expressionMayBeNull(*binaryOp->rhs) ||
                           arithmeticMayOverflowToNull(*binaryOp);
                default:
                    return expressionMayBeNull(*binaryOp->lhs) || expressionMayBeNull(*binaryOp->rhs);
            }
        }
        if (auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
            return expressionMayBeNull(*unaryOp->operand);
        }
        if (auto* collate = dynamic_cast<const CollateNode*>(&astNode)) {
            return expressionMayBeNull(*collate->operand);
        }
        if (auto* between = dynamic_cast<const BetweenNode*>(&astNode)) {
            // `1 BETWEEN NULL AND 9` is NULL and `1 BETWEEN NULL AND 0` is 0, so an operand that
            // can be NULL is what makes the whole test one. Negating it changes nothing.
            return anyOperandMayBeNull({between->operand.get(), between->low.get(), between->high.get()});
        }
        if (auto* in = dynamic_cast<const InNode*>(&astNode)) {
            if (!in->tableName.empty() || in->subquerySelect) {
                // What the right-hand side holds is not spelled out here, and `1 IN (SELECT a)`
                // over a NULL row is NULL.
                return true;
            }
            if (in->values.empty()) {
                // An empty list is the one form SQLite answers without looking at the operand:
                // `NULL IN ()` is 0.
                return false;
            }
            if (expressionMayBeNull(*in->operand)) {
                return true;
            }
            for (const AstNodePointer& value: in->values) {
                if (value && expressionMayBeNull(*value)) {
                    return true;
                }
            }
            return false;
        }
        if (auto* like = dynamic_cast<const LikeNode*>(&astNode)) {
            // The ESCAPE operand counts too: `'x' LIKE 'x' ESCAPE NULL` is NULL rather than an error.
            return anyOperandMayBeNull({like->operand.get(), like->pattern.get(), like->escape.get()});
        }
        if (auto* glob = dynamic_cast<const GlobNode*>(&astNode)) {
            return anyOperandMayBeNull({glob->operand.get(), glob->pattern.get()});
        }
        if (auto* cast = dynamic_cast<const CastNode*>(&astNode)) {
            // A CAST changes the type of a value, never whether it is one: `CAST(NULL AS TEXT)` is NULL.
            return expressionMayBeNull(*cast->operand);
        }
        if (auto* caseExpression = dynamic_cast<const CaseNode*>(&astNode)) {
            // A CASE answers the result of the branch it takes, the ELSE result where no branch
            // does, and a NULL where neither is there: `SELECT CASE WHEN 0 THEN 1 END` is NULL. The
            // operand and the conditions pick the branch rather than fill it, so a NULL among them
            // only takes no branch — `CASE NULL WHEN 1 THEN 2 ELSE 3 END` and
            // `CASE WHEN NULL THEN 2 ELSE 3 END` are both 3 — and neither is asked here.
            if (!caseExpression->elseResult) {
                // Whether a branch always matches is not asked: it would take evaluating what
                // SQLite makes of a condition — `'1'` is true and `'x'` is false — and getting
                // that wrong the other way leaves a NULL narrow again, while a `CASE WHEN 1 THEN 2
                // END` widened for nothing carries an `std::optional` that is always engaged.
                return true;
            }
            for (const CaseBranch& branch: caseExpression->branches) {
                if (expressionMayBeNull(*branch.result)) {
                    return true;
                }
            }
            return expressionMayBeNull(*caseExpression->elseResult);
        }
        if (auto* functionCall = dynamic_cast<const FunctionCallNode*>(&astNode)) {
            return functionCallMayBeNull(*functionCall);
        }
        return true;
    }

    namespace {

        /** The largest magnitude a `double` still holds every integer up to, i.e. 2^53. */
        constexpr std::int64_t kDoubleExactIntegerLimit = 9007199254740992;

        /** What a `double` has to carry for an expression, see `MagnitudeBound`. */
        enum class ResultBound {
            /** SQLite answers a REAL or a NULL, both of which a `double` carries as they are. */
            exact,
            /** SQLite answers a REAL, a NULL or an INTEGER of at most `magnitude`. */
            bounded,
            /** Nothing is known: the SQL no longer spells the value out. */
            unbounded,
        };

        /**
         *  What sqlite_orm's `double` has to carry for an expression. The magnitude is an upper
         *  bound and lives in the int64 domain SQLite computes in — rounding it into a `double`
         *  would lose the very integers this bound exists to find — so every step past the int64
         *  range saturates to `unbounded` instead of growing. An INTEGER SQLite cannot hold is a
         *  REAL anyway, which the caller reads back exactly.
         */
        struct MagnitudeBound {
            ResultBound kind = ResultBound::unbounded;
            std::int64_t magnitude = 0;
        };

        constexpr MagnitudeBound exactBound{ResultBound::exact, 0};
        constexpr MagnitudeBound unboundedBound{ResultBound::unbounded, 0};

        MagnitudeBound boundedBy(std::int64_t magnitude) {
            return MagnitudeBound{ResultBound::bounded, magnitude};
        }

        /** Whether `bound` is an INTEGER answer of at most `magnitude`. */
        bool isBounded(const MagnitudeBound& bound) {
            return bound.kind == ResultBound::bounded;
        }

        /** `left + right` over two magnitudes, both of them at least zero, saturating to `unbounded`. */
        MagnitudeBound saturatingSum(std::int64_t left, std::int64_t right) {
            static constexpr std::int64_t int64Max = std::numeric_limits<std::int64_t>::max();
            return left > int64Max - right ? unboundedBound : boundedBy(left + right);
        }

        /** `left * right` over two magnitudes, both of them at least zero, saturating to `unbounded`. */
        MagnitudeBound saturatingProduct(std::int64_t left, std::int64_t right) {
            static constexpr std::int64_t int64Max = std::numeric_limits<std::int64_t>::max();
            if (left == 0 || right == 0) {
                return boundedBy(0);
            }
            return left > int64Max / right ? unboundedBound : boundedBy(left * right);
        }

        /** The magnitude of `value` as a bound, the int64 minimum saturating to `unbounded`. */
        MagnitudeBound magnitudeOf(std::int64_t value) {
            static constexpr std::int64_t int64Min = std::numeric_limits<std::int64_t>::min();
            // The magnitude of the int64 minimum does not fit an int64, and it is past the range a
            // double holds exactly anyway.
            return value == int64Min ? unboundedBound : boundedBy(value < 0 ? -value : value);
        }

        /**
         *  What a `double` has to carry for the value SQLite answers `astNode` with. A REAL and a
         *  NULL are carried as they are, so a node answering one of those is `exact`: `+`, `-`,
         *  `*`, `/` and `%` all answer a REAL as soon as an operand is one and a NULL as soon as
         *  an operand is one — checked over 1710 operand pairs against sqlite3 3.51. An INTEGER is
         *  bounded only while the SQL spells the operands out; a column, a call or a subquery is a
         *  value only SQLite knows.
         */
        MagnitudeBound integerMagnitudeBound(const AstNode& astNode) {
            if (auto* collate = dynamic_cast<const CollateNode*>(&astNode)) {
                // COLLATE decides how a value compares, not what the value is.
                return integerMagnitudeBound(*collate->operand);
            }
            std::size_t foldedSigns = 0;
            const AstNode& literal = *withoutFoldedSigns(astNode, foldedSigns);
            if (dynamic_cast<const IntegerLiteralNode*>(&literal) != nullptr) {
                const std::optional<std::int64_t> value = integerLiteralInt64Value(astNode);
                // A decimal literal past the int64 range — the sign SQLite folds in already
                // counted, so `-9223372036854775808` is not one — is a REAL to SQLite.
                return value ? magnitudeOf(*value) : exactBound;
            }
            if (dynamic_cast<const RealLiteralNode*>(&literal) != nullptr ||
                dynamic_cast<const NullLiteralNode*>(&literal) != nullptr) {
                return exactBound;
            }
            if (dynamic_cast<const BoolLiteralNode*>(&literal) != nullptr) {
                // SQLite spells TRUE and FALSE as the integers 1 and 0.
                return boundedBy(1);
            }
            if (auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
                switch (unaryOperator->unaryOperator) {
                    case UnaryOperator::plus:
                    case UnaryOperator::minus:
                        // `+x` is a no-op to SQLite and a negation keeps the magnitude it is given.
                        return integerMagnitudeBound(*unaryOperator->operand);
                    case UnaryOperator::bitwiseNot: {
                        // `~` casts its operand to an INTEGER first, so a REAL operand is not carried
                        // through it the way it is through the arithmetic operators: `~1.5` answers
                        // the INTEGER -2 and `~1e300` the int64 minimum. Only an operand already
                        // bounded as an INTEGER bounds `~x`, which is `-x - 1`; a NULL answers NULL.
                        if (dynamic_cast<const NullLiteralNode*>(unaryOperator->operand.get()) != nullptr) {
                            return exactBound;
                        }
                        const MagnitudeBound operand = integerMagnitudeBound(*unaryOperator->operand);
                        return isBounded(operand) ? saturatingSum(operand.magnitude, 1) : unboundedBound;
                    }
                    case UnaryOperator::logicalNot:
                        // `NOT x` answers 0, 1 or NULL.
                        return boundedBy(1);
                }
                return unboundedBound;
            }
            if (auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&astNode)) {
                switch (binaryOperator->binaryOperator) {
                    case BinaryOperator::add:
                    case BinaryOperator::subtract:
                    case BinaryOperator::multiply:
                    case BinaryOperator::divide:
                    case BinaryOperator::modulo:
                        break;
                    default:
                        // Every other operator either answers a value of its own — a bitwise result is
                        // an INTEGER of any magnitude, a comparison a 0, 1 or NULL — or is not an
                        // operand an arithmetic result column is reached through.
                        return unboundedBound;
                }
                const MagnitudeBound left = integerMagnitudeBound(*binaryOperator->lhs);
                const MagnitudeBound right = integerMagnitudeBound(*binaryOperator->rhs);
                if (left.kind == ResultBound::exact || right.kind == ResultBound::exact) {
                    // A REAL operand makes the whole answer a REAL and a NULL operand a NULL.
                    return exactBound;
                }
                switch (binaryOperator->binaryOperator) {
                    case BinaryOperator::add:
                    case BinaryOperator::subtract:
                        return isBounded(left) && isBounded(right) ? saturatingSum(left.magnitude, right.magnitude)
                                                                   : unboundedBound;
                    case BinaryOperator::multiply:
                        // A factor of zero answers zero however large the other side is.
                        if ((isBounded(left) && left.magnitude == 0) || (isBounded(right) && right.magnitude == 0)) {
                            return boundedBy(0);
                        }
                        return isBounded(left) && isBounded(right) ? saturatingProduct(left.magnitude, right.magnitude)
                                                                   : unboundedBound;
                    case BinaryOperator::divide:
                        // An integer division never grows the dividend, and a zero divisor is NULL, so
                        // the dividend bounds the quotient whatever the divisor is.
                        return left;
                    case BinaryOperator::modulo:
                        // A remainder is smaller than both the dividend and the divisor, so either of
                        // them bounds it on its own.
                        if (isBounded(left) && isBounded(right)) {
                            return boundedBy(std::min(left.magnitude, right.magnitude));
                        }
                        return isBounded(left) ? left : right;
                    default:
                        return unboundedBound;
                }
            }
            return unboundedBound;
        }

        /**
         *  The double SQLite computes with for a numeric literal, the signs folded in front of it
         *  applied, and nothing for any other node. A decimal literal past the int64 range is a
         *  REAL to SQLite and reaches an infinity of its own — a `1` followed by 400 zeros answers
         *  Inf — so the text is read rather than the int64 the literal does not hold. A string and
         *  a blob literal are left out: SQLite reads a number out of their bytes by a rule of its
         *  own, where `'9e999' + 0` is Inf while `x'41' + 0` is 0.
         */
        std::optional<double> numericLiteralDoubleValue(const AstNode& astNode) {
            std::size_t foldedSigns = 0;
            const AstNode& literal = *withoutFoldedSigns(astNode, foldedSigns);
            const double sign = foldedSigns % 2 == 0 ? 1.0 : -1.0;
            if (auto* collate = dynamic_cast<const CollateNode*>(&literal)) {
                // COLLATE decides how a value compares, not what the value is, which is what the
                // two helpers next to this one look through it for as well. It binds tighter than
                // a sign does, so `-1e300 COLLATE BINARY` is `-(1e300 COLLATE BINARY)`.
                if (const std::optional<double> value = numericLiteralDoubleValue(*collate->operand)) {
                    return sign * *value;
                }
                return std::nullopt;
            }
            if (auto* boolLiteral = dynamic_cast<const BoolLiteralNode*>(&literal)) {
                // SQLite spells TRUE and FALSE as the integers 1 and 0.
                return sign * (boolLiteral->value ? 1.0 : 0.0);
            }
            // This one folds the signs in itself, which is why it is handed the node as it came.
            if (const std::optional<std::int64_t> integer = integerLiteralInt64Value(astNode)) {
                return static_cast<double>(*integer);
            }
            const std::string_view text = numericLiteralText(literal);
            if (text.empty() || isHexadecimalIntegerLiteral(text)) {
                // A hex literal past the int64 range is refused before codegen, so the value
                // above is the only one it has; anything else here is not a number at all.
                return std::nullopt;
            }
            return sign * decimalLiteralDoubleValue(text);
        }

        /**
         *  Whether SQLite answers `astNode` with an INTEGER or a NULL whatever its operands hold,
         *  which makes every value it carries a finite double. A comparison, a logical operator
         *  and a predicate answer 0, 1 or NULL, and the bitwise operators cast their operands to
         *  an INTEGER first: `~1e300` is the int64 minimum rather than an infinity.
         */
        bool expressionAnswersIntegerOrNull(const AstNode& astNode) {
            if (auto* collate = dynamic_cast<const CollateNode*>(&astNode)) {
                // COLLATE decides how a value compares, not what the value is.
                return expressionAnswersIntegerOrNull(*collate->operand);
            }
            if (auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
                switch (unaryOperator->unaryOperator) {
                    case UnaryOperator::bitwiseNot:
                    case UnaryOperator::logicalNot:
                        return true;
                    case UnaryOperator::plus:
                    case UnaryOperator::minus:
                        // A sign keeps the storage class of the value it stands on.
                        return expressionAnswersIntegerOrNull(*unaryOperator->operand);
                }
                return false;
            }
            if (auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&astNode)) {
                switch (binaryOperator->binaryOperator) {
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
                    case BinaryOperator::bitwiseAnd:
                    case BinaryOperator::bitwiseOr:
                    case BinaryOperator::shiftLeft:
                    case BinaryOperator::shiftRight:
                        return true;
                    default:
                        return false;
                }
            }
            return dynamic_cast<const IsNullNode*>(&astNode) || dynamic_cast<const IsNotNullNode*>(&astNode) ||
                   dynamic_cast<const BetweenNode*>(&astNode) || dynamic_cast<const ExistsNode*>(&astNode) ||
                   dynamic_cast<const InNode*>(&astNode) || dynamic_cast<const LikeNode*>(&astNode) ||
                   dynamic_cast<const GlobNode*>(&astNode) || dynamic_cast<const MatchNode*>(&astNode);
        }

        /**
         *  Whether SQLite can answer `astNode` with an infinite REAL, the operand every NaN an
         *  arithmetic operator runs into is built out of. Conservative the way the rest of this
         *  file is: a value the SQL no longer spells out counts as one.
         */
        bool expressionMayBeInfinite(const AstNode& astNode) {
            if (const std::optional<double> value = numericLiteralDoubleValue(astNode)) {
                return !std::isfinite(*value);
            }
            if (dynamic_cast<const CurrentDatetimeLiteralNode*>(&astNode)) {
                // These answer a timestamp, and an arithmetic operator reads a number off the
                // front of its text: `CURRENT_DATE + 0` is the year.
                return false;
            }
            if (expressionAnswersIntegerOrNull(astNode)) {
                return false;
            }
            if (auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&astNode)) {
                switch (binaryOperator->binaryOperator) {
                    case BinaryOperator::divide:
                    case BinaryOperator::modulo:
                        // The bound below is the dividend, which holds while SQLite divides integers
                        // and not once it divides doubles: `5 / 1e-320` is Inf. Both answer NULL over
                        // a zero divisor anyway, so an operand of theirs never reaches this question.
                        return true;
                    default:
                        break;
                }
            }
            // An INTEGER SQLite answers is a finite double whatever its magnitude, and one it
            // cannot hold it answers as a REAL, which is where an overflow to an infinity starts.
            return !isBounded(integerMagnitudeBound(astNode));
        }

        /**
         *  Whether SQLite can answer `astNode` with a zero, the other operand `0 * Inf` needs.
         *  Only a numeric literal rules it out: a string and a blob read back as 0 as soon as
         *  their bytes do not start with a number, which is why `x'41' * (1e300 * 1e300)` is NULL.
         */
        bool expressionMayBeZero(const AstNode& astNode) {
            const std::optional<double> value = numericLiteralDoubleValue(astNode);
            return !value || *value == 0.0;
        }

        /** An arithmetic operator node and the SQL token it is spelled with. */
        struct ArithmeticOperatorNode {
            const AstNode* node = nullptr;
            std::string_view operatorText;
        };

        /**
         *  The arithmetic operator a result column is read back through, together with the node it
         *  is located at; an empty text for a column read back through anything else.
         */
        ArithmeticOperatorNode doubleTypedResultOperator(const AstNode& astNode) {
            // A COLLATE and a unary plus emit their operand and nothing else, so the sqlite_orm
            // node the result column comes out as is the one the operand under them comes out as.
            const AstNode& generatedNode = generatedOperandNode(astNode);
            if (auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
                // A negation reaches sqlite_orm as the subtraction `0 - x`, which is typed
                // `double` like the other arithmetic operators; the folded and the warned forms
                // carry no operator of their own.
                if (unaryOperator->unaryOperator == UnaryOperator::minus &&
                    negationFormFor(*unaryOperator->operand) == NegationForm::zeroMinusSubtraction) {
                    return {&generatedNode, "-"};
                }
                return {};
            }
            if (auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
                switch (binaryOperator->binaryOperator) {
                    case BinaryOperator::add:
                        return {&generatedNode, "+"};
                    case BinaryOperator::subtract:
                        return {&generatedNode, "-"};
                    case BinaryOperator::multiply:
                        return {&generatedNode, "*"};
                    case BinaryOperator::divide:
                        return {&generatedNode, "/"};
                    case BinaryOperator::modulo:
                        return {&generatedNode, "%"};
                    default:
                        return {};
                }
            }
            return {};
        }

        bool arithmeticMayOverflowToNull(const BinaryOperatorNode& binaryOperator) {
            const AstNode& lhs = *binaryOperator.lhs;
            const AstNode& rhs = *binaryOperator.rhs;
            switch (binaryOperator.binaryOperator) {
                case BinaryOperator::add:
                case BinaryOperator::subtract:
                    // Two infinities that cancel: `1e300 * 1e300 - 1e300 * 1e300` is NULL, and so is
                    // `-(1e300 * 1e300) + 1e300 * 1e300`.
                    return expressionMayBeInfinite(lhs) && expressionMayBeInfinite(rhs);
                case BinaryOperator::multiply:
                    // An infinity times a zero: `0 * (1e300 * 1e300)`.
                    return (expressionMayBeInfinite(lhs) && expressionMayBeZero(rhs)) ||
                           (expressionMayBeZero(lhs) && expressionMayBeInfinite(rhs));
                default:
                    return false;
            }
        }

    }  // namespace

    bool selectResultNeedsIntegerCast(const AstNode& astNode) {
        // A COLLATE and a unary plus emit their operand and nothing else, so the sqlite_orm node
        // the result column comes out as — and with it the type the row is read back into — is the
        // one the operand under them comes out as.
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if (auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
            return unaryOperator->unaryOperator == UnaryOperator::bitwiseNot;
        }
        if (auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
            switch (binaryOperator->binaryOperator) {
                case BinaryOperator::bitwiseAnd:
                case BinaryOperator::bitwiseOr:
                case BinaryOperator::shiftLeft:
                case BinaryOperator::shiftRight:
                    return true;
                default:
                    return false;
            }
        }
        return false;
    }

    std::optional<CodegenWarning> selectResultDoublePrecisionWarning(const AstNode& astNode) {
        const ArithmeticOperatorNode arithmetic = doubleTypedResultOperator(astNode);
        if (arithmetic.operatorText.empty()) {
            return std::nullopt;
        }
        const MagnitudeBound bound = integerMagnitudeBound(astNode);
        if (bound.kind == ResultBound::exact || (isBounded(bound) && bound.magnitude <= kDoubleExactIntegerLimit)) {
            return std::nullopt;
        }
        std::string message = "result column computed with `";
        message += arithmetic.operatorText;
        message += "` is read back through a double: sqlite_orm types `+`, `-`, `*`, `/` and `%` as "
                   "`double`, so an INTEGER result past 2^53 comes back rounded (9223372036854775807 "
                   "reads back as 9223372036854775808)";
        // The operator token is what its node is located at, and it is what the message names.
        return CodegenWarning{std::move(message),
                              arithmetic.node->location,
                              underlineLengthOf(arithmetic.operatorText)};
    }

    std::optional<CodegenWarning> selectResultJsonExtractTypeWarning(const AstNode& astNode) {
        // A COLLATE and a unary plus emit their operand and nothing else, so the call the row is
        // read back through is the one the operand under them comes out as.
        const AstNode& generatedNode = generatedOperandNode(astNode);
        // The written text the warning underlines: the arrow operator a node is located at, or the
        // name a call was written under. Both are the source text as written, quoted in neither.
        std::string_view writtenText;
        SourceLocation location;
        if (auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
            // `->>` is a call over the one path it was written with, and answers the very value
            // that call does. `->` answers the JSON text of that value — always a text, whatever
            // the JSON holds — so a `std::string` loses nothing of what `->` itself answers, and
            // what it does lose is what `->` and the call differ by, which `jsonTextArrowWarning`
            // already reports at these same two characters.
            if (binaryOp->binaryOperator != BinaryOperator::jsonArrow2) {
                return std::nullopt;
            }
            writtenText = jsonArrowOperatorText(binaryOp->binaryOperator);
            location = binaryOp->location;
        } else if (auto* functionCall = dynamic_cast<const FunctionCallNode*>(&generatedNode)) {
            if (functionCall->star || toLowerAscii(functionCall->name) != "json_extract") {
                return std::nullopt;
            }
            // The first argument is the JSON itself, every other one a path into it.
            if (functionCall->arguments.size() != 2) {
                return std::nullopt;
            }
            writtenText = functionCall->name;
            location = functionCall->location;
        } else {
            return std::nullopt;
        }
        return CodegenWarning{"result column taken from JSON_EXTRACT over one path comes back as text: "
                              "SQLite answers the value at the path, of whatever storage class the JSON holds, and "
                              "sqlite_orm deduces no result type for the call, so it is generated as "
                              "`json_extract<std::string>(…)` — a JSON number comes back as its digits. Spell the "
                              "result type the value at the path has where it is known",
                              location,
                              underlineLengthOf(writtenText)};
    }

    namespace {

        /**
         *  Whether the C++ type sqlite_orm gives `argument` is known to hold no NULL. A constant
         *  other than NULL, a column of a NOT NULL field, an operator, a predicate, a CAST, a CASE
         *  and a call sqlite_orm does not already type nullably all answer yes; a column the scope
         *  does not resolve, a bind parameter and a subquery answer no, the type they are read
         *  back as being one this layer does not know.
         */
        bool generatedArgumentTypeHoldsNoNull(const AstNode& argument, const ReferencedColumnResolver& resolveColumn) {
            const AstNode& generatedNode = generatedOperandNode(argument);
            if (const std::optional<GeneratedValueCppType> valueType = generatedValueCppType(generatedNode)) {
                return *valueType != GeneratedValueCppType::null;
            }
            if (dynamic_cast<const ColumnRefNode*>(&generatedNode) ||
                dynamic_cast<const QualifiedColumnRefNode*>(&generatedNode)) {
                const SourceTableColumn* column = resolveColumn(generatedNode);
                return column != nullptr && !column->nullable;
            }
            if (generatedResultColumnCppType(generatedNode) || dynamic_cast<const CaseNode*>(&generatedNode)) {
                return true;
            }
            if (auto* functionCall = dynamic_cast<const FunctionCallNode*>(&generatedNode)) {
                // SQLite refuses a window call nested in the argument of another one.
                return !functionCall->over && !generatedFunctionResultIsAlreadyNullable(*functionCall, resolveColumn);
            }
            return false;
        }

        /**
         *  `expressionMayBeNull` with the schema asked where it answers: a column the scope resolves
         *  holds a NULL exactly when it is not declared NOT NULL, while `expressionMayBeNull`, which
         *  sees no schema, takes every column for one that may.
         */
        bool operandMayBeNull(const AstNode& operand, const ReferencedColumnResolver& resolveColumn) {
            const AstNode& generatedNode = generatedOperandNode(operand);
            if (dynamic_cast<const ColumnRefNode*>(&generatedNode) ||
                dynamic_cast<const QualifiedColumnRefNode*>(&generatedNode)) {
                if (const SourceTableColumn* column = resolveColumn(generatedNode)) {
                    return column->nullable;
                }
            }
            return expressionMayBeNull(operand);
        }

        /**
         *  Whether a call of `lag`, `lead`, `first_value`, `last_value` or `nth_value` can answer
         *  NULL over a value argument that holds none. Checked against sqlite3 3.51 over a NOT NULL
         *  column: `lag(b)` and `lead(b)` are NULL where the row they reach lies outside the
         *  partition, and `lag(b, 1, 0)` answers its default there instead — NULL only where the
         *  default is one, `lag(b, NULL, 0)` included. `first_value(b)` and `last_value(b)` are NULL
         *  over an empty frame, which only a frame spelled out can be — `ROWS BETWEEN 2 PRECEDING
         *  AND 1 PRECEDING` on the first row, `EXCLUDE CURRENT ROW` — while the default frame always
         *  holds the current row. A named window may carry a frame of its own, so it is not taken
         *  for the default one. `nth_value(b, N)` is NULL where the frame holds fewer than N rows,
         *  so only `nth_value(b, 1)` over the default frame answers a value always.
         */
        bool windowValueCallMayAnswerNull(const FunctionCallNode& functionCall,
                                          std::string_view functionLower,
                                          const ReferencedColumnResolver& resolveColumn) {
            if (functionLower == "lag" || functionLower == "lead") {
                if (functionCall.arguments.size() < 3 || !functionCall.arguments.at(2)) {
                    return true;
                }
                return operandMayBeNull(*functionCall.arguments.at(2), resolveColumn);
            }
            const bool frameHoldsTheCurrentRow = !functionCall.over->namedWindow && !functionCall.over->frame;
            if (!frameHoldsTheCurrentRow) {
                return true;
            }
            if (functionLower == "nth_value") {
                const AstNode* position =
                    functionCall.arguments.size() >= 2 ? functionCall.arguments.at(1).get() : nullptr;
                auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(position);
                return integerLiteral == nullptr || integerLiteral->value != "1";
            }
            return false;
        }

        /**
         *  Whether `astNode` is computed over an aggregate call — `count(*)`, `max(a)`, `sum(a) + 1`,
         *  `coalesce(max(a), 0)` — which makes the select it is a result column of an aggregate
         *  query. Only the forms a result column is commonly spelled with are stepped into: an
         *  aggregate under any other node answers no, and the select is taken for one that may
         *  answer no row. A nested subquery is a select of its own, so it is not stepped into.
         */
        bool holdsAggregateCall(const AstNode& astNode) {
            const AstNode& generatedNode = generatedOperandNode(astNode);
            if (auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
                return (binaryOp->lhs && holdsAggregateCall(*binaryOp->lhs)) ||
                       (binaryOp->rhs && holdsAggregateCall(*binaryOp->rhs));
            }
            if (auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
                return unaryOp->operand && holdsAggregateCall(*unaryOp->operand);
            }
            if (auto* castNode = dynamic_cast<const CastNode*>(&generatedNode)) {
                return castNode->operand && holdsAggregateCall(*castNode->operand);
            }
            if (auto* functionCall = dynamic_cast<const FunctionCallNode*>(&generatedNode)) {
                // An aggregate run as a window answers one value per row, not one row per group.
                if (functionCall->over) {
                    return false;
                }
                if (const std::optional<SqliteOrmFunctionForm> form = resolveFunctionCallForm(*functionCall, false)) {
                    if (form->kind == SqliteOrmFormKind::builtinAggregate ||
                        form->kind == SqliteOrmFormKind::countAsterisk ||
                        form->kind == SqliteOrmFormKind::countWithoutType) {
                        return true;
                    }
                }
                for (const auto& argument: functionCall->arguments) {
                    if (argument && holdsAggregateCall(*argument)) {
                        return true;
                    }
                }
            }
            return false;
        }

        /**
         *  Whether a select used as a scalar subquery may answer no row at all, which SQLite reads as
         *  NULL whatever the result column is: checked against sqlite3 3.51 over an empty
         *  `t(a INTEGER NOT NULL)`, `(SELECT a FROM t)`, `(SELECT 1 WHERE 0)`, `(SELECT 1 LIMIT 0)`,
         *  `(SELECT 1 LIMIT 1 OFFSET 1)`, `(SELECT count(*) FROM t GROUP BY a)` and
         *  `(SELECT count(*) FROM t HAVING 0)` are all NULL. An aggregate query with no GROUP BY
         *  answers exactly one row however many it reads — `(SELECT count(*) FROM t)` is 0 and
         *  `(SELECT count(*) WHERE 0)` too — and so does a select with no FROM and no WHERE. Any
         *  LIMIT, OFFSET, GROUP BY or HAVING is taken for one that may empty the rowset.
         */
        bool scalarSubqueryMayAnswerNoRow(const SelectNode& select) {
            if (select.limitValue || select.offsetValue || select.groupBy || select.having) {
                return true;
            }
            for (const auto& column: select.columns) {
                if (column.expression && holdsAggregateCall(*column.expression)) {
                    return false;
                }
            }
            return !select.fromClause.empty() || select.whereClause != nullptr;
        }

        /**
         *  `selectResultNeedsAsOptional` over one scope: `resolveColumn` answers what the emitter
         *  writes a reference of the select the column belongs to as, which is the select this
         *  stands in and not always the one the context holds — a scalar subquery carries a scope
         *  of its own, and the descent into it swaps the resolver for that scope's.
         */
        bool resultNeedsAsOptional(const AstNode& astNode,
                                   const CodeGeneratorContext& context,
                                   const ReferencedColumnResolver& resolveColumn) {
            // A COLLATE and a unary plus emit their operand and nothing else, so the sqlite_orm node
            // the result column comes out as — and with it the type the row is read back into — is the
            // one the operand under them comes out as.
            const AstNode& generatedNode = generatedOperandNode(astNode);
            if (auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
                switch (binaryOp->binaryOperator) {
                    case BinaryOperator::isOp:
                    case BinaryOperator::isNot:
                    case BinaryOperator::isDistinctFrom:
                    case BinaryOperator::isNotDistinctFrom:
                        // `NULL IS 1` is 0, not NULL: the IS family compares NULL rather than
                        // propagates it, so it answers 0 or 1 whatever its operands are.
                        return false;
                    default:
                        // The JSON arrows are generated as a `json_extract<std::string>()` call, whose
                        // result type is as much the type the row is read back into as an operator's
                        // is, and a `std::string` reads a NULL back as an empty string.
                        return expressionMayBeNull(generatedNode);
                }
            }
            if (auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
                // A unary operator is typed from the operator alone too — `-x` is generated as the
                // subtraction `(c(0) - x)`, `~x` as a `bitwise_not_t`. A sign folded into a numeric
                // constant leaves no operator behind, but a constant is never NULL either, so
                // `expressionMayBeNull` already answers no for it.
                if (unaryOp->unaryOperator == UnaryOperator::minus &&
                    negationFormFor(*unaryOp->operand) == NegationForm::unaryOverPredicate) {
                    // The one form codegen already warns has no working sqlite_orm spelling: it does
                    // not compile at all, so there is no result type to widen.
                    return false;
                }
                return expressionMayBeNull(generatedNode);
            }
            if (dynamic_cast<const BetweenNode*>(&generatedNode) || dynamic_cast<const InNode*>(&generatedNode) ||
                dynamic_cast<const LikeNode*>(&generatedNode) || dynamic_cast<const GlobNode*>(&generatedNode)) {
                // sqlite_orm types `between_t`, `in_t`, `like_t` and `glob_t` — and the
                // `negated_condition_t` a NOT form comes out as — as `bool`, so a NULL row was read
                // back as false. A MATCH is left out: SQLite refuses `a MATCH 'x'` as a result column
                // outside an FTS table, and sqlite_orm has no result type for `match_t` either.
                return expressionMayBeNull(generatedNode);
            }
            if (dynamic_cast<const CastNode*>(&generatedNode)) {
                // `cast_t<T, E>` is typed T, the very type the CAST asks for, so a NULL row was read
                // back as the default of that type: `CAST(a AS TEXT)` came back as the empty string.
                return expressionMayBeNull(generatedNode);
            }
            if (dynamic_cast<const CaseNode*>(&generatedNode)) {
                // `case_t<R, …>` is typed R, and R is the widest type inferred over the branch results
                // and the ELSE — `int` where one of them holds a NULL — so a CASE that answers NULL
                // was read back as 0:
                // `SELECT CASE WHEN 1 THEN NULL ELSE 0 END` came out as
                // `case_<int>().when(1, then(nullptr)).else_(0).end()` and read 0 where SQLite answers
                // NULL. The inference never names a nullable type, so nothing here is already widened.
                return expressionMayBeNull(generatedNode);
            }
            if (auto* subquery = dynamic_cast<const SubqueryNode*>(&generatedNode)) {
                // A scalar subquery is read back through the type its own result column comes out as:
                // `(SELECT 1 / 0)` is generated as the nested `select(c(1) / 0)`, and sqlite_orm types
                // a `select_t` as the column list it carries, so the `double` of the division is what
                // the outer row holds. The widening belongs here rather than inside the nested
                // `select(...)`: `as_optional` is the widening of a RESULT column, and the generator
                // the nested select shares with a view body, a CTE, an IN and an INSERT ... SELECT
                // hands its columns to no caller. Wrapping the subquery keeps its SQL byte for byte —
                // `as_optional` serializes its operand and nothing else.
                auto* nestedSelect = dynamic_cast<const SelectNode*>(subquery->select.get());
                // A compound subquery is typed from the common type of its parts, and a nested WITH is
                // not mapped to sqlite_orm codegen at all. Both are left to the arms that own them.
                if (!nestedSelect || nestedSelect->columns.size() != 1u || !nestedSelect->columns.at(0).expression) {
                    return false;
                }
                const AstNode& nestedColumn = *nestedSelect->columns.at(0).expression;
                // The subquery answers a NULL of its own too: SQLite reads a scalar subquery over an
                // empty rowset as NULL, so `(SELECT a FROM t)` over a NOT NULL column is NULL on an
                // empty `t` and was read back as 0. That NULL needs the widening only where the type
                // the column comes out as holds none: a column of an `std::optional` field, a
                // `max(...)` typed `std::unique_ptr` and a column no scope resolves are left alone.
                const auto nestedResultNeedsAsOptional = [&](const ReferencedColumnResolver& nestedResolver) {
                    if (resultNeedsAsOptional(nestedColumn, context, nestedResolver)) {
                        return true;
                    }
                    return scalarSubqueryMayAnswerNoRow(*nestedSelect) &&
                           generatedArgumentTypeHoldsNoNull(nestedColumn, nestedResolver);
                };
                // The nested select names its own sources, so what its column is read back through is
                // answered over ITS FROM clause: asking the outer scope resolved a name of the inner
                // select to a same-named column of an outer table and typed the call from a field the
                // generated code does not read.
                if (nestedSelect->fromClause.empty()) {
                    // A select with no FROM of its own names no scope: every reference in it is
                    // correlated and read over the scope around it, which is the scope this stands
                    // in. Swapping the resolver for a scope that names nothing answered `nullptr`
                    // for every reference, so a spelled result type went unseen. Such a select is
                    // generated only where it names no table — one that does has no sqlite_orm
                    // form and leaves the statement out — so the answer matters for its constants.
                    return nestedResultNeedsAsOptional(resolveColumn);
                }
                const SelectScopeColumns nestedScope(*nestedSelect, context, resolveColumn);
                return nestedResultNeedsAsOptional(nestedScope.resolver());
            }
            if (auto* functionCall = dynamic_cast<const FunctionCallNode*>(&generatedNode)) {
                if (functionCall->over) {
                    const std::string functionLower = toLowerAscii(functionCall->name);
                    if (isOneOfFunctions(functionLower, {"first_value", "lag", "last_value", "lead", "nth_value"})) {
                        // sqlite_orm types these as their value argument, so a nullable argument —
                        // a column of an `std::optional` field — carries a NULL through on its own.
                        // An argument typed without one does not: over a NOT NULL column
                        // `lag(b) OVER (ORDER BY b)` is NULL on the first row and was read back as
                        // 0, and so was `lag(b + 1)`, typed `double` by the operator whatever
                        // NULL `b` holds.
                        if (functionCall->arguments.empty() || !functionCall->arguments.front()) {
                            return false;
                        }
                        const AstNode& value = *functionCall->arguments.front();
                        if (!generatedArgumentTypeHoldsNoNull(value, resolveColumn)) {
                            return false;
                        }
                        return windowValueCallMayAnswerNull(*functionCall, functionLower, resolveColumn) ||
                               operandMayBeNull(value, resolveColumn);
                    }
                    const SqliteOrmFunctionForm* form = sqliteOrmFunctionForm(functionLower);
                    if (form != nullptr && form->kind == SqliteOrmFormKind::windowFunction) {
                        // `row_number`, `rank`, `dense_rank`, `percent_rank`, `cume_dist` and `ntile`
                        // are never NULL.
                        return false;
                    }
                    // An aggregate run as a window answers over its frame what it answers over a
                    // group, so it is widened by the rule a plain call is: `avg(a)` and
                    // `group_concat(a)` are NULL over an empty frame — `ROWS BETWEEN 2 PRECEDING
                    // AND 1 PRECEDING` on the first row — and are typed `double` and `std::string`,
                    // `sum`, `max` and `min` are typed `std::unique_ptr` already, and `count` and
                    // `total` are never NULL.
                }
                if (generatedFunctionResultIsAlreadyNullable(*functionCall, resolveColumn)) {
                    return false;
                }
                return expressionMayBeNull(generatedNode);
            }
            return false;
        }

    }  // namespace

    bool selectResultNeedsAsOptional(const AstNode& astNode, const CodeGeneratorContext& context) {
        return resultNeedsAsOptional(astNode, context, [&context](const AstNode& argument) {
            return context.findReferencedColumn(argument);
        });
    }

    std::optional<std::string> generatedResultColumnCppType(const AstNode& astNode) {
        // A COLLATE and a unary plus emit their operand and nothing else, so the type the row is
        // read back into is the one the operand under them comes out as.
        const AstNode& generatedNode = generatedOperandNode(astNode);
        if (generatesFoldedNegation(generatedNode)) {
            // The sign is folded into the constant, leaving a plain C++ literal whose type the
            // compiler picks by magnitude, the way it does for a literal written without one.
            return std::nullopt;
        }
        if (auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&generatedNode)) {
            switch (binaryOperator->binaryOperator) {
                case BinaryOperator::add:
                case BinaryOperator::subtract:
                case BinaryOperator::multiply:
                case BinaryOperator::divide:
                case BinaryOperator::modulo:
                    return "double";
                case BinaryOperator::bitwiseAnd:
                case BinaryOperator::bitwiseOr:
                case BinaryOperator::shiftLeft:
                case BinaryOperator::shiftRight:
                    // `int`, not `int64_t`: this is the type of the expression as generated, and
                    // the CAST `selectResultNeedsIntegerCast` asks for is a widening the result
                    // column places on top of it, as it places `as_optional`.
                    return "int";
                case BinaryOperator::concatenate:
                    return "std::string";
                case BinaryOperator::jsonArrow:
                case BinaryOperator::jsonArrow2:
                    // Generated as `json_extract<std::string>(…)`, which is the type it is read as.
                    return "std::string";
                case BinaryOperator::equals:
                case BinaryOperator::notEquals:
                case BinaryOperator::lessThan:
                case BinaryOperator::lessOrEqual:
                case BinaryOperator::greaterThan:
                case BinaryOperator::greaterOrEqual:
                case BinaryOperator::logicalAnd:
                case BinaryOperator::logicalOr:
                // `is_t` and the rest of the IS family are a `binary_condition<…, bool>` too.
                case BinaryOperator::isOp:
                case BinaryOperator::isNot:
                case BinaryOperator::isDistinctFrom:
                case BinaryOperator::isNotDistinctFrom:
                    return "bool";
            }
            return std::nullopt;
        }
        if (auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&generatedNode)) {
            switch (unaryOperator->unaryOperator) {
                case UnaryOperator::minus:
                    if (negationFormFor(*unaryOperator->operand) != NegationForm::zeroMinusSubtraction) {
                        // The form codegen warns about has no sqlite_orm spelling at all, so there
                        // is no type it is read back through.
                        return std::nullopt;
                    }
                    // `(c(0) - x)`, a subtraction like any other.
                    return "double";
                case UnaryOperator::bitwiseNot:
                    return "int";
                case UnaryOperator::logicalNot:
                    return "bool";
                case UnaryOperator::plus:
                    // Emitted as its operand, which `generatedOperandNode` already stepped through.
                    return std::nullopt;
            }
            return std::nullopt;
        }
        if (dynamic_cast<const BetweenNode*>(&generatedNode) || dynamic_cast<const InNode*>(&generatedNode) ||
            dynamic_cast<const LikeNode*>(&generatedNode) || dynamic_cast<const GlobNode*>(&generatedNode)) {
            // `between_t`, `in_t`, `like_t`, `glob_t` and the `negated_condition_t` a NOT form
            // comes out as are all typed `bool`.
            return "bool";
        }
        if (auto* castNode = dynamic_cast<const CastNode*>(&generatedNode)) {
            // `cast_t<T, E>` is typed T, and T is what codegen writes into the CAST.
            return castTypeToCpp(castNode->typeName);
        }
        return std::nullopt;
    }

    std::vector<ResultColumnWidening> compoundSelectResultWidening(const CompoundSelectNode& compoundNode,
                                                                   const CodeGeneratorContext& context) {
        std::vector<const SelectNode*> arms;
        arms.reserve(compoundNode.selects.size());
        for (const auto& select: compoundNode.selects) {
            auto* armSelect = dynamic_cast<const SelectNode*>(select.get());
            if (!armSelect) {
                return {};
            }
            arms.push_back(armSelect);
        }
        if (arms.empty()) {
            return {};
        }
        const size_t columnCount = arms.front()->columns.size();
        for (const auto* arm: arms) {
            if (arm->columns.size() != columnCount) {
                return {};
            }
            for (const auto& column: arm->columns) {
                // A `*` names the columns of a row rather than an expression of its own, and a row
                // is read back through the struct it is mapped to, which holds a NULL already.
                if (!column.expression) {
                    return {};
                }
            }
        }
        std::vector<ResultColumnWidening> widenedColumns(columnCount);
        bool widensAnyColumn = false;
        const SelectNode& leadingArm = *arms.front();
        for (size_t columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
            const AstNode& leadingExpression = *leadingArm.columns.at(columnIndex).expression;
            const std::optional<std::string> cppType = generatedResultColumnCppType(leadingExpression);
            bool sameTypeEverywhere = true;
            bool everyArmNeedsIntegerCast = true;
            bool someArmNeedsWidening = false;
            for (const auto* arm: arms) {
                const AstNode& columnExpression = *arm->columns.at(columnIndex).expression;
                // Where no type can be named here — a call, whose type the function and its
                // arguments decide — an arm spelling the same expression over the same sources is
                // generated as the same code, and so comes out as the same type whatever it is.
                const bool sameType =
                    cppType ? generatedResultColumnCppType(columnExpression) == cppType
                            : arm->fromClause == leadingArm.fromClause && columnExpression == leadingExpression;
                if (!sameType) {
                    sameTypeEverywhere = false;
                    break;
                }
                everyArmNeedsIntegerCast = everyArmNeedsIntegerCast && selectResultNeedsIntegerCast(columnExpression);
                someArmNeedsWidening = someArmNeedsWidening || selectResultNeedsAsOptional(columnExpression, context);
            }
            if (!sameTypeEverywhere) {
                continue;
            }
            // Every arm is a bitwise operator here, so every arm comes out `int` and every arm is
            // cast: the arms move to `int64_t` together and keep their common type.
            widenedColumns[columnIndex].integerCast = everyArmNeedsIntegerCast;
            widenedColumns[columnIndex].asOptional = someArmNeedsWidening;
            widensAnyColumn = widensAnyColumn || widenedColumns[columnIndex] != ResultColumnWidening{};
        }
        if (!widensAnyColumn) {
            return {};
        }
        return widenedColumns;
    }

    std::optional<CodegenWarning> comparisonUnaryPlusAffinityWarning(const AstNode& astNode) {
        auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode);
        if (!unaryOp || unaryOp->unaryOperator != UnaryOperator::plus) {
            return std::nullopt;
        }
        // A second plus and a COLLATE stand between the plus and the column without putting an
        // expression of their own in the way — sqlite3 3.51 answers `+(a COLLATE BINARY) = 1` the
        // way it answers `+a = 1` — so the column under them is the one whose affinity is lost. A
        // table qualifier changes nothing: `+t.a = 1` answers what `+a = 1` does.
        const AstNode& operandNode = generatedOperandNode(astNode);
        std::string_view columnName;
        if (auto* column = dynamic_cast<const ColumnRefNode*>(&operandNode)) {
            columnName = column->columnName;
        } else if (auto* qualified = dynamic_cast<const QualifiedColumnRefNode*>(&operandNode)) {
            columnName = qualified->columnName;
        } else {
            return std::nullopt;
        }
        std::string message = "unary plus over column `";
        message += columnName;
        message += "` is dropped: it takes the column's affinity out of the comparison, and "
                   "sqlite_orm has no form that does. Where that affinity carries — a TEXT column "
                   "`t` holding '1' — SQLite answers `t = 1` with 1 and `+t = 1` with 0, while the "
                   "generated comparison is the one without the plus either way. Whether this "
                   "column is one of those depends on the affinity it was declared with, which is "
                   "not read here";
        // The plus is the token the node is located at, and it is the token that goes missing.
        return CodegenWarning{std::move(message), unaryOp->location, 1};
    }

}  // namespace sqlite2orm
