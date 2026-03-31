#include <sqlite2orm/json_emit.h>

#include <nlohmann/json.hpp>

namespace sqlite2orm {

    void to_json(nlohmann::json& out, const Alternative& alternative) {
        out = nlohmann::json{{"value", alternative.value},
                            {"code", alternative.code},
                            {"description", alternative.description},
                            {"hidden", alternative.hidden},
                            {"comments", alternative.comments}};
    }

    void to_json(nlohmann::json& out, const DecisionPoint& decisionPoint) {
        out = nlohmann::json{{"id", decisionPoint.id},
                            {"category", decisionPoint.category},
                            {"chosenValue", decisionPoint.chosenValue},
                            {"chosenCode", decisionPoint.chosenCode},
                            {"alternatives", decisionPoint.alternatives}};
    }

    std::string decisionPointsToJson(const std::vector<DecisionPoint>& decisionPoints) {
        return nlohmann::json(decisionPoints).dump();
    }

    std::string sqliteSchemaResultToJson(const ProcessSqliteSchemaResult& schema) {
        nlohmann::json statements = nlohmann::json::array();
        for(const SchemaStatementResult& statement : schema.statements) {
            nlohmann::json row = {{"type", statement.meta.type},
                                 {"name", statement.meta.name},
                                 {"tableName", statement.meta.tableName},
                                 {"ok", statement.pipeline.ok()}};
            if(statement.pipeline.ok()) {
                row["decisionPoints"] = statement.pipeline.codegen.decisionPoints;
                row["comments"] = statement.pipeline.codegen.comments;
            } else {
                row["decisionPoints"] = nlohmann::json::array();
            }
            statements.push_back(std::move(row));
        }
        return nlohmann::json{{"statements", std::move(statements)}}.dump();
    }

}  // namespace sqlite2orm
