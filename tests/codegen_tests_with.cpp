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

TEST_CASE("codegen: WITH single-CTE SELECT does not emit synthetic struct Cnt in prefix") {
    REQUIRE(prefix_for("WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM cnt LIMIT 1000000) SELECT x "
                       "FROM cnt;") == "");
}

TEST_CASE("codegen: WITH single CTE exposes with_cte_style decision point") {
    constexpr std::string_view sql = "WITH RECURSIVE cnt(x) AS (SELECT 1) SELECT x FROM cnt;";

    CodeGenPolicy polIndexed;
    polIndexed.chosenAlternativeValueByCategory["with_cte_style"] = "indexed_typedef";
    CodeGenPolicy polLegacy;
    polLegacy.chosenAlternativeValueByCategory["with_cte_style"] = "legacy_colalias";
    CodeGenPolicy polCpp20;
    polCpp20.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";

    const std::string codeIndexed = generate_with_policy_suppress_with_cte_dp(sql, polIndexed).code;
    const std::string codeLegacy = generate_with_policy_suppress_with_cte_dp(sql, polLegacy).code;
    const std::string codeCpp20 = generate_with_policy_suppress_with_cte_dp(sql, polCpp20).code;

    const CodeGenResult expected{
        codeIndexed,
        {DecisionPoint{
            1,
            "with_cte_style",
            "indexed_typedef",
            codeIndexed,
            {Alternative{"indexed_typedef", codeIndexed,
                         "using cte_N + column<cte_N>(\"col\") (default sqlite2orm style)"},
             Alternative{"legacy_colalias", codeLegacy,
                         "using typedef from SQL CTE name + colalias_i… + column<T>(var)"},
             Alternative{"cpp20_monikers", codeCpp20,
                         "constexpr orm_cte_moniker / orm_column_alias + operator->* (C++20 sqlite_orm)"}}}},
        {"WITH: requires SQLite ≥ 3.8.3, sqlite_orm built with SQLITE_ORM_WITH_CTE, and `using namespace "
         "sqlite_orm::literals` scope for `_ctealias`"},
        {},
        {}};

    REQUIRE(generate_full(sql) == expected);
}

TEST_CASE("codegen: with_cte_style legacy_colalias") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["with_cte_style"] = "legacy_colalias";
    CodeGenResult codeGenResult = generate_with_policy(
        "WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM cnt LIMIT 999) SELECT x FROM cnt;", pol);
    const std::string expected =
        "using namespace sqlite_orm::literals;\n"
        "using cnt = decltype(1_ctealias);\n"
        "constexpr auto cnt_x = colalias_i{};\n"
        "auto rows = storage.with_recursive(cte<cnt>().as(union_all(select(1), select(c(column<cnt>(cnt_x)) + 1, "
        "limit(999)))), column<cnt>(cnt_x));";
    REQUIRE(codeGenResult.code == expected);
}

TEST_CASE("codegen: with_cte_style cpp20_monikers") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";
    CodeGenResult codeGenResult = generate_with_policy(
        "WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM cnt LIMIT 999) SELECT x FROM cnt;", pol);
    const std::string expected =
        "using namespace sqlite_orm::literals;\n"
        "constexpr orm_cte_moniker auto cnt_cte = \"cnt\"_cte;\n"
        "constexpr orm_column_alias auto cnt__x = \"x\"_col;\n"
        "auto rows = storage.with_recursive(cnt_cte().as(union_all(select(1), select(c(cnt_cte->*cnt__x) + 1, "
        "limit(999)))), cnt_cte->*cnt__x);";
    REQUIRE(codeGenResult.code == expected);
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
