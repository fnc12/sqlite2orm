#include <sqlite2orm/process.h>

#include <fmt/format.h>

int main() {
    auto result = sqlite2orm::processSql(
        "CREATE TABLE users (\n"
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    name TEXT NOT NULL,\n"
        "    email TEXT UNIQUE,\n"
        "    age INTEGER DEFAULT 0,\n"
        "    created_at TEXT DEFAULT (datetime('now'))\n"
        ");");

    if(!result.ok()) {
        for(const auto& error : result.validationErrors) {
            fmt::print(stderr, "validation error: {}\n", error.message);
        }
        return 1;
    }

    fmt::print("{}\n", result.codegen.code);
    return 0;
}
