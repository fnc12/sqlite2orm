#include "parser_tests_common.hpp"

using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: IN with subquery") {
    auto parseResult = parse("a IN (SELECT id FROM users)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    subSelect->columns = {SelectColumn{makeSharedNode<ColumnRefNode>("id"), ""}};
    subSelect->fromClause = fromOne("users");
    InNode expected(makeNode<ColumnRefNode>("a"), std::vector<AstNodePointer>{}, std::move(subSelect), false,
                    SourceLocation{});
    REQUIRE(requireNode<InNode>(parseResult) == expected);
}

TEST_CASE("parser: NOT IN with subquery") {
    auto parseResult = parse("a NOT IN (SELECT 1)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    subSelect->columns = {SelectColumn{makeSharedNode<IntegerLiteralNode>("1"), ""}};
    InNode expected(makeNode<ColumnRefNode>("a"), std::vector<AstNodePointer>{}, std::move(subSelect), true,
                    SourceLocation{});
    REQUIRE(requireNode<InNode>(parseResult) == expected);
}

TEST_CASE("parser: scalar subquery in parentheses") {
    auto parseResult = parse("(SELECT COUNT(*) FROM t)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    {
        AstNodePointer countCall = makeFunc("COUNT", false, true);
        subSelect->columns = {SelectColumn{std::shared_ptr<AstNode>(std::move(countCall)), ""}};
    }
    subSelect->fromClause = fromOne("t");
    SubqueryNode expected(std::move(subSelect), SourceLocation{});
    REQUIRE(requireNode<SubqueryNode>(parseResult) == expected);
}

TEST_CASE("parser: EXISTS subquery") {
    auto parseResult = parse("EXISTS (SELECT * FROM users)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    subSelect->columns = {SelectColumn{nullptr, ""}};
    subSelect->fromClause = fromOne("users");
    ExistsNode expected(std::move(subSelect), SourceLocation{});
    REQUIRE(requireNode<ExistsNode>(parseResult) == expected);
}

TEST_CASE("parser: NOT EXISTS subquery") {
    auto parseResult = parse("NOT EXISTS (SELECT * FROM users)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    subSelect->columns = {SelectColumn{nullptr, ""}};
    subSelect->fromClause = fromOne("users");
    UnaryOperatorNode expected(UnaryOperator::logicalNot,
                               std::make_unique<ExistsNode>(std::move(subSelect), SourceLocation{}),
                               SourceLocation{});
    REQUIRE(requireNode<UnaryOperatorNode>(parseResult) == expected);
}

TEST_CASE("parser: comparison to scalar subquery") {
    auto parseResult = parse("id > (SELECT MAX(x) FROM t)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    {
        AstNodePointer maxCall = makeFunc("MAX", false, false, makeNode<ColumnRefNode>("x"));
        subSelect->columns = {SelectColumn{std::shared_ptr<AstNode>(std::move(maxCall)), ""}};
    }
    subSelect->fromClause = fromOne("t");
    BinaryOperatorNode expected(BinaryOperator::greaterThan, makeNode<ColumnRefNode>("id"),
                                std::make_unique<SubqueryNode>(std::move(subSelect), SourceLocation{}),
                                SourceLocation{});
    REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == expected);
}
