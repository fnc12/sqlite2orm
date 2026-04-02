#include "parser_tests_common.hpp"
using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: INSERT ON CONFLICT DO NOTHING") {
    auto parseResult = parse("INSERT INTO t (x) VALUES (1) ON CONFLICT DO NOTHING");
    REQUIRE(parseResult);
    InsertNode expected({});
    expected.tableName = "t";
    expected.columnNames = {"x"};
    expected.dataKind = InsertDataKind::values;
    {
        std::vector<AstNodePointer> row;
        row.push_back(makeNode<IntegerLiteralNode>("1"));
        expected.valueRows.push_back(std::move(row));
    }
    expected.hasUpsertClause = true;
    expected.upsertAction = InsertUpsertAction::doNothing;
    REQUIRE(requireNode<InsertNode>(parseResult) == expected);
}

TEST_CASE("parser: INSERT ON CONFLICT (id) DO NOTHING") {
    auto parseResult = parse("INSERT INTO users (id, name) VALUES (1, 'a') ON CONFLICT (id) DO NOTHING");
    REQUIRE(parseResult);
    InsertNode expected({});
    expected.tableName = "users";
    expected.columnNames = {"id", "name"};
    expected.dataKind = InsertDataKind::values;
    {
        std::vector<AstNodePointer> row;
        row.push_back(makeNode<IntegerLiteralNode>("1"));
        row.push_back(makeNode<StringLiteralNode>("'a'"));
        expected.valueRows.push_back(std::move(row));
    }
    expected.hasUpsertClause = true;
    expected.upsertConflictColumns = {"id"};
    expected.upsertAction = InsertUpsertAction::doNothing;
    REQUIRE(requireNode<InsertNode>(parseResult) == expected);
}

TEST_CASE("parser: INSERT ON CONFLICT DO UPDATE SET excluded") {
    auto parseResult = parse(
        "INSERT INTO users (id, name) VALUES (1, 'b') ON CONFLICT (id) DO UPDATE SET name = excluded.name");
    REQUIRE(parseResult);
    InsertNode expected({});
    expected.tableName = "users";
    expected.columnNames = {"id", "name"};
    expected.dataKind = InsertDataKind::values;
    {
        std::vector<AstNodePointer> row;
        row.push_back(makeNode<IntegerLiteralNode>("1"));
        row.push_back(makeNode<StringLiteralNode>("'b'"));
        expected.valueRows.push_back(std::move(row));
    }
    expected.hasUpsertClause = true;
    expected.upsertConflictColumns = {"id"};
    expected.upsertAction = InsertUpsertAction::doUpdate;
    expected.upsertUpdateAssignments.push_back(
        UpdateAssignment{"name", makeNode<ExcludedRefNode>("name")});
    REQUIRE(requireNode<InsertNode>(parseResult) == expected);
}

TEST_CASE("parser: INSERT ON CONFLICT two columns DO UPDATE") {
    auto parseResult = parse(
        "INSERT INTO t (a, b) VALUES (1, 2) ON CONFLICT (a, b) DO UPDATE SET a = a + 1");
    REQUIRE(parseResult);
    InsertNode expected({});
    expected.tableName = "t";
    expected.columnNames = {"a", "b"};
    expected.dataKind = InsertDataKind::values;
    {
        std::vector<AstNodePointer> row;
        row.push_back(makeNode<IntegerLiteralNode>("1"));
        row.push_back(makeNode<IntegerLiteralNode>("2"));
        expected.valueRows.push_back(std::move(row));
    }
    expected.hasUpsertClause = true;
    expected.upsertConflictColumns = {"a", "b"};
    expected.upsertAction = InsertUpsertAction::doUpdate;
    expected.upsertUpdateAssignments.push_back(UpdateAssignment{
        "a",
        makeNode<BinaryOperatorNode>(BinaryOperator::add, makeNode<ColumnRefNode>("a"),
                                      makeNode<IntegerLiteralNode>("1"))});
    REQUIRE(requireNode<InsertNode>(parseResult) == expected);
}

TEST_CASE("parser: INSERT ON CONFLICT target WHERE and DO UPDATE WHERE") {
    auto parseResult = parse(
        "INSERT INTO users (id, score) VALUES (1, 10) ON CONFLICT (id) WHERE score > 0 DO UPDATE SET score = "
        "score + 1 WHERE score < 100");
    REQUIRE(parseResult);
    InsertNode expected({});
    expected.tableName = "users";
    expected.columnNames = {"id", "score"};
    expected.dataKind = InsertDataKind::values;
    {
        std::vector<AstNodePointer> row;
        row.push_back(makeNode<IntegerLiteralNode>("1"));
        row.push_back(makeNode<IntegerLiteralNode>("10"));
        expected.valueRows.push_back(std::move(row));
    }
    expected.hasUpsertClause = true;
    expected.upsertConflictColumns = {"id"};
    expected.upsertConflictWhere =
        makeNode<BinaryOperatorNode>(BinaryOperator::greaterThan, makeNode<ColumnRefNode>("score"),
                                      makeNode<IntegerLiteralNode>("0"));
    expected.upsertAction = InsertUpsertAction::doUpdate;
    expected.upsertUpdateAssignments.push_back(UpdateAssignment{
        "score",
        makeNode<BinaryOperatorNode>(BinaryOperator::add, makeNode<ColumnRefNode>("score"),
                                      makeNode<IntegerLiteralNode>("1"))});
    expected.upsertUpdateWhere =
        makeNode<BinaryOperatorNode>(BinaryOperator::lessThan, makeNode<ColumnRefNode>("score"),
                                      makeNode<IntegerLiteralNode>("100"));
    REQUIRE(requireNode<InsertNode>(parseResult) == expected);
}
