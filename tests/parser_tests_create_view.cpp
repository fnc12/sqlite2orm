#include "parser_tests_common.hpp"
using namespace sqlite2orm::parser_test_helpers;

namespace {

    AstNodePointer selectLiteralOne() {
        auto sel = std::make_unique<SelectNode>(SourceLocation{});
        sel->columns = {SelectColumn{make_shared_node<IntegerLiteralNode>("1"), ""}};
        return sel;
    }

    AstNodePointer selectLiteralsOneTwo() {
        auto sel = std::make_unique<SelectNode>(SourceLocation{});
        sel->columns = {SelectColumn{make_shared_node<IntegerLiteralNode>("1"), ""},
                        SelectColumn{make_shared_node<IntegerLiteralNode>("2"), ""}};
        return sel;
    }

}  // namespace

TEST_CASE("parser: CREATE VIEW") {
    auto parseResult = parse("CREATE VIEW v AS SELECT 1;");
    REQUIRE(parseResult);
    CreateViewNode expected(SourceLocation{}, false, std::nullopt, "v", {}, selectLiteralOne());
    REQUIRE(require_node<CreateViewNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE VIEW IF NOT EXISTS") {
    auto parseResult = parse("CREATE VIEW IF NOT EXISTS v AS SELECT 1;");
    REQUIRE(parseResult);
    CreateViewNode expected(SourceLocation{}, true, std::nullopt, "v", {}, selectLiteralOne());
    REQUIRE(require_node<CreateViewNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE VIEW with schema-qualified name") {
    auto parseResult = parse("CREATE VIEW main.v AS SELECT 1;");
    REQUIRE(parseResult);
    CreateViewNode expected(SourceLocation{}, false, std::optional<std::string>{"main"}, "v", {}, selectLiteralOne());
    REQUIRE(require_node<CreateViewNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE VIEW with column name list") {
    auto parseResult = parse("CREATE VIEW v (a, b) AS SELECT 1, 2;");
    REQUIRE(parseResult);
    CreateViewNode expected(SourceLocation{}, false, std::nullopt, "v", {"a", "b"}, selectLiteralsOneTwo());
    REQUIRE(require_node<CreateViewNode>(parseResult) == expected);
}
