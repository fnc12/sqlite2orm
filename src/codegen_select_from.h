#pragma once

#include "select_from_sources.h"

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_result.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sqlite2orm {

    class CodeGeneratorContext;

    /** How a join loop writes a FROM item after the first out. */
    enum class FromItemEmission {
        /** The `from<...>()` of the select stands for it, aliases and all. */
        namedByFromClause,
        /** Left for sqlite_orm to infer from the mentions: a CTE, or the first table after a leading CTE. */
        inferred,
        /** A join clause of its own. */
        joinClause,
    };

    constexpr std::string_view kWarningSelectWithoutFromNamesTable =
        "a SELECT with no FROM that names a table is not mapped to sqlite_orm: a select(...) with "
        "no from<...>() gets a FROM of every table its code names, and there is no form of it "
        "that has none";

    /**
     *  How the FROM clause of a select reaches sqlite_orm, defined in `codegen_select_from.cpp` and
     *  shared by the generator of a top-level select and the one of a subquery. Each is documented
     *  at its definition.
     */

    JoinKind generatedJoinKind(const FromClauseItem& item);
    bool generatedJoinLosesCrossJoinBarrier(const FromClauseItem& item);
    CodegenWarning constrainedCrossJoinWarning(const FromClauseItem& item, std::string_view generatedType);
    std::vector<FromItemEmission> fromItemEmissions(const CodeGeneratorContext& context,
                                                    const std::vector<FromClauseItem>& fromClause,
                                                    size_t sourcesNamedByFromClause);
    SelectFromSources selectFromSources(CodeGeneratorContext& context, const std::vector<FromClauseItem>& fromClause);
    std::vector<std::string> sourcesWrittenWithoutAlias(const std::vector<FromClauseItem>& fromClause,
                                                        const SelectFromSources& sources,
                                                        const std::vector<FromItemEmission>& emissions,
                                                        bool isStar);
    bool namesInferredSource(const SelectFromSources& sources, std::string_view rowType);
    bool inferredFromWouldBeInvented(const SelectNode& selectNode, const CodeGeneratorContext& context);
    std::string selectExplicitFrom(CodeGeneratorContext& context,
                                   const SelectNode& selectNode,
                                   const SelectFromSources& sources,
                                   size_t sourcesNamedByFromClause,
                                   std::string_view starRowType);
    std::vector<std::string> fromSourceTables(const SelectNode& selectNode, const CodeGeneratorContext& context);

}  // namespace sqlite2orm
