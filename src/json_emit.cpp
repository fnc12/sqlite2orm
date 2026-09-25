#include <sqlite2orm/json_emit.h>

#include <nlohmann/json.hpp>

namespace sqlite2orm {

    namespace {

        /**
         *  The comments as this JSON carries them: their messages alone. A comment anchored at the
         *  SQL it explains hands that anchor to a consumer calling the C++ API — this JSON is the
         *  summary a `--db --json` run prints, and it reports no anchor for a warning either, so
         *  putting one on a comment here would give the same diagnostic two shapes.
         */
        nlohmann::json commentMessages(const std::vector<CodegenComment>& comments) {
            nlohmann::json messages = nlohmann::json::array();
            for (const CodegenComment& comment: comments) {
                messages.push_back(comment.message);
            }
            return messages;
        }

    }  // namespace

    void to_json(nlohmann::json& out, const Option& alternative) {
        out = nlohmann::json{{"value", alternative.value},
                             {"code", alternative.code},
                             {"description", alternative.description},
                             {"hidden", alternative.hidden},
                             {"comments", commentMessages(alternative.comments)},
                             {"minCppStandard", alternative.minCppStandard}};
    }

    void to_json(nlohmann::json& out, const DecisionPoint& decisionPoint) {
        out = nlohmann::json{{"id", decisionPoint.id},
                             {"category", decisionPoint.category},
                             {"chosenValue", decisionPoint.chosenValue},
                             {"chosenCode", decisionPoint.chosenCode},
                             {"options", decisionPoint.options}};
    }

    std::string decisionPointsToJson(const std::vector<DecisionPoint>& decisionPoints) {
        return nlohmann::json(decisionPoints).dump();
    }

    std::string sqliteSchemaResultToJson(const ProcessSqliteSchemaResult& schema) {
        nlohmann::json statements = nlohmann::json::array();
        for (const SchemaStatementResult& statement: schema.statements) {
            nlohmann::json row = {{"type", statement.meta.type},
                                  {"name", statement.meta.name},
                                  {"tableName", statement.meta.tableName},
                                  {"ok", statement.pipeline.ok()}};
            if (statement.pipeline.ok()) {
                row["decisionPoints"] = statement.pipeline.codegen.decisionPoints;
                row["comments"] = commentMessages(statement.pipeline.codegen.comments);
            } else {
                // A consumer reads the same keys either way: a statement that did not generate has
                // no decision points and no comments, not a missing key.
                row["decisionPoints"] = nlohmann::json::array();
                row["comments"] = nlohmann::json::array();
            }
            statements.push_back(std::move(row));
        }
        return nlohmann::json{{"statements", std::move(statements)}}.dump();
    }

}  // namespace sqlite2orm
