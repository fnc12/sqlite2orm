#include <sqlite2orm/json_emit.h>
#include <sqlite2orm/process.h>

#include <fmt/format.h>

int main() {
    auto result = sqlite2orm::processSql(
        "SELECT u.id, u.name, o.total "
        "FROM users u JOIN orders o ON u.id = o.user_id "
        "WHERE o.total > 100;");

    if(!result.ok()) {
        fmt::print(stderr, "pipeline failed\n");
        return 1;
    }

    auto json = sqlite2orm::decisionPointsToJson(result.codegen.decisionPoints);
    fmt::print("{}\n", json);
    return 0;
}
