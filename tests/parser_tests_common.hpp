#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/parser.h>
#include <sqlite2orm/tokenizer.h>

#include <catch2/catch_all.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sqlite2orm::parser_test_helpers {

    ParseResult parse(std::string_view sql);

    ColumnDef columnWithDefault(std::string name, std::string typeName, std::shared_ptr<AstNode> defaultValue);

    std::vector<FromClauseItem> fromOne(std::string_view tableName);

    template<typename NodeType>
    const NodeType& requireNode(const ParseResult& parseResult) {
        REQUIRE(parseResult);
        auto* node = dynamic_cast<const NodeType*>(parseResult.astNodePointer.get());
        REQUIRE(node != nullptr);
        return *node;
    }

    template<typename NodeType, typename... Args>
    AstNodePointer makeNode(Args&&... args) {
        return std::make_unique<NodeType>(std::forward<Args>(args)..., SourceLocation{});
    }

    template<typename... Args>
    AstNodePointer makeFunc(std::string name, bool distinct, bool star, Args&&... args) {
        std::vector<AstNodePointer> arguments;
        (arguments.push_back(std::forward<Args>(args)), ...);
        return std::make_unique<FunctionCallNode>(
            std::move(name), std::move(arguments), distinct, star, SourceLocation{});
    }

    template<typename NodeType, typename... Args>
    std::shared_ptr<AstNode> makeSharedNode(Args&&... args) {
        return std::make_shared<NodeType>(std::forward<Args>(args)..., SourceLocation{});
    }

}  // namespace sqlite2orm::parser_test_helpers

// Test TUs include only this header; unqualified AST names match the original monolithic tests.
using namespace sqlite2orm;
