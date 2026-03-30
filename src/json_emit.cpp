#include <sqlite2orm/json_emit.h>

#include <nlohmann/json.hpp>

namespace sqlite2orm {

    namespace {

        nlohmann::json alternativeToJson(const Alternative& alternative) {
            return {{"value", alternative.value},
                    {"code", alternative.code},
                    {"description", alternative.description},
                    {"hidden", alternative.hidden}};
        }

        nlohmann::json decisionPointToJson(const DecisionPoint& decisionPoint) {
            nlohmann::json alternatives = nlohmann::json::array();
            for(const Alternative& alternative : decisionPoint.alternatives) {
                alternatives.push_back(alternativeToJson(alternative));
            }
            return {{"id", decisionPoint.id},
                    {"category", decisionPoint.category},
                    {"chosenValue", decisionPoint.chosenValue},
                    {"chosenCode", decisionPoint.chosenCode},
                    {"alternatives", std::move(alternatives)}};
        }

    }  // namespace

    std::string decisionPointsToJson(const std::vector<DecisionPoint>& decisionPoints) {
        nlohmann::json array = nlohmann::json::array();
        for(const DecisionPoint& decisionPoint : decisionPoints) {
            array.push_back(decisionPointToJson(decisionPoint));
        }
        return array.dump();
    }

    std::string sqliteSchemaResultToJson(const ProcessSqliteSchemaResult& schema) {
        nlohmann::json statements = nlohmann::json::array();
        for(const SchemaStatementResult& statement : schema.statements) {
            nlohmann::json row = {{"type", statement.meta.type},
                                 {"name", statement.meta.name},
                                 {"tableName", statement.meta.tableName},
                                 {"ok", statement.pipeline.ok()}};
            if(statement.pipeline.ok()) {
                nlohmann::json decisionPointsJson = nlohmann::json::array();
                for(const DecisionPoint& decisionPoint : statement.pipeline.codegen.decisionPoints) {
                    decisionPointsJson.push_back(decisionPointToJson(decisionPoint));
                }
                row["decisionPoints"] = std::move(decisionPointsJson);
            } else {
                row["decisionPoints"] = nlohmann::json::array();
            }
            statements.push_back(std::move(row));
        }
        return nlohmann::json{{"statements", std::move(statements)}}.dump();
    }

}  // namespace sqlite2orm
