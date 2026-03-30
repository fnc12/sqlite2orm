#include "parser_tests_common.hpp"

using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: IN with subquery") {
    auto parseResult = parse("a IN (SELECT id FROM users)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    subSelect->columns = {SelectColumn{make_shared_node<ColumnRefNode>("id"), ""}};
    subSelect->fromClause = from_one("users");
    InNode expected(make_node<ColumnRefNode>("a"), std::vector<AstNodePointer>{}, std::move(subSelect), false,
                    SourceLocation{});
    REQUIRE(require_node<InNode>(parseResult) == expected);
}

TEST_CASE("parser: NOT IN with subquery") {
    auto parseResult = parse("a NOT IN (SELECT 1)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    subSelect->columns = {SelectColumn{make_shared_node<IntegerLiteralNode>("1"), ""}};
    InNode expected(make_node<ColumnRefNode>("a"), std::vector<AstNodePointer>{}, std::move(subSelect), true,
                    SourceLocation{});
    REQUIRE(require_node<InNode>(parseResult) == expected);
}

TEST_CASE("parser: scalar subquery in parentheses") {
    auto parseResult = parse("(SELECT COUNT(*) FROM t)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    {
        AstNodePointer countCall = make_func("COUNT", false, true);
        subSelect->columns = {SelectColumn{std::shared_ptr<AstNode>(std::move(countCall)), ""}};
    }
    subSelect->fromClause = from_one("t");
    SubqueryNode expected(std::move(subSelect), SourceLocation{});
    REQUIRE(require_node<SubqueryNode>(parseResult) == expected);
}

TEST_CASE("parser: EXISTS subquery") {
    auto parseResult = parse("EXISTS (SELECT * FROM users)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    subSelect->columns = {SelectColumn{nullptr, ""}};
    subSelect->fromClause = from_one("users");
    ExistsNode expected(std::move(subSelect), SourceLocation{});
    REQUIRE(require_node<ExistsNode>(parseResult) == expected);
}

TEST_CASE("parser: NOT EXISTS subquery") {
    auto parseResult = parse("NOT EXISTS (SELECT * FROM users)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    subSelect->columns = {SelectColumn{nullptr, ""}};
    subSelect->fromClause = from_one("users");
    UnaryOperatorNode expected(UnaryOperator::logicalNot,
                               std::make_unique<ExistsNode>(std::move(subSelect), SourceLocation{}),
                               SourceLocation{});
    REQUIRE(require_node<UnaryOperatorNode>(parseResult) == expected);
}

TEST_CASE("parser: comparison to scalar subquery") {
    auto parseResult = parse("id > (SELECT MAX(x) FROM t)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    {
        AstNodePointer maxCall = make_func("MAX", false, false, make_node<ColumnRefNode>("x"));
        subSelect->columns = {SelectColumn{std::shared_ptr<AstNode>(std::move(maxCall)), ""}};
    }
    subSelect->fromClause = from_one("t");
    BinaryOperatorNode expected(BinaryOperator::greaterThan, make_node<ColumnRefNode>("id"),
                                std::make_unique<SubqueryNode>(std::move(subSelect), SourceLocation{}),
                                SourceLocation{});
    REQUIRE(require_node<BinaryOperatorNode>(parseResult) == expected);
}
