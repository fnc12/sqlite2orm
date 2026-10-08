#include "codegen_select_from.h"
#include "codegen_context.h"
#include "codegen_utils.h"
#include <sqlite2orm/utils.h>

#include <algorithm>

namespace sqlite2orm {

    /**
     *  The join to generate for a FROM item. SQLite reads an ON or a USING after CROSS JOIN —
     *  and after a comma — the way it reads one after JOIN, and answers the rows of an inner
     *  join; `cross_join_t` takes no constraint at all, so a constrained CROSS JOIN is
     *  generated as the plain join whose form carries one. Everything that reads an item as a
     *  source the implicit FROM stands for asks here too: an item carrying a constraint is a
     *  join clause of its own, not one more source of a comma list.
     */
    JoinKind generatedJoinKind(const FromClauseItem& item) {
        if (item.leadingJoin == JoinKind::crossJoin && (item.onExpression || !item.usingColumnNames.empty())) {
            return JoinKind::joinPlain;
        }
        return item.leadingJoin;
    }

    /**
     *  Whether the join generated for `item` gives up the reordering barrier the SQL asked
     *  for. A comma and a written `CROSS JOIN` both parse as `crossJoin` and answer the same
     *  rows, but only the keyword keeps SQLite from reordering the tables: measured on 3.51.0
     *  with EXPLAIN QUERY PLAN, `FROM big, small ON big.x = small.y` searches the indexed
     *  table exactly as `JOIN` does, while `CROSS JOIN` scans both in the order written. So
     *  `join<T>(...)` is an exact translation of the comma form and there is nothing to warn
     *  about, while the keyword form loses the barrier along the way.
     */
    bool generatedJoinLosesCrossJoinBarrier(const FromClauseItem& item) {
        return item.leadingJoin == JoinKind::crossJoin && !item.leadingJoinWrittenAsComma &&
               generatedJoinKind(item) != JoinKind::crossJoin;
    }

    /**
     *  The warning a written CROSS JOIN carrying a constraint leaves behind, anchored on the
     *  condition the ON was written with. A USING names its columns with no node of its own,
     *  so there is no span to underline and the warning goes out with no anchor at all.
     */
    CodegenWarning constrainedCrossJoinWarning(const FromClauseItem& item, std::string_view generatedType) {
        std::string message = "CROSS JOIN carrying a constraint has no sqlite_orm form; generated join<" +
                              std::string(generatedType) +
                              ">(...) answers the same rows, but leaves SQLite free to reorder the join";
        if (item.onExpression) {
            return sourceSpanWarning(std::move(message), *item.onExpression);
        }
        return CodegenWarning{std::move(message)};
    }

    /**
     *  How each item of `fromClause` is written out, indexed like it; the first item is the
     *  select's own source and is marked `inferred` unless the `from<...>()` stands for it.
     *  `sourcesNamedByFromClause` is how many leading items that clause stands for. The join
     *  loops of both generators write their clauses from this, and whatever has to know how
     *  a source reaches sqlite_orm reads it from here too.
     */
    std::vector<FromItemEmission> fromItemEmissions(const CodeGeneratorContext& context,
                                                    const std::vector<FromClauseItem>& fromClause,
                                                    size_t sourcesNamedByFromClause) {
        auto isCteSource = [&](std::string_view tableName) -> bool {
            auto key = normalizeSqlIdentifier(tableName);
            return context.activeCteTypedefByTableKey.find(key) != context.activeCteTypedefByTableKey.end();
        };
        std::vector<FromItemEmission> emissions;
        emissions.reserve(fromClause.size());
        if (fromClause.empty()) {
            return emissions;
        }
        emissions.push_back(sourcesNamedByFromClause > 0u ? FromItemEmission::namedByFromClause
                                                          : FromItemEmission::inferred);
        const bool firstFromIsCte = isCteSource(fromClause.at(0).table.tableName);
        bool emittedNonCteJoin = false;
        for (size_t joinIndex = 1; joinIndex < fromClause.size(); ++joinIndex) {
            const auto& joinItem = fromClause.at(joinIndex);
            const JoinKind joinKind = generatedJoinKind(joinItem);
            if (joinIndex < sourcesNamedByFromClause) {
                emissions.push_back(FromItemEmission::namedByFromClause);
            } else if (isCteSource(joinItem.table.tableName) && joinKind == JoinKind::crossJoin) {
                emissions.push_back(FromItemEmission::inferred);
            } else if (firstFromIsCte && !emittedNonCteJoin && joinKind == JoinKind::crossJoin) {
                emittedNonCteJoin = true;
                emissions.push_back(FromItemEmission::inferred);
            } else {
                emittedNonCteJoin = true;
                emissions.push_back(FromItemEmission::joinClause);
            }
        }
        return emissions;
    }

    /**
     *  The FROM sources of `fromClause` as the select generators spell them: the same alias,
     *  CTE and struct resolution their clause loops use, and the same rule for which items
     *  become a `join<T>(...)` clause and which are left for sqlite_orm to infer.
     */
    SelectFromSources selectFromSources(CodeGeneratorContext& context, const std::vector<FromClauseItem>& fromClause) {
        SelectFromSources sources;
        auto isCteSource = [&](std::string_view tableName) -> bool {
            auto key = normalizeSqlIdentifier(tableName);
            return context.activeCteTypedefByTableKey.find(key) != context.activeCteTypedefByTableKey.end();
        };
        auto recordsetType = [&](const FromTableClause& table) -> std::string {
            const std::string aliasKey = table.alias ? *table.alias : table.tableName;
            auto aliasIt = context.activeTableAliases.find(aliasKey);
            if (aliasIt != context.activeTableAliases.end()) {
                return aliasIt->second.ormAliasType;
            }
            auto key = normalizeSqlIdentifier(table.tableName);
            if (auto cteIt = context.activeCteTypedefByTableKey.find(key);
                cteIt != context.activeCteTypedefByTableKey.end()) {
                return cteIt->second;
            }
            return context.structNameForTable(table.tableName);
        };
        auto addImplicitSource = [&](const FromTableClause& table) {
            if (isCteSource(table.tableName)) {
                sources.hasCteSource = true;
            }
            sources.implicitTypes.push_back(recordsetType(table));
        };
        bool inLeadingRun = true;
        for (size_t index = 0; index < fromClause.size(); ++index) {
            const auto& item = fromClause.at(index);
            if (index > 0 && generatedJoinKind(item) != JoinKind::crossJoin) {
                inLeadingRun = false;
            }
            if (inLeadingRun) {
                sources.leadingTypes.push_back(recordsetType(item.table));
                if (item.table.alias &&
                    context.activeTableAliases.find(*item.table.alias) != context.activeTableAliases.end()) {
                    sources.leadingAnyAliased = true;
                }
            }
            sources.allTypes.push_back(recordsetType(item.table));
            sources.covered.insert(sources.allTypes.back());
            if (!isCteSource(item.table.tableName)) {
                std::string structName = context.structNameForTable(item.table.tableName);
                if (structName != sources.allTypes.back()) {
                    sources.aliasBaseStructs.insert(std::move(structName));
                }
            }
        }
        if (fromClause.empty()) {
            return sources;
        }
        addImplicitSource(fromClause.at(0).table);
        const auto emissions = fromItemEmissions(context, fromClause, 0u);
        for (size_t joinIndex = 1; joinIndex < fromClause.size(); ++joinIndex) {
            if (emissions.at(joinIndex) == FromItemEmission::inferred) {
                addImplicitSource(fromClause.at(joinIndex).table);
            }
        }
        return sources;
    }

    /**
     *  The recordsets of `fromClause` that reach sqlite_orm with no alias written for them,
     *  however they are aliased in the SQL. A bare `*` reads its leading source as the plain
     *  struct — `get_all<T>()` or `asterisk<T>()` — and `cross_join<alias_b<T>>()` and
     *  `natural_join<alias_b<T>>()` serialize as a plain `CROSS JOIN "t"` / `NATURAL JOIN "t"`
     *  wherever in the FROM they stand; `from<alias_a<T>>()` and a constrained
     *  `join<alias_a<T>>(on(…))` write the alias out. Read off the same `emissions` the join
     *  loop writes the clauses from, so the two cannot drift apart.
     */
    std::vector<std::string> sourcesWrittenWithoutAlias(const std::vector<FromClauseItem>& fromClause,
                                                        const SelectFromSources& sources,
                                                        const std::vector<FromItemEmission>& emissions,
                                                        bool isStar) {
        std::vector<std::string> types;
        if (isStar && !sources.allTypes.empty()) {
            types.push_back(sources.allTypes.front());
        }
        for (size_t joinIndex = 1; joinIndex < fromClause.size(); ++joinIndex) {
            if (emissions.at(joinIndex) != FromItemEmission::joinClause) {
                continue;
            }
            switch (generatedJoinKind(fromClause.at(joinIndex))) {
                case JoinKind::crossJoin:
                case JoinKind::naturalInnerJoin:
                case JoinKind::naturalLeftJoin:
                    types.push_back(sources.allTypes.at(joinIndex));
                    break;
                default:
                    break;
            }
        }
        return types;
    }

    /** Whether `rowType` is one of the sources the FROM would name. */
    bool namesInferredSource(const SelectFromSources& sources, std::string_view rowType) {
        return std::find(sources.implicitTypes.begin(), sources.implicitTypes.end(), rowType) !=
               sources.implicitTypes.end();
    }

    namespace {

        /**
         *  Whether the FROM sqlite_orm infers would differ from the one the SQL asked for — wider,
         *  because the code names a recordset no source of this FROM clause stands for, or gone
         *  altogether, because the code of this select names none. A subquery in the WHERE names
         *  its own tables just as plainly as the outer ones, and that is what turns it into a
         *  cartesian product, so the widening half weighs `mentioned`, the mentions of this select
         *  and of every select nested in it alike.
         *
         *  The losing half weighs `ownVisibleMentions` instead: the mentions this select made
         *  itself, and of those the ones sqlite_orm can see. A mention that reached this level out
         *  of a nested scope is no FROM for this one to infer from — the subquery answers for it
         *  with a `from<...>()` of its own, and a `from<...>()` fixes the level it stands on and no
         *  other. With `SELECT 1 FROM users WHERE EXISTS (SELECT 1 FROM users)` the two halves
         *  used to cancel out — the inherited mention of `Users` is one this FROM covers, so
         *  nothing looked wider, and nothing looked lost either — and the outer select went out
         *  with no table at all, answering one row where SQLite answers one per row of the table.
         *  A mention made under the field operand of a MATCH is the level's own and yet invisible
         *  to sqlite_orm, so it is out of this half too: `SELECT iif(a MATCH 'x', 1, 2) FROM users`
         *  hands the inferred FROM nothing, however plainly the code names the table.
         */
        bool implicitFromDiffers(const SelectFromSources& sources,
                                 const std::set<std::string>& mentioned,
                                 const std::set<std::string>& ownVisibleMentions) {
            // Code that names no recordset of its own leaves sqlite_orm nothing to infer a FROM
            // from, and the table is dropped rather than widened: `SELECT row_number() OVER () FROM
            // users` runs as `SELECT ROW_NUMBER() OVER ()` and answers with one row where SQLite
            // answers with one per row of the table. Naming the sources is what brings the table
            // back.
            if (ownVisibleMentions.empty()) {
                return true;
            }
            for (const auto& type: mentioned) {
                if (sources.covered.find(type) == sources.covered.end()) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    namespace {

        /**
         *  Whether the clauses of this select name every recordset the code of this select names —
         *  the invariant a pinned FROM rests on. A base struct of an aliased source is the case
         *  that breaks it: where a column of an aliased table still comes out as `&T::x`, a
         *  `from<alias_a<T>>()` beside it leaves the SQL selecting from a table that column no
         *  longer belongs to — `no such column`, where the inferred FROM at least ran.
         *
         *  Only the mentions this level made itself are asked about. A subquery naming the same
         *  table plainly is the shape `SELECT name FROM users u WHERE id IN (SELECT id FROM
         *  users)`, and there the outer `from<alias_a<Users>>()` is exactly what is needed: the
         *  subquery answers for its own FROM, while leaving the outer one implicit collects both
         *  mentions into it and multiplies the rows.
         */
        bool clausesNameEveryMention(const SelectFromSources& sources, const std::set<std::string>& mentioned) {
            for (const auto& type: mentioned) {
                if (sources.aliasBaseStructs.find(type) != sources.aliasBaseStructs.end()) {
                    return false;
                }
            }
            return true;
        }

    }  // namespace

    /**
     *  Whether sqlite_orm would give a select that has no FROM clause one of its own. A
     *  `select(...)` with no `from<...>()` is serialized with a FROM of every recordset its code
     *  names, a nested select's included, and there is no spelling that leaves the FROM out
     *  once one is named. `SELECT (SELECT b) FROM t` reads `b` of the row the outer select is
     *  at; `select(&T::b)` runs as `(SELECT "t"."b" FROM "t")` and answers the first row of the
     *  table for every row. `SELECT (SELECT count(*) FROM u)` is the same at the top level:
     *  one row where the table has any, none where it has none.
     *
     *  Every mention sqlite_orm sees counts, a nested select's included, because it collects
     *  those into the FROM as well. Where no statement encloses this one, a column it names itself
     *  refers to nothing SQLite could resolve, and such a select is left to the FROM sqlite_orm
     *  infers as before: it is only when the code of this select names no table of its own that
     *  the mentions come from a query SQLite would run.
     */
    bool inferredFromWouldBeInvented(const SelectNode& selectNode, const CodeGeneratorContext& context) {
        if (!selectNode.fromClause.empty() || context.emittedTableTypes.empty()) {
            return false;
        }
        const bool enclosedByStatement = context.selectNestingLevel > 1u;
        return enclosedByStatement || context.ownVisibleEmittedTableTypes.empty();
    }

    namespace {

        /** The `from<...>()` naming `types`. */
        std::string explicitFromClause(const std::vector<std::string>& types) {
            std::string clause = "from<";
            for (size_t i = 0; i < types.size(); ++i) {
                if (i > 0) {
                    clause += ", ";
                }
                clause += types.at(i);
            }
            clause += ">()";
            return clause;
        }

    }  // namespace

    /**
     *  The single point a select's FROM sources are written out at, for the statement generator
     *  and the subquery one alike. Twice already this decision was changed in one of the two
     *  generators and left as it was in the other, so there is one of it to change.
     *
     *  It is called once every clause that can name a source has been generated, which is what
     *  makes both halves of the criterion final — `count(*)` names an aliased source from a
     *  HAVING or an ORDER BY as readily as from the result columns. `starRowType` is the row a
     *  `*` reads, empty for any other select: the star is built by the generators rather than
     *  by the expression emitter the other result columns go through, so it is recorded here
     *  to be weighed like any other mention.
     *
     *  The FROM is spelled out when leaving it implicit would get it wrong in either of two
     *  ways — it would DIFFER from what the SQL asked for (a subquery in the WHERE names its
     *  own tables, and sqlite_orm collects them into this FROM; code naming no recordset of
     *  its own leaves it with no FROM to infer), or it would LOSE an alias this select has
     *  already named. The second half cannot be vetoed: the forms that took `canNameInferredFromSources`
     *  up are already generated, and `count<alias_a<T>>()` carries no table at all, so without
     *  the clause the select would go out with no FROM whatsoever.
     */
    std::string selectExplicitFrom(CodeGeneratorContext& context,
                                   const SelectNode& selectNode,
                                   const SelectFromSources& sources,
                                   size_t sourcesNamedByFromClause,
                                   std::string_view starRowType) {
        if (!starRowType.empty()) {
            context.recordEmittedTableType(std::string(starRowType));
        }
        std::string explicitFrom;
        const bool inferredFromWouldLoseAlias =
            context.canNameInferredFromSources && (sourcesNamedByFromClause > 0u || context.countedAliasedSource);
        if (inferredFromWouldLoseAlias) {
            explicitFrom =
                explicitFromClause(sourcesNamedByFromClause > 0u ? sources.leadingTypes : sources.implicitTypes);
            // The clause is what the whole select is generated with, so the select is what the
            // hint underlines: no one source of it is the reason the FROM is spelled out.
            context.recordComment(sourceSpanComment(kCommentAliasedFromSources, selectNode));
        } else if (!sources.implicitTypes.empty() && clausesNameEveryMention(sources, context.ownEmittedTableTypes) &&
                   implicitFromDiffers(sources, context.emittedTableTypes, context.ownVisibleEmittedTableTypes)) {
            explicitFrom = explicitFromClause(sources.implicitTypes);
        }
        for (const auto& sourceType: sources.allTypes) {
            context.recordEmittedTableType(sourceType);
        }
        return explicitFrom;
    }

    /**
     *  The tables the FROM of `selectNode` names, in the form `ColumnNameScope::sourceTables`
     *  holds them: a CTE, a derived table and a table-valued function name no schema table, so
     *  each stands as an empty name there.
     */
    std::vector<std::string> fromSourceTables(const SelectNode& selectNode, const CodeGeneratorContext& context) {
        std::vector<std::string> sourceTables;
        for (const auto& fromItem: selectNode.fromClause) {
            const auto& fromTable = fromItem.table;
            const bool opaque = fromTable.derivedSelect || !fromTable.tableFunctionArgs.empty() ||
                                context.activeCteTypedefByTableKey.find(normalizeSqlIdentifier(fromTable.tableName)) !=
                                    context.activeCteTypedefByTableKey.end();
            sourceTables.push_back(opaque ? std::string() : fromTable.tableName);
        }
        return sourceTables;
    }

}  // namespace sqlite2orm
