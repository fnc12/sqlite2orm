#include "codegen_forms.h"

#include "codegen_utils.h"

#include <sqlite2orm/utils.h>

#include <algorithm>
#include <array>
#include <string>

namespace sqlite2orm {

    bool ArityRange::accepts(size_t argumentCount) const {
        return argumentCount >= this->minimum && argumentCount <= this->maximum;
    }

    namespace {

        /**
         *  Every SQL function name codegen may write a call of, with the sqlite_orm form it becomes.
         *  `ormArity` is read off the sqlite_orm headers (the pinned revision the build fetches),
         *  `sqliteArity` off sqlite3 3.51.0 — every name was prepared with 0 to 6 arguments, the
         *  math and SOUNDEX ones against an amalgamation built with SQLITE_ENABLE_MATH_FUNCTIONS
         *  and SQLITE_SOUNDEX, and what is written here is what it accepted. The two are recorded
         *  apart because they disagree: sqlite_orm declares the three-argument `iif` alone while
         *  SQLite has taken `iif(X, Y)` and the n-ary form since 3.48, and sqlite_orm's `coalesce`
         *  takes one argument where SQLite wants two. `kAcceptsNothing` in the sqlite_orm column
         *  means the library spells no call of that name at all; in the SQLite column it means
         *  SQLite has no such function either, i.e. `json_each` and `json_tree`, which are
         *  table-valued and refused as `no such function` in an expression.
         *
         *  `count`, `max` and `min` carry the form their NAME resolves to most often; the call
         *  picks between their overloads in `resolveFunctionCallForm`, the one place that split
         *  lives.
         */
        constexpr std::array<SqliteOrmFunctionForm, 106> kFunctionForms{{
            {"abs", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"acos", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"acosh", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"asin", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"asinh", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"atan", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"atan2", SqliteOrmFormKind::builtinScalar, {2, 2}, {2, 2}},
            {"atanh", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"avg", SqliteOrmFormKind::builtinAggregate, {1, 1}, {1, 1}},
            {"ceil", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"ceiling", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"changes", SqliteOrmFormKind::builtinScalar, {0, 0}, {0, 0}},
            {"char", SqliteOrmFormKind::notMapped, kAcceptsNothing, {0, kVariadicArity}, "char_"},
            {"coalesce", SqliteOrmFormKind::builtinScalar, {0, kVariadicArity}, {2, kVariadicArity}},
            {"cos", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"cosh", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"count", SqliteOrmFormKind::builtinAggregate, {1, 1}, {0, 1}},
            {"cume_dist", SqliteOrmFormKind::windowFunction, {0, 0}, {0, 0}},
            {"date", SqliteOrmFormKind::builtinScalar, {0, kVariadicArity}, {0, kVariadicArity}},
            {"datetime", SqliteOrmFormKind::builtinScalar, {0, kVariadicArity}, {0, kVariadicArity}},
            {"degrees", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"dense_rank", SqliteOrmFormKind::windowFunction, {0, 0}, {0, 0}},
            {"exp", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"first_value", SqliteOrmFormKind::windowFunction, {1, 1}, {1, 1}},
            {"floor", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"glob", SqliteOrmFormKind::builtinScalar, {2, 2}, {2, 2}},
            {"group_concat", SqliteOrmFormKind::builtinAggregate, {1, 2}, {1, 2}},
            {"hex", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"highlight", SqliteOrmFormKind::builtinScalar, {0, kVariadicArity}, {0, kVariadicArity}},
            {"ifnull", SqliteOrmFormKind::builtinScalar, {2, 2}, {2, 2}},
            {"iif", SqliteOrmFormKind::builtinScalar, {3, 3}, {2, kVariadicArity}},
            {"instr", SqliteOrmFormKind::builtinScalar, {2, 2}, {2, 2}},
            {"json", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"json_array", SqliteOrmFormKind::builtinScalar, {0, kVariadicArity}, {0, kVariadicArity}},
            {"json_array_length", SqliteOrmFormKind::builtinScalar, {1, 2}, {1, 2}},
            {"json_each", SqliteOrmFormKind::notMapped, kAcceptsNothing, kAcceptsNothing},
            {"json_extract", SqliteOrmFormKind::builtinScalar, {0, kVariadicArity}, {0, kVariadicArity}},
            {"json_group_array", SqliteOrmFormKind::builtinAggregate, {1, 1}, {1, 1}},
            {"json_group_object", SqliteOrmFormKind::builtinAggregate, {2, 2}, {2, 2}},
            {"json_insert", SqliteOrmFormKind::builtinScalar, {1, kVariadicArity}, {0, kVariadicArity}},
            {"json_object", SqliteOrmFormKind::builtinScalar, {0, kVariadicArity}, {0, kVariadicArity}},
            {"json_patch", SqliteOrmFormKind::builtinScalar, {2, 2}, {2, 2}},
            {"json_quote", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"json_remove", SqliteOrmFormKind::builtinScalar, {1, kVariadicArity}, {0, kVariadicArity}},
            {"json_replace", SqliteOrmFormKind::builtinScalar, {1, kVariadicArity}, {0, kVariadicArity}},
            {"json_set", SqliteOrmFormKind::builtinScalar, {1, kVariadicArity}, {0, kVariadicArity}},
            {"json_tree", SqliteOrmFormKind::notMapped, kAcceptsNothing, kAcceptsNothing},
            {"json_type", SqliteOrmFormKind::builtinScalar, {1, 2}, {1, 2}},
            {"json_valid", SqliteOrmFormKind::builtinScalar, {1, 2}, {1, 2}},
            {"julianday", SqliteOrmFormKind::builtinScalar, {0, kVariadicArity}, {0, kVariadicArity}},
            {"lag", SqliteOrmFormKind::windowFunction, {1, 3}, {1, 3}},
            {"last_insert_rowid", SqliteOrmFormKind::builtinScalar, {0, 0}, {0, 0}},
            {"last_value", SqliteOrmFormKind::windowFunction, {1, 1}, {1, 1}},
            {"lead", SqliteOrmFormKind::windowFunction, {1, 3}, {1, 3}},
            {"length", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"like", SqliteOrmFormKind::builtinScalar, {2, 3}, {2, 3}},
            {"likelihood", SqliteOrmFormKind::builtinScalar, {2, 2}, {2, 2}},
            {"likely", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"ln", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"log", SqliteOrmFormKind::builtinScalar, {1, 2}, {1, 2}},
            {"log10", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"log2", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"lower", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"ltrim", SqliteOrmFormKind::builtinScalar, {1, 2}, {1, 2}},
            {"match", SqliteOrmFormKind::matchFunction, {2, 2}, {2, 2}},
            {"max", SqliteOrmFormKind::builtinAggregate, {1, 1}, {1, kVariadicArity}},
            {"min", SqliteOrmFormKind::builtinAggregate, {1, 1}, {1, kVariadicArity}},
            {"mod", SqliteOrmFormKind::builtinScalar, {2, 2}, {2, 2}},
            {"nth_value", SqliteOrmFormKind::windowFunction, {2, 2}, {2, 2}},
            {"ntile", SqliteOrmFormKind::windowFunction, {1, 1}, {1, 1}},
            {"nullif", SqliteOrmFormKind::builtinScalar, {2, 2}, {2, 2}},
            {"percent_rank", SqliteOrmFormKind::windowFunction, {0, 0}, {0, 0}},
            {"pi", SqliteOrmFormKind::builtinScalar, {0, 0}, {0, 0}},
            {"pow", SqliteOrmFormKind::builtinScalar, {2, 2}, {2, 2}},
            {"power", SqliteOrmFormKind::builtinScalar, {2, 2}, {2, 2}},
            {"printf", SqliteOrmFormKind::builtinScalar, {0, kVariadicArity}, {0, kVariadicArity}},
            {"quote", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"radians", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"random", SqliteOrmFormKind::builtinScalar, {0, 0}, {0, 0}},
            {"randomblob", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"rank", SqliteOrmFormKind::windowFunction, {0, 0}, {0, 0}},
            {"replace", SqliteOrmFormKind::builtinScalar, {3, 3}, {3, 3}},
            {"round", SqliteOrmFormKind::builtinScalar, {1, 2}, {1, 2}},
            {"row_number", SqliteOrmFormKind::windowFunction, {0, 0}, {0, 0}},
            {"rtrim", SqliteOrmFormKind::builtinScalar, {1, 2}, {1, 2}},
            {"sign", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"sin", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"sinh", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"soundex", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"sqrt", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"strftime", SqliteOrmFormKind::builtinScalar, {0, kVariadicArity}, {0, kVariadicArity}},
            {"substr", SqliteOrmFormKind::builtinScalar, {2, 3}, {2, 3}},
            {"substring", SqliteOrmFormKind::builtinScalar, {2, 3}, {2, 3}},
            {"sum", SqliteOrmFormKind::builtinAggregate, {1, 1}, {1, 1}},
            {"tan", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"tanh", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"time", SqliteOrmFormKind::builtinScalar, {0, kVariadicArity}, {0, kVariadicArity}},
            {"total", SqliteOrmFormKind::builtinAggregate, {1, 1}, {1, 1}},
            {"total_changes", SqliteOrmFormKind::builtinScalar, {0, 0}, {0, 0}},
            {"trim", SqliteOrmFormKind::builtinScalar, {1, 2}, {1, 2}},
            {"trunc", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"typeof", SqliteOrmFormKind::notMapped, kAcceptsNothing, {1, 1}, "typeof_"},
            {"unicode", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"unlikely", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"upper", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
            {"zeroblob", SqliteOrmFormKind::builtinScalar, {1, 1}, {1, 1}},
        }};

        /** `count` with the noun in the number it takes: `0 arguments`, `1 argument`, `2 arguments`. */
        std::string argumentCountText(size_t count) {
            return std::to_string(count) + (count == 1 ? " argument" : " arguments");
        }

        /** What a form accepts, as the sentence fragment a refusal is built from. */
        std::string acceptedArgumentCountText(const ArityRange& arity) {
            if (arity.maximum == kVariadicArity) {
                return argumentCountText(arity.minimum) + " or more";
            }
            if (arity.minimum == arity.maximum) {
                return arity.minimum == 0 ? "no argument" : argumentCountText(arity.minimum);
            }
            return std::to_string(arity.minimum) + " to " + argumentCountText(arity.maximum);
        }

        /** The argument count a written call generates, a star counting as none as it does in SQLite. */
        size_t writtenArgumentCount(const FunctionCallNode& functionCall) {
            return functionCall.star ? 0 : functionCall.arguments.size();
        }

        /** How the refusal names what the call was written with. */
        std::string writtenArgumentCountText(const FunctionCallNode& functionCall) {
            return functionCall.star ? "a star, which counts as none"
                                     : argumentCountText(functionCall.arguments.size());
        }

        /**
         *  What real SQLite does with the very same call, which is the other half of every refusal:
         *  a schema arrives here through `--db`, and SQLite stores a trigger or a view holding a
         *  call it refuses at prepare, so the SQL being well formed and the generated code being
         *  generatable are two different questions.
         */
        std::string sqliteVerdictOnArity(const SqliteOrmFunctionForm& form, const FunctionCallNode& functionCall) {
            if (form.sqliteArity.accepts(writtenArgumentCount(functionCall))) {
                return "SQLite takes the same call, so the SQL is well formed and the generated code alone would "
                       "not be";
            }
            return "SQLite refuses the same call — wrong number of arguments to function " +
                   std::string(functionCall.name) + "() — but stores a trigger or a view holding it";
        }

        /**
         *  Why a FILTER over the call cannot be generated. sqlite_orm declares `filter()` on
         *  `count_asterisk_t` and on the built-in aggregate function calls alone, so a scalar
         *  function, a window function, a MATCH in its function spelling and a user-defined
         *  function — written as a `func<…>()` call — all carry none. `functionName` is spelled as
         *  the SQL writes it, which is how SQLite echoes a function name in the diagnostics quoted
         *  here; they were read off sqlite3 3.51.0, which refuses such a call at prepare and stores
         *  a trigger or a view holding one all the same. The two forms SQLite does take — the
         *  argument-less `count()` and a `userDefinedFunction` registered as an aggregate — say so
         *  instead: there the generated code alone is at fault.
         */
        std::string filterRefusal(std::string_view functionName, bool hasOverClause, bool userDefinedFunction) {
            const std::string name(functionName);
            const std::string preamble = name + "() has no filter() in sqlite_orm: only the aggregate function calls "
                                                "and count(*) take a FILTER, so there is no form to generate the "
                                                "call as. ";
            if (userDefinedFunction) {
                return preamble +
                       "SQLite takes a FILTER over a user-defined aggregate function, so the SQL may well be fine "
                       "while the func<" +
                       toStructName(functionName) + ">() call standing for " + name + "() is not";
            }
            const std::string lowerName = toLowerAscii(functionName);
            if (lowerName == "count") {
                // The only COUNT generated without a `filter()` is the argument-less one, which
                // SQLite takes as readily as the star it counts the same rows as.
                return preamble + "SQLite takes the same call — count() counts the rows count(*) does — so the SQL is "
                                  "well formed and the generated code alone is not";
            }
            const SqliteOrmFunctionForm* form = sqliteOrmFunctionForm(lowerName);
            const bool windowFunction = form != nullptr && form->kind == SqliteOrmFormKind::windowFunction;
            std::string refusal;
            if (windowFunction) {
                refusal = hasOverClause ? "FILTER clause may only be used with aggregate window functions"
                                        : "misuse of window function " + name + "()";
            } else {
                refusal = hasOverClause ? name + "() may not be used as a window function"
                                        : "FILTER may not be used with non-aggregate " + name + "()";
            }
            return preamble + "SQLite refuses the same call — " + refusal +
                   " — but stores a trigger or a view holding it";
        }

        /**
         *  Why a name sqlite_orm spells no call of cannot be generated. A name the library merely
         *  spells differently says so — codegen writes the SQL name, which in C++ is a cast
         *  (`char(65)`) or a compiler extension (`typeof(x)`) rather than a call of the form.
         *  SQLite's own verdict follows the registry: a function it does not have either is the
         *  `no such function` it echoes the name as written into.
         */
        std::string notMappedRefusal(const SqliteOrmFunctionForm& form, const FunctionCallNode& functionCall) {
            const std::string name(functionCall.name);
            std::string message = "sqlite_orm declares no " + name + "()";
            if (!form.ormSpelling.empty()) {
                message +=
                    ": the call it spells is " + std::string(form.ormSpelling) + "(), which codegen does not generate";
            }
            message += ", so there is no form to generate the call as. ";
            if (form.sqliteArity == kAcceptsNothing) {
                return message +
                       "SQLite has no such function either — it refuses the same call with no such "
                       "function: " +
                       name;
            }
            return message + "SQLite takes the same call, so the SQL is well formed and the generated code alone "
                             "would not be";
        }

    }  // namespace

    const SqliteOrmFunctionForm* sqliteOrmFunctionForm(std::string_view lowerFunctionName) {
        const auto found = std::find_if(kFunctionForms.begin(),
                                        kFunctionForms.end(),
                                        [lowerFunctionName](const SqliteOrmFunctionForm& form) {
                                            return form.sqlName == lowerFunctionName;
                                        });
        return found == kFunctionForms.end() ? nullptr : &*found;
    }

    std::optional<SqliteOrmFunctionForm> resolveFunctionCallForm(std::string_view lowerFunctionName,
                                                                 size_t argumentCount,
                                                                 bool star,
                                                                 bool generatedAsCountAsterisk) {
        const SqliteOrmFunctionForm* named = sqliteOrmFunctionForm(lowerFunctionName);
        if (named == nullptr) {
            return std::nullopt;
        }
        SqliteOrmFunctionForm resolved = *named;
        if (lowerFunctionName == "count") {
            if (star && generatedAsCountAsterisk) {
                resolved.kind = SqliteOrmFormKind::countAsterisk;
                resolved.ormArity = {0, 0};
            } else if (star || argumentCount == 0) {
                // A star with no FROM clause to name a row type is written as `count()`, the same
                // form the argument-less call takes.
                resolved.kind = SqliteOrmFormKind::countWithoutType;
                resolved.ormArity = {0, 0};
            }
        } else if (!star && argumentCount >= 2 && (lowerFunctionName == "max" || lowerFunctionName == "min")) {
            resolved.kind = SqliteOrmFormKind::builtinScalar;
            resolved.ormArity = {2, kVariadicArity};
        }
        return resolved;
    }

    std::optional<SqliteOrmFunctionForm> resolveFunctionCallForm(const FunctionCallNode& functionCall,
                                                                 bool generatedAsCountAsterisk) {
        return resolveFunctionCallForm(toLowerAscii(functionCall.name),
                                       writtenArgumentCount(functionCall),
                                       functionCall.star,
                                       generatedAsCountAsterisk);
    }

    bool formTakesFilter(SqliteOrmFormKind kind) {
        return kind == SqliteOrmFormKind::builtinAggregate || kind == SqliteOrmFormKind::countAsterisk;
    }

    bool functionCallHasDefaultConstructor(std::string_view lowerFunctionName, bool star) {
        const SqliteOrmFunctionForm* form = sqliteOrmFunctionForm(lowerFunctionName);
        // The window functions and a function-spelled MATCH are each generated as an aggregate of
        // their own — `row_number_t`, `lag_t`, `match_t` — and those are the forms holding a
        // default constructor; a `builtin_function_t` and the `function_call` a user-defined
        // function becomes declare a constructor and no default one.
        const bool aggregateOfItsOwn = form != nullptr && (form->kind == SqliteOrmFormKind::windowFunction ||
                                                           form->kind == SqliteOrmFormKind::matchFunction);
        if (aggregateOfItsOwn && form->ormArity.maximum == 0) {
            // A form taking no argument holds nothing whatever the call is written with, a star
            // included: `row_number(*)` is generated as `row_number()`.
            return true;
        }
        if (star) {
            // Every other star is generated as `name()`, and `count(*)` is the one of them
            // sqlite_orm has a form for: `count_asterisk_t` holds nothing either.
            return lowerFunctionName == "count";
        }
        return aggregateOfItsOwn;
    }

    std::optional<std::string> functionCallFormRefusal(const FunctionCallNode& functionCall,
                                                       bool generatedAsCountAsterisk,
                                                       bool userDefinedFunction) {
        if (userDefinedFunction) {
            // A `func<…>()` call takes whatever argument list it is written with — the generated
            // stub declares one `operator()` per call — and carries no `filter()` at all.
            if (functionCall.filterWhere != nullptr) {
                return filterRefusal(functionCall.name, functionCall.over != nullptr, true);
            }
            return std::nullopt;
        }
        const std::optional<SqliteOrmFunctionForm> form =
            resolveFunctionCallForm(functionCall, generatedAsCountAsterisk);
        if (!form) {
            return std::nullopt;
        }
        if (form->kind == SqliteOrmFormKind::notMapped) {
            return notMappedRefusal(*form, functionCall);
        }
        if (!form->ormArity.accepts(writtenArgumentCount(functionCall))) {
            return std::string(functionCall.name) + "() takes " + acceptedArgumentCountText(form->ormArity) +
                   " in sqlite_orm, and the call is written with " + writtenArgumentCountText(functionCall) +
                   ": the library declares no overload for that many, so there is no form to generate the "
                   "call as. " +
                   sqliteVerdictOnArity(*form, functionCall);
        }
        if (functionCall.filterWhere != nullptr && !formTakesFilter(form->kind)) {
            return filterRefusal(functionCall.name, functionCall.over != nullptr, false);
        }
        return std::nullopt;
    }

}  // namespace sqlite2orm
