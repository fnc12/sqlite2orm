#pragma once

#include <sqlite2orm/codegen.h>
#include <sqlite2orm/schema_process.h>

#include <string>

namespace sqlite2orm {

    std::string decisionPointsToJson(const std::vector<DecisionPoint>& decisionPoints);

    std::string sqliteSchemaResultToJson(const ProcessSqliteSchemaResult& schema);

}  // namespace sqlite2orm
