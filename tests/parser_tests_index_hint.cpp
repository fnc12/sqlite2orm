#include "parser_tests_common.hpp"

using namespace sqlite2orm;
using namespace sqlite2orm::parser_test_helpers;

// `INDEXED BY <index>` and `NOT INDEXED` are part of SQLite's qualified-table-name, after the
// alias and before whatever clause follows the table: sqlite3 3.51.0 takes every statement below.
// Left unread, the hint stopped the parse at the table, and the clauses after it — the WHERE among
// them — went missing from the tree codegen was handed.

namespace {

    std::vector<FromClauseItem> fromOneWithHint(std::string_view tableName, TableIndexHint hint) {
        auto items = fromOne(tableName);
        items.at(0).table.indexHint = std::move(hint);
        return items;
    }

    AstNodePointer aGreaterThanOne() {
        return makeNode<BinaryOperatorNode>(BinaryOperator::greaterThan,
                                            makeNode<ColumnRefNode>("a"),
                                            makeNode<IntegerLiteralNode>("1"));
    }

}  // namespace

TEST_CASE("parser: SELECT FROM a table NOT INDEXED keeps the WHERE after it") {
    auto parseResult = parse("SELECT a FROM t NOT INDEXED WHERE a > 1");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{std::shared_ptr<AstNode>(makeNode<ColumnRefNode>("a")), ""}};
    expected.fromClause = fromOneWithHint("t", TableIndexHint{TableIndexHintKind::notIndexed, ""});
    expected.whereClause = aGreaterThanOne();
    const auto& selectNode = requireNode<SelectNode>(parseResult);
    REQUIRE(selectNode == expected);
    REQUIRE(selectNode.fromClause.at(0).table.indexHint.sourceSpan == SourceSpan{SourceLocation{1, 17}, "NOT INDEXED"});
}

TEST_CASE("parser: SELECT FROM a table INDEXED BY an index keeps the WHERE after it") {
    auto parseResult = parse("SELECT a FROM t INDEXED  BY i WHERE a > 1");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{std::shared_ptr<AstNode>(makeNode<ColumnRefNode>("a")), ""}};
    expected.fromClause = fromOneWithHint("t", TableIndexHint{TableIndexHintKind::indexedBy, "i"});
    expected.whereClause = aGreaterThanOne();
    const auto& selectNode = requireNode<SelectNode>(parseResult);
    REQUIRE(selectNode == expected);
    REQUIRE(selectNode.fromClause.at(0).table.indexHint.sourceSpan ==
            SourceSpan{SourceLocation{1, 17}, "INDEXED  BY i"});
}

TEST_CASE("parser: the two index hints are different statements") {
    REQUIRE_FALSE(requireNode<SelectNode>(parse("SELECT a FROM t NOT INDEXED")) ==
                  requireNode<SelectNode>(parse("SELECT a FROM t INDEXED BY i")));
    REQUIRE_FALSE(requireNode<SelectNode>(parse("SELECT a FROM t INDEXED BY i")) ==
                  requireNode<SelectNode>(parse("SELECT a FROM t INDEXED BY j")));
    REQUIRE_FALSE(requireNode<SelectNode>(parse("SELECT a FROM t")) ==
                  requireNode<SelectNode>(parse("SELECT a FROM t NOT INDEXED")));
}

TEST_CASE("parser: an index hint after the alias of a schema-qualified table") {
    auto parseResult = parse("SELECT x.a FROM main.t AS x INDEXED BY i");
    REQUIRE(parseResult);
    const auto& table = requireNode<SelectNode>(parseResult).fromClause.at(0).table;
    REQUIRE(table ==
            FromTableClause{"main", "t", "x", nullptr, {}, TableIndexHint{TableIndexHintKind::indexedBy, "i"}});
}

TEST_CASE("parser: an index hint on each side of a join") {
    auto parseResult = parse("SELECT t.a FROM t NOT INDEXED JOIN t AS u INDEXED BY i ON t.a = u.a WHERE t.a > 1");
    REQUIRE(parseResult);
    const auto& selectNode = requireNode<SelectNode>(parseResult);
    REQUIRE(selectNode.fromClause.size() == 2);
    REQUIRE(selectNode.fromClause.at(0).table.indexHint == TableIndexHint{TableIndexHintKind::notIndexed, ""});
    REQUIRE(selectNode.fromClause.at(1).table.indexHint == TableIndexHint{TableIndexHintKind::indexedBy, "i"});
    REQUIRE(selectNode.fromClause.at(1).onExpression != nullptr);
    REQUIRE(selectNode.whereClause != nullptr);
}

TEST_CASE("parser: an index hint in a subquery") {
    auto parseResult = parse("SELECT (SELECT a FROM t INDEXED BY i WHERE a > 1)");
    REQUIRE(parseResult);
}

// What sqlite3 3.51.0 refuses with `near "…": syntax error`: the hint before the alias, the
// alias after it, both hints at once, a hint on a subquery or a table-valued function, and one
// on the table an INSERT writes.
TEST_CASE("parser: error on an index hint where SQLite reads none") {
    auto requireRefusedAt = [](std::string_view sql, std::string_view message, SourceLocation location) {
        INFO(sql);
        auto parseResult = parse(std::string(sql));
        REQUIRE_FALSE(parseResult);
        REQUIRE(parseResult.errors == std::vector<ParseError>{ParseError{std::string(message), location}});
        REQUIRE(parseResult.errors.front().location == location);
    };
    requireRefusedAt("SELECT a FROM t INDEXED BY i AS x", "unexpected token after statement: AS", {1, 30});
    requireRefusedAt("SELECT a FROM t NOT INDEXED x", "unexpected token after statement: x", {1, 29});
    requireRefusedAt("SELECT a FROM t INDEXED BY i NOT INDEXED", "unexpected token after statement: NOT", {1, 30});
    requireRefusedAt("SELECT a FROM t NOT INDEXED INDEXED BY i", "unexpected token after statement: INDEXED", {1, 29});
    // A subquery in FROM takes any keyword after it for its alias, NOT included, so the statement
    // is refused one token later than sqlite3 refuses it; it is refused all the same.
    requireRefusedAt("SELECT a FROM (SELECT a FROM t) NOT INDEXED",
                     "unexpected token after statement: INDEXED",
                     {1, 37});
    requireRefusedAt("SELECT * FROM json_each('[1]') NOT INDEXED", "unexpected token after statement: NOT", {1, 32});
    requireRefusedAt("INSERT INTO t NOT INDEXED VALUES (1)", "unexpected token: NOT", {1, 15});
}

TEST_CASE("parser: error on INDEXED with no BY or no index name") {
    auto noBy = parse("SELECT a FROM t INDEXED i WHERE a > 1");
    REQUIRE_FALSE(noBy);
    REQUIRE(noBy.astNodePointer == nullptr);
    REQUIRE(noBy.errors == std::vector<ParseError>{ParseError{"expected BY after INDEXED", {1, 25}}});
    REQUIRE(noBy.errors.front().location == SourceLocation{1, 25});

    // A keyword SQLite does not read as a name is no index name either: read as an index named
    // WHERE, the condition would have been left behind.
    auto noName = parse("SELECT a FROM t INDEXED BY WHERE a > 1");
    REQUIRE_FALSE(noName);
    REQUIRE(noName.astNodePointer == nullptr);
    REQUIRE(noName.errors == std::vector<ParseError>{ParseError{"expected an index name after INDEXED BY", {1, 28}}});
    REQUIRE(noName.errors.front().location == SourceLocation{1, 28});

    auto atEnd = parse("SELECT a FROM t INDEXED BY");
    REQUIRE_FALSE(atEnd);
    REQUIRE(atEnd.errors == std::vector<ParseError>{ParseError{"expected an index name after INDEXED BY", {1, 27}}});
}

// SQLite reads the index as `nm`: a quoted name, a string, or a keyword it folds back into a name.
TEST_CASE("parser: an index name SQLite reads as a name") {
    REQUIRE(requireNode<SelectNode>(parse("SELECT a FROM t INDEXED BY \"i\"")).fromClause.at(0).table.indexHint ==
            TableIndexHint{TableIndexHintKind::indexedBy, "\"i\""});
    REQUIRE(requireNode<SelectNode>(parse("SELECT a FROM t INDEXED BY 'i'")).fromClause.at(0).table.indexHint ==
            TableIndexHint{TableIndexHintKind::indexedBy, "'i'"});
    REQUIRE(requireNode<SelectNode>(parse("SELECT a FROM t INDEXED BY key")).fromClause.at(0).table.indexHint ==
            TableIndexHint{TableIndexHintKind::indexedBy, "key"});
}

TEST_CASE("parser: DELETE FROM a table NOT INDEXED keeps the WHERE after it") {
    auto parseResult = parse("DELETE FROM t NOT INDEXED WHERE a > 1");
    REQUIRE(parseResult);
    DeleteNode expected({});
    expected.tableName = "t";
    expected.indexHint = TableIndexHint{TableIndexHintKind::notIndexed, ""};
    expected.whereClause = aGreaterThanOne();
    REQUIRE(requireNode<DeleteNode>(parseResult) == expected);
}

TEST_CASE("parser: UPDATE a table INDEXED BY an index keeps the SET and WHERE after it") {
    auto parseResult = parse("UPDATE t INDEXED BY i SET a = 1 WHERE a > 1");
    REQUIRE(parseResult);
    UpdateNode expected({});
    expected.tableName = "t";
    expected.indexHint = TableIndexHint{TableIndexHintKind::indexedBy, "i"};
    expected.assignments.push_back(UpdateAssignment{"a", makeNode<IntegerLiteralNode>("1")});
    expected.whereClause = aGreaterThanOne();
    const auto& updateNode = requireNode<UpdateNode>(parseResult);
    REQUIRE(updateNode == expected);
    REQUIRE(updateNode.indexHint.sourceSpan == SourceSpan{SourceLocation{1, 10}, "INDEXED BY i"});
}

TEST_CASE("parser: DELETE and UPDATE differ by their index hint") {
    REQUIRE_FALSE(requireNode<DeleteNode>(parse("DELETE FROM t")) ==
                  requireNode<DeleteNode>(parse("DELETE FROM t NOT INDEXED")));
    REQUIRE_FALSE(requireNode<UpdateNode>(parse("UPDATE t SET a = 1")) ==
                  requireNode<UpdateNode>(parse("UPDATE t INDEXED BY i SET a = 1")));
}

// A trigger body takes the hint on a SELECT, and refuses it on the table an UPDATE or a DELETE
// writes, in these words on 3.51.0.
TEST_CASE("parser: a trigger body SELECT takes an index hint") {
    auto parseResult = parse("CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT a FROM t INDEXED BY i WHERE a > 1; END");
    REQUIRE(parseResult);
}

TEST_CASE("parser: error on an index hint on a trigger body UPDATE or DELETE") {
    auto deleteStep = parse("CREATE TRIGGER tr AFTER INSERT ON t BEGIN DELETE FROM t NOT INDEXED WHERE a > 1; END");
    REQUIRE_FALSE(deleteStep);
    REQUIRE(deleteStep.astNodePointer == nullptr);
    REQUIRE(deleteStep.errors ==
            std::vector<ParseError>{
                ParseError{"the NOT INDEXED clause is not allowed on UPDATE or DELETE statements within triggers",
                           {1, 57}}});
    REQUIRE(deleteStep.errors.front().location == SourceLocation{1, 57});

    auto updateStep = parse("CREATE TRIGGER tr AFTER INSERT ON t BEGIN UPDATE t INDEXED BY i SET a = 1; END");
    REQUIRE_FALSE(updateStep);
    REQUIRE(updateStep.astNodePointer == nullptr);
    REQUIRE(updateStep.errors ==
            std::vector<ParseError>{
                ParseError{"the INDEXED BY clause is not allowed on UPDATE or DELETE statements within triggers",
                           {1, 52}}});
    REQUIRE(updateStep.errors.front().location == SourceLocation{1, 52});
}
