#include "parser_tests_common.hpp"

using namespace sqlite2orm;
using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: CREATE VIRTUAL TABLE fts5 two columns") {
    CreateVirtualTableNode expected(SourceLocation{});
    expected.tableName = "posts_fts";
    expected.moduleName = "fts5";
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("title"));
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("body"));

    auto parseResult = parse("CREATE VIRTUAL TABLE posts_fts USING fts5(title, body)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateVirtualTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE VIRTUAL TABLE IF NOT EXISTS") {
    CreateVirtualTableNode expected(SourceLocation{});
    expected.ifNotExists = true;
    expected.tableName = "x";
    expected.moduleName = "fts5";
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("a"));

    auto parseResult = parse("CREATE VIRTUAL TABLE IF NOT EXISTS x USING fts5(a)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateVirtualTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE VIRTUAL TABLE qualified name") {
    CreateVirtualTableNode expected(SourceLocation{});
    expected.tableSchemaName = "main";
    expected.tableName = "doc";
    expected.moduleName = "fts5";
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("t"));

    auto parseResult = parse("CREATE VIRTUAL TABLE main.doc USING fts5(t)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateVirtualTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE TEMP VIRTUAL TABLE") {
    CreateVirtualTableNode expected(SourceLocation{});
    expected.temporary = true;
    expected.tableName = "s";
    expected.moduleName = "generate_series";

    auto parseResult = parse("CREATE TEMP VIRTUAL TABLE s USING generate_series");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateVirtualTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE VIRTUAL TABLE rtree with five columns") {
    CreateVirtualTableNode expected(SourceLocation{});
    expected.tableName = "geo";
    expected.moduleName = "rtree";
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("id"));
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("minX"));
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("maxX"));
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("minY"));
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("maxY"));

    auto parseResult = parse("CREATE VIRTUAL TABLE geo USING rtree(id, minX, maxX, minY, maxY)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateVirtualTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE VIRTUAL TABLE dbstat optional schema string") {
    CreateVirtualTableNode expected(SourceLocation{});
    expected.tableName = "d";
    expected.moduleName = "dbstat";
    expected.moduleArguments.push_back(makeNode<StringLiteralNode>("'main'"));

    auto parseResult = parse("CREATE VIRTUAL TABLE d USING dbstat('main')");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateVirtualTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE VIRTUAL TABLE module args expression") {
    CreateVirtualTableNode expected(SourceLocation{});
    expected.tableName = "f";
    expected.moduleName = "fts5";
    expected.moduleArguments.push_back(makeNode<ModuleArgumentNode>("lower(title)"));

    auto parseResult = parse("CREATE VIRTUAL TABLE f USING fts5(lower(title))");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateVirtualTableNode>(parseResult) == expected);
}

// SQLite hands module arguments to the module unparsed, so what is no expression is still an
// argument: FTS5 column options, `key=value` settings with or without a value, fts5vocab's types.
TEST_CASE("parser: CREATE VIRTUAL TABLE fts5 UNINDEXED column option") {
    CreateVirtualTableNode expected(SourceLocation{});
    expected.tableName = "mail";
    expected.moduleName = "fts5";
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("subject"));
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("body"));
    expected.moduleArguments.push_back(makeNode<ModuleArgumentNode>("sender UNINDEXED"));

    auto parseResult = parse("CREATE VIRTUAL TABLE mail USING fts5(subject, body, sender UNINDEXED)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateVirtualTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE VIRTUAL TABLE fts5 options keep their text as written") {
    CreateVirtualTableNode expected(SourceLocation{});
    expected.tableName = "c";
    expected.moduleName = "fts5";
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("a"));
    expected.moduleArguments.push_back(makeNode<ModuleArgumentNode>("tokenize='trigram'"));
    expected.moduleArguments.push_back(makeNode<ModuleArgumentNode>("prefix = '2  3'"));
    expected.moduleArguments.push_back(makeNode<ModuleArgumentNode>("content="));

    auto parseResult = parse("CREATE VIRTUAL TABLE c USING fts5(a, tokenize='trigram',  prefix = '2  3' , content=)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateVirtualTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE VIRTUAL TABLE keyword module arguments are names") {
    CreateVirtualTableNode expected(SourceLocation{});
    expected.tableName = "v";
    expected.moduleName = "fts5vocab";
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("u"));
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("row"));

    auto parseResult = parse("CREATE VIRTUAL TABLE v USING fts5vocab(u, row)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateVirtualTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE VIRTUAL TABLE empty module arguments are left out") {
    CreateVirtualTableNode expected(SourceLocation{});
    expected.tableName = "m";
    expected.moduleName = "fts5";
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("a"));
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("b"));

    auto parseResult = parse("CREATE VIRTUAL TABLE m USING fts5(, a,, b,)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateVirtualTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE VIRTUAL TABLE commas inside parentheses stay in one argument") {
    CreateVirtualTableNode expected(SourceLocation{});
    expected.tableName = "m";
    expected.moduleName = "nosuch";
    expected.moduleArguments.push_back(makeNode<ModuleArgumentNode>("x (a, (b, c))"));
    expected.moduleArguments.push_back(makeNode<ColumnRefNode>("d"));

    auto parseResult = parse("CREATE VIRTUAL TABLE m USING nosuch(x (a, (b, c)), d)");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateVirtualTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE VIRTUAL TABLE unbalanced module argument parentheses are refused") {
    REQUIRE_FALSE(parse("CREATE VIRTUAL TABLE m USING fts5(a, (b)"));
    REQUIRE_FALSE(parse("CREATE VIRTUAL TABLE m USING fts5(a, b))"));
}
