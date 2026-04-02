#include "parser_tests_common.hpp"

using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: SAVEPOINT name") {
    auto result = parse("SAVEPOINT sp1;");
    requireNode<SavepointNode>(result);
    REQUIRE(requireNode<SavepointNode>(result) == SavepointNode({}, "sp1"));
}

TEST_CASE("parser: RELEASE SAVEPOINT name") {
    auto result = parse("RELEASE SAVEPOINT sp1;");
    REQUIRE(requireNode<ReleaseNode>(result) == ReleaseNode({}, "sp1"));
}

TEST_CASE("parser: RELEASE name (without SAVEPOINT keyword)") {
    auto result = parse("RELEASE sp1;");
    REQUIRE(requireNode<ReleaseNode>(result) == ReleaseNode({}, "sp1"));
}

TEST_CASE("parser: ATTACH DATABASE") {
    auto result = parse("ATTACH DATABASE 'test.db' AS test_schema;");
    const auto& node = requireNode<AttachDatabaseNode>(result);
    REQUIRE(node.schemaName == "test_schema");
    REQUIRE(dynamic_cast<const StringLiteralNode*>(node.fileExpression.get()) != nullptr);
}

TEST_CASE("parser: ATTACH without DATABASE keyword") {
    auto result = parse("ATTACH 'data.db' AS aux;");
    const auto& node = requireNode<AttachDatabaseNode>(result);
    REQUIRE(node.schemaName == "aux");
}

TEST_CASE("parser: DETACH DATABASE") {
    auto result = parse("DETACH DATABASE aux;");
    REQUIRE(requireNode<DetachDatabaseNode>(result) == DetachDatabaseNode({}, "aux"));
}

TEST_CASE("parser: DETACH without DATABASE keyword") {
    auto result = parse("DETACH aux;");
    REQUIRE(requireNode<DetachDatabaseNode>(result) == DetachDatabaseNode({}, "aux"));
}

TEST_CASE("parser: ANALYZE bare") {
    auto result = parse("ANALYZE;");
    auto expected = AnalyzeNode({});
    REQUIRE(requireNode<AnalyzeNode>(result) == expected);
}

TEST_CASE("parser: ANALYZE table") {
    auto result = parse("ANALYZE users;");
    auto expected = AnalyzeNode({});
    expected.schemaOrTableName = "users";
    REQUIRE(requireNode<AnalyzeNode>(result) == expected);
}

TEST_CASE("parser: ANALYZE schema.table") {
    auto result = parse("ANALYZE main.users;");
    auto expected = AnalyzeNode({});
    expected.schemaOrTableName = "main";
    expected.tableName = "users";
    REQUIRE(requireNode<AnalyzeNode>(result) == expected);
}

TEST_CASE("parser: REINDEX bare") {
    auto result = parse("REINDEX;");
    auto expected = ReindexNode({});
    REQUIRE(requireNode<ReindexNode>(result) == expected);
}

TEST_CASE("parser: REINDEX collation") {
    auto result = parse("REINDEX NOCASE;");
    auto expected = ReindexNode({});
    expected.schemaOrObjectName = "NOCASE";
    REQUIRE(requireNode<ReindexNode>(result) == expected);
}

TEST_CASE("parser: REINDEX schema.table") {
    auto result = parse("REINDEX main.users;");
    auto expected = ReindexNode({});
    expected.schemaOrObjectName = "main";
    expected.objectName = "users";
    REQUIRE(requireNode<ReindexNode>(result) == expected);
}

TEST_CASE("parser: PRAGMA query") {
    auto result = parse("PRAGMA table_info;");
    auto expected = PragmaNode({});
    expected.pragmaName = "table_info";
    REQUIRE(requireNode<PragmaNode>(result) == expected);
}

TEST_CASE("parser: PRAGMA with value") {
    auto result = parse("PRAGMA journal_mode = WAL;");
    const auto& node = requireNode<PragmaNode>(result);
    REQUIRE(node.pragmaName == "journal_mode");
    REQUIRE(node.value != nullptr);
}

TEST_CASE("parser: PRAGMA with parenthesized value") {
    auto result = parse("PRAGMA table_info('users');");
    const auto& node = requireNode<PragmaNode>(result);
    REQUIRE(node.pragmaName == "table_info");
    REQUIRE(node.value != nullptr);
}

TEST_CASE("parser: PRAGMA schema.name") {
    auto result = parse("PRAGMA main.journal_mode;");
    const auto& node = requireNode<PragmaNode>(result);
    REQUIRE(node.schemaName == "main");
    REQUIRE(node.pragmaName == "journal_mode");
}

TEST_CASE("parser: EXPLAIN SELECT") {
    auto result = parse("EXPLAIN SELECT 1;");
    const auto& node = requireNode<ExplainNode>(result);
    REQUIRE_FALSE(node.queryPlan);
    REQUIRE(dynamic_cast<const SelectNode*>(node.statement.get()) != nullptr);
}

TEST_CASE("parser: EXPLAIN QUERY PLAN SELECT") {
    auto result = parse("EXPLAIN QUERY PLAN SELECT * FROM users;");
    const auto& node = requireNode<ExplainNode>(result);
    REQUIRE(node.queryPlan);
    REQUIRE(dynamic_cast<const SelectNode*>(node.statement.get()) != nullptr);
}

TEST_CASE("parser: EXPLAIN INSERT") {
    auto result = parse("EXPLAIN INSERT INTO t (a) VALUES (1);");
    const auto& node = requireNode<ExplainNode>(result);
    REQUIRE_FALSE(node.queryPlan);
    REQUIRE(dynamic_cast<const InsertNode*>(node.statement.get()) != nullptr);
}

TEST_CASE("parser: ORDER BY with NULLS FIRST") {
    auto result = parse("SELECT * FROM users ORDER BY name ASC NULLS FIRST;");
    const auto& node = requireNode<SelectNode>(result);
    REQUIRE(node.orderBy.size() == 1);
    REQUIRE(node.orderBy.at(0).nulls == NullsOrdering::first);
}

TEST_CASE("parser: ORDER BY with NULLS LAST") {
    auto result = parse("SELECT * FROM users ORDER BY name DESC NULLS LAST;");
    const auto& node = requireNode<SelectNode>(result);
    REQUIRE(node.orderBy.size() == 1);
    REQUIRE(node.orderBy.at(0).nulls == NullsOrdering::last);
}

TEST_CASE("parser: INSERT with RETURNING") {
    auto result = parse("INSERT INTO t (a) VALUES (1) RETURNING id, name AS n;");
    const auto& node = requireNode<InsertNode>(result);
    REQUIRE(node.returning.size() == 2);
    REQUIRE(node.returning.at(0).alias.empty());
    REQUIRE(node.returning.at(1).alias == "n");
}

TEST_CASE("parser: UPDATE with RETURNING *") {
    auto result = parse("UPDATE t SET a = 1 RETURNING *;");
    const auto& node = requireNode<UpdateNode>(result);
    REQUIRE(node.returning.size() == 1);
}

TEST_CASE("parser: DELETE with RETURNING") {
    auto result = parse("DELETE FROM t WHERE id = 1 RETURNING id;");
    auto expectedAst = std::make_unique<DeleteNode>(SourceLocation{});
    expectedAst->tableName = "t";
    expectedAst->whereClause = makeNode<BinaryOperatorNode>(BinaryOperator::equals, makeNode<ColumnRefNode>("id"),
                                                             makeNode<IntegerLiteralNode>("1"));
    expectedAst->returning.push_back(ReturningColumn{makeNode<ColumnRefNode>("id"), ""});
    REQUIRE(result == ParseResult{std::move(expectedAst), {}});
}
