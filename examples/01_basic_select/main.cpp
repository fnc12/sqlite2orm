#include <sqlite2orm/process.h>

#include <fmt/format.h>

int main() {
    auto result = sqlite2orm::processSql(
        "SELECT id, name FROM users WHERE age > 18 ORDER BY name;");

    if(!result.ok()) {
        for(const auto& error : result.parseResult.errors) {
            fmt::print(stderr, "parse error: {}\n", error.message);
        }
        for(const auto& error : result.validationErrors) {
            fmt::print(stderr, "validation error: {}\n", error.message);
        }
        return 1;
    }

    fmt::print("{}\n", result.codegen.code);

    for(const auto& warning : result.codegen.warnings) {
        fmt::print(stderr, "warning: {}\n", warning);
    }

    return 0;
}
