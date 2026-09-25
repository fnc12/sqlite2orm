#include "parser_tests_common.hpp"
using namespace sqlite2orm::parser_test_helpers;

// --- SELECT ---

TEST_CASE("parser: SELECT * FROM table") {
    auto parseResult = parse("SELECT * FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT table.* FROM table") {
    auto parseResult = parse("SELECT users.* FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{makeSharedNode<QualifiedAsteriskNode>("users"), ""}};
    expected.fromClause = fromOne("users");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT schema.table.*") {
    auto parseResult = parse("SELECT main.users.* FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{std::make_shared<QualifiedAsteriskNode>("main", "users", SourceLocation{}), ""}};
    expected.fromClause = fromOne("users");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT table.* with column list") {
    auto parseResult = parse("SELECT id, users.* FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {
        SelectColumn{makeSharedNode<ColumnRefNode>("id"), ""},
        SelectColumn{makeSharedNode<QualifiedAsteriskNode>("users"), ""},
    };
    expected.fromClause = fromOne("users");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: FROM table AS alias") {
    auto parseResult = parse("SELECT name FROM users AS u");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{makeSharedNode<ColumnRefNode>("name"), ""}};
    expected.fromClause = {FromClauseItem{JoinKind::none,
                                          FromTableClause{std::nullopt, std::string("users"), std::string("u")},
                                          nullptr,
                                          {}}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: FROM table implicit alias") {
    auto parseResult = parse("SELECT id FROM users u");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{makeSharedNode<ColumnRefNode>("id"), ""}};
    expected.fromClause = {FromClauseItem{JoinKind::none,
                                          FromTableClause{std::nullopt, std::string("users"), std::string("u")},
                                          nullptr,
                                          {}}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: FROM schema.table") {
    auto parseResult = parse("SELECT * FROM main.users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = {FromClauseItem{
        JoinKind::none,
        FromTableClause{std::optional<std::string>(std::string("main")), std::string("users"), std::nullopt},
        nullptr,
        {}}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

// A comma parses as `crossJoin`, and the item records that the operator was the comma: the two
// spellings answer the same rows, but only the written `CROSS JOIN` keeps SQLite from reordering
// the tables, and codegen reads the difference. The `true` below is the whole of that record.
TEST_CASE("parser: FROM two tables comma") {
    auto parseResult = parse("SELECT * FROM users, posts");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = {
        FromClauseItem{JoinKind::none, FromTableClause{std::nullopt, std::string("users"), std::nullopt}, nullptr, {}},
        FromClauseItem{JoinKind::crossJoin,
                       FromTableClause{std::nullopt, std::string("posts"), std::nullopt},
                       nullptr,
                       {},
                       true},
    };
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: INNER JOIN ON") {
    auto parseResult = parse("SELECT * FROM users INNER JOIN posts ON users.id = posts.user_id");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.fromClause.size() == 2);
    REQUIRE(sel.fromClause.at(0).leadingJoin == JoinKind::none);
    REQUIRE(sel.fromClause.at(1).leadingJoin == JoinKind::innerJoin);
    REQUIRE(sel.fromClause.at(1).table.tableName == "posts");
    REQUIRE(sel.fromClause.at(1).onExpression != nullptr);
    REQUIRE(sel.fromClause.at(1).usingColumnNames.empty());
}

TEST_CASE("parser: LEFT OUTER JOIN USING") {
    auto parseResult = parse("SELECT * FROM users LEFT OUTER JOIN posts USING (user_id)");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.fromClause.size() == 2);
    REQUIRE(sel.fromClause.at(1).leadingJoin == JoinKind::leftOuterJoin);
    REQUIRE(sel.fromClause.at(1).usingColumnNames == std::vector<std::string>{"user_id"});
    REQUIRE(sel.fromClause.at(1).onExpression == nullptr);
}

// sqlite3 reads a constraint after CROSS JOIN the way it reads one after any other join
// operator: `SELECT a.name FROM users a CROSS JOIN orders b ON a.id = b.uid` answers the rows the
// ON keeps, not the whole cartesian product.
TEST_CASE("parser: CROSS JOIN ON") {
    auto parseResult = parse("SELECT * FROM users CROSS JOIN posts ON users.id = posts.user_id");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.fromClause.size() == 2);
    REQUIRE(sel.fromClause.at(1).leadingJoin == JoinKind::crossJoin);
    REQUIRE_FALSE(sel.fromClause.at(1).leadingJoinWrittenAsComma);
    REQUIRE(sel.fromClause.at(1).table.tableName == "posts");
    REQUIRE(sel.fromClause.at(1).onExpression != nullptr);
    REQUIRE(sel.fromClause.at(1).usingColumnNames.empty());
}

TEST_CASE("parser: CROSS JOIN USING") {
    auto parseResult = parse("SELECT * FROM users CROSS JOIN posts USING (user_id)");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.fromClause.size() == 2);
    REQUIRE(sel.fromClause.at(1).leadingJoin == JoinKind::crossJoin);
    REQUIRE(sel.fromClause.at(1).usingColumnNames == std::vector<std::string>{"user_id"});
    REQUIRE(sel.fromClause.at(1).onExpression == nullptr);
}

// A comma is a join operator of its own, and sqlite3 takes a constraint after it too. It parses
// as `crossJoin` like the keyword does, and is told apart from it by the record of the spelling.
TEST_CASE("parser: comma join ON") {
    auto parseResult = parse("SELECT * FROM users, posts ON users.id = posts.user_id");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.fromClause.size() == 2);
    REQUIRE(sel.fromClause.at(1).leadingJoin == JoinKind::crossJoin);
    REQUIRE(sel.fromClause.at(1).leadingJoinWrittenAsComma);
    REQUIRE(sel.fromClause.at(1).onExpression != nullptr);
}

// sqlite3 takes a USING after a comma as well: `SELECT * FROM t1, t2 USING (a)` over rows
// (1,2),(3,4) and (1,9),(5,6) answers the one row 1|2|9.
TEST_CASE("parser: comma join USING") {
    auto parseResult = parse("SELECT * FROM t1, t2 USING (a)");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.fromClause.size() == 2);
    REQUIRE(sel.fromClause.at(1).leadingJoin == JoinKind::crossJoin);
    REQUIRE(sel.fromClause.at(1).leadingJoinWrittenAsComma);
    REQUIRE(sel.fromClause.at(1).usingColumnNames == std::vector<std::string>{"a"});
    REQUIRE(sel.fromClause.at(1).onExpression == nullptr);
}

// The one join operator that takes no constraint: sqlite3 answers "a NATURAL join may not have an
// ON or USING clause". The keyword is left unread, so the statement ends on a token of its own.
TEST_CASE("parser: error on NATURAL JOIN with ON") {
    auto parseResult = parse("SELECT * FROM users NATURAL JOIN posts ON users.id = posts.user_id");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

TEST_CASE("parser: error on NATURAL JOIN with USING") {
    auto parseResult = parse("SELECT * FROM users NATURAL JOIN posts USING (user_id)");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

TEST_CASE("parser: comma then INNER JOIN") {
    auto parseResult = parse("SELECT * FROM users, posts INNER JOIN comments ON posts.id = comments.post_id");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.fromClause.size() == 3);
    REQUIRE(sel.fromClause.at(1).leadingJoin == JoinKind::crossJoin);
    REQUIRE(sel.fromClause.at(2).leadingJoin == JoinKind::innerJoin);
}

TEST_CASE("parser: SELECT column FROM table") {
    auto parseResult = parse("SELECT name FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{makeSharedNode<ColumnRefNode>("name"), ""}};
    expected.fromClause = fromOne("users");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT multiple columns FROM table") {
    auto parseResult = parse("SELECT id, name, age FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {
        SelectColumn{makeSharedNode<ColumnRefNode>("id"), ""},
        SelectColumn{makeSharedNode<ColumnRefNode>("name"), ""},
        SelectColumn{makeSharedNode<ColumnRefNode>("age"), ""},
    };
    expected.fromClause = fromOne("users");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with WHERE") {
    auto parseResult = parse("SELECT * FROM users WHERE age > 18");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    expected.whereClause = makeSharedNode<BinaryOperatorNode>(BinaryOperator::greaterThan,
                                                              makeNode<ColumnRefNode>("age"),
                                                              makeNode<IntegerLiteralNode>("18"));
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with complex WHERE") {
    auto parseResult = parse("SELECT name FROM users WHERE age >= 18 AND active = 1");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{makeSharedNode<ColumnRefNode>("name"), ""}};
    expected.fromClause = fromOne("users");
    expected.whereClause =
        makeSharedNode<BinaryOperatorNode>(BinaryOperator::logicalAnd,
                                           makeNode<BinaryOperatorNode>(BinaryOperator::greaterOrEqual,
                                                                        makeNode<ColumnRefNode>("age"),
                                                                        makeNode<IntegerLiteralNode>("18")),
                                           makeNode<BinaryOperatorNode>(BinaryOperator::equals,
                                                                        makeNode<ColumnRefNode>("active"),
                                                                        makeNode<IntegerLiteralNode>("1")));
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT DISTINCT") {
    auto parseResult = parse("SELECT DISTINCT name FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.distinct = true;
    expected.columns = {SelectColumn{makeSharedNode<ColumnRefNode>("name"), ""}};
    expected.fromClause = fromOne("users");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with column alias") {
    auto parseResult = parse("SELECT name AS user_name FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{makeSharedNode<ColumnRefNode>("name"), "user_name"}};
    expected.fromClause = fromOne("users");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with implicit column alias") {
    auto parseResult = parse("SELECT name user_name FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{makeSharedNode<ColumnRefNode>("name"), "user_name"}};
    expected.fromClause = fromOne("users");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT function with implicit alias") {
    auto parseResult = parse("SELECT instr(abilities, 'o') i FROM marvel");
    REQUIRE(parseResult);
    std::vector<AstNodePointer> instrArgs;
    instrArgs.push_back(makeNode<ColumnRefNode>("abilities"));
    instrArgs.push_back(makeNode<StringLiteralNode>("'o'"));
    auto instrCall = std::make_shared<FunctionCallNode>("instr", std::move(instrArgs), false, false, SourceLocation{});
    SelectNode expected({});
    expected.columns = {SelectColumn{std::move(instrCall), "i"}};
    expected.fromClause = fromOne("marvel");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT expression") {
    auto parseResult = parse("SELECT id + 1 FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{makeSharedNode<BinaryOperatorNode>(BinaryOperator::add,
                                                                        makeNode<ColumnRefNode>("id"),
                                                                        makeNode<IntegerLiteralNode>("1")),
                                     ""}};
    expected.fromClause = fromOne("users");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT without FROM") {
    auto parseResult = parse("SELECT 1");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{makeSharedNode<IntegerLiteralNode>("1"), ""}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

// --- ORDER BY ---

TEST_CASE("parser: SELECT with ORDER BY") {
    auto parseResult = parse("SELECT * FROM users ORDER BY name");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    expected.orderBy = {OrderByTerm{makeSharedNode<ColumnRefNode>("name"), SortDirection::none}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with ORDER BY ASC") {
    auto parseResult = parse("SELECT * FROM users ORDER BY name ASC");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    expected.orderBy = {OrderByTerm{makeSharedNode<ColumnRefNode>("name"), SortDirection::asc}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with ORDER BY DESC") {
    auto parseResult = parse("SELECT * FROM users ORDER BY age DESC");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    expected.orderBy = {OrderByTerm{makeSharedNode<ColumnRefNode>("age"), SortDirection::desc}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with multiple ORDER BY") {
    auto parseResult = parse("SELECT * FROM users ORDER BY name ASC, age DESC");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    expected.orderBy = {
        OrderByTerm{makeSharedNode<ColumnRefNode>("name"), SortDirection::asc},
        OrderByTerm{makeSharedNode<ColumnRefNode>("age"), SortDirection::desc},
    };
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

// --- LIMIT / OFFSET ---

TEST_CASE("parser: SELECT with LIMIT") {
    auto parseResult = parse("SELECT * FROM users LIMIT 10");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    expected.limitValue = makeNode<IntegerLiteralNode>("10");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with LIMIT OFFSET") {
    auto parseResult = parse("SELECT * FROM users LIMIT 10 OFFSET 20");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    expected.limitValue = makeNode<IntegerLiteralNode>("10");
    expected.offsetValue = makeNode<IntegerLiteralNode>("20");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

// Real sqlite3 (3.51) accepts `LIMIT -1` (it means "no limit"), so the clause takes a whole
// expression, not an unsigned integer literal.
TEST_CASE("parser: SELECT with negative LIMIT") {
    auto parseResult = parse("SELECT * FROM users LIMIT -1");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    expected.limitValue = makeNode<UnaryOperatorNode>(UnaryOperator::minus, makeNode<IntegerLiteralNode>("1"));
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with negative OFFSET") {
    auto parseResult = parse("SELECT * FROM users LIMIT 10 OFFSET -1");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    expected.limitValue = makeNode<IntegerLiteralNode>("10");
    expected.offsetValue = makeNode<UnaryOperatorNode>(UnaryOperator::minus, makeNode<IntegerLiteralNode>("1"));
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with negative LIMIT and OFFSET") {
    auto parseResult = parse("SELECT * FROM users LIMIT -1 OFFSET 2");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    expected.limitValue = makeNode<UnaryOperatorNode>(UnaryOperator::minus, makeNode<IntegerLiteralNode>("1"));
    expected.offsetValue = makeNode<IntegerLiteralNode>("2");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with unary plus LIMIT") {
    auto parseResult = parse("SELECT * FROM users LIMIT +10");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    expected.limitValue = makeNode<UnaryOperatorNode>(UnaryOperator::plus, makeNode<IntegerLiteralNode>("10"));
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with computed LIMIT") {
    auto parseResult = parse("SELECT * FROM users LIMIT 2 * 3");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    expected.limitValue = makeNode<BinaryOperatorNode>(BinaryOperator::multiply,
                                                       makeNode<IntegerLiteralNode>("2"),
                                                       makeNode<IntegerLiteralNode>("3"));
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

// `LIMIT <offset>, <count>` is SQLite's comma form: the first expression is the offset.
TEST_CASE("parser: SELECT with LIMIT offset comma count") {
    auto parseResult = parse("SELECT * FROM users LIMIT 5, 10");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("users");
    expected.limitValue = makeNode<IntegerLiteralNode>("10");
    expected.offsetValue = makeNode<IntegerLiteralNode>("5");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

// sqlite3 rejects a LIMIT without a value; dropping the clause silently would generate code for a
// different query than the one that was asked for.
TEST_CASE("parser: error on LIMIT without a value") {
    auto parseResult = parse("SELECT * FROM users LIMIT");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

TEST_CASE("parser: error on OFFSET without a value") {
    auto parseResult = parse("SELECT * FROM users LIMIT 10 OFFSET");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

// A result column with no expression is the bare `*`, which SQLite also allows next to other
// columns (`SELECT *, a FROM t` answers with every column of `t` and then `a` again).
TEST_CASE("parser: SELECT with a star next to another result column") {
    auto parseResult = parse("SELECT *, name FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}, SelectColumn{makeSharedNode<ColumnRefNode>("name"), ""}};
    expected.fromClause = fromOne("users");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

// sqlite3 refuses a result list that ends on a comma (`near ";": syntax error`), and letting it
// through would leave a result column standing for nothing — which is how the bare `*` is spelled.
TEST_CASE("parser: error on a result list ending on a comma") {
    auto parseResult = parse("SELECT 1,");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

// The clauses that take an expression all refuse a missing one, the way sqlite3 does.
TEST_CASE("parser: error on WHERE without an expression") {
    auto parseResult = parse("SELECT a FROM users WHERE");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

TEST_CASE("parser: error on GROUP BY without a term") {
    auto parseResult = parse("SELECT a FROM users GROUP BY");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

TEST_CASE("parser: error on GROUP BY ending on a comma") {
    auto parseResult = parse("SELECT a FROM users GROUP BY a,");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

TEST_CASE("parser: error on HAVING without an expression") {
    auto parseResult = parse("SELECT a FROM users GROUP BY a HAVING");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

// HAVING is a clause of the select core of its own: SQLite takes one with no GROUP BY in front of
// it and answers the aggregate over the whole table, `SELECT count(*) FROM users HAVING count(*) >
// 1` on 3.51.0. Read as a tail of GROUP BY alone, the clause was left in the token stream and the
// SELECT parsed as the query without it.
TEST_CASE("parser: SELECT with HAVING and no GROUP BY") {
    auto parseResult = parse("SELECT count(*) FROM users HAVING count(*) > 1");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{std::shared_ptr<AstNode>(makeFunc("count", false, true)), ""}};
    expected.fromClause = fromOne("users");
    expected.having = makeSharedNode<BinaryOperatorNode>(BinaryOperator::greaterThan,
                                                         makeFunc("count", false, true),
                                                         makeNode<IntegerLiteralNode>("1"));
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: error on HAVING without an expression and no GROUP BY") {
    auto parseResult = parse("SELECT count(*) FROM users HAVING");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

// The clause stands where SQLite reads it, after GROUP BY and before WINDOW, ORDER BY and LIMIT:
// `SELECT count(*) FROM users ORDER BY 1 HAVING count(*) > 1` and the LIMIT form of it are both
// `near "HAVING": syntax error` on 3.51.0.
TEST_CASE("parser: SELECT with HAVING before ORDER BY and LIMIT") {
    auto parseResult = parse("SELECT count(*) FROM users HAVING count(*) > 1 ORDER BY 1 LIMIT 2");
    REQUIRE(parseResult);
    const auto& selectNode = requireNode<SelectNode>(parseResult);
    REQUIRE(selectNode.having != nullptr);
    REQUIRE(selectNode.orderBy.size() == 1);
    REQUIRE(*selectNode.limitValue == *makeNode<IntegerLiteralNode>("2"));
}

TEST_CASE("parser: error on HAVING after ORDER BY") {
    auto parseResult = parse("SELECT count(*) FROM users ORDER BY 1 HAVING count(*) > 1");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

TEST_CASE("parser: error on HAVING after LIMIT") {
    auto parseResult = parse("SELECT count(*) FROM users LIMIT 1 HAVING count(*) > 1");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

// --- GROUP BY ---

TEST_CASE("parser: SELECT with GROUP BY") {
    auto parseResult = parse("SELECT name, count(*) FROM users GROUP BY name");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {
        SelectColumn{makeSharedNode<ColumnRefNode>("name"), ""},
        SelectColumn{std::shared_ptr<AstNode>(makeFunc("count", false, true)), ""},
    };
    expected.fromClause = fromOne("users");
    expected.groupBy = GroupByClause{{makeSharedNode<ColumnRefNode>("name")}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with GROUP BY HAVING") {
    auto parseResult = parse("SELECT name, count(*) FROM users GROUP BY name HAVING count(*) > 1");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {
        SelectColumn{makeSharedNode<ColumnRefNode>("name"), ""},
        SelectColumn{std::shared_ptr<AstNode>(makeFunc("count", false, true)), ""},
    };
    expected.fromClause = fromOne("users");
    expected.groupBy = GroupByClause{{makeSharedNode<ColumnRefNode>("name")}};
    expected.having = makeSharedNode<BinaryOperatorNode>(BinaryOperator::greaterThan,
                                                         makeFunc("count", false, true),
                                                         makeNode<IntegerLiteralNode>("1"));
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with GROUP BY multiple columns") {
    auto parseResult = parse("SELECT department, role FROM users GROUP BY department, role");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {
        SelectColumn{makeSharedNode<ColumnRefNode>("department"), ""},
        SelectColumn{makeSharedNode<ColumnRefNode>("role"), ""},
    };
    expected.fromClause = fromOne("users");
    expected.groupBy =
        GroupByClause{{makeSharedNode<ColumnRefNode>("department"), makeSharedNode<ColumnRefNode>("role")}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT WINDOW clause") {
    auto parseResult = parse("SELECT row_number() OVER w FROM t WINDOW w AS (ORDER BY id)");
    REQUIRE(parseResult);
    SelectNode expected({});
    auto rowNumberCall =
        std::make_shared<FunctionCallNode>("row_number", std::vector<AstNodePointer>{}, false, false, SourceLocation{});
    rowNumberCall->over = std::make_unique<OverClause>();
    rowNumberCall->over->namedWindow = "w";
    expected.columns = {SelectColumn{rowNumberCall, ""}};
    expected.fromClause = fromOne("t");
    NamedWindowDefinition namedWindow;
    namedWindow.name = "w";
    namedWindow.definition = std::make_unique<OverClause>();
    namedWindow.definition->orderBy.push_back(OrderByTerm{makeSharedNode<ColumnRefNode>("id"), SortDirection::none});
    expected.namedWindows.push_back(std::move(namedWindow));
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

// --- Full SELECT ---

TEST_CASE("parser: SELECT with all clauses") {
    auto parseResult = parse("SELECT name FROM users WHERE age > 18 GROUP BY name ORDER BY name ASC LIMIT 10 OFFSET 5");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{makeSharedNode<ColumnRefNode>("name"), ""}};
    expected.fromClause = fromOne("users");
    expected.whereClause = makeSharedNode<BinaryOperatorNode>(BinaryOperator::greaterThan,
                                                              makeNode<ColumnRefNode>("age"),
                                                              makeNode<IntegerLiteralNode>("18"));
    expected.groupBy = GroupByClause{{makeSharedNode<ColumnRefNode>("name")}};
    expected.orderBy = {OrderByTerm{makeSharedNode<ColumnRefNode>("name"), SortDirection::asc}};
    expected.limitValue = makeNode<IntegerLiteralNode>("10");
    expected.offsetValue = makeNode<IntegerLiteralNode>("5");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

// --- ORDER BY COLLATE ---

TEST_CASE("parser: ORDER BY COLLATE") {
    auto parseResult = parse("SELECT * FROM t ORDER BY name COLLATE NOCASE");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("t");
    expected.orderBy = {OrderByTerm{makeSharedNode<ColumnRefNode>("name"), SortDirection::none, "NOCASE"}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

// --- Window frame EXCLUDE ---

TEST_CASE("parser: EXCLUDE NO OTHERS in window frame") {
    auto parseResult = parse("SELECT sum(x) OVER (ROWS UNBOUNDED PRECEDING EXCLUDE NO OTHERS) FROM t");
    std::vector<AstNodePointer> args;
    args.push_back(makeNode<ColumnRefNode>("x"));
    auto funcNode = std::make_shared<FunctionCallNode>("sum", std::move(args), false, false, SourceLocation{});
    funcNode->over = std::make_unique<OverClause>();
    funcNode->over->frame = std::make_unique<WindowFrameSpec>();
    funcNode->over->frame->unit = WindowFrameUnit::rows;
    funcNode->over->frame->start = WindowFrameBound{WindowFrameBoundKind::unboundedPreceding, nullptr};
    funcNode->over->frame->exclude = WindowFrameExcludeKind::none;
    SelectNode expected({});
    expected.columns = {SelectColumn{funcNode, ""}};
    expected.fromClause = fromOne("t");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: VALUES standalone") {
    auto parseResult = parse("VALUES (1, 'a'), (2, 'b')");
    SelectNode expected({});
    expected.columns = {
        SelectColumn{makeSharedNode<IntegerLiteralNode>("1"), ""},
        SelectColumn{makeSharedNode<StringLiteralNode>("'a'"), ""},
    };
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

namespace {

    AstNodePointer selectLiteralInt(std::string_view v) {
        auto selectNode = std::make_unique<SelectNode>(SourceLocation{});
        selectNode->columns = {SelectColumn{makeSharedNode<IntegerLiteralNode>(v), ""}};
        return selectNode;
    }

}  // namespace

TEST_CASE("parser: VALUES UNION ALL SELECT compound") {
    auto parseResult = parse("VALUES (1) UNION ALL SELECT 2;");
    REQUIRE(parseResult);
    std::vector<AstNodePointer> arms;
    arms.push_back(selectLiteralInt("1"));
    arms.push_back(selectLiteralInt("2"));
    CompoundSelectNode expected(std::move(arms),
                                std::vector<CompoundSelectOperator>{CompoundSelectOperator::unionAll},
                                SourceLocation{});
    REQUIRE(requireNode<CompoundSelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT UNION ALL VALUES compound") {
    auto parseResult = parse("SELECT 1 UNION ALL VALUES (2);");
    REQUIRE(parseResult);
    std::vector<AstNodePointer> arms;
    arms.push_back(selectLiteralInt("1"));
    arms.push_back(selectLiteralInt("2"));
    CompoundSelectNode expected(std::move(arms),
                                std::vector<CompoundSelectOperator>{CompoundSelectOperator::unionAll},
                                SourceLocation{});
    REQUIRE(requireNode<CompoundSelectNode>(parseResult) == expected);
}

TEST_CASE("parser: table-function in FROM") {
    auto parseResult = parse("SELECT * FROM generate_series(1, 10)");
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    FromTableClause tableFunc;
    tableFunc.tableName = "generate_series";
    tableFunc.tableFunctionArgs.push_back(makeSharedNode<IntegerLiteralNode>("1"));
    tableFunc.tableFunctionArgs.push_back(makeSharedNode<IntegerLiteralNode>("10"));
    expected.fromClause = {FromClauseItem{JoinKind::none, std::move(tableFunc), nullptr, {}}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: parenthesized join in FROM") {
    auto parseResult = parse("SELECT * FROM (t1 INNER JOIN t2 ON t1.id = t2.t1_id)");
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = {
        FromClauseItem{JoinKind::none, FromTableClause{std::nullopt, std::string("t1"), std::nullopt}, nullptr, {}},
        FromClauseItem{JoinKind::innerJoin,
                       FromTableClause{std::nullopt, std::string("t2"), std::nullopt},
                       makeSharedNode<BinaryOperatorNode>(BinaryOperator::equals,
                                                          makeNode<QualifiedColumnRefNode>("t1", "id"),
                                                          makeNode<QualifiedColumnRefNode>("t2", "t1_id")),
                       {}},
    };
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: parenthesized join combined with outer join") {
    auto parseResult = parse("SELECT * FROM t0 LEFT JOIN (t1 INNER JOIN t2 ON t1.id = t2.t1_id) ON t0.id = t1.id");
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = {
        FromClauseItem{JoinKind::none, FromTableClause{std::nullopt, std::string("t0"), std::nullopt}, nullptr, {}},
        FromClauseItem{JoinKind::leftJoin,
                       FromTableClause{std::nullopt, std::string("t1"), std::nullopt},
                       makeSharedNode<BinaryOperatorNode>(BinaryOperator::equals,
                                                          makeNode<QualifiedColumnRefNode>("t0", "id"),
                                                          makeNode<QualifiedColumnRefNode>("t1", "id")),
                       {}},
        FromClauseItem{JoinKind::innerJoin,
                       FromTableClause{std::nullopt, std::string("t2"), std::nullopt},
                       makeSharedNode<BinaryOperatorNode>(BinaryOperator::equals,
                                                          makeNode<QualifiedColumnRefNode>("t1", "id"),
                                                          makeNode<QualifiedColumnRefNode>("t2", "t1_id")),
                       {}},
    };
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

// --- Single-quoted identifiers (SQLite quirk) ---

TEST_CASE("parser: FROM single-quoted table name") {
    auto parseResult = parse("SELECT * FROM 'users'");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = fromOne("'users'");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: FROM multiple single-quoted table names") {
    auto parseResult = parse("SELECT * FROM 'users', 'posts'");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.fromClause.size() == 2);
    REQUIRE(sel.fromClause[0].table.tableName == "'users'");
    REQUIRE(sel.fromClause[1].table.tableName == "'posts'");
}

TEST_CASE("parser: single-quoted qualified column ref") {
    auto parseResult = parse("SELECT 'users'.\"name\" FROM 'users'");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.columns.size() == 1);
    auto* qualRef = dynamic_cast<const QualifiedColumnRefNode*>(sel.columns[0].expression.get());
    REQUIRE(qualRef != nullptr);
    REQUIRE(qualRef->tableName == "'users'");
    REQUIRE(qualRef->columnName == "\"name\"");
}
