#include "parser_tests_common.hpp"
using namespace sqlite2orm::parser_test_helpers;

// --- SELECT ---

TEST_CASE("parser: SELECT * FROM table") {
    auto parseResult = parse("SELECT * FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = from_one("users");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT table.* FROM table") {
    auto parseResult = parse("SELECT users.* FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{make_shared_node<QualifiedAsteriskNode>("users"), ""}};
    expected.fromClause = from_one("users");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT schema.table.*") {
    auto parseResult = parse("SELECT main.users.* FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{std::make_shared<QualifiedAsteriskNode>("main", "users", SourceLocation{}), ""}};
    expected.fromClause = from_one("users");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT table.* with column list") {
    auto parseResult = parse("SELECT id, users.* FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {
        SelectColumn{make_shared_node<ColumnRefNode>("id"), ""},
        SelectColumn{make_shared_node<QualifiedAsteriskNode>("users"), ""},
    };
    expected.fromClause = from_one("users");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: FROM table AS alias") {
    auto parseResult = parse("SELECT name FROM users AS u");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{make_shared_node<ColumnRefNode>("name"), ""}};
    expected.fromClause = {FromClauseItem{JoinKind::none,
                                          FromTableClause{std::nullopt, std::string("users"), std::string("u")},
                                          nullptr,
                                          {}}};
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: FROM table implicit alias") {
    auto parseResult = parse("SELECT id FROM users u");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{make_shared_node<ColumnRefNode>("id"), ""}};
    expected.fromClause = {FromClauseItem{JoinKind::none,
                                          FromTableClause{std::nullopt, std::string("users"), std::string("u")},
                                          nullptr,
                                          {}}};
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
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
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: FROM two tables comma") {
    auto parseResult = parse("SELECT * FROM users, posts");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = {
        FromClauseItem{JoinKind::none, FromTableClause{std::nullopt, std::string("users"), std::nullopt}, nullptr, {}},
        FromClauseItem{JoinKind::crossJoin, FromTableClause{std::nullopt, std::string("posts"), std::nullopt},
                       nullptr, {}},
    };
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: INNER JOIN ON") {
    auto parseResult = parse("SELECT * FROM users INNER JOIN posts ON users.id = posts.user_id");
    REQUIRE(parseResult);
    const auto& sel = require_node<SelectNode>(parseResult);
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
    const auto& sel = require_node<SelectNode>(parseResult);
    REQUIRE(sel.fromClause.size() == 2);
    REQUIRE(sel.fromClause.at(1).leadingJoin == JoinKind::leftOuterJoin);
    REQUIRE(sel.fromClause.at(1).usingColumnNames == std::vector<std::string>{"user_id"});
    REQUIRE(sel.fromClause.at(1).onExpression == nullptr);
}

TEST_CASE("parser: comma then INNER JOIN") {
    auto parseResult = parse("SELECT * FROM users, posts INNER JOIN comments ON posts.id = comments.post_id");
    REQUIRE(parseResult);
    const auto& sel = require_node<SelectNode>(parseResult);
    REQUIRE(sel.fromClause.size() == 3);
    REQUIRE(sel.fromClause.at(1).leadingJoin == JoinKind::crossJoin);
    REQUIRE(sel.fromClause.at(2).leadingJoin == JoinKind::innerJoin);
}

TEST_CASE("parser: SELECT column FROM table") {
    auto parseResult = parse("SELECT name FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{make_shared_node<ColumnRefNode>("name"), ""}};
    expected.fromClause = from_one("users");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT multiple columns FROM table") {
    auto parseResult = parse("SELECT id, name, age FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {
        SelectColumn{make_shared_node<ColumnRefNode>("id"), ""},
        SelectColumn{make_shared_node<ColumnRefNode>("name"), ""},
        SelectColumn{make_shared_node<ColumnRefNode>("age"), ""},
    };
    expected.fromClause = from_one("users");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with WHERE") {
    auto parseResult = parse("SELECT * FROM users WHERE age > 18");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = from_one("users");
    expected.whereClause = make_shared_node<BinaryOperatorNode>(
        BinaryOperator::greaterThan, make_node<ColumnRefNode>("age"), make_node<IntegerLiteralNode>("18"));
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with complex WHERE") {
    auto parseResult = parse("SELECT name FROM users WHERE age >= 18 AND active = 1");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{make_shared_node<ColumnRefNode>("name"), ""}};
    expected.fromClause = from_one("users");
    expected.whereClause = make_shared_node<BinaryOperatorNode>(
        BinaryOperator::logicalAnd,
        make_node<BinaryOperatorNode>(BinaryOperator::greaterOrEqual, make_node<ColumnRefNode>("age"), make_node<IntegerLiteralNode>("18")),
        make_node<BinaryOperatorNode>(BinaryOperator::equals, make_node<ColumnRefNode>("active"), make_node<IntegerLiteralNode>("1")));
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT DISTINCT") {
    auto parseResult = parse("SELECT DISTINCT name FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.distinct = true;
    expected.columns = {SelectColumn{make_shared_node<ColumnRefNode>("name"), ""}};
    expected.fromClause = from_one("users");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with column alias") {
    auto parseResult = parse("SELECT name AS user_name FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{make_shared_node<ColumnRefNode>("name"), "user_name"}};
    expected.fromClause = from_one("users");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with implicit column alias") {
    auto parseResult = parse("SELECT name user_name FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{make_shared_node<ColumnRefNode>("name"), "user_name"}};
    expected.fromClause = from_one("users");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT function with implicit alias") {
    auto parseResult = parse("SELECT instr(abilities, 'o') i FROM marvel");
    REQUIRE(parseResult);
    std::vector<AstNodePointer> instrArgs;
    instrArgs.push_back(make_node<ColumnRefNode>("abilities"));
    instrArgs.push_back(make_node<StringLiteralNode>("'o'"));
    auto instrCall =
        std::make_shared<FunctionCallNode>("instr", std::move(instrArgs), false, false, SourceLocation{});
    SelectNode expected({});
    expected.columns = {SelectColumn{std::move(instrCall), "i"}};
    expected.fromClause = from_one("marvel");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT expression") {
    auto parseResult = parse("SELECT id + 1 FROM users");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{
        make_shared_node<BinaryOperatorNode>(BinaryOperator::add, make_node<ColumnRefNode>("id"), make_node<IntegerLiteralNode>("1")), ""}};
    expected.fromClause = from_one("users");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT without FROM") {
    auto parseResult = parse("SELECT 1");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{make_shared_node<IntegerLiteralNode>("1"), ""}};
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

// --- ORDER BY ---

TEST_CASE("parser: SELECT with ORDER BY") {
    auto parseResult = parse("SELECT * FROM users ORDER BY name");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = from_one("users");
    expected.orderBy = {OrderByTerm{make_shared_node<ColumnRefNode>("name"), SortDirection::none}};
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with ORDER BY ASC") {
    auto parseResult = parse("SELECT * FROM users ORDER BY name ASC");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = from_one("users");
    expected.orderBy = {OrderByTerm{make_shared_node<ColumnRefNode>("name"), SortDirection::asc}};
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with ORDER BY DESC") {
    auto parseResult = parse("SELECT * FROM users ORDER BY age DESC");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = from_one("users");
    expected.orderBy = {OrderByTerm{make_shared_node<ColumnRefNode>("age"), SortDirection::desc}};
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with multiple ORDER BY") {
    auto parseResult = parse("SELECT * FROM users ORDER BY name ASC, age DESC");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = from_one("users");
    expected.orderBy = {
        OrderByTerm{make_shared_node<ColumnRefNode>("name"), SortDirection::asc},
        OrderByTerm{make_shared_node<ColumnRefNode>("age"), SortDirection::desc},
    };
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

// --- LIMIT / OFFSET ---

TEST_CASE("parser: SELECT with LIMIT") {
    auto parseResult = parse("SELECT * FROM users LIMIT 10");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = from_one("users");
    expected.limitValue = 10;
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with LIMIT OFFSET") {
    auto parseResult = parse("SELECT * FROM users LIMIT 10 OFFSET 20");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = from_one("users");
    expected.limitValue = 10;
    expected.offsetValue = 20;
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

// --- GROUP BY ---

TEST_CASE("parser: SELECT with GROUP BY") {
    auto parseResult = parse("SELECT name, count(*) FROM users GROUP BY name");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {
        SelectColumn{make_shared_node<ColumnRefNode>("name"), ""},
        SelectColumn{std::shared_ptr<AstNode>(make_func("count", false, true)), ""},
    };
    expected.fromClause = from_one("users");
    expected.groupBy = GroupByClause{{make_shared_node<ColumnRefNode>("name")}, nullptr};
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with GROUP BY HAVING") {
    auto parseResult = parse("SELECT name, count(*) FROM users GROUP BY name HAVING count(*) > 1");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {
        SelectColumn{make_shared_node<ColumnRefNode>("name"), ""},
        SelectColumn{std::shared_ptr<AstNode>(make_func("count", false, true)), ""},
    };
    expected.fromClause = from_one("users");
    expected.groupBy = GroupByClause{
        {make_shared_node<ColumnRefNode>("name")},
        make_shared_node<BinaryOperatorNode>(
            BinaryOperator::greaterThan,
            make_func("count", false, true),
            make_node<IntegerLiteralNode>("1"))};
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT with GROUP BY multiple columns") {
    auto parseResult = parse("SELECT department, role FROM users GROUP BY department, role");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {
        SelectColumn{make_shared_node<ColumnRefNode>("department"), ""},
        SelectColumn{make_shared_node<ColumnRefNode>("role"), ""},
    };
    expected.fromClause = from_one("users");
    expected.groupBy = GroupByClause{
        {make_shared_node<ColumnRefNode>("department"), make_shared_node<ColumnRefNode>("role")}, nullptr};
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT WINDOW clause") {
    auto parseResult =
        parse("SELECT row_number() OVER w FROM t WINDOW w AS (ORDER BY id)");
    REQUIRE(parseResult);
    SelectNode expected({});
    auto rowNumberCall = std::make_shared<FunctionCallNode>(
        "row_number", std::vector<AstNodePointer>{}, false, false, SourceLocation{});
    rowNumberCall->over = std::make_unique<OverClause>();
    rowNumberCall->over->namedWindow = "w";
    expected.columns = {SelectColumn{rowNumberCall, ""}};
    expected.fromClause = from_one("t");
    NamedWindowDefinition namedWindow;
    namedWindow.name = "w";
    namedWindow.definition = std::make_unique<OverClause>();
    namedWindow.definition->orderBy.push_back(
        OrderByTerm{make_shared_node<ColumnRefNode>("id"), SortDirection::none});
    expected.namedWindows.push_back(std::move(namedWindow));
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

// --- Full SELECT ---

TEST_CASE("parser: SELECT with all clauses") {
    auto parseResult = parse("SELECT name FROM users WHERE age > 18 GROUP BY name ORDER BY name ASC LIMIT 10 OFFSET 5");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{make_shared_node<ColumnRefNode>("name"), ""}};
    expected.fromClause = from_one("users");
    expected.whereClause = make_shared_node<BinaryOperatorNode>(
        BinaryOperator::greaterThan, make_node<ColumnRefNode>("age"), make_node<IntegerLiteralNode>("18"));
    expected.groupBy = GroupByClause{{make_shared_node<ColumnRefNode>("name")}, nullptr};
    expected.orderBy = {OrderByTerm{make_shared_node<ColumnRefNode>("name"), SortDirection::asc}};
    expected.limitValue = 10;
    expected.offsetValue = 5;
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

// --- ORDER BY COLLATE ---

TEST_CASE("parser: ORDER BY COLLATE") {
    auto parseResult = parse("SELECT * FROM t ORDER BY name COLLATE NOCASE");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = from_one("t");
    expected.orderBy = {OrderByTerm{make_shared_node<ColumnRefNode>("name"), SortDirection::none, "NOCASE"}};
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

// --- Window frame EXCLUDE ---

TEST_CASE("parser: EXCLUDE NO OTHERS in window frame") {
    auto parseResult = parse("SELECT sum(x) OVER (ROWS UNBOUNDED PRECEDING EXCLUDE NO OTHERS) FROM t");
    std::vector<AstNodePointer> args;
    args.push_back(make_node<ColumnRefNode>("x"));
    auto funcNode = std::make_shared<FunctionCallNode>("sum", std::move(args), false, false, SourceLocation{});
    funcNode->over = std::make_unique<OverClause>();
    funcNode->over->frame = std::make_unique<WindowFrameSpec>();
    funcNode->over->frame->unit = WindowFrameUnit::rows;
    funcNode->over->frame->start = WindowFrameBound{WindowFrameBoundKind::unboundedPreceding, nullptr};
    funcNode->over->frame->exclude = WindowFrameExcludeKind::none;
    SelectNode expected({});
    expected.columns = {SelectColumn{funcNode, ""}};
    expected.fromClause = from_one("t");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: VALUES standalone") {
    auto parseResult = parse("VALUES (1, 'a'), (2, 'b')");
    SelectNode expected({});
    expected.columns = {
        SelectColumn{make_shared_node<IntegerLiteralNode>("1"), ""},
        SelectColumn{make_shared_node<StringLiteralNode>("'a'"), ""},
    };
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

namespace {

    AstNodePointer select_literal_int(std::string_view v) {
        auto selectNode = std::make_unique<SelectNode>(SourceLocation{});
        selectNode->columns = {SelectColumn{make_shared_node<IntegerLiteralNode>(v), ""}};
        return selectNode;
    }

}  // namespace

TEST_CASE("parser: VALUES UNION ALL SELECT compound") {
    auto parseResult = parse("VALUES (1) UNION ALL SELECT 2;");
    REQUIRE(parseResult);
    std::vector<AstNodePointer> arms;
    arms.push_back(select_literal_int("1"));
    arms.push_back(select_literal_int("2"));
    CompoundSelectNode expected(std::move(arms), std::vector<CompoundSelectOperator>{CompoundSelectOperator::unionAll},
                                SourceLocation{});
    REQUIRE(require_node<CompoundSelectNode>(parseResult) == expected);
}

TEST_CASE("parser: SELECT UNION ALL VALUES compound") {
    auto parseResult = parse("SELECT 1 UNION ALL VALUES (2);");
    REQUIRE(parseResult);
    std::vector<AstNodePointer> arms;
    arms.push_back(select_literal_int("1"));
    arms.push_back(select_literal_int("2"));
    CompoundSelectNode expected(std::move(arms), std::vector<CompoundSelectOperator>{CompoundSelectOperator::unionAll},
                                SourceLocation{});
    REQUIRE(require_node<CompoundSelectNode>(parseResult) == expected);
}

TEST_CASE("parser: table-function in FROM") {
    auto parseResult = parse("SELECT * FROM generate_series(1, 10)");
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    FromTableClause tableFunc;
    tableFunc.tableName = "generate_series";
    tableFunc.tableFunctionArgs.push_back(make_shared_node<IntegerLiteralNode>("1"));
    tableFunc.tableFunctionArgs.push_back(make_shared_node<IntegerLiteralNode>("10"));
    expected.fromClause = {FromClauseItem{JoinKind::none, std::move(tableFunc), nullptr, {}}};
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: parenthesized join in FROM") {
    auto parseResult = parse("SELECT * FROM (t1 INNER JOIN t2 ON t1.id = t2.t1_id)");
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = {
        FromClauseItem{JoinKind::none,
            FromTableClause{std::nullopt, std::string("t1"), std::nullopt}, nullptr, {}},
        FromClauseItem{JoinKind::innerJoin,
            FromTableClause{std::nullopt, std::string("t2"), std::nullopt},
            make_shared_node<BinaryOperatorNode>(
                BinaryOperator::equals,
                make_node<QualifiedColumnRefNode>("t1", "id"),
                make_node<QualifiedColumnRefNode>("t2", "t1_id")),
            {}},
    };
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: parenthesized join combined with outer join") {
    auto parseResult = parse("SELECT * FROM t0 LEFT JOIN (t1 INNER JOIN t2 ON t1.id = t2.t1_id) ON t0.id = t1.id");
    SelectNode expected({});
    expected.columns = {SelectColumn{nullptr, ""}};
    expected.fromClause = {
        FromClauseItem{JoinKind::none,
            FromTableClause{std::nullopt, std::string("t0"), std::nullopt}, nullptr, {}},
        FromClauseItem{JoinKind::leftJoin,
            FromTableClause{std::nullopt, std::string("t1"), std::nullopt},
            make_shared_node<BinaryOperatorNode>(
                BinaryOperator::equals,
                make_node<QualifiedColumnRefNode>("t0", "id"),
                make_node<QualifiedColumnRefNode>("t1", "id")),
            {}},
        FromClauseItem{JoinKind::innerJoin,
            FromTableClause{std::nullopt, std::string("t2"), std::nullopt},
            make_shared_node<BinaryOperatorNode>(
                BinaryOperator::equals,
                make_node<QualifiedColumnRefNode>("t1", "id"),
                make_node<QualifiedColumnRefNode>("t2", "t1_id")),
            {}},
    };
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}
