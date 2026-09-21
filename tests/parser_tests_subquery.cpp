#include "parser_tests_common.hpp"

using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: IN with subquery") {
    auto parseResult = parse("a IN (SELECT id FROM users)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    subSelect->columns = {SelectColumn{makeSharedNode<ColumnRefNode>("id"), ""}};
    subSelect->fromClause = fromOne("users");
    InNode expected(makeNode<ColumnRefNode>("a"),
                    std::vector<AstNodePointer>{},
                    std::move(subSelect),
                    false,
                    SourceLocation{});
    REQUIRE(requireNode<InNode>(parseResult) == expected);
}

TEST_CASE("parser: NOT IN with subquery") {
    auto parseResult = parse("a NOT IN (SELECT 1)");
    REQUIRE(parseResult);
    auto subSelect = std::make_unique<SelectNode>(SourceLocation{});
    subSelect->columns = {SelectColumn{makeSharedNode<IntegerLiteralNode>("1"), ""}};
    InNode expected(makeNode<ColumnRefNode>("a"),
                    std::vector<AstNodePointer>{},
                    std::move(subSelect),
                    true,
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
    BinaryOperatorNode expected(BinaryOperator::greaterThan,
                                makeNode<ColumnRefNode>("id"),
                                std::make_unique<SubqueryNode>(std::move(subSelect), SourceLocation{}),
                                SourceLocation{});
    REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == expected);
}

// --- Nesting depth ---

namespace {
    /** `SELECT * FROM (SELECT * FROM (… t …))`, with `queryCount` queries nested in one another. */
    std::string nestedFromSubqueries(size_t queryCount) {
        std::string sql = "SELECT * FROM ";
        for (size_t i = 1; i < queryCount; ++i) {
            sql += "(SELECT * FROM ";
        }
        sql += "t";
        sql += std::string(queryCount - 1, ')');
        return sql;
    }

    /** `SELECT * FROM (t JOIN (t JOIN … t …))`, with `groupCount` join groups nested in one another. */
    std::string nestedJoinGroups(size_t groupCount) {
        std::string sql = "SELECT * FROM ";
        for (size_t i = 0; i < groupCount; ++i) {
            sql += "(t JOIN ";
        }
        sql += "t";
        sql += std::string(groupCount, ')');
        return sql;
    }

    /** `WITH x AS (WITH x AS (… SELECT 1 …) SELECT * FROM x)…`, with `queryCount` queries in all. */
    std::string nestedWithBodies(size_t queryCount) {
        std::string sql;
        for (size_t i = 1; i < queryCount; ++i) {
            sql += "WITH x AS (";
        }
        sql += "SELECT 1";
        for (size_t i = 1; i < queryCount; ++i) {
            sql += ") SELECT * FROM x";
        }
        return sql;
    }
}

TEST_CASE("parser: queries at the nesting depth limit are accepted") {
    auto parseResult = parse(nestedFromSubqueries(kMaxQueryDepth));
    REQUIRE(parseResult);
}

TEST_CASE("parser: FROM subquery nested past the depth limit is refused") {
    auto parseResult = parse(nestedFromSubqueries(kMaxQueryDepth + 1));
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.astNodePointer == nullptr);
    REQUIRE(parseResult.errors.size() == 1);
    CHECK(parseResult.errors.front().message == "query is nested too deeply (maximum depth 200)");
}

TEST_CASE("parser: FROM subquery nested far past the depth limit is refused, not crashed on") {
    // The depth that took the process down with a stack overflow before the limit was there: the
    // parser has to refuse this without ever recursing that far.
    auto parseResult = parse(nestedFromSubqueries(20000));
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.astNodePointer == nullptr);
    REQUIRE(parseResult.errors.size() == 1);
    CHECK(parseResult.errors.front().message == "query is nested too deeply (maximum depth 200)");
}

TEST_CASE("parser: parenthesized join groups at the nesting depth limit are accepted") {
    auto parseResult = parse(nestedJoinGroups(kMaxQueryDepth - 1));
    REQUIRE(parseResult);
}

TEST_CASE("parser: parenthesized join groups nested past the depth limit are refused") {
    auto parseResult = parse(nestedJoinGroups(kMaxQueryDepth));
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.astNodePointer == nullptr);
    REQUIRE(parseResult.errors.size() == 1);
    CHECK(parseResult.errors.front().message == "query is nested too deeply (maximum depth 200)");
}

TEST_CASE("parser: CTE bodies at the nesting depth limit are accepted") {
    auto parseResult = parse(nestedWithBodies(kMaxQueryDepth));
    REQUIRE(parseResult);
}

TEST_CASE("parser: CTE bodies nested past the depth limit are refused") {
    auto parseResult = parse(nestedWithBodies(kMaxQueryDepth + 1));
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.astNodePointer == nullptr);
    REQUIRE(parseResult.errors.size() == 1);
    CHECK(parseResult.errors.front().message == "query is nested too deeply (maximum depth 200)");
}

TEST_CASE("parser: a query at the nesting depth limit is still parsed in full") {
    // Refusing one level deeper is the point; what sits just under the limit has to come back
    // whole, down to the innermost table name.
    auto parseResult = parse(nestedFromSubqueries(3));
    const auto& outer = requireNode<SelectNode>(parseResult);
    REQUIRE(outer.fromClause.size() == 1);
    const auto* middle = dynamic_cast<const SelectNode*>(outer.fromClause.at(0).table.derivedSelect.get());
    REQUIRE(middle != nullptr);
    REQUIRE(middle->fromClause.size() == 1);
    const auto* inner = dynamic_cast<const SelectNode*>(middle->fromClause.at(0).table.derivedSelect.get());
    REQUIRE(inner != nullptr);
    REQUIRE(inner->fromClause.size() == 1);
    CHECK(inner->fromClause.at(0).table.tableName == "t");
}
