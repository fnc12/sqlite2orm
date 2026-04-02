#include "parser_tests_common.hpp"

using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: FROM derived table with AS alias") {
    auto parseResult = parse("SELECT n FROM (SELECT 1 AS n) AS t");
    REQUIRE(parseResult);
    auto innerSelect = std::make_unique<SelectNode>(SourceLocation{});
    innerSelect->columns = {SelectColumn{makeSharedNode<IntegerLiteralNode>("1"), "n"}};
    FromTableClause derivedTable;
    derivedTable.derivedSelect = std::shared_ptr<AstNode>(std::move(innerSelect));
    derivedTable.alias = "t";
    SelectNode expected(SourceLocation{});
    expected.columns = {SelectColumn{makeSharedNode<ColumnRefNode>("n"), ""}};
    expected.fromClause = {FromClauseItem{JoinKind::none, std::move(derivedTable), nullptr, {}}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: FROM derived table implicit alias") {
    auto parseResult = parse("SELECT n FROM (SELECT 1 AS n) t");
    REQUIRE(parseResult);
    auto innerSelect = std::make_unique<SelectNode>(SourceLocation{});
    innerSelect->columns = {SelectColumn{makeSharedNode<IntegerLiteralNode>("1"), "n"}};
    FromTableClause derivedTable;
    derivedTable.derivedSelect = std::shared_ptr<AstNode>(std::move(innerSelect));
    derivedTable.alias = "t";
    SelectNode expected(SourceLocation{});
    expected.columns = {SelectColumn{makeSharedNode<ColumnRefNode>("n"), ""}};
    expected.fromClause = {FromClauseItem{JoinKind::none, std::move(derivedTable), nullptr, {}}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: FROM comma base table and derived") {
    auto parseResult = parse("SELECT 1 FROM users, (SELECT 2) s");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.fromClause.size() == 2);
    REQUIRE(sel.fromClause.at(0).table.tableName == "users");
    REQUIRE(sel.fromClause.at(0).table.derivedSelect == nullptr);
    REQUIRE(sel.fromClause.at(1).table.derivedSelect != nullptr);
    REQUIRE(sel.fromClause.at(1).table.alias == "s");
    REQUIRE(sel.fromClause.at(1).leadingJoin == JoinKind::crossJoin);
}
