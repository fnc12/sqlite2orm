#include "parser_tests_common.hpp"
using namespace sqlite2orm::parser_test_helpers;

namespace {

    AstNodePointer selectIntegerOne() {
        auto s = std::make_unique<SelectNode>(SourceLocation{});
        s->columns = {SelectColumn{make_shared_node<IntegerLiteralNode>("1"), ""}};
        return s;
    }

    AstNodePointer selectIntegerTwo() {
        auto s = std::make_unique<SelectNode>(SourceLocation{});
        s->columns = {SelectColumn{make_shared_node<IntegerLiteralNode>("2"), ""}};
        return s;
    }

    AstNodePointer insertIntoTValues1() {
        auto ins = std::make_unique<InsertNode>(SourceLocation{});
        ins->tableName = "t";
        ins->columnNames = {"x"};
        ins->dataKind = InsertDataKind::values;
        std::vector<AstNodePointer> row;
        row.push_back(make_node<IntegerLiteralNode>("1"));
        ins->valueRows.push_back(std::move(row));
        return ins;
    }

    AstNodePointer replaceIntoTValues1() {
        auto ins = std::make_unique<InsertNode>(SourceLocation{});
        ins->replaceInto = true;
        ins->tableName = "t";
        ins->columnNames = {"x"};
        ins->dataKind = InsertDataKind::values;
        std::vector<AstNodePointer> row;
        row.push_back(make_node<IntegerLiteralNode>("1"));
        ins->valueRows.push_back(std::move(row));
        return ins;
    }

    AstNodePointer updateUSetX1() {
        auto u = std::make_unique<UpdateNode>(SourceLocation{});
        u->tableName = "u";
        u->assignments.push_back(UpdateAssignment{"x", make_node<IntegerLiteralNode>("1")});
        return u;
    }

    AstNodePointer deleteFromDWhere1() {
        auto d = std::make_unique<DeleteNode>(SourceLocation{});
        d->tableName = "d";
        d->whereClause = make_node<IntegerLiteralNode>("1");
        return d;
    }

}  // namespace

TEST_CASE("parser: WITH … SELECT (single CTE)") {
    auto parseResult = parse("WITH c AS (SELECT 1) SELECT 1;");
    REQUIRE(parseResult);
    WithClause withClause;
    CommonTableExpression cte;
    cte.cteName = "c";
    cte.query = selectIntegerOne();
    withClause.tables.push_back(std::move(cte));
    WithQueryNode expected(std::move(withClause), selectIntegerOne(), SourceLocation{});
    REQUIRE(require_node<WithQueryNode>(parseResult) == expected);
}

TEST_CASE("parser: WITH RECURSIVE … SELECT") {
    auto parseResult = parse("WITH RECURSIVE r AS (SELECT 1) SELECT 1;");
    REQUIRE(parseResult);
    WithClause withClause;
    withClause.recursive = true;
    CommonTableExpression cte;
    cte.cteName = "r";
    cte.query = selectIntegerOne();
    withClause.tables.push_back(std::move(cte));
    WithQueryNode expected(std::move(withClause), selectIntegerOne(), SourceLocation{});
    REQUIRE(require_node<WithQueryNode>(parseResult) == expected);
}

TEST_CASE("parser: WITH column list on CTE") {
    auto parseResult = parse("WITH t (x) AS (SELECT 1) SELECT 1;");
    REQUIRE(parseResult);
    WithClause withClause;
    CommonTableExpression cte;
    cte.cteName = "t";
    cte.columnNames = {"x"};
    cte.query = selectIntegerOne();
    withClause.tables.push_back(std::move(cte));
    WithQueryNode expected(std::move(withClause), selectIntegerOne(), SourceLocation{});
    REQUIRE(require_node<WithQueryNode>(parseResult) == expected);
}

TEST_CASE("parser: multiple CTEs") {
    auto parseResult = parse("WITH a AS (SELECT 1), b AS (SELECT 2) SELECT 2;");
    REQUIRE(parseResult);
    WithClause withClause;
    CommonTableExpression c1;
    c1.cteName = "a";
    c1.query = selectIntegerOne();
    withClause.tables.push_back(std::move(c1));
    CommonTableExpression c2;
    c2.cteName = "b";
    c2.query = selectIntegerTwo();
    withClause.tables.push_back(std::move(c2));
    WithQueryNode expected(std::move(withClause), selectIntegerTwo(), SourceLocation{});
    REQUIRE(require_node<WithQueryNode>(parseResult) == expected);
}

TEST_CASE("parser: WITH … INSERT") {
    auto parseResult = parse("WITH c AS (SELECT 1) INSERT INTO t (x) VALUES (1);");
    REQUIRE(parseResult);
    WithClause withClause;
    CommonTableExpression cte;
    cte.cteName = "c";
    cte.query = selectIntegerOne();
    withClause.tables.push_back(std::move(cte));
    WithQueryNode expected(std::move(withClause), insertIntoTValues1(), SourceLocation{});
    REQUIRE(require_node<WithQueryNode>(parseResult) == expected);
}

TEST_CASE("parser: WITH … REPLACE INTO") {
    auto parseResult = parse("WITH c AS (SELECT 1) REPLACE INTO t (x) VALUES (1);");
    REQUIRE(parseResult);
    WithClause withClause;
    CommonTableExpression cte;
    cte.cteName = "c";
    cte.query = selectIntegerOne();
    withClause.tables.push_back(std::move(cte));
    WithQueryNode expected(std::move(withClause), replaceIntoTValues1(), SourceLocation{});
    REQUIRE(require_node<WithQueryNode>(parseResult) == expected);
}

TEST_CASE("parser: WITH … UPDATE") {
    auto parseResult = parse("WITH c AS (SELECT 1) UPDATE u SET x = 1;");
    REQUIRE(parseResult);
    WithClause withClause;
    CommonTableExpression cte;
    cte.cteName = "c";
    cte.query = selectIntegerOne();
    withClause.tables.push_back(std::move(cte));
    WithQueryNode expected(std::move(withClause), updateUSetX1(), SourceLocation{});
    REQUIRE(require_node<WithQueryNode>(parseResult) == expected);
}

TEST_CASE("parser: WITH … DELETE") {
    auto parseResult = parse("WITH c AS (SELECT 1) DELETE FROM d WHERE 1;");
    REQUIRE(parseResult);
    WithClause withClause;
    CommonTableExpression cte;
    cte.cteName = "c";
    cte.query = selectIntegerOne();
    withClause.tables.push_back(std::move(cte));
    WithQueryNode expected(std::move(withClause), deleteFromDWhere1(), SourceLocation{});
    REQUIRE(require_node<WithQueryNode>(parseResult) == expected);
}

TEST_CASE("parser: WITH CTE AS MATERIALIZED / NOT MATERIALIZED") {
    {
        auto parseResult = parse("WITH c AS MATERIALIZED (SELECT 1) SELECT 1;");
        REQUIRE(parseResult);
        WithClause withClause;
        CommonTableExpression cte;
        cte.cteName = "c";
        cte.materialization = CteMaterialization::materialized;
        cte.query = selectIntegerOne();
        withClause.tables.push_back(std::move(cte));
        WithQueryNode expected(std::move(withClause), selectIntegerOne(), SourceLocation{});
        REQUIRE(require_node<WithQueryNode>(parseResult) == expected);
    }
    {
        auto parseResult = parse("WITH c AS NOT MATERIALIZED (SELECT 1) SELECT 1;");
        REQUIRE(parseResult);
        WithClause withClause;
        CommonTableExpression cte;
        cte.cteName = "c";
        cte.materialization = CteMaterialization::notMaterialized;
        cte.query = selectIntegerOne();
        withClause.tables.push_back(std::move(cte));
        WithQueryNode expected(std::move(withClause), selectIntegerOne(), SourceLocation{});
        REQUIRE(require_node<WithQueryNode>(parseResult) == expected);
    }
}
