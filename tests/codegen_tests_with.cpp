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

// The `over()` sqlite_orm declares is the other half of the same gate, and it was the half nobody
// asked about: a FILTER over a form without a `filter()` was refused while an OVER over a form
// without an `over()` was written out all the same, so `abs(id) OVER ()` came out as
// `abs(&Users::id).over()` — code no compiler takes — with nothing warned. sqlite_orm gives
// `over()` to the window functions, to `count(*)` and to the aggregate function calls (and to the
// `filtered_aggregate_function` a FILTER over one returns), so a scalar function, a MATCH in its
// function spelling, the argument-less `count()` and the `func<…>()` a user-defined function is
// written as all carry none. SQLite refuses a windowed non-aggregate too — `upper() may not be
// used as a window function`, checked against sqlite3 3.51.0 — but stores a trigger or a view
// holding one, which is how such a call reaches codegen.
TEST_CASE("codegen: an OVER over a scalar function is not generated") {
    const auto result = generateFull("SELECT upper(name) OVER () FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"upper() has no over() in sqlite_orm: only the window functions, the aggregate function calls and "
                 "count(*) take an OVER, so there is no form to generate the call as. SQLite refuses the same call — "
                 "upper() may not be used as a window function — but stores a trigger or a view holding it",
                 SourceLocation{1, 8},
                 19},
                {kStatementNotGenerated}});
}

// An OVER naming a window of the WINDOW clause is the same refusal over a shorter span: what the
// warning underlines is the call as written, and SQLite answers a named window the same way.
TEST_CASE("codegen: an OVER naming a window is refused for a form without one") {
    const auto result = generateFull("SELECT upper(name) OVER w FROM users WINDOW w AS (ORDER BY id);");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"upper() has no over() in sqlite_orm: only the window functions, the aggregate function calls and "
                 "count(*) take an OVER, so there is no form to generate the call as. SQLite refuses the same call — "
                 "upper() may not be used as a window function — but stores a trigger or a view holding it",
                 SourceLocation{1, 8},
                 18},
                {kStatementNotGenerated}});
}

TEST_CASE("codegen: an OVER over a function-spelled MATCH is not generated") {
    const auto result = generateFull("SELECT match(name, 'x') OVER () FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"match() has no over() in sqlite_orm: only the window functions, the aggregate function calls and "
                 "count(*) take an OVER, so there is no form to generate the call as. SQLite refuses the same call — "
                 "match() may not be used as a window function — but stores a trigger or a view holding it",
                 SourceLocation{1, 8},
                 24},
                {kStatementNotGenerated}});
}

// The same one-argument split the FILTER refusal makes: MAX is the aggregate, which windows, up to
// a second argument, and the scalar overload from there on, which does not.
TEST_CASE("codegen: an OVER over MAX is refused for its scalar form only") {
    SECTION("aggregate form") {
        const auto result = generateFull("SELECT max(id) OVER (ORDER BY id) FROM users;");
        REQUIRE(result.code == "auto rows = storage.select(max(&Users::id).over(order_by(&Users::id)));");
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    }

    SECTION("scalar form") {
        const auto result = generateFull("SELECT max(id, size) OVER () FROM users;");
        REQUIRE(result.code.empty());
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"max() has no over() in sqlite_orm: only the window functions, the aggregate function calls and "
                     "count(*) take an OVER, so there is no form to generate the call as. SQLite refuses the same "
                     "call — max() may not be used as a window function — but stores a trigger or a view holding it",
                     SourceLocation{1, 8},
                     21},
                    {kStatementNotGenerated}});
    }
}

// `count(*)` comes out a `count_asterisk_t`, which windows; the argument-less `count()` SQLite
// counts the same rows with comes out a `count_asterisk_without_type`, which holds nothing at all.
// SQLite windows either, so the warning says the generated code alone is at fault.
TEST_CASE("codegen: an OVER over the argument-less count() is refused, over count(*) it is not") {
    SECTION("count(*)") {
        const auto result = generateFull("SELECT count(*) OVER () FROM users;");
        REQUIRE(result.code == "auto rows = storage.select(count<Users>().over());");
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    }

    SECTION("count()") {
        const auto result = generateFull("SELECT count() OVER () FROM users;");
        REQUIRE(result.code.empty());
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"count() has no over() in sqlite_orm: only the window functions, the aggregate function calls and "
                     "count(*) take an OVER, so there is no form to generate the call as. SQLite takes the same call — "
                     "count() counts the rows count(*) does — so the SQL is well formed and the generated code alone "
                     "is not",
                     SourceLocation{1, 8},
                     15},
                    {kStatementNotGenerated}});
    }
}

// A user-defined function is written as a `func<…>()` call, which sqlite_orm gives no `over()`
// whatever the function is registered as — while SQLite windows one registered through
// `sqlite3_create_window_function` (and refuses a plain aggregate or scalar, checked by
// registering all three against sqlite3 3.51.0), so the SQL itself may be perfectly well formed.
TEST_CASE("codegen: an OVER over a user-defined function is not generated") {
    const auto result = generateFull("SELECT myagg(id) OVER () FROM users;");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"myagg() has no over() in sqlite_orm: only the window functions, the aggregate function calls and "
                 "count(*) take an OVER, so there is no form to generate the call as. SQLite takes an OVER over a "
                 "user-defined window function, so the SQL may well be fine while the func<Myagg>() call standing for "
                 "myagg() is not",
                 SourceLocation{1, 8},
                 17},
                {kStatementNotGenerated}});
}

// And the forms that do carry an `over()` go on generating with nothing to warn about — the
// aggregate with a FILTER under it included, because `filter()` returns a
// `filtered_aggregate_function`, which declares one of its own.
TEST_CASE("codegen: an OVER over a form that carries one warns of nothing") {
    SECTION("built-in aggregate") {
        const auto result = generateFull("SELECT sum(id) OVER (PARTITION BY name) FROM users;");
        REQUIRE(result.code == "auto rows = storage.select(sum(&Users::id).over(partition_by(&Users::name)));");
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    }

    SECTION("aggregate under a FILTER") {
        const auto result = generateFull("SELECT sum(id) FILTER (WHERE id > 0) OVER () FROM users;");
        REQUIRE(result.code == "auto rows = storage.select(sum(&Users::id).filter(where(c(&Users::id) > 0)).over());");
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    }

    SECTION("window function taking an argument") {
        const auto result = generateFull("SELECT lag(id) OVER () FROM users;");
        REQUIRE(result.code == "auto rows = storage.select(lag(&Users::id).over());");
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    }
}

// A DISTINCT under an OVER is the one windowed call sqlite_orm writes and SQLite takes nowhere:
// `count(distinct(&Users::id)).over()` compiles and the storage throws `SQL logic error` when it
// prepares it, because sqlite3 3.51.0 answers `DISTINCT is not supported for window functions`
// over every function — an aggregate, a scalar, a window function, one it does not know — and
// refuses to create a trigger or a view holding the call too. That verdict is the one quoted,
// asked before whatever the registry says of the form.
TEST_CASE("codegen: a DISTINCT under an OVER is not generated") {
    SECTION("count") {
        const auto result = generateFull("SELECT count(DISTINCT id) OVER () FROM users;");
        REQUIRE(result.code.empty());
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"count() is written with DISTINCT under an OVER, which SQLite takes over no function — "
                     "DISTINCT is not supported for window functions — so there is no call to generate: code written "
                     "for it would fail when the storage prepares the statement. SQLite refuses the call as it parses "
                     "the SQL, so no trigger or view holding it is ever stored either",
                     SourceLocation{1, 8},
                     26},
                    {kStatementNotGenerated}});
    }

    SECTION("aggregate under a FILTER over a named window") {
        const auto result =
            generateFull("SELECT sum(DISTINCT id) FILTER (WHERE id > 0) OVER w FROM users WINDOW w AS (ORDER BY id);");
        REQUIRE(result.code.empty());
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"sum() is written with DISTINCT under an OVER, which SQLite takes over no function — "
                     "DISTINCT is not supported for window functions — so there is no call to generate: code written "
                     "for it would fail when the storage prepares the statement. SQLite refuses the call as it parses "
                     "the SQL, so no trigger or view holding it is ever stored either",
                     SourceLocation{1, 8},
                     45},
                    {kStatementNotGenerated}});
    }

    SECTION("scalar function") {
        const auto result = generateFull("SELECT upper(DISTINCT name) OVER () FROM users;");
        REQUIRE(result.code.empty());
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"upper() is written with DISTINCT under an OVER, which SQLite takes over no function — "
                     "DISTINCT is not supported for window functions — so there is no call to generate: code written "
                     "for it would fail when the storage prepares the statement. SQLite refuses the call as it parses "
                     "the SQL, so no trigger or view holding it is ever stored either",
                     SourceLocation{1, 8},
                     28},
                    {kStatementNotGenerated}});
    }

    SECTION("window function") {
        const auto result = generateFull("SELECT row_number(DISTINCT id) OVER () FROM users;");
        REQUIRE(result.code.empty());
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"row_number() is written with DISTINCT under an OVER, which SQLite takes over no function — "
                     "DISTINCT is not supported for window functions — so there is no call to generate: code written "
                     "for it would fail when the storage prepares the statement. SQLite refuses the call as it parses "
                     "the SQL, so no trigger or view holding it is ever stored either",
                     SourceLocation{1, 8},
                     31},
                    {kStatementNotGenerated}});
    }

    SECTION("user-defined function") {
        const auto result = generateFull("SELECT myagg(DISTINCT id) OVER () FROM users;");
        REQUIRE(result.code.empty());
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"myagg() is written with DISTINCT under an OVER, which SQLite takes over no function — "
                     "DISTINCT is not supported for window functions — so there is no call to generate: code written "
                     "for it would fail when the storage prepares the statement. SQLite refuses the call as it parses "
                     "the SQL, so no trigger or view holding it is ever stored either",
                     SourceLocation{1, 8},
                     26},
                    {kStatementNotGenerated}});
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

// A subquery inside a statement whose FROM is a CTE reads its own FROM, and that is the table its
// columns belong to. The implicit-CTE marks the enclosing select left on the context were taken out
// of it with `std::move`, which leaves an `std::optional` engaged over an emptied string, so the
// subquery read that leftover as an implicit CTE source of its own and spelled `u`'s column
// `column<>("b")` — a form sqlite_orm declares no overload for, handed out at exit 0. The
// `from<cte_0>()` beside it is what keeps the table the subquery names out of the FROM sqlite_orm
// infers for this level, the way an ordinary select's is spelled out for the same reason.
TEST_CASE("codegen: a subquery under a CTE-sourced select names its own table") {
    const std::string prologue = "using namespace sqlite_orm::literals;\n"
                                 "using cte_0 = decltype(1_ctealias);\n";

    REQUIRE(generate("WITH c AS (SELECT a FROM t) SELECT a FROM c WHERE (SELECT b FROM u);") ==
            prologue + "auto rows = storage.with(cte<cte_0>().as(select(&T::a)), select(column<cte_0>(&T::a), "
                       "from<cte_0>(), where(select(&U::b))));");
    REQUIRE(generate("WITH c AS (SELECT a FROM t) SELECT a FROM c WHERE a > (SELECT b FROM u);") ==
            prologue + "auto rows = storage.with(cte<cte_0>().as(select(&T::a)), select(column<cte_0>(&T::a), "
                       "from<cte_0>(), where(column<cte_0>(&T::a) > select(&U::b))));");
    REQUIRE(generate("WITH c AS (SELECT a FROM t) SELECT a FROM c WHERE a IN (SELECT b FROM u);") ==
            prologue + "auto rows = storage.with(cte<cte_0>().as(select(&T::a)), select(column<cte_0>(&T::a), "
                       "from<cte_0>(), where(in(column<cte_0>(&T::a), select(&U::b)))));");
    REQUIRE(generate("WITH c AS (SELECT a FROM t) SELECT a FROM c WHERE EXISTS (SELECT b FROM u);") ==
            prologue + "auto rows = storage.with(cte<cte_0>().as(select(&T::a)), select(column<cte_0>(&T::a), "
                       "from<cte_0>(), where(exists(select(&U::b)))));");
}

TEST_CASE("codegen: WITH cpp20_monikers declares the table alias of the outer SELECT") {
    constexpr std::string_view sql =
        "WITH c AS (SELECT id FROM users) SELECT u.name FROM users u WHERE u.id IN (SELECT id FROM c);";
    const std::string expected = "using namespace sqlite_orm::literals;\n"
                                 "constexpr orm_cte_moniker auto c_cte = \"c\"_cte;\n"
                                 "constexpr orm_table_alias auto u = \"u\"_alias.for_<Users>();\n"
                                 "auto rows = storage.with(c_cte().as(select(&Users::id)), select(u->*&Users::name, "
                                 "from<u>(), where(in(u->*&Users::id, select(c_cte->*&Users::id)))));";
    SECTION("chosen style") {
        CodeGenPolicy pol;
        pol.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";
        REQUIRE(generateWithPolicy(sql, pol).code == expected);
    }
    SECTION("option of the with_cte_style decision point") {
        const CodeGenResult result = generateFull(sql);
        std::vector<std::string> cpp20MonikerCodes;
        for (const auto& dp: result.decisionPoints) {
            if (dp.category == "with_cte_style") {
                for (const auto& option: dp.options) {
                    if (option.value == "cpp20_monikers") {
                        cpp20MonikerCodes.push_back(option.code);
                    }
                }
            }
        }
        REQUIRE(cpp20MonikerCodes == std::vector<std::string>{expected});
    }
}

TEST_CASE("codegen: WITH cpp20_monikers declares an alias shared by the CTE body and the outer SELECT once") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";
    CodeGenResult codeGenResult = generateWithPolicy(
        "WITH c AS (SELECT u.id FROM users u) SELECT u.name FROM users u WHERE u.id IN (SELECT id FROM c);",
        pol);
    REQUIRE(codeGenResult.code == "using namespace sqlite_orm::literals;\n"
                                  "constexpr orm_cte_moniker auto c_cte = \"c\"_cte;\n"
                                  "constexpr orm_table_alias auto u = \"u\"_alias.for_<Users>();\n"
                                  "auto rows = storage.with(c_cte().as(select(u->*&Users::id)), "
                                  "select(u->*&Users::name, from<u>(), "
                                  "where(in(u->*&Users::id, select(c_cte->*&Users::id)))));");
}

TEST_CASE("codegen: WITH indexed_typedef declares the C++20 table alias of the outer SELECT") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["table_alias_style"] = "cpp20";
    CodeGenResult codeGenResult = generateWithPolicy(
        "WITH c AS (SELECT id FROM users) SELECT u.name FROM users u WHERE u.id IN (SELECT id FROM c);",
        pol);
    REQUIRE(codeGenResult.code == "using namespace sqlite_orm::literals;\n"
                                  "using cte_0 = decltype(1_ctealias);\n"
                                  "constexpr orm_table_alias auto u = \"u\"_alias.for_<Users>();\n"
                                  "auto rows = storage.with(cte<cte_0>().as(select(&Users::id)), "
                                  "select(u->*&Users::name, from<u>(), "
                                  "where(in(u->*&Users::id, select(column<cte_0>(&Users::id))))));");
}

TEST_CASE("codegen: WITH cpp20_monikers DELETE declares the table alias of its subquery") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";
    CodeGenResult codeGenResult = generateWithPolicy(
        "WITH c AS (SELECT id FROM users) DELETE FROM users WHERE id IN (SELECT u.id FROM users u JOIN c ON c.id = "
        "u.id);",
        pol);
    REQUIRE(codeGenResult.code == "using namespace sqlite_orm::literals;\n"
                                  "constexpr orm_cte_moniker auto c_cte = \"c\"_cte;\n"
                                  "constexpr orm_table_alias auto u = \"u\"_alias.for_<Users>();\n"
                                  "storage.with(c_cte().as(select(&Users::id)), remove_all<Users>(where(in(&Users::id, "
                                  "select(u->*&Users::id, join<c_cte>(on(c_cte->*&Users::id == u->*&Users::id)))))));");
}

// Each column of a CTE's column list is declared as a `constexpr` alias variable named after the
// CTE and the column, so two column names C++ has one spelling for declare that variable twice —
// SQLite takes the CTE — and it is reported, anchored at the second name, in every style.
TEST_CASE("codegen: WITH - two CTE column names mapped to one alias variable are reported") {
    const auto result = generateFull("WITH cte(\"a-b\", \"a b\") AS (SELECT 1, 2) SELECT * FROM cte;");
    REQUIRE(result.code == "using namespace sqlite_orm::literals;\n"
                           "using cte_0 = decltype(1_ctealias);\n"
                           "constexpr auto cte__a_b = colalias_a{};\n"
                           "constexpr auto cte__a_b = colalias_b{};\n"
                           "auto rows = storage.with(cte<cte_0>(\"a-b\", \"a b\").as(select(columns(1 >>= cte__a_b, "
                           "2 >>= cte__a_b))), select(asterisk<cte_0>()));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"CTE cte: column `a-b` is not a C++ identifier; the column alias declared for it is named after "
                 "`a_b`",
                 SourceLocation{1, 10},
                 5},
                {"CTE cte: columns `a-b` and `a b` are both named `a_b` in C++; the generated code declares their "
                 "column alias twice and does not compile",
                 SourceLocation{1, 17},
                 5},
                {"WITH: requires SQLite ≥ 3.8.3, sqlite_orm built with SQLITE_ORM_WITH_CTE, and `using namespace "
                 "sqlite_orm::literals` scope for `_ctealias`"}});
}

TEST_CASE("codegen: WITH legacy_colalias - two CTE column names mapped to one alias variable are reported") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["with_cte_style"] = "legacy_colalias";
    const auto result =
        generateWithPolicySuppressWithCteDp("WITH cte(\"a-b\", \"a b\") AS (SELECT 1, 2) SELECT * FROM cte;", policy);
    REQUIRE(result.code == "using namespace sqlite_orm::literals;\n"
                           "using cte = decltype(1_ctealias);\n"
                           "constexpr auto cte_a_b = colalias_a{};\n"
                           "constexpr auto cte_a_b = colalias_b{};\n"
                           "auto rows = storage.with(cte<cte>(\"a-b\", \"a b\").as(select(columns(1 >>= cte_a_b, "
                           "2 >>= cte_a_b))), select(asterisk<cte>()));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"CTE cte: column `a-b` is not a C++ identifier; the column alias declared for it is named after "
                 "`a_b`",
                 SourceLocation{1, 10},
                 5},
                {"CTE cte: columns `a-b` and `a b` are both named `a_b` in C++; the generated code declares their "
                 "column alias twice and does not compile",
                 SourceLocation{1, 17},
                 5},
                {"WITH: requires SQLite ≥ 3.8.3, sqlite_orm built with SQLITE_ORM_WITH_CTE, and `using namespace "
                 "sqlite_orm::literals` scope for `_ctealias`"}});
}

TEST_CASE("codegen: WITH cpp20_monikers - two CTE column names mapped to one alias variable are reported") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";
    const auto result =
        generateWithPolicySuppressWithCteDp("WITH cte(\"a-b\", \"a b\") AS (SELECT 1, 2) SELECT * FROM cte;", policy);
    REQUIRE(result.code == "using namespace sqlite_orm::literals;\n"
                           "constexpr orm_cte_moniker auto cte_cte = \"cte\"_cte;\n"
                           "constexpr orm_column_alias auto cte__a_b = \"a-b\"_col;\n"
                           "constexpr orm_column_alias auto cte__a_b = \"a b\"_col;\n"
                           "auto rows = storage.with(cte_cte(cte__a_b, cte__a_b).as(select(columns(1, 2))), "
                           "select(asterisk<cte_cte>()));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"CTE cte: column `a-b` is not a C++ identifier; the column alias declared for it is named after "
                 "`a_b`",
                 SourceLocation{1, 10},
                 5},
                {"CTE cte: columns `a-b` and `a b` are both named `a_b` in C++; the generated code declares their "
                 "column alias twice and does not compile",
                 SourceLocation{1, 17},
                 5},
                {"WITH: cpp20_monikers requires C++20, SQLITE_ORM_WITH_CPP20_ALIASES, and matching sqlite_orm"},
                {"WITH: requires SQLite ≥ 3.8.3, sqlite_orm built with SQLITE_ORM_WITH_CTE, and `using namespace "
                 "sqlite_orm::literals` scope for `_ctealias`"}});
}

// Two CTE column names SQLite tells apart only by case are two columns with two C++ names, so
// each is declared as its own alias variable, by its place in the column list. A reference to
// the name resolves the way SQLite resolves it, to the first of the two columns.
TEST_CASE("codegen: WITH - CTE column names differing only in case are declared apart") {
    const auto result = generateFull("WITH c(a, A) AS (SELECT 1, 2) SELECT a FROM c;");
    REQUIRE(result.code == "using namespace sqlite_orm::literals;\n"
                           "using cte_0 = decltype(1_ctealias);\n"
                           "constexpr auto c__a = colalias_a{};\n"
                           "constexpr auto c__A = colalias_b{};\n"
                           "auto rows = storage.with(cte<cte_0>(\"a\", \"A\").as(select(columns(1 >>= c__a, 2 >>= "
                           "c__A))), select(column<cte_0>(c__a)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"WITH: requires SQLite ≥ 3.8.3, sqlite_orm built with SQLITE_ORM_WITH_CTE, and `using namespace "
                 "sqlite_orm::literals` scope for `_ctealias`"}});
}

TEST_CASE("codegen: WITH legacy_colalias - CTE column names differing only in case are declared apart") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["with_cte_style"] = "legacy_colalias";
    const auto result = generateWithPolicySuppressWithCteDp("WITH c(a, A) AS (SELECT 1, 2) SELECT a FROM c;", policy);
    REQUIRE(result.code == "using namespace sqlite_orm::literals;\n"
                           "using c = decltype(1_ctealias);\n"
                           "constexpr auto c_a = colalias_a{};\n"
                           "constexpr auto c_A = colalias_b{};\n"
                           "auto rows = storage.with(cte<c>(\"a\", \"A\").as(select(columns(1 >>= c_a, 2 >>= "
                           "c_A))), select(column<c>(c_a)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"WITH: requires SQLite ≥ 3.8.3, sqlite_orm built with SQLITE_ORM_WITH_CTE, and `using namespace "
                 "sqlite_orm::literals` scope for `_ctealias`"}});
}

TEST_CASE("codegen: WITH cpp20_monikers - CTE column names differing only in case are declared apart") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["with_cte_style"] = "cpp20_monikers";
    const auto result = generateWithPolicySuppressWithCteDp("WITH c(a, A) AS (SELECT 1, 2) SELECT a FROM c;", policy);
    REQUIRE(result.code == "using namespace sqlite_orm::literals;\n"
                           "constexpr orm_cte_moniker auto c_cte = \"c\"_cte;\n"
                           "constexpr orm_column_alias auto c__a = \"a\"_col;\n"
                           "constexpr orm_column_alias auto c__A = \"A\"_col;\n"
                           "auto rows = storage.with(c_cte(c__a, c__A).as(select(columns(1, 2))), "
                           "select(c_cte->*c__a));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"WITH: cpp20_monikers requires C++20, SQLITE_ORM_WITH_CPP20_ALIASES, and matching sqlite_orm"},
                {"WITH: requires SQLite ≥ 3.8.3, sqlite_orm built with SQLITE_ORM_WITH_CTE, and `using namespace "
                 "sqlite_orm::literals` scope for `_ctealias`"}});
}

namespace {
    const std::string kCteSelectPlaceholder = "/* WITH: CTE SELECT not mapped to sqlite_orm */";
    const std::string kGroupBySubqueryWarning = "GROUP BY in subquery is not yet mapped to sqlite_orm select(...)";

    CodegenWarning cteSelectNotMapped(std::string_view cteName, SourceLocation location, size_t length) {
        return CodegenWarning{"the SELECT of CTE " + std::string(cteName) +
                                  " is not mapped to a sqlite_orm select(...) subexpression, so the statement "
                                  "reading it is not generated",
                              location,
                              length};
    }
}

// Without storage.with() nothing declares a CTE, so a statement whose CTE body has no select(...)
// form stands as a placeholder of its own rather than reading `C`, a struct no schema declares.
// SQLite runs every one of these (checked on sqlite3 3.51.0).
TEST_CASE("codegen: WITH - a CTE body with GROUP BY placeholds the statement") {
    const auto result = generateFull("WITH c AS (SELECT count(*) FROM users GROUP BY name) SELECT * FROM c;");
    REQUIRE(result.code == kCteSelectPlaceholder);
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{{kGroupBySubqueryWarning}, cteSelectNotMapped("c", SourceLocation{1, 12}, 40)});
    REQUIRE(result.decisionPoints.empty());
    REQUIRE(result.errors.empty());
}

TEST_CASE("codegen: WITH - a CTE body with HAVING and no GROUP BY placeholds the statement") {
    const auto result = generateFull("WITH c AS (SELECT count(*) FROM users HAVING count(*) > 1) SELECT * FROM c;");
    REQUIRE(result.code == kCteSelectPlaceholder);
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{{"HAVING without GROUP BY in subquery is not mapped to sqlite_orm select(...)"},
                                        cteSelectNotMapped("c", SourceLocation{1, 12}, 46)});
    REQUIRE(result.decisionPoints.empty());
    REQUIRE(result.errors.empty());
}

// The first CTE maps and offers a decision point of its own; it is dropped with the statement,
// since none of the code it was made for is generated.
TEST_CASE("codegen: WITH - one unmapped CTE of two placeholds the statement") {
    const auto result = generateFull(
        "WITH c AS (SELECT id FROM users), d AS (SELECT name FROM users GROUP BY name) SELECT * FROM c, d;");
    REQUIRE(result.code == kCteSelectPlaceholder);
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{{kGroupBySubqueryWarning}, cteSelectNotMapped("d", SourceLocation{1, 41}, 36)});
    REQUIRE(result.decisionPoints.empty());
    REQUIRE(result.errors.empty());
}

TEST_CASE("codegen: WITH - an unmapped CTE body placeholds a DML statement") {
    const auto result =
        generateFull("WITH c AS (SELECT name FROM users GROUP BY name) INSERT INTO users (name) SELECT name FROM c;");
    REQUIRE(result.code == kCteSelectPlaceholder);
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{{kGroupBySubqueryWarning}, cteSelectNotMapped("c", SourceLocation{1, 12}, 36)});
    REQUIRE(result.decisionPoints.empty());
    REQUIRE(result.errors.empty());
}

// A CTE shadows a table of the same name, so dropping the WITH would read the table instead — code
// that compiles and answers other rows.
TEST_CASE("codegen: WITH - an unmapped CTE body shadowing a table placeholds the statement") {
    const auto result = generateLastOfBatch("CREATE TABLE users (id INTEGER, name TEXT);\n"
                                            "CREATE TABLE t (z INTEGER);\n"
                                            "WITH t AS (SELECT name FROM users GROUP BY name) SELECT * FROM t;");
    REQUIRE(result.code == kCteSelectPlaceholder);
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{{kGroupBySubqueryWarning}, cteSelectNotMapped("t", SourceLocation{3, 12}, 36)});
    REQUIRE(result.decisionPoints.empty());
    REQUIRE(result.errors.empty());
}

// sqlite_orm's select(...) embeds no CTE, and dropping the WITH clause would leave the SELECT
// reading the CTE as a table, so a SELECT with a WITH of its own is not mapped. SQLite takes one as
// the source of an INSERT and as the body of a view (checked on sqlite3 3.51.0).
TEST_CASE("codegen: a WITH in the SELECT an INSERT reads from leaves the INSERT unmapped") {
    const auto result = generateFull("INSERT INTO users (name) WITH c AS (SELECT name FROM users) SELECT name FROM c;");
    REQUIRE(result.code == "/* INSERT ... SELECT: inner SELECT not mapped to sqlite_orm */");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"nested WITH in subquery: sqlite_orm select(...) cannot embed CTEs, so the SELECT is not mapped"},
                {"the SELECT an INSERT reads from is not mapped to sqlite_orm codegen", SourceLocation{1, 26}, 53}});
    REQUIRE(result.errors.empty());
}

TEST_CASE("codegen: a WITH in the body of a view leaves the view ungenerated") {
    const auto result = generateLastOfBatch("CREATE TABLE users (id INTEGER, name TEXT);\n"
                                            "CREATE VIEW v AS WITH c AS (SELECT name FROM users) SELECT name FROM c;");
    REQUIRE(result.code == "/* CREATE VIEW v \xe2\x80\x94 not supported for sqlite_orm */");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"nested WITH in subquery: sqlite_orm select(...) cannot embed CTEs, so the SELECT is not mapped"},
                {"CREATE VIEW v: SELECT is not supported for sqlite_orm code generation"}});
    REQUIRE(result.errors.empty());
}
