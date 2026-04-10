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
        "constexpr auto cnt__x = colalias_a{};\n"
        "auto rows = storage.with_recursive(cte<cte_0>(\"x\").as(union_all(select(1 >>= cnt__x), "
        "select(c(column<cte_0>(cnt__x)) + 1, limit(1000000)))), column<cte_0>(cnt__x));");
}

TEST_CASE("codegen: WITH single-CTE SELECT does not emit synthetic struct Cnt in prefix") {
    REQUIRE(prefixFor("WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM cnt LIMIT 1000000) SELECT x "
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

    const std::string codeIndexed = generateWithPolicySuppressWithCteDp(sql, polIndexed).code;
    const std::string codeLegacy = generateWithPolicySuppressWithCteDp(sql, polLegacy).code;
    const std::string codeCpp20 = generateWithPolicySuppressWithCteDp(sql, polCpp20).code;

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
                         "using typedef from SQL CTE name + colalias_a… + column<T>(var)"},
             Alternative{"cpp20_monikers", codeCpp20,
                         "constexpr orm_cte_moniker / orm_table_alias + operator->* (C++20 sqlite_orm)"}}}},
        {"WITH: requires SQLite ≥ 3.8.3, sqlite_orm built with SQLITE_ORM_WITH_CTE, and `using namespace "
         "sqlite_orm::literals` scope for `_ctealias`"},
        {},
        {}};

    REQUIRE(generateFull(sql) == expected);
}

TEST_CASE("codegen: with_cte_style legacy_colalias") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["with_cte_style"] = "legacy_colalias";
    CodeGenResult codeGenResult = generateWithPolicy(
        "WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM cnt LIMIT 999) SELECT x FROM cnt;", pol);
    const std::string expected =
        "using namespace sqlite_orm::literals;\n"
        "using cnt = decltype(1_ctealias);\n"
        "constexpr auto cnt_x = colalias_a{};\n"
        "auto rows = storage.with_recursive(cte<cnt>().as(union_all(select(1), select(c(column<cnt>(cnt_x)) + 1, "
        "limit(999)))), column<cnt>(cnt_x));";
    REQUIRE(codeGenResult.code == expected);
}

TEST_CASE("codegen: with_cte_style cpp20_monikers") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";
    CodeGenResult codeGenResult = generateWithPolicy(
        "WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM cnt LIMIT 999) SELECT x FROM cnt;", pol);
    const std::string expected =
        "using namespace sqlite_orm::literals;\n"
        "constexpr orm_cte_moniker auto cnt_cte = \"cnt\"_cte;\n"
        "constexpr orm_column_alias auto cnt__x = \"x\"_col;\n"
        "auto rows = storage.with_recursive(cnt_cte().as(union_all(select(1), select(cnt_cte->*cnt__x + 1, "
        "limit(999)))), cnt_cte->*cnt__x);";
    REQUIRE(codeGenResult.code == expected);
}

TEST_CASE("codegen: WITH RECURSIVE comma-join CTE skips cross_join and uses alias + member pointers") {
    auto result = generate(
        "WITH RECURSIVE chain AS("
        "SELECT * FROM org WHERE name = 'Fred' "
        "UNION ALL "
        "SELECT parent.* FROM org parent, chain WHERE parent.name = chain.boss"
        ") SELECT name FROM chain;");
    REQUIRE(result ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "auto rows = storage.with_recursive("
            "cte<cte_0>().as(union_all("
            "select(asterisk<Org>(), where(c(&Org::name) == \"Fred\")), "
            "select(asterisk<alias_a<Org>>(), where(alias_column<alias_a<Org>>(&Org::name) == column<cte_0>(&Org::boss))))), "
            "column<cte_0>(&Org::name));");
}

TEST_CASE("codegen: WITH CTE column refs use member pointers when base struct known") {
    auto result = generate(
        "WITH c AS (SELECT name, id FROM users WHERE id > 0) SELECT c.name FROM c;");
    REQUIRE(result ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "auto rows = storage.with("
            "cte<cte_0>().as(select(columns(&Users::name, &Users::id), where(c(&Users::id) > 0))), "
            "column<cte_0>(&Users::name));");
}

TEST_CASE("codegen: WITH RECURSIVE cpp20_monikers with table alias and member pointers") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";
    CodeGenResult codeGenResult = generateWithPolicy(
        "WITH RECURSIVE chain AS("
        "SELECT * FROM org WHERE name = 'Fred' "
        "UNION ALL "
        "SELECT parent.* FROM org parent, chain WHERE parent.name = chain.boss"
        ") SELECT name FROM chain;",
        pol);
    REQUIRE(codeGenResult.code ==
            "using namespace sqlite_orm::literals;\n"
            "constexpr orm_cte_moniker auto chain_cte = \"chain\"_cte;\n"
            "constexpr orm_table_alias auto parent = \"parent\"_alias.for_<Org>();\n"
            "auto rows = storage.with_recursive("
            "chain_cte().as(union_all("
            "select(asterisk<Org>(), where(c(&Org::name) == \"Fred\")), "
            "select(asterisk<parent>(), where(parent->*&Org::name == chain_cte->*&Org::boss)))), "
            "chain_cte->*&Org::name);");
}

TEST_CASE("codegen: WITH no column list still offers cpp20_monikers as alternative") {
    constexpr std::string_view sql =
        "WITH RECURSIVE chain AS("
        "SELECT * FROM org WHERE name = 'Fred' "
        "UNION ALL "
        "SELECT parent.* FROM org parent, chain WHERE parent.name = chain.boss"
        ") SELECT name FROM chain;";
    auto result = generateFull(sql);
    bool hasCpp20 = false;
    for(const auto& dp : result.decisionPoints) {
        if(dp.category == "with_cte_style") {
            for(const auto& alt : dp.alternatives) {
                if(alt.value == "cpp20_monikers") {
                    hasCpp20 = true;
                }
            }
        }
    }
    REQUIRE(hasCpp20);
}

TEST_CASE("codegen: WITH single-quoted table names in FROM and column refs") {
    auto result = generate(
        "WITH cte_1(\"n\") AS(SELECT 'Alice' UNION SELECT 'org'.\"name\" FROM 'cte_1', 'org' "
        "WHERE('org'.\"boss\" = 'cte_1'.\"n\")) "
        "SELECT AVG('org'.\"height\") FROM 'org' "
        "WHERE(\"name\" IN(SELECT 'cte_1'.\"n\" FROM 'cte_1'))");
    REQUIRE(result.find("storage.with") != std::string::npos);
    REQUIRE(result.find("column<cte_0>(cte_1__n)") != std::string::npos);
}

TEST_CASE("codegen: WITH RECURSIVE multi-CTE with JOIN USING between CTEs") {
    REQUIRE(generate("WITH RECURSIVE "
                     "parent_of(name, parent) AS "
                     "(SELECT name, mom FROM family UNION SELECT name, dad FROM family), "
                     "ancestor_of_alice(name) AS "
                     "(SELECT parent FROM parent_of WHERE name = 'Alice' "
                     "UNION ALL "
                     "SELECT parent FROM parent_of JOIN ancestor_of_alice USING(name)) "
                     "SELECT family.name FROM ancestor_of_alice, family "
                     "WHERE ancestor_of_alice.name = family.name "
                     "AND died IS NULL "
                     "ORDER BY born;") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "using cte_1 = decltype(2_ctealias);\n"
            "constexpr auto parent_of__name = colalias_a{};\n"
            "constexpr auto parent_of__parent = colalias_b{};\n"
            "constexpr auto ancestor_of_alice__name = colalias_c{};\n"
            "auto rows = storage.with_recursive("
            "std::make_tuple("
            "cte<cte_0>(\"name\", \"parent\").as("
            "union_("
            "select(columns(&Family::name >>= parent_of__name, &Family::mom >>= parent_of__parent)), "
            "select(columns(&Family::name, &Family::dad)))), "
            "cte<cte_1>(\"name\").as("
            "union_all("
            "select(column<cte_0>(parent_of__parent) >>= ancestor_of_alice__name, "
            "where(c(column<cte_0>(parent_of__name)) == \"Alice\")), "
            "select(column<cte_0>(parent_of__parent), "
            "join<cte_1>(using_(column<cte_0>(parent_of__name))))))), "
            "&Family::name, "
            "where(c(column<cte_1>(ancestor_of_alice__name)) == &Family::name and is_null(&Family::died)), "
            "order_by(&Family::born));");
}

TEST_CASE("codegen: CTE explicit column resolved as colalias, not string literal") {
    REQUIRE(generate("WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM cnt LIMIT 1000000) "
                     "SELECT x FROM cnt;") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "constexpr auto cnt__x = colalias_a{};\n"
            "auto rows = storage.with_recursive("
            "cte<cte_0>(\"x\").as("
            "union_all(select(1 >>= cnt__x), select(c(column<cte_0>(cnt__x)) + 1, limit(1000000)))), "
            "column<cte_0>(cnt__x));");
}

TEST_CASE("codegen: outer SELECT with CTE+real table resolves bare columns to real table") {
    REQUIRE(generate("WITH c(val) AS (SELECT 1) SELECT name FROM c, users WHERE c.val = id;") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "constexpr auto c__val = colalias_a{};\n"
            "auto rows = storage.with("
            "cte<cte_0>(\"val\").as(select(1 >>= c__val)), "
            "&Users::name, "
            "where(c(column<cte_0>(c__val)) == &Users::id));");
}

TEST_CASE("codegen: SELECT from single-quoted table is same as double-quoted") {
    auto resultSingleQuote = generate("SELECT \"name\" FROM 'users'");
    auto resultDoubleQuote = generate("SELECT \"name\" FROM \"users\"");
    REQUIRE(resultSingleQuote == resultDoubleQuote);
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
