#include <sqlite2orm/process.h>

#include <fmt/format.h>

int main() {
    sqlite2orm::CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["expr_style"] = "functional";

    auto result = sqlite2orm::processSql(
        "SELECT * FROM users WHERE age >= 21 AND name = 'Alice';", &policy);

    if(!result.ok()) {
        for(const auto& error : result.validationErrors) {
            fmt::print(stderr, "error: {}\n", error.message);
        }
        return 1;
    }

    fmt::print("{}\n", result.codegen.code);
    return 0;
}
