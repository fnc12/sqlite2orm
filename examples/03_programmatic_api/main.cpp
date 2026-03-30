#include <sqlite2orm/codegen.h>
#include <sqlite2orm/parser.h>
#include <sqlite2orm/tokenizer.h>
#include <sqlite2orm/validator.h>

#include <fmt/format.h>

int main() {
    const std::string_view sql =
        "INSERT INTO logs (level, message) VALUES ('info', 'started');";

    sqlite2orm::Tokenizer tokenizer;
    auto tokens = tokenizer.tokenize(sql);

    sqlite2orm::Parser parser;
    auto parseResult = parser.parse(std::move(tokens));
    if(!parseResult) {
        for(const auto& error : parseResult.errors) {
            fmt::print(stderr, "parse error: {}\n", error.message);
        }
        return 1;
    }

    sqlite2orm::Validator validator;
    auto validationErrors = validator.validate(*parseResult.astNodePointer);
    if(!validationErrors.empty()) {
        for(const auto& error : validationErrors) {
            fmt::print(stderr, "validation: {}\n", error.message);
        }
        return 1;
    }

    sqlite2orm::CodeGenerator codegen;
    auto codegenResult = codegen.generate(*parseResult.astNodePointer);

    if(!codegenResult.errors.empty()) {
        for(const auto& error : codegenResult.errors) {
            fmt::print(stderr, "codegen error: {}\n", error);
        }
        return 1;
    }

    fmt::print("Generated C++:\n{}\n", codegenResult.code);

    if(!codegenResult.decisionPoints.empty()) {
        fmt::print("\nDecision points:\n");
        for(const auto& dp : codegenResult.decisionPoints) {
            fmt::print("  [{}] {}: {}\n", dp.id, dp.category, dp.chosenValue);
            for(const auto& alt : dp.alternatives) {
                fmt::print("    - {}: {}\n", alt.value, alt.description);
            }
        }
    }

    return 0;
}
