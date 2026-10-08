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
        row.push_back(makeNode<IntegerLiteralNode>("1"));
        row.push_back(makeNode<StringLiteralNode>("'a'"));
        expected.valueRows.push_back(std::move(row));
    }
    REQUIRE(requireNode<InsertNode>(parseResult) == expected);
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
        r1.push_back(makeNode<IntegerLiteralNode>("1"));
        expected.valueRows.push_back(std::move(r1));
        std::vector<AstNodePointer> r2;
        r2.push_back(makeNode<IntegerLiteralNode>("2"));
        expected.valueRows.push_back(std::move(r2));
    }
    REQUIRE(requireNode<InsertNode>(parseResult) == expected);
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
        row.push_back(makeNode<IntegerLiteralNode>("5"));
        expected.valueRows.push_back(std::move(row));
    }
    REQUIRE(requireNode<InsertNode>(parseResult) == expected);
}

TEST_CASE("parser: INSERT DEFAULT VALUES") {
    auto parseResult = parse("INSERT INTO users DEFAULT VALUES");
    REQUIRE(parseResult);
    InsertNode expected({});
    expected.tableName = "users";
    expected.dataKind = InsertDataKind::defaultValues;
    REQUIRE(requireNode<InsertNode>(parseResult) == expected);
}

TEST_CASE("parser: INSERT INTO SELECT") {
    auto parseResult = parse("INSERT INTO archive (id) SELECT id FROM users WHERE active = 0");
    REQUIRE(parseResult);
    auto selectAst = std::make_unique<SelectNode>(SourceLocation{});
    selectAst->columns.push_back({makeSharedNode<ColumnRefNode>("id"), ""});
    selectAst->fromClause = fromOne("users");
    selectAst->whereClause =
        std::make_shared<BinaryOperatorNode>(BinaryOperator::equals,
                                             std::make_unique<ColumnRefNode>("active", SourceLocation{}),
                                             std::make_unique<IntegerLiteralNode>("0", SourceLocation{}),
                                             SourceLocation{});
    InsertNode expected({});
    expected.tableName = "archive";
    expected.columnNames = {"id"};
    expected.dataKind = InsertDataKind::selectQuery;
    expected.selectStatement = std::move(selectAst);
    REQUIRE(requireNode<InsertNode>(parseResult) == expected);
}

TEST_CASE("parser: UPDATE SET WHERE") {
    auto parseResult = parse("UPDATE users SET name = 'x' WHERE id = 1");
    REQUIRE(parseResult);
    UpdateNode expected({});
    expected.tableName = "users";
    expected.assignments.push_back(UpdateAssignment{"name", makeNode<StringLiteralNode>("'x'")});
    expected.whereClause = makeNode<BinaryOperatorNode>(BinaryOperator::equals,
                                                        makeNode<ColumnRefNode>("id"),
                                                        makeNode<IntegerLiteralNode>("1"));
    REQUIRE(requireNode<UpdateNode>(parseResult) == expected);
}

TEST_CASE("parser: UPDATE multiple SET") {
    auto parseResult = parse("UPDATE users SET a = 1, b = 2");
    REQUIRE(parseResult);
    UpdateNode expected({});
    expected.tableName = "users";
    expected.assignments.push_back(UpdateAssignment{"a", makeNode<IntegerLiteralNode>("1")});
    expected.assignments.push_back(UpdateAssignment{"b", makeNode<IntegerLiteralNode>("2")});
    REQUIRE(requireNode<UpdateNode>(parseResult) == expected);
}

TEST_CASE("parser: DELETE FROM WHERE") {
    auto parseResult = parse("DELETE FROM users WHERE id = 1");
    REQUIRE(parseResult);
    DeleteNode expected({});
    expected.tableName = "users";
    expected.whereClause = makeNode<BinaryOperatorNode>(BinaryOperator::equals,
                                                        makeNode<ColumnRefNode>("id"),
                                                        makeNode<IntegerLiteralNode>("1"));
    REQUIRE(requireNode<DeleteNode>(parseResult) == expected);
}

TEST_CASE("parser: DELETE FROM without WHERE") {
    auto parseResult = parse("DELETE FROM users");
    REQUIRE(parseResult);
    DeleteNode expected({});
    expected.tableName = "users";
    REQUIRE(requireNode<DeleteNode>(parseResult) == expected);
}

// SQLite names the table of an UPDATE or a DELETE as `xfullname`: `t AS x`, the alias with `AS`
// only, ahead of any index hint. Before it was read, the parser stopped at `AS` with the WHERE
// still in the stream, and `DELETE FROM users AS u WHERE id > 1` came out as remove_all<Users>().
TEST_CASE("parser: DELETE FROM a table AS an alias keeps the WHERE after it") {
    auto parseResult = parse("DELETE FROM main.users AS u NOT INDEXED WHERE u.id > 1");
    REQUIRE(parseResult);
    DeleteNode expected({});
    expected.schemaName = "main";
    expected.tableName = "users";
    expected.alias = "u";
    expected.indexHint.kind = TableIndexHintKind::notIndexed;
    expected.whereClause = makeNode<BinaryOperatorNode>(BinaryOperator::greaterThan,
                                                        makeNode<QualifiedColumnRefNode>("u", "id"),
                                                        makeNode<IntegerLiteralNode>("1"));
    REQUIRE(requireNode<DeleteNode>(parseResult) == expected);
}

TEST_CASE("parser: UPDATE a table AS an alias keeps the SET and the WHERE after it") {
    auto parseResult = parse("UPDATE OR IGNORE users AS u SET name = u.name WHERE u.id = 1");
    REQUIRE(parseResult);
    UpdateNode expected({});
    expected.orConflict = ConflictClause::ignore;
    expected.tableName = "users";
    expected.alias = "u";
    expected.assignments.push_back(UpdateAssignment{"name", makeNode<QualifiedColumnRefNode>("u", "name")});
    expected.whereClause = makeNode<BinaryOperatorNode>(BinaryOperator::equals,
                                                        makeNode<QualifiedColumnRefNode>("u", "id"),
                                                        makeNode<IntegerLiteralNode>("1"));
    REQUIRE(requireNode<UpdateNode>(parseResult) == expected);
}

TEST_CASE("parser: the alias is part of an UPDATE and a DELETE") {
    REQUIRE_FALSE(requireNode<DeleteNode>(parse("DELETE FROM users AS u")) ==
                  requireNode<DeleteNode>(parse("DELETE FROM users")));
    REQUIRE_FALSE(requireNode<DeleteNode>(parse("DELETE FROM users AS u")) ==
                  requireNode<DeleteNode>(parse("DELETE FROM users AS v")));
    REQUIRE_FALSE(requireNode<UpdateNode>(parse("UPDATE users AS u SET a = 1")) ==
                  requireNode<UpdateNode>(parse("UPDATE users SET a = 1")));
}

// What sqlite3 3.51.0 refuses with `near "…": syntax error`: an alias without `AS`, `AS` with no
// name or a reserved word after it, and the alias after the index hint.
TEST_CASE("parser: error on a DML table alias where SQLite reads none") {
    auto requireRefusedAt = [](std::string_view sql, std::string_view message, SourceLocation location) {
        INFO(sql);
        auto parseResult = parse(std::string(sql));
        REQUIRE_FALSE(parseResult);
        REQUIRE(parseResult.errors == std::vector<ParseError>{ParseError{std::string(message), location}});
        REQUIRE(parseResult.errors.front().location == location);
    };
    requireRefusedAt("DELETE FROM users u WHERE id > 1", "unexpected token after statement: u", {1, 19});
    requireRefusedAt("DELETE FROM users AS", "expected a table alias after AS", {1, 21});
    requireRefusedAt("DELETE FROM users AS WHERE 1", "expected a table alias after AS", {1, 22});
    requireRefusedAt("DELETE FROM users INDEXED BY i AS u WHERE 1", "unexpected token after statement: AS", {1, 32});
    requireRefusedAt("UPDATE users u SET name = 'x'", "unexpected token: u", {1, 14});
    requireRefusedAt("UPDATE users AS u SET u.name = 'x'", "unexpected token: .", {1, 24});
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
        row.push_back(makeNode<IntegerLiteralNode>("1"));
        expected.valueRows.push_back(std::move(row));
    }
    REQUIRE(requireNode<InsertNode>(parseResult) == expected);
}

// --- UPDATE FROM ---

TEST_CASE("parser: UPDATE FROM") {
    auto parseResult = parse("UPDATE t SET a = b.a FROM b WHERE t.id = b.id");
    UpdateNode expected({});
    expected.tableName = "t";
    expected.assignments.push_back(UpdateAssignment{"a", makeNode<QualifiedColumnRefNode>("b", "a")});
    expected.fromClause = {
        FromClauseItem{JoinKind::none, FromTableClause{std::nullopt, std::string("b"), std::nullopt}, nullptr, {}}};
    expected.whereClause = makeNode<BinaryOperatorNode>(BinaryOperator::equals,
                                                        makeNode<QualifiedColumnRefNode>("t", "id"),
                                                        makeNode<QualifiedColumnRefNode>("b", "id"));
    REQUIRE(requireNode<UpdateNode>(parseResult) == expected);
}
