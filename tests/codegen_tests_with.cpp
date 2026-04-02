#include "codegen_tests_common.hpp"

TEST_CASE("codegen: WITH … INSERT uses storage.with") {
    REQUIRE(generate("WITH c AS (SELECT 1) INSERT INTO users (id) VALUES (1);") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "storage.with(cte<cte_0>().as(select(1)), "
            "insert(into<Users>(), columns(&Users::id), values(std::make_tuple(1))));");
}

TEST_CASE("codegen: WITH … SELECT single CTE FROM uses column<cte_0> for bare column") {
    REQUIRE(generate("WITH c AS (SELECT 1 AS a) SELECT a FROM c;") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "auto rows = storage.with(cte<cte_0>().as(select(1)), column<cte_0>(\"a\"));");
}

TEST_CASE("codegen: WITH RECURSIVE … UNION ALL arm with LIMIT still uses with_recursive") {
    REQUIRE(
        generate("WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM cnt LIMIT 1000000) SELECT x FROM "
                 "cnt;") ==
        "using namespace sqlite_orm::literals;\n"
        "using cte_0 = decltype(1_ctealias);\n"
        "auto rows = storage.with_recursive(cte<cte_0>(\"x\").as(union_all(select(1), "
        "select(c(column<cte_0>(\"x\")) + 1, limit(1000000)))), column<cte_0>(\"x\"));");
}

TEST_CASE("codegen: aggregate FILTER (WHERE) without OVER") {
    REQUIRE(generate("SELECT count(*) FILTER (WHERE id > 0) FROM users;") ==
            "auto rows = storage.select(count<Users>().filter(where(c(&Users::id) > 0)));");
}

TEST_CASE("codegen: WINDOW clause maps to window(...) on select") {
    REQUIRE(generate("SELECT row_number() OVER w FROM users WINDOW w AS (ORDER BY id);") ==
            "auto rows = storage.select(row_number().over(window_ref(\"w\")), "
            "window(\"w\", order_by(&Users::id)));");
}
