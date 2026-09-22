#pragma once

#include <sqlite2orm/ast.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace sqlite2orm {

    /**
     *  The registry of the sqlite_orm forms codegen may generate a call as, and the gate every
     *  generated call passes through.
     *
     *  Codegen used to answer "does sqlite_orm take this?" from a list written next to whoever
     *  asked — one list of the forms carrying `filter()`, another of the ones with a fixed set of
     *  overloads, none at all for the argument counts of the built-ins — so a form nobody had
     *  written down was emitted at a guess and the consumer heard about it from a compiler. What
     *  the library accepts is recorded here instead, once; a call no record covers is not
     *  generated, and the caller leaves a placeholder and a warning underlining the SQL.
     */

    /** The `maximum` of a form that takes any number of arguments from its `minimum` on. */
    inline constexpr size_t kVariadicArity = static_cast<size_t>(-1);

    /**
     *  Argument counts a form declares an overload for.
     *
     *  A single stretch, which is every form's shape but three: `json_insert`, `json_replace` and
     *  `json_set` take a path and a value per pair, so their overload is the odd counts from three
     *  on, and `json_object` the even ones. Their rows are recorded as the whole stretch and are
     *  knowingly wider than the declaration — the gate lets an even `json_insert` through, as it
     *  did before there was a gate — because a stretch cannot say "every other one". Narrowing
     *  them is a card of its own.
     */
    struct ArityRange {
        size_t minimum = 0;
        size_t maximum = 0;

        bool accepts(size_t argumentCount) const;

        bool operator==(const ArityRange&) const = default;
    };

    /** The range no argument count falls in, i.e. "there is no such call at all". */
    inline constexpr ArityRange kAcceptsNothing{1, 0};

    /** Which sqlite_orm type a call of a name is generated as. */
    enum class SqliteOrmFormKind {
        /** A `builtin_function_t`: a constructor and no default one, and neither `filter()` nor `over()`. */
        builtinScalar,
        /** A `builtin_aggregate_function_t`, the form carrying both `filter()` and `over()`. */
        builtinAggregate,
        /**
         *  `row_number_t`, `lag_t`, … — each an aggregate of its own: default-constructible, and
         *  carrying `over()` and no `filter()`.
         */
        windowFunction,
        /**
         *  `match_t`, which derives from nothing: default-constructible, no `filter()`, no `over()`,
         *  no operand traits.
         */
        matchFunction,
        /** `count_asterisk_t`, what `count(*)` over a known FROM clause becomes; it takes both. */
        countAsterisk,
        /** `count_asterisk_without_type`, what the argument-less `count()` becomes; it holds nothing. */
        countWithoutType,
        /**
         *  An FTS5 auxiliary function — `highlight()` — which sqlite_orm declares, and declares
         *  over the table's hidden column: `highlight(posts, 0, '<b>', '</b>')` names the table in
         *  its first argument, and the library takes that argument as an `fts5::hidden::any`
         *  column of the mapped virtual table. Codegen writes no such column, so no call it can
         *  write resolves to the form, whatever the argument count.
         */
        fts5Auxiliary,
        /** sqlite_orm has no form under this name at all, whatever the call is written with. */
        notMapped,
    };

    /**
     *  What sqlite_orm accepts for one SQL function name, and what SQLite itself accepts for it.
     *  `ormArity` belongs to the form a call RESOLVES to — `count`, `max` and `min` name more than
     *  one, told apart by the call rather than by the name — while `sqliteArity` belongs to the
     *  name, because what it answers is whether real SQLite refuses the very same call.
     */
    struct SqliteOrmFunctionForm {
        std::string_view sqlName;
        SqliteOrmFormKind kind = SqliteOrmFormKind::builtinScalar;
        ArityRange ormArity;
        ArityRange sqliteArity;
        /**
         *  What sqlite_orm calls this function when it calls it something else — `char_` for
         *  CHAR, `typeof_` for TYPEOF, both names C++ already means something by. Empty when the
         *  library spells the SQL name itself, and for a `notMapped` name it has no form for
         *  under any spelling.
         */
        std::string_view ormSpelling;
    };

    /**
     *  The record for `lowerFunctionName`, or nothing for a name the registry does not carry — a
     *  user-defined function, which is generated as a `func<…>()` call over a stub and takes
     *  whatever it is written with.
     */
    const SqliteOrmFunctionForm* sqliteOrmFunctionForm(std::string_view lowerFunctionName);

    /**
     *  The form a written call resolves to: the single place where a call is matched against the
     *  registry. Three names carry more than one form, and which one a call takes follows from the
     *  call and not from the name — `count(*)` is a `count_asterisk_t`, the argument-less `count()`
     *  a `count_asterisk_without_type` and `count(X)` the aggregate, while MAX and MIN are the
     *  aggregates in their one-argument form and the scalar overload from a second argument on,
     *  the same split SQLite makes. `generatedAsCountAsterisk` says whether the star was generated
     *  as `count<T>()`, which needs a FROM clause; every other star is written as `name()`, so it
     *  counts as no argument. Nothing for a name the registry does not carry.
     */
    std::optional<SqliteOrmFunctionForm> resolveFunctionCallForm(std::string_view lowerFunctionName,
                                                                 size_t argumentCount,
                                                                 bool star,
                                                                 bool generatedAsCountAsterisk);

    /** The same for a call node, whose name is lowercased and whose star counts as no argument. */
    std::optional<SqliteOrmFunctionForm> resolveFunctionCallForm(const FunctionCallNode& functionCall,
                                                                 bool generatedAsCountAsterisk);

    /** Whether the form carries a `filter()`, i.e. whether a FILTER over it can be generated at all. */
    bool formTakesFilter(SqliteOrmFormKind kind);

    /**
     *  Whether the form carries an `over()`, i.e. whether an OVER over it can be generated at all.
     *  sqlite_orm declares the two members apart — `over()` on the window functions, which have no
     *  `filter()`, and both on the aggregate function calls and on `count(*)` — so the two
     *  questions are asked apart as well.
     */
    bool formTakesOver(SqliteOrmFormKind kind);

    /**
     *  True when a call of `lowerFunctionName` — written with a star for its argument list or with
     *  arguments of its own — is generated as a sqlite_orm type that has a default constructor.
     *  A trigger's WHEN clause is kept in an `optional_container`, which default-constructs the
     *  expression before assigning it, so a WHEN built from a form without one does not compile at
     *  all; the trigger generator warns about what it finds.
     */
    bool functionCallHasDefaultConstructor(std::string_view lowerFunctionName, bool star);

    /**
     *  Why sqlite_orm has no form for `functionCall` as written, as the message the refusal is
     *  reported with — and nothing when the call is one the library takes. This is the gate: every
     *  generated call passes it, and a call it refuses is placeheld rather than guessed at.
     *  `userDefinedFunction` says the call is written as `func<…>()`, which takes any argument list
     *  and carries neither `filter()` nor `over()`.
     */
    std::optional<std::string> functionCallFormRefusal(const FunctionCallNode& functionCall,
                                                       bool generatedAsCountAsterisk,
                                                       bool userDefinedFunction);

}  // namespace sqlite2orm
