#pragma once

#include <sqlite2orm/codegen.h>
#include <sqlite2orm/schema_coverage.h>
#include <sqlite2orm/schema_process.h>

#include <string>

namespace sqlite2orm {

    std::string decisionPointsToJson(const std::vector<DecisionPoint>& decisionPoints);

    std::string sqliteSchemaResultToJson(const ProcessSqliteSchemaResult& schema);

    /**
     *  Same as `sqliteSchemaResultToJson(schema)` plus a top-level `targetCppStandard` naming the C++
     *  standard the statements were generated for: the decision points offered depend on it, so a
     *  consumer reading them can tell which standard they were offered for.
     */
    std::string sqliteSchemaResultToJson(const ProcessSqliteSchemaResult& schema, int targetCppStandard);

    /**
     *  Same as `sqliteSchemaResultToJson(schema, targetCppStandard)` plus a top-level
     *  `coverage: {generated, total}` — how many of the schema's rows the generated header carries,
     *  so a consumer can tell a partial translation from a whole one without reading stderr.
     */
    std::string sqliteSchemaResultToJson(const ProcessSqliteSchemaResult& schema,
                                         int targetCppStandard,
                                         const SchemaCoverage& coverage);

}  // namespace sqlite2orm
