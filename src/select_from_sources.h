#pragma once

#include <set>
#include <string>
#include <vector>

namespace sqlite2orm {

    /**
     *  The recordsets a select's own FROM clause stands for, and what it has to mention before
     *  sqlite_orm would widen the implicit FROM beyond them.
     *
     *  `implicitTypes` are the sources sqlite_orm has to infer — the first FROM item plus any
     *  the join loop leaves without a `join<T>(...)` clause — spelled the way `from<...>()`
     *  takes them, so a table with an alias is named by the alias. `covered` is every recordset
     *  the generated clauses name, joined ones included: a mention outside it is one the
     *  implicit FROM would widen past.
     *
     *  `aliasBaseStructs` are the base structs of the aliased sources, which the clauses do
     *  not name. A column of an aliased table is generated as `&T::x` rather than
     *  `alias_column<alias_a<T>>(...)` whenever the statement leaves the alias off (card
     *  1868205961584313865), and a `from<alias_a<T>>()` beside it leaves the SQL selecting
     *  from a table that column no longer belongs to — `no such column`, where the implicit
     *  FROM at least ran. So the FROM is pinned only when every recordset the code of this
     *  level names is one the clauses name too.
     */
    struct SelectFromSources {
        std::vector<std::string> implicitTypes;
        /**
         *  The leading run of sources — the first item and every one introduced by a comma or
         *  a CROSS JOIN, up to the first introduced by any other join kind. A CROSS JOIN with
         *  an ON or a USING of its own stays in the run: the join kind is all this looks at,
         *  the same way the join loop below does. These are the items a `from<...>()` can
         *  stand for in full: they reach sqlite_orm through
         *  `cross_join<alias_b<T>>()`, which serializes as a plain `CROSS JOIN "t"` — only a
         *  constrained join writes an alias out — so an aliased run is spelled out instead.
         */
        std::vector<std::string> leadingTypes;
        /** Whether any source of that run carries a SQL alias: with none there is nothing to carry. */
        bool leadingAnyAliased = false;
        /** Every source of the clause, joined ones included, as the parent select sees them. */
        std::vector<std::string> allTypes;
        /** The same set, for looking a mention up. */
        std::set<std::string> covered;
        /** Base structs of the aliased sources: mentioned by the code, named by no clause. */
        std::set<std::string> aliasBaseStructs;
        /**
         *  A CTE among the sources sqlite_orm infers. `from<cte_0>()` names one, so the FROM of
         *  such a select is spelled out like any other; what a CTE source does rule out is the
         *  alias half of the criterion, a CTE carrying no SQL alias of its own to lose.
         */
        bool hasCteSource = false;
    };

}  // namespace sqlite2orm
