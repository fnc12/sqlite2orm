#include "parser_tests_common.hpp"
using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: INSERT INTO columns VALUES single row") {
    auto parseResult = parse("INSERT INTO users (id, name) VALUES (1, 'a')");
    REQUIRE(parseResult);
    InsertNode expected({});
    expected.tableName = "users";
    expected.columnNames = {"id", "name"};
    expected.dataKind = InsertDataKind::values;
    {
        std::vector<AstNodePointer> row;
        row.push_back(make_node<IntegerLiteralNode>("1"));
        row.push_back(make_node<StringLiteralNode>("'a'"));
        expected.valueRows.push_back(std::move(row));
    }
    REQUIRE(require_node<InsertNode>(parseResult) == expected);
}

TEST_CASE("parser: INSERT OR IGNORE multiple VALUES rows") {
    auto parseResult = parse("INSERT OR IGNORE INTO t (x) VALUES (1), (2)");
    REQUIRE(parseResult);
    InsertNode expected({});
    expected.orConflict = ConflictClause::ignore;
    expected.tableName = "t";
    expected.columnNames = {"x"};
    {
        std::vector<AstNodePointer> r1;
        r1.push_back(make_node<IntegerLiteralNode>("1"));
        expected.valueRows.push_back(std::move(r1));
        std::vector<AstNodePointer> r2;
        r2.push_back(make_node<IntegerLiteralNode>("2"));
        expected.valueRows.push_back(std::move(r2));
    }
    REQUIRE(require_node<InsertNode>(parseResult) == expected);
}

TEST_CASE("parser: REPLACE INTO") {
    auto parseResult = parse("REPLACE INTO posts (user_id) VALUES (5)");
    REQUIRE(parseResult);
    InsertNode expected({});
    expected.replaceInto = true;
    expected.tableName = "posts";
    expected.columnNames = {"user_id"};
    {
        std::vector<AstNodePointer> row;
        row.push_back(make_node<IntegerLiteralNode>("5"));
        expected.valueRows.push_back(std::move(row));
    }
    REQUIRE(require_node<InsertNode>(parseResult) == expected);
}

TEST_CASE("parser: INSERT DEFAULT VALUES") {
    auto parseResult = parse("INSERT INTO users DEFAULT VALUES");
    REQUIRE(parseResult);
    InsertNode expected({});
    expected.tableName = "users";
    expected.dataKind = InsertDataKind::defaultValues;
    REQUIRE(require_node<InsertNode>(parseResult) == expected);
}

TEST_CASE("parser: INSERT INTO SELECT") {
    auto parseResult = parse("INSERT INTO archive (id) SELECT id FROM users WHERE active = 0");
    REQUIRE(parseResult);
    auto selectAst = std::make_unique<SelectNode>(SourceLocation{});
    selectAst->columns.push_back({make_shared_node<ColumnRefNode>("id"), ""});
    selectAst->fromClause = from_one("users");
    selectAst->whereClause = std::make_shared<BinaryOperatorNode>(
        BinaryOperator::equals, std::make_unique<ColumnRefNode>("active", SourceLocation{}),
        std::make_unique<IntegerLiteralNode>("0", SourceLocation{}), SourceLocation{});
    InsertNode expected({});
    expected.tableName = "archive";
    expected.columnNames = {"id"};
    expected.dataKind = InsertDataKind::selectQuery;
    expected.selectStatement = std::move(selectAst);
    REQUIRE(require_node<InsertNode>(parseResult) == expected);
}

TEST_CASE("parser: UPDATE SET WHERE") {
    auto parseResult = parse("UPDATE users SET name = 'x' WHERE id = 1");
    REQUIRE(parseResult);
    UpdateNode expected({});
    expected.tableName = "users";
    expected.assignments.push_back(UpdateAssignment{"name", make_node<StringLiteralNode>("'x'")});
    expected.whereClause =
        make_node<BinaryOperatorNode>(BinaryOperator::equals, make_node<ColumnRefNode>("id"),
                                      make_node<IntegerLiteralNode>("1"));
    REQUIRE(require_node<UpdateNode>(parseResult) == expected);
}

TEST_CASE("parser: UPDATE multiple SET") {
    auto parseResult = parse("UPDATE users SET a = 1, b = 2");
    REQUIRE(parseResult);
    UpdateNode expected({});
    expected.tableName = "users";
    expected.assignments.push_back(UpdateAssignment{"a", make_node<IntegerLiteralNode>("1")});
    expected.assignments.push_back(UpdateAssignment{"b", make_node<IntegerLiteralNode>("2")});
    REQUIRE(require_node<UpdateNode>(parseResult) == expected);
}

TEST_CASE("parser: DELETE FROM WHERE") {
    auto parseResult = parse("DELETE FROM users WHERE id = 1");
    REQUIRE(parseResult);
    DeleteNode expected({});
    expected.tableName = "users";
    expected.whereClause =
        make_node<BinaryOperatorNode>(BinaryOperator::equals, make_node<ColumnRefNode>("id"),
                                      make_node<IntegerLiteralNode>("1"));
    REQUIRE(require_node<DeleteNode>(parseResult) == expected);
}

TEST_CASE("parser: DELETE FROM without WHERE") {
    auto parseResult = parse("DELETE FROM users");
    REQUIRE(parseResult);
    DeleteNode expected({});
    expected.tableName = "users";
    REQUIRE(require_node<DeleteNode>(parseResult) == expected);
}

TEST_CASE("parser: schema-qualified DML table") {
    auto parseResult = parse("INSERT INTO main.users (id) VALUES (1)");
    REQUIRE(parseResult);
    InsertNode expected({});
    expected.schemaName = "main";
    expected.tableName = "users";
    expected.columnNames = {"id"};
    {
        std::vector<AstNodePointer> row;
        row.push_back(make_node<IntegerLiteralNode>("1"));
        expected.valueRows.push_back(std::move(row));
    }
    REQUIRE(require_node<InsertNode>(parseResult) == expected);
}

// --- UPDATE FROM ---

TEST_CASE("parser: UPDATE FROM") {
    auto parseResult = parse("UPDATE t SET a = b.a FROM b WHERE t.id = b.id");
    UpdateNode expected({});
    expected.tableName = "t";
    expected.assignments.push_back(UpdateAssignment{"a", make_node<QualifiedColumnRefNode>("b", "a")});
    expected.fromClause = {FromClauseItem{JoinKind::none,
        FromTableClause{std::nullopt, std::string("b"), std::nullopt}, nullptr, {}}};
    expected.whereClause = make_node<BinaryOperatorNode>(
        BinaryOperator::equals,
        make_node<QualifiedColumnRefNode>("t", "id"),
        make_node<QualifiedColumnRefNode>("b", "id"));
    REQUIRE(require_node<UpdateNode>(parseResult) == expected);
}
