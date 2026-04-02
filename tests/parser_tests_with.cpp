#include "parser_tests_common.hpp"
using namespace sqlite2orm::parser_test_helpers;

namespace {

    AstNodePointer selectIntegerOne() {
        auto s = std::make_unique<SelectNode>(SourceLocation{});
        s->columns = {SelectColumn{makeSharedNode<IntegerLiteralNode>("1"), ""}};
        return s;
    }

    AstNodePointer selectIntegerTwo() {
        auto s = std::make_unique<SelectNode>(SourceLocation{});
        s->columns = {SelectColumn{makeSharedNode<IntegerLiteralNode>("2"), ""}};
        return s;
    }

    AstNodePointer insertIntoTValues1() {
        auto ins = std::make_unique<InsertNode>(SourceLocation{});
        ins->tableName = "t";
        ins->columnNames = {"x"};
        ins->dataKind = InsertDataKind::values;
        std::vector<AstNodePointer> row;
        row.push_back(makeNode<IntegerLiteralNode>("1"));
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
        row.push_back(makeNode<IntegerLiteralNode>("1"));
        ins->valueRows.push_back(std::move(row));
        return ins;
    }

    AstNodePointer updateUSetX1() {
        auto u = std::make_unique<UpdateNode>(SourceLocation{});
        u->tableName = "u";
        u->assignments.push_back(UpdateAssignment{"x", makeNode<IntegerLiteralNode>("1")});
        return u;
    }

    AstNodePointer deleteFromDWhere1() {
        auto d = std::make_unique<DeleteNode>(SourceLocation{});
        d->tableName = "d";
        d->whereClause = makeNode<IntegerLiteralNode>("1");
        return d;
    }

    /** `VALUES(1) UNION ALL SELECT x + 1 FROM cnt WHERE x < 3` — same shape as parsed recursive CTE body. */
    AstNodePointer cntRecursiveCteCompoundBody() {
        std::vector<AstNodePointer> arms;
        arms.push_back(selectIntegerOne());
        auto second = std::make_unique<SelectNode>(SourceLocation{});
        second->columns = {SelectColumn{
            makeSharedNode<BinaryOperatorNode>(BinaryOperator::add, makeNode<ColumnRefNode>("x"),
                                                 makeNode<IntegerLiteralNode>("1")),
            ""}};
        second->fromClause = fromOne("cnt");
        second->whereClause = makeSharedNode<BinaryOperatorNode>(
            BinaryOperator::lessThan, makeNode<ColumnRefNode>("x"), makeNode<IntegerLiteralNode>("3"));
        arms.push_back(std::move(second));
        return std::make_unique<CompoundSelectNode>(
            std::move(arms), std::vector<CompoundSelectOperator>{CompoundSelectOperator::unionAll}, SourceLocation{});
    }

    AstNodePointer selectXFromCnt() {
        auto s = std::make_unique<SelectNode>(SourceLocation{});
        s->columns = {SelectColumn{makeSharedNode<ColumnRefNode>("x"), ""}};
        s->fromClause = fromOne("cnt");
        return s;
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
    REQUIRE(requireNode<WithQueryNode>(parseResult) == expected);
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
    REQUIRE(requireNode<WithQueryNode>(parseResult) == expected);
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
    REQUIRE(requireNode<WithQueryNode>(parseResult) == expected);
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
    REQUIRE(requireNode<WithQueryNode>(parseResult) == expected);
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
    REQUIRE(requireNode<WithQueryNode>(parseResult) == expected);
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
    REQUIRE(requireNode<WithQueryNode>(parseResult) == expected);
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
    REQUIRE(requireNode<WithQueryNode>(parseResult) == expected);
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
    REQUIRE(requireNode<WithQueryNode>(parseResult) == expected);
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
        REQUIRE(requireNode<WithQueryNode>(parseResult) == expected);
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
        REQUIRE(requireNode<WithQueryNode>(parseResult) == expected);
    }
}

TEST_CASE("parser: WITH … CTE body allows VALUES") {
    auto parseResult = parse("WITH c AS (VALUES (1)) SELECT 1;");
    REQUIRE(parseResult);
    WithClause withClause;
    CommonTableExpression cte;
    cte.cteName = "c";
    cte.query = selectIntegerOne();
    withClause.tables.push_back(std::move(cte));
    WithQueryNode expected(std::move(withClause), selectIntegerOne(), SourceLocation{});
    REQUIRE(requireNode<WithQueryNode>(parseResult) == expected);
}

TEST_CASE("parser: WITH RECURSIVE CTE compound starting with VALUES") {
    const char* sql =
        "WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x + 1 FROM cnt WHERE x < 3) SELECT x FROM cnt;";
    auto parseResult = parse(sql);
    REQUIRE(parseResult);
    WithClause withClause;
    withClause.recursive = true;
    CommonTableExpression cte;
    cte.cteName = "cnt";
    cte.columnNames = {"x"};
    cte.query = cntRecursiveCteCompoundBody();
    withClause.tables.push_back(std::move(cte));
    WithQueryNode expected(std::move(withClause), selectXFromCnt(), SourceLocation{});
    REQUIRE(requireNode<WithQueryNode>(parseResult) == expected);
}
