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
            "auto rows = storage.with(cte<cte_0>().as(select(1)), select(column<cte_0>(\"a\")));");
}

TEST_CASE("codegen: WITH RECURSIVE … UNION ALL arm with LIMIT still uses with_recursive") {
    REQUIRE(generate("WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM cnt LIMIT 1000000) SELECT x FROM "
                     "cnt;") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "constexpr auto cnt__x = colalias_a{};\n"
            "auto rows = storage.with_recursive(cte<cte_0>(\"x\").as(union_all(select(1 >>= cnt__x), "
            "select(column<cte_0>(cnt__x) + 1, limit(1000000)))), select(column<cte_0>(cnt__x)));");
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
            // options lists every applicable style (the chosen "indexed_typedef" included).
            {Option{"indexed_typedef", codeIndexed, "using cte_N + column<cte_N>(\"col\") (default sqlite2orm style)"},
             Option{"legacy_colalias", codeLegacy, "using typedef from SQL CTE name + colalias_a… + column<T>(var)"},
             Option{"cpp20_monikers",
                    codeCpp20,
                    "constexpr orm_cte_moniker / orm_table_alias + operator->* (C++20 sqlite_orm)",
                    false,
                    {},
                    20}}}},
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
        "WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM cnt LIMIT 999) SELECT x FROM cnt;",
        pol);
    const std::string expected = "using namespace sqlite_orm::literals;\n"
                                 "using cnt = decltype(1_ctealias);\n"
                                 "constexpr auto cnt_x = colalias_a{};\n"
                                 "auto rows = storage.with_recursive(cte<cnt>(\"x\").as(union_all(select(1 >>= cnt_x), "
                                 "select(column<cnt>(cnt_x) + 1, limit(999)))), select(column<cnt>(cnt_x)));";
    REQUIRE(codeGenResult.code == expected);
}

TEST_CASE("codegen: with_cte_style cpp20_monikers") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";
    CodeGenResult codeGenResult = generateWithPolicy(
        "WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM cnt LIMIT 999) SELECT x FROM cnt;",
        pol);
    const std::string expected = "using namespace sqlite_orm::literals;\n"
                                 "constexpr orm_cte_moniker auto cnt_cte = \"cnt\"_cte;\n"
                                 "constexpr orm_column_alias auto cnt__x = \"x\"_col;\n"
                                 "auto rows = storage.with_recursive(cnt_cte(cnt__x).as(union_all(select(1), "
                                 "select(cnt_cte->*cnt__x + 1, limit(999)))), select(cnt_cte->*cnt__x));";
    REQUIRE(codeGenResult.code == expected);
}

TEST_CASE("codegen: WITH RECURSIVE comma-join CTE skips cross_join and uses alias + member pointers") {
    auto result = generate("WITH RECURSIVE chain AS("
                           "SELECT * FROM org WHERE name = 'Fred' "
                           "UNION ALL "
                           "SELECT parent.* FROM org parent, chain WHERE parent.name = chain.boss"
                           ") SELECT name FROM chain;");
    REQUIRE(result == "using namespace sqlite_orm::literals;\n"
                      "using cte_0 = decltype(1_ctealias);\n"
                      "auto rows = storage.with_recursive("
                      "cte<cte_0>().as(union_all("
                      "select(asterisk<Org>(), where(c(&Org::name) == \"Fred\")), "
                      "select(asterisk<alias_a<Org>>(), where(alias_column<alias_a<Org>>(&Org::name) == "
                      "column<cte_0>(&Org::boss))))), "
                      "select(column<cte_0>(&Org::name)));");
}

TEST_CASE("codegen: WITH CTE column refs use member pointers when base struct known") {
    auto result = generate("WITH c AS (SELECT name, id FROM users WHERE id > 0) SELECT c.name FROM c;");
    REQUIRE(result == "using namespace sqlite_orm::literals;\n"
                      "using cte_0 = decltype(1_ctealias);\n"
                      "auto rows = storage.with("
                      "cte<cte_0>().as(select(columns(&Users::name, &Users::id), where(c(&Users::id) > 0))), "
                      "select(column<cte_0>(&Users::name)));");
}

TEST_CASE("codegen: WITH RECURSIVE cpp20_monikers with table alias and member pointers") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";
    CodeGenResult codeGenResult =
        generateWithPolicy("WITH RECURSIVE chain AS("
                           "SELECT * FROM org WHERE name = 'Fred' "
                           "UNION ALL "
                           "SELECT parent.* FROM org parent, chain WHERE parent.name = chain.boss"
                           ") SELECT name FROM chain;",
                           pol);
    REQUIRE(codeGenResult.code == "using namespace sqlite_orm::literals;\n"
                                  "constexpr orm_cte_moniker auto chain_cte = \"chain\"_cte;\n"
                                  "constexpr orm_table_alias auto parent = \"parent\"_alias.for_<Org>();\n"
                                  "auto rows = storage.with_recursive("
                                  "chain_cte().as(union_all("
                                  "select(asterisk<Org>(), where(c(&Org::name) == \"Fred\")), "
                                  "select(asterisk<parent>(), where(parent->*&Org::name == chain_cte->*&Org::boss)))), "
                                  "select(chain_cte->*&Org::name));");
}

TEST_CASE("codegen: WITH no column list still offers cpp20_monikers as alternative") {
    constexpr std::string_view sql = "WITH RECURSIVE chain AS("
                                     "SELECT * FROM org WHERE name = 'Fred' "
                                     "UNION ALL "
                                     "SELECT parent.* FROM org parent, chain WHERE parent.name = chain.boss"
                                     ") SELECT name FROM chain;";
    auto result = generateFull(sql);
    bool hasCpp20 = false;
    for (const auto& dp: result.decisionPoints) {
        if (dp.category == "with_cte_style") {
            for (const auto& alt: dp.options) {
                if (alt.value == "cpp20_monikers") {
                    hasCpp20 = true;
                }
            }
        }
    }
    REQUIRE(hasCpp20);
}

TEST_CASE("codegen: WITH single-quoted table names in FROM and column refs") {
    auto result = generate("WITH cte_1(\"n\") AS(SELECT 'Alice' UNION SELECT 'org'.\"name\" FROM 'cte_1', 'org' "
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
            "where(column<cte_0>(parent_of__name) == \"Alice\")), "
            "select(column<cte_0>(parent_of__parent), "
            "join<cte_1>(using_(column<cte_0>(parent_of__name))))))), "
            "select(&Family::name, "
            "where(column<cte_1>(ancestor_of_alice__name) == &Family::name and is_null(&Family::died)), "
            "order_by(&Family::born)));");
}

TEST_CASE("codegen: CTE explicit column resolved as colalias, not string literal") {
    REQUIRE(generate("WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM cnt LIMIT 1000000) "
                     "SELECT x FROM cnt;") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "constexpr auto cnt__x = colalias_a{};\n"
            "auto rows = storage.with_recursive("
            "cte<cte_0>(\"x\").as("
            "union_all(select(1 >>= cnt__x), select(column<cte_0>(cnt__x) + 1, limit(1000000)))), "
            "select(column<cte_0>(cnt__x)));");
}

TEST_CASE("codegen: outer SELECT with CTE+real table resolves bare columns to real table") {
    REQUIRE(generate("WITH c(val) AS (SELECT 1) SELECT name FROM c, users WHERE c.val = id;") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "constexpr auto c__val = colalias_a{};\n"
            "auto rows = storage.with("
            "cte<cte_0>(\"val\").as(select(1 >>= c__val)), "
            "select(&Users::name, "
            "where(column<cte_0>(c__val) == &Users::id)));");
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

// sqlite_orm puts `filter()` on the aggregate function calls and on `count_asterisk_t` only, so a
// FILTER over a window function names a call the library does not declare. The expressibility gate
// refuses it: nothing is generated, and the warning underlines the call. SQLite refuses the same
// thing at prepare — `FILTER clause may only be used with aggregate window functions` — but stores
// a trigger or a view that holds one (checked against sqlite3 3.51.0), which is how such a call
// reaches codegen at all.
TEST_CASE("codegen: a FILTER over a window function is not generated") {
    const auto result = generateFull("SELECT row_number() FILTER (WHERE id > 0) OVER () FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"row_number() has no filter() in sqlite_orm: only the aggregate function calls and count(*) take a "
                 "FILTER, so there is no form to generate the call as. SQLite refuses the same call — FILTER clause "
                 "may only be used with aggregate window functions — but stores a trigger or a view holding it",
                 SourceLocation{1, 8},
                 42},
                {kStatementNotGenerated}});
}

// The registry records one overload per argument count sqlite_orm declares for every window
// function and for a MATCH in its function spelling — none for `row_number` and the other ranking
// functions, one for `ntile`, `first_value` and `last_value`, one to three for `lag` and `lead`,
// two for `nth_value` and `match` — so a call written with any other count matches no overload and
// the gate refuses it. SQLite refuses the same call at prepare — `wrong number of arguments to
// function` — but stores a trigger or a view that holds one (checked against sqlite3 3.51.0), so
// it reaches codegen from `--db` all the same, and what the consumer gets is the underlined
// warning rather than a header that cannot be built.
TEST_CASE("codegen: a window function taking no argument called with one is not generated") {
    const auto result = generateFull("SELECT row_number(id) OVER () FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(
        result.warnings ==
        std::vector<CodegenWarning>{
            {"row_number() takes no argument in sqlite_orm, and the call is written with 1 argument: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function row_number() — but stores a trigger or a view holding it",
             SourceLocation{1, 8},
             22},
            {kStatementNotGenerated}});
}

TEST_CASE("codegen: a window function called with too few arguments is not generated") {
    const auto result = generateFull("SELECT lag() OVER () FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"lag() takes 1 to 3 arguments in sqlite_orm, and the call is written with 0 arguments: the library "
                 "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the "
                 "same call — wrong number of arguments to function lag() — but stores a trigger or a view holding it",
                 SourceLocation{1, 8},
                 13},
                {kStatementNotGenerated}});
}

TEST_CASE("codegen: a window function called with too many arguments is not generated") {
    const auto result = generateFull("SELECT lag(id, 1, 0, 9) OVER () FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"lag() takes 1 to 3 arguments in sqlite_orm, and the call is written with 4 arguments: the library "
                 "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the "
                 "same call — wrong number of arguments to function lag() — but stores a trigger or a view holding it",
                 SourceLocation{1, 8},
                 24},
                {kStatementNotGenerated}});
}

// A star is no argument list SQLite counts: `ntile(*)` is refused exactly as `ntile()` is, while
// `row_number(*)` is accepted exactly as `row_number()` is and generates the same call.
TEST_CASE("codegen: a star counts as no argument for a window function") {
    const auto starred = generateFull("SELECT ntile(*) OVER () FROM users;");
    REQUIRE(starred.code.empty());
    REQUIRE(starred.warnings ==
            std::vector<CodegenWarning>{
                {"ntile() takes 1 argument in sqlite_orm, and the call is written with a star, which counts as none: "
                 "the library declares no overload for that many, so there is no form to generate the call as. SQLite "
                 "refuses the same call — wrong number of arguments to function ntile() — but stores a trigger or a "
                 "view holding it",
                 SourceLocation{1, 8},
                 16},
                {kStatementNotGenerated}});

    const auto nullary = generateFull("SELECT row_number(*) OVER () FROM users;");
    REQUIRE(nullary.code == "auto rows = storage.select(row_number().over(), from<Users>());");
    REQUIRE(nullary.warnings == std::vector<CodegenWarning>{});
}

// The message spells the name as written, the way SQLite spells it in its own error.
TEST_CASE("codegen: the arity refusal spells the function name as written") {
    const auto result = generateFull("SELECT RoW_NumBer(id) OVER () FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(
        result.warnings ==
        std::vector<CodegenWarning>{
            {"RoW_NumBer() takes no argument in sqlite_orm, and the call is written with 1 argument: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function RoW_NumBer() — but stores a trigger or a view holding it",
             SourceLocation{1, 8},
             22},
            {kStatementNotGenerated}});
}

// MATCH spelled as a function is the same family: `match_t` is built by the two-argument factory
// only, and SQLite takes the call with two arguments and no other.
TEST_CASE("codegen: a function-spelled MATCH with one argument is not generated") {
    const auto result = generateFull("SELECT match(name) FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(
        result.warnings ==
        std::vector<CodegenWarning>{
            {"match() takes 2 arguments in sqlite_orm, and the call is written with 1 argument: the library declares "
             "no overload for that many, so there is no form to generate the call as. SQLite refuses the same call — "
             "wrong number of arguments to function match() — but stores a trigger or a view holding it",
             SourceLocation{1, 8},
             11},
            {kStatementNotGenerated}});
}

// The call SQLite stores rather than prepares is the one that reaches codegen from `--db`, so the
// warning has to survive a trigger body — and underline the call where it stands there. The step
// holding the refused call is unmappable, so the whole trigger goes with it.
TEST_CASE("codegen: a wrong arity inside a trigger body is not generated") {
    const auto result =
        generateFull("CREATE TRIGGER tr AFTER INSERT ON users BEGIN SELECT row_number(NEW.id) OVER (); END;");
    REQUIRE(result.code.empty());
    REQUIRE(
        result.warnings ==
        std::vector<CodegenWarning>{
            {"row_number() takes no argument in sqlite_orm, and the call is written with 1 argument: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function row_number() — but stores a trigger or a view holding it",
             SourceLocation{1, 54},
             26},
            {"a statement in the trigger body is not mapped to sqlite_orm codegen", SourceLocation{1, 47}, 33},
            {kStatementNotGenerated}});
}

// Every window form the registry holds is pinned by a call written with an argument count SQLite
// refuses, so that widening a row cannot pass unnoticed; the eight the cases above leave out are
// called here. `sqlite3` 3.51.0 refuses each of them with `wrong number of arguments to function`.
TEST_CASE("codegen: every window form is refused on an argument count SQLite refuses") {
    const auto result =
        generateFull("SELECT cume_dist(id) OVER (), first_value(id, 1) OVER (), last_value() OVER (), "
                     "nth_value(id, 1, 2) OVER (), lead(id, 1, 0, 9) OVER (), rank(id) OVER (), dense_rank(id) "
                     "OVER (), percent_rank(id) OVER () FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(
        result.warnings ==
        std::vector<CodegenWarning>{
            {"cume_dist() takes no argument in sqlite_orm, and the call is written with 1 argument: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function cume_dist() — but stores a trigger or a view holding it",
             SourceLocation{1, 8},
             21},
            {"first_value() takes 1 argument in sqlite_orm, and the call is written with 2 arguments: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function first_value() — but stores a trigger or a view holding it",
             SourceLocation{1, 31},
             26},
            {"last_value() takes 1 argument in sqlite_orm, and the call is written with 0 arguments: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function last_value() — but stores a trigger or a view holding it",
             SourceLocation{1, 59},
             20},
            {"nth_value() takes 2 arguments in sqlite_orm, and the call is written with 3 arguments: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function nth_value() — but stores a trigger or a view holding it",
             SourceLocation{1, 81},
             27},
            {"lead() takes 1 to 3 arguments in sqlite_orm, and the call is written with 4 arguments: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function lead() — but stores a trigger or a view holding it",
             SourceLocation{1, 110},
             25},
            {"rank() takes no argument in sqlite_orm, and the call is written with 1 argument: the library declares no "
             "overload for that many, so there is no form to generate the call as. SQLite refuses the same call — "
             "wrong number of arguments to function rank() — but stores a trigger or a view holding it",
             SourceLocation{1, 137},
             16},
            {"dense_rank() takes no argument in sqlite_orm, and the call is written with 1 argument: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function dense_rank() — but stores a trigger or a view holding it",
             SourceLocation{1, 155},
             22},
            {"percent_rank() takes no argument in sqlite_orm, and the call is written with 1 argument: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function percent_rank() — but stores a trigger or a view holding it",
             SourceLocation{1, 179},
             24},
            {kStatementNotGenerated}});
}

// A row is two bounds, and moving either one is a wrong answer of its own, so the end every case
// above leaves unwritten is written here: a count over the maximum for `ntile`, `last_value` and
// `match`, one under the minimum for `lead`, `first_value` and `nth_value`. The ranking functions
// take no argument at all, so a count under their minimum cannot be written. `sqlite3` 3.51.0
// refuses each of these six calls as well.
TEST_CASE("codegen: the other bound of every window form is refused too") {
    const auto result =
        generateFull("SELECT ntile(id, 2) OVER (), lead() OVER (), first_value() OVER (), last_value(id, 1) OVER "
                     "(), nth_value(id) OVER (), match(name, 'x', 'y') FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(
        result.warnings ==
        std::vector<CodegenWarning>{
            {"ntile() takes 1 argument in sqlite_orm, and the call is written with 2 arguments: the library declares "
             "no overload for that many, so there is no form to generate the call as. SQLite refuses the same call — "
             "wrong number of arguments to function ntile() — but stores a trigger or a view holding it",
             SourceLocation{1, 8},
             20},
            {"lead() takes 1 to 3 arguments in sqlite_orm, and the call is written with 0 arguments: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function lead() — but stores a trigger or a view holding it",
             SourceLocation{1, 30},
             14},
            {"first_value() takes 1 argument in sqlite_orm, and the call is written with 0 arguments: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function first_value() — but stores a trigger or a view holding it",
             SourceLocation{1, 46},
             21},
            {"last_value() takes 1 argument in sqlite_orm, and the call is written with 2 arguments: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function last_value() — but stores a trigger or a view holding it",
             SourceLocation{1, 69},
             25},
            {"nth_value() takes 2 arguments in sqlite_orm, and the call is written with 1 argument: the library "
             "declares no overload for that many, so there is no form to generate the call as. SQLite refuses the same "
             "call — wrong number of arguments to function nth_value() — but stores a trigger or a view holding it",
             SourceLocation{1, 96},
             21},
            {"match() takes 2 arguments in sqlite_orm, and the call is written with 3 arguments: the library declares "
             "no overload for that many, so there is no form to generate the call as. SQLite refuses the same call — "
             "wrong number of arguments to function match() — but stores a trigger or a view holding it",
             SourceLocation{1, 119},
             21},
            {kStatementNotGenerated}});
}

// Every argument count SQLite accepts is one sqlite_orm has an overload for, so none of these
// warns; the counts are the ones `sqlite3` 3.51.0 prepares.
TEST_CASE("codegen: the argument counts SQLite accepts generate without a warning") {
    const auto lagAndLead =
        generateFull("SELECT lag(id) OVER (), lag(id, 1) OVER (), lag(id, 1, 0) OVER (), lead(id) OVER (), "
                     "lead(id, 1) OVER (), lead(id, 1, 0) OVER () FROM users;");
    REQUIRE(lagAndLead.code ==
            "auto rows = storage.select(columns(lag(&Users::id).over(), lag(&Users::id, 1).over(), lag(&Users::id, 1, "
            "0).over(), lead(&Users::id).over(), lead(&Users::id, 1).over(), lead(&Users::id, 1, 0).over()));");
    REQUIRE(lagAndLead.warnings == std::vector<CodegenWarning>{});

    const auto others = generateFull("SELECT row_number() OVER (), rank() OVER (), dense_rank() OVER (), "
                                     "percent_rank() OVER (), cume_dist() OVER (), ntile(2) OVER (), "
                                     "first_value(id) OVER (), last_value(id) OVER (), nth_value(id, 1) OVER () "
                                     "FROM users;");
    REQUIRE(others.code ==
            "auto rows = storage.select(columns(row_number().over(), rank().over(), dense_rank().over(), "
            "percent_rank().over(), cume_dist().over(), ntile(2).over(), first_value(&Users::id).over(), "
            "last_value(&Users::id).over(), nth_value(&Users::id, 1).over()));");
    REQUIRE(others.warnings == std::vector<CodegenWarning>{});
}

// The registry covers the built-ins as well as the window forms, which is what card
// 1868323633923884702 asked for: a variadic name generates as written — the scalar
// `max(X, Y, ...)` takes the three arguments SQLite accepts here, on the C++20 and on the legacy
// header path alike — while `abs(a, 1)`, which SQLite refuses too, is refused rather than
// generated silently into code that compiles on neither path. An unknown name becomes a
// user-defined function instead, whatever it is written with, and the gate lets it through.
TEST_CASE("codegen: a variadic form generates as written and a wrong built-in arity does not") {
    const auto variadic = generateFull("SELECT max(id, id, id) FROM users;");
    REQUIRE(variadic.code == "auto rows = storage.select(max(&Users::id, &Users::id, &Users::id));");
    REQUIRE(variadic.warnings == std::vector<CodegenWarning>{});

    const auto wrongArityBuiltin = generateFull("SELECT abs(id, 1) FROM users;");
    REQUIRE(wrongArityBuiltin.code.empty());
    REQUIRE(wrongArityBuiltin.warnings ==
            std::vector<CodegenWarning>{
                {"abs() takes 1 argument in sqlite_orm, and the call is written with 2 arguments: the library declares "
                 "no overload for that many, so there is no form to generate the call as. SQLite refuses the same call "
                 "— wrong number of arguments to function abs() — but stores a trigger or a view holding it",
                 SourceLocation{1, 8},
                 10},
                {kStatementNotGenerated}});
}

// The same `filter()` sqlite_orm withholds from the window functions is missing from every other
// form that is not an aggregate function call: a scalar function, a MATCH in its function
// spelling, the argument-less `count()` and the `func<…>()` a user-defined function is written as
// all carry none, so the gate refuses each of them. SQLite refuses a FILTER on a built-in
// non-aggregate too — with the diagnostic each warning quotes, checked against sqlite3 3.51.0 —
// but stores a trigger or a view holding one, which is how such a call reaches codegen.
TEST_CASE("codegen: a FILTER over a scalar function is not generated") {
    const auto result = generateFull("SELECT upper(name) FILTER (WHERE id > 0) FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"upper() has no filter() in sqlite_orm: only the aggregate function calls and count(*) take a FILTER, "
                 "so there is no form to generate the call as. SQLite refuses the same call — FILTER may not be used "
                 "with non-aggregate upper() — but stores a trigger or a view holding it",
                 SourceLocation{1, 8},
                 33},
                {kStatementNotGenerated}});
}

TEST_CASE("codegen: a FILTER over a scalar function under an OVER quotes the window diagnostic") {
    const auto result = generateFull("SELECT upper(name) FILTER (WHERE id > 0) OVER () FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"upper() has no filter() in sqlite_orm: only the aggregate function calls and count(*) take a FILTER, "
                 "so there is no form to generate the call as. SQLite refuses the same call — upper() may not be used "
                 "as a window function — but stores a trigger or a view holding it",
                 SourceLocation{1, 8},
                 41},
                {kStatementNotGenerated}});
}

TEST_CASE("codegen: a FILTER over a window function without an OVER is not generated") {
    const auto result = generateFull("SELECT row_number() FILTER (WHERE id > 0) FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"row_number() has no filter() in sqlite_orm: only the aggregate function calls and count(*) take a "
                 "FILTER, so there is no form to generate the call as. SQLite refuses the same call — misuse of window "
                 "function row_number() — but stores a trigger or a view holding it",
                 SourceLocation{1, 8},
                 34},
                {kStatementNotGenerated}});
}

TEST_CASE("codegen: a FILTER over a function-spelled MATCH is not generated") {
    const auto result = generateFull("SELECT match(name, 'x') FILTER (WHERE id > 0) FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"match() has no filter() in sqlite_orm: only the aggregate function calls and count(*) take a FILTER, "
                 "so there is no form to generate the call as. SQLite refuses the same call — FILTER may not be used "
                 "with non-aggregate match() — but stores a trigger or a view holding it",
                 SourceLocation{1, 8},
                 38},
                {kStatementNotGenerated}});
}

// MAX and MIN are the aggregates in their one-argument form alone: sqlite_orm picks the scalar
// overload from a second argument on, and SQLite makes the same split.
TEST_CASE("codegen: a FILTER over MAX is refused for its scalar form only") {
    SECTION("aggregate form") {
        const auto result = generateFull("SELECT max(id) FILTER (WHERE id > 0) FROM users;");
        REQUIRE(result.code == "auto rows = storage.select(max(&Users::id).filter(where(c(&Users::id) > 0)));");
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    }

    SECTION("scalar form") {
        const auto result = generateFull("SELECT max(id, size) FILTER (WHERE id > 0) FROM users;");
        REQUIRE(result.code.empty());
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"max() has no filter() in sqlite_orm: only the aggregate function calls and count(*) take a "
                     "FILTER, so there is no form to generate the call as. SQLite refuses the same call — FILTER may "
                     "not be used with non-aggregate max() — but stores a trigger or a view holding it",
                     SourceLocation{1, 8},
                     35},
                    {kStatementNotGenerated}});
    }
}

// `count(*)` is generated as `count_asterisk_t`, which takes a FILTER; the argument-less `count()`
// SQLite counts the same rows with comes out a `count_asterisk_without_type`, which holds nothing
// at all. The SQL is well formed either way, so the warning says so.
TEST_CASE("codegen: a FILTER over the argument-less count() is refused, over count(*) it is not") {
    SECTION("count(*)") {
        const auto result = generateFull("SELECT count(*) FILTER (WHERE id > 0) FROM users;");
        REQUIRE(result.code == "auto rows = storage.select(count<Users>().filter(where(c(&Users::id) > 0)));");
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    }

    SECTION("count()") {
        const auto result = generateFull("SELECT count() FILTER (WHERE id > 0) FROM users;");
        REQUIRE(result.code.empty());
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"count() has no filter() in sqlite_orm: only the aggregate function calls and count(*) take a "
                     "FILTER, so there is no form to generate the call as. SQLite takes the same call — count() counts "
                     "the rows count(*) does — so the SQL is well formed and the generated code alone is not",
                     SourceLocation{1, 8},
                     29},
                    {kStatementNotGenerated}});
    }
}

// A user-defined function is written as a `func<…>()` call, which sqlite_orm gives no `filter()`
// whatever the function is registered as — so unlike the built-in non-aggregates, the SQL itself
// may be perfectly well formed.
TEST_CASE("codegen: a FILTER over a user-defined function is not generated") {
    const auto result = generateFull("SELECT myagg(id) FILTER (WHERE id > 0) FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"myagg() has no filter() in sqlite_orm: only the aggregate function calls and count(*) take a FILTER, "
                 "so there is no form to generate the call as. SQLite takes a FILTER over a user-defined aggregate "
                 "function, so the SQL may well be fine while the func<Myagg>() call standing for myagg() is not",
                 SourceLocation{1, 8},
                 31},
                {kStatementNotGenerated}});
}

// Every built-in aggregate sqlite_orm declares as a `builtin_aggregate_function_t` carries a
// `filter()`, so these generate with nothing to warn about.
TEST_CASE("codegen: a FILTER over a built-in aggregate call warns of nothing") {
    SECTION("sum") {
        const auto result = generateFull("SELECT sum(id) FILTER (WHERE id > 0) FROM users;");
        REQUIRE(result.code == "auto rows = storage.select(sum(&Users::id).filter(where(c(&Users::id) > 0)));");
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    }

    SECTION("avg") {
        const auto result = generateFull("SELECT avg(id) FILTER (WHERE id > 0) FROM users;");
        REQUIRE(result.code ==
                "auto rows = storage.select(as_optional(avg(&Users::id).filter(where(c(&Users::id) > 0))));");
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    }

    SECTION("total") {
        const auto result = generateFull("SELECT total(id) FILTER (WHERE id > 0) FROM users;");
        REQUIRE(result.code == "auto rows = storage.select(total(&Users::id).filter(where(c(&Users::id) > 0)));");
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    }

    SECTION("group_concat") {
        const auto result = generateFull("SELECT group_concat(name) FILTER (WHERE id > 0) FROM users;");
        REQUIRE(
            result.code ==
            "auto rows = storage.select(as_optional(group_concat(&Users::name).filter(where(c(&Users::id) > 0))));");
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    }

    SECTION("json_group_array") {
        const auto result = generateFull("SELECT json_group_array(name) FILTER (WHERE id > 0) FROM users;");
        REQUIRE(result.code ==
                "auto rows = storage.select(json_group_array(&Users::name).filter(where(c(&Users::id) > 0)));");
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    }

    SECTION("count over DISTINCT") {
        const auto result = generateFull("SELECT count(DISTINCT id) FILTER (WHERE id > 0) FROM users;");
        REQUIRE(result.code ==
                "auto rows = storage.select(count(distinct(&Users::id)).filter(where(c(&Users::id) > 0)));");
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    }
}

TEST_CASE("codegen: WINDOW clause maps to window(...) on select") {
    REQUIRE(generate("SELECT row_number() OVER w FROM users WINDOW w AS (ORDER BY id);") ==
            "auto rows = storage.select(row_number().over(window_ref(\"w\")), "
            "window(\"w\", order_by(&Users::id)));");
}

TEST_CASE("codegen: WITH RECURSIVE VALUES(1) UNION ALL — Klaus example") {
    constexpr std::string_view sql =
        "WITH RECURSIVE cnt(x) AS(VALUES(1) UNION ALL SELECT x + 1 FROM cnt WHERE x < 1000000) SELECT x FROM cnt;";

    SECTION("default indexed_typedef style") {
        REQUIRE(generate(sql) ==
                "using namespace sqlite_orm::literals;\n"
                "using cte_0 = decltype(1_ctealias);\n"
                "constexpr auto cnt__x = colalias_a{};\n"
                "auto rows = storage.with_recursive(cte<cte_0>(\"x\").as(union_all(select(1 >>= cnt__x), "
                "select(column<cte_0>(cnt__x) + 1, where(column<cte_0>(cnt__x) < 1000000)))), "
                "select(column<cte_0>(cnt__x)));");
    }

    SECTION("cpp20_monikers style") {
        CodeGenPolicy codeGenPolicy;
        codeGenPolicy.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";
        auto result = generateWithPolicy(sql, codeGenPolicy);
        REQUIRE(result.code == "using namespace sqlite_orm::literals;\n"
                               "constexpr orm_cte_moniker auto cnt_cte = \"cnt\"_cte;\n"
                               "constexpr orm_column_alias auto cnt__x = \"x\"_col;\n"
                               "auto rows = storage.with_recursive(cnt_cte(cnt__x).as(union_all(select(1), "
                               "select(cnt_cte->*cnt__x + 1, where(cnt_cte->*cnt__x < 1000000)))), "
                               "select(cnt_cte->*cnt__x));");
    }
}

TEST_CASE("codegen: targetCppStandard 17 drops the C++20 with_cte_style option") {
    CodeGenPolicy policy;
    policy.targetCppStandard = 17;
    auto result = generateWithPolicy("WITH cnt(x) AS (SELECT 1 AS x) SELECT x FROM cnt;", policy);
    const DecisionPoint* dp = nullptr;
    for (const auto& candidate: result.decisionPoints) {
        if (candidate.category == "with_cte_style") {
            dp = &candidate;
        }
    }
    REQUIRE(dp != nullptr);
    for (const auto& option: dp->options) {
        CHECK(option.value != "cpp20_monikers");
        CHECK(option.minCppStandard <= 17);
    }
    CHECK(dp->chosenValue != "cpp20_monikers");
}

TEST_CASE("codegen: explicit cpp20_monikers policy overridden by targetCppStandard 17") {
    CodeGenPolicy policy;
    policy.targetCppStandard = 17;
    policy.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";
    auto result = generateWithPolicy("WITH cnt(x) AS (SELECT 1 AS x) SELECT x FROM cnt;", policy);
    for (const auto& candidate: result.decisionPoints) {
        if (candidate.category == "with_cte_style") {
            CHECK(candidate.chosenValue != "cpp20_monikers");
        }
    }
    CHECK(result.code.find("orm_cte_moniker") == std::string::npos);
}

TEST_CASE("codegen: WITH … SELECT * FROM cte wraps the outer select (regression)") {
    // Previously the outer `SELECT *` rendered as storage.get_all<cte_0>(), which is not a
    // storage.with() argument, so the whole WITH was dropped and the code referenced an
    // undefined cte_0. It must now wrap as storage.with(…, select(asterisk<cte_0>())).
    REQUIRE(generate("WITH e(id, name, salary) AS (SELECT id, name, salary FROM employee WHERE salary > 60000.0) "
                     "SELECT * FROM e;") ==
            "using namespace sqlite_orm::literals;\n"
            "using cte_0 = decltype(1_ctealias);\n"
            "constexpr auto e__id = colalias_a{};\n"
            "constexpr auto e__name = colalias_b{};\n"
            "constexpr auto e__salary = colalias_c{};\n"
            "auto rows = storage.with(cte<cte_0>(\"id\", \"name\", \"salary\").as(select(columns("
            "&Employee::id >>= e__id, &Employee::name >>= e__name, &Employee::salary >>= e__salary), "
            "where(c(&Employee::salary) > 60000.0))), select(asterisk<cte_0>()));");
}

TEST_CASE("codegen: WITH … SELECT * FROM cte cpp20_monikers also wraps") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";
    CodeGenResult result =
        generateWithPolicy("WITH e(id, name, salary) AS (SELECT id, name, salary FROM employee WHERE salary > 60000.0) "
                           "SELECT * FROM e;",
                           pol);
    REQUIRE(result.code == "using namespace sqlite_orm::literals;\n"
                           "constexpr orm_cte_moniker auto e_cte = \"e\"_cte;\n"
                           "constexpr orm_column_alias auto e__id = \"id\"_col;\n"
                           "constexpr orm_column_alias auto e__name = \"name\"_col;\n"
                           "constexpr orm_column_alias auto e__salary = \"salary\"_col;\n"
                           "auto rows = storage.with(e_cte(e__id, e__name, e__salary).as(select(columns("
                           "&Employee::id, &Employee::name, &Employee::salary), "
                           "where(c(&Employee::salary) > 60000.0))), select(asterisk<e_cte>()));");
}
