#include "parser_tests_common.hpp"

using namespace sqlite2orm;
using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: CREATE INDEX one column") {
    CreateIndexNode expected(SourceLocation{});
    expected.indexName = "idx_users_id";
    expected.tableName = "users";
    expected.indexedColumns.push_back(IndexColumnSpec{makeNode<ColumnRefNode>("id"), SortDirection::none, ""});

    auto parseResult = parse("CREATE INDEX idx_users_id ON users (id)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateIndexNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE UNIQUE INDEX two columns") {
    CreateIndexNode expected(SourceLocation{});
    expected.unique = true;
    expected.indexName = "u_ab";
    expected.tableName = "t";
    expected.indexedColumns.push_back(IndexColumnSpec{makeNode<ColumnRefNode>("a"), SortDirection::none, ""});
    expected.indexedColumns.push_back(IndexColumnSpec{makeNode<ColumnRefNode>("b"), SortDirection::none, ""});

    auto parseResult = parse("CREATE UNIQUE INDEX u_ab ON t (a, b)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateIndexNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE INDEX IF NOT EXISTS") {
    CreateIndexNode expected(SourceLocation{});
    expected.ifNotExists = true;
    expected.indexName = "i";
    expected.tableName = "x";
    expected.indexedColumns.push_back(IndexColumnSpec{makeNode<ColumnRefNode>("z"), SortDirection::none, ""});

    auto parseResult = parse("CREATE INDEX IF NOT EXISTS i ON x (z)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateIndexNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE INDEX COLLATE ASC DESC order flexible") {
    CreateIndexNode expected(SourceLocation{});
    expected.indexName = "i1";
    expected.tableName = "t";
    expected.indexedColumns.push_back(
        IndexColumnSpec{makeNode<ColumnRefNode>("n"), SortDirection::asc, "NOCASE"});

    auto parseResult = parse("CREATE INDEX i1 ON t (n COLLATE NOCASE ASC)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateIndexNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE INDEX DESC then COLLATE") {
    CreateIndexNode expected(SourceLocation{});
    expected.indexName = "i2";
    expected.tableName = "t";
    expected.indexedColumns.push_back(
        IndexColumnSpec{makeNode<ColumnRefNode>("v"), SortDirection::desc, "binary"});

    auto parseResult = parse("CREATE INDEX i2 ON t (v DESC COLLATE binary)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateIndexNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE INDEX partial WHERE") {
    CreateIndexNode expected(SourceLocation{});
    expected.indexName = "p";
    expected.tableName = "posts";
    expected.indexedColumns.push_back(IndexColumnSpec{makeNode<ColumnRefNode>("user_id"), SortDirection::none, ""});
    expected.whereClause = makeNode<ColumnRefNode>("active");

    auto parseResult = parse("CREATE INDEX p ON posts (user_id) WHERE active");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateIndexNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE INDEX expression column") {
    CreateIndexNode expected(SourceLocation{});
    expected.indexName = "i_lower";
    expected.tableName = "users";
    expected.indexedColumns.push_back(
        IndexColumnSpec{makeFunc("lower", false, false, makeNode<ColumnRefNode>("name")), SortDirection::none, ""});

    auto parseResult = parse("CREATE INDEX i_lower ON users (lower(name))");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateIndexNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE INDEX qualified names") {
    CreateIndexNode expected(SourceLocation{});
    expected.indexSchemaName = "main";
    expected.indexName = "ix";
    expected.tableSchemaName = "main";
    expected.tableName = "users";
    expected.indexedColumns.push_back(IndexColumnSpec{makeNode<ColumnRefNode>("id"), SortDirection::none, ""});

    auto parseResult = parse("CREATE INDEX main.ix ON main.users (id)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateIndexNode>(parseResult) == expected);
}
