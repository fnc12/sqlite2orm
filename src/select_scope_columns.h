#pragma once

#include <sqlite2orm/ast.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "codegen_utils.h"

namespace sqlite2orm {

    class CodeGeneratorContext;
    struct SourceTableColumn;

    /**
     *  The sources one SELECT names in its own FROM clause, and the schema columns behind them.
     *
     *  What C++ type an argument of a call is read back as is answered from the struct the emitter
     *  writes that argument as a member of, which the context holds — while the emitter stands in
     *  the select the argument belongs to. Two askers do not: the inferrer of a view's fields,
     *  which runs after the view body was generated as a subquery that restored the scope it
     *  found, and the result column of a SELECT deciding about a scalar subquery nested in it,
     *  which stands in the OUTER scope. Both used to ask the context anyway and be answered over
     *  the wrong FROM clause — a name of the inner select resolving to a same-named column of an
     *  outer table, and the call typed from a column that is not in it. The lattice reducing the
     *  argument types to one stays where it is; resolving the NAME is what each scope does for
     *  itself, and that resolution is written here, once.
     */
    class SelectScopeColumns {
      public:
        SelectScopeColumns(const SelectNode& selectNode, const CodeGeneratorContext& context);

        /**
         *  The schema column `columnName` names under `tableOrAlias`, and `nullptr` where the
         *  qualifier names a source no CREATE TABLE of the batch answers for — a CTE, a view, a
         *  derived table, a name this scope does not hold at all.
         */
        const SourceTableColumn* findQualified(std::string_view tableOrAlias, std::string_view columnName) const;

        /**
         *  The schema column `columnName` names in the first source of this scope declaring one.
         *  This answers which column the SQL means; `resolve` answers which field the generated
         *  code reads it back through, and over a join the two part company — see there.
         */
        const SourceTableColumn* findUnqualified(std::string_view columnName) const;

        /**
         *  The schema column a reference node is read back through in this scope, and `nullptr`
         *  for a node that is no reference or names no column of a source table.
         *
         *  A reference naming no table comes out a member of the struct of the FIRST source, the
         *  one the emitter settles into `structName`: over `FROM u JOIN v` an unqualified `y` of
         *  `v` is written `&U::y` all the same. Answering `v`'s column for it would type the
         *  argument from a field the generated code does not read.
         */
        const SourceTableColumn* resolve(const AstNode& node) const;

        /** `resolve` as a resolver; this object has to outlive every call of the answer. */
        ReferencedColumnResolver resolver() const;

        /** The schema tables this scope reads, in FROM order — what a `SELECT *` expands over. */
        const std::vector<std::string>& sourceNames() const;

        /** The source an alias of this scope stands for, and `alias` itself where it names none. */
        std::string_view sourceForAlias(std::string_view alias) const;

      private:
        const CodeGeneratorContext& context;
        /** Source names in FROM order, quotes stripped, the ones no schema table can answer left out. */
        std::vector<std::string> sources;
        /** Normalized alias → the source name it stands for. */
        std::map<std::string, std::string> sourceByAlias;
        /** Normalized names — sources and their aliases both — that name no schema table. */
        std::set<std::string> opaqueNames;
        /** The source a reference naming no table comes out a member of, where there is one. */
        std::optional<std::string> implicitSource;
    };

}  // namespace sqlite2orm
