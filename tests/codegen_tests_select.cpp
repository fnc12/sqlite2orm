#include "codegen_tests_common.hpp"

namespace {

    /** Must match `kCommentCpp20ColumnAliases` in codegen.cpp (C++20 `_col` aliases). */
    const char* const kExpectedCpp20ColumnAliasComment =
        "C++20 literal column aliases (`orm_column_alias`, string literal `_col`) require sqlite_orm to be "
        "built with the preprocessor macro SQLITE_ORM_WITH_CPP20_ALIASES defined. Your project may enable "
        "that via CMake target_compile_definitions, compiler `-D`, a config header, or any other suitable "
        "mechanism.";

}  // namespace

TEST_CASE("codegen: SELECT * FROM table") {
    REQUIRE(generateFull("SELECT * FROM users") ==
            CodeGenResult{"auto rows = storage.get_all<Users>();",
                          {apiLevelStarSelectDp(1, "Users", "")},
                          {}});
}

TEST_CASE("codegen: SELECT table.* FROM table") {
    auto result = generate("SELECT users.* FROM users");
    REQUIRE(result == "auto rows = storage.select(asterisk<Users>());");
}

TEST_CASE("codegen: SELECT table.* with WHERE") {
    auto result = generate("SELECT users.* FROM users WHERE id = 1");
    REQUIRE(result == "auto rows = storage.select(asterisk<Users>(), where(c(&Users::id) == 1));");
}

TEST_CASE("codegen: SELECT column and table.*") {
    auto result = generate("SELECT id, users.* FROM users");
    REQUIRE(result == "auto rows = storage.select(columns(&Users::id, asterisk<Users>()));");
}

TEST_CASE("codegen: SELECT DISTINCT table.*") {
    auto result = generate("SELECT DISTINCT users.* FROM users");
    REQUIRE(result == "auto rows = storage.select(distinct(asterisk<Users>()));");
}

TEST_CASE("codegen: SELECT schema.table.* generates asterisk with warning") {
    auto result = generateFull("SELECT main.users.* FROM users");
    REQUIRE(result.code == "auto rows = storage.select(asterisk<Users>());");
    REQUIRE(result.warnings ==
        std::vector<CodegenWarning>{
            "schema-qualified SELECT result column main.users.* is not represented in sqlite_orm; generated code uses "
            "asterisk<Users>() (table type only)"});
}

TEST_CASE("codegen: SELECT column via FROM table alias") {
    auto result = generate("SELECT u.name FROM users u");
    REQUIRE(result == "auto rows = storage.select(alias_column<alias_a<Users>>(&Users::name));");
}

TEST_CASE("codegen: SELECT t.* FROM table alias uses asterisk with alias type") {
    auto result = generate("SELECT t.* FROM users t");
    REQUIRE(result == "auto rows = storage.select(asterisk<alias_a<Users>>());");
}

TEST_CASE("codegen: self-join with aliases") {
    auto result = generate("SELECT a.name, b.name FROM users a, users b");
    REQUIRE(result == "auto rows = storage.select(columns(alias_column<alias_a<Users>>(&Users::name), "
                      "alias_column<alias_b<Users>>(&Users::name)), cross_join<alias_b<Users>>());");
}

TEST_CASE("codegen: SELECT column via FROM table alias C++20 style") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["table_alias_style"] = "cpp20";
    auto result = generateWithPolicy("SELECT u.name FROM users u", pol);
    REQUIRE(result.code ==
            "constexpr orm_table_alias auto u = \"u\"_alias.for_<Users>();\n"
            "auto rows = storage.select(u->*&Users::name);");
}

TEST_CASE("codegen: SELECT t.* FROM alias C++20 style") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["table_alias_style"] = "cpp20";
    auto result = generateWithPolicy("SELECT t.* FROM users t", pol);
    REQUIRE(result.code ==
            "constexpr orm_table_alias auto t = \"t\"_alias.for_<Users>();\n"
            "auto rows = storage.select(asterisk<t>());");
}

TEST_CASE("codegen: self-join C++20 style") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["table_alias_style"] = "cpp20";
    auto result = generateWithPolicy("SELECT a.name, b.name FROM users a, users b", pol);
    REQUIRE(result.code ==
            "constexpr orm_table_alias auto a = \"a\"_alias.for_<Users>();\n"
            "constexpr orm_table_alias auto b = \"b\"_alias.for_<Users>();\n"
            "auto rows = storage.select(columns(a->*&Users::name, b->*&Users::name), cross_join<b>());");
}

TEST_CASE("codegen: table_alias_style decision point present when alias used") {
    auto result = generateFull("SELECT u.name FROM users u");
    bool hasDp = false;
    for(const auto& dp : result.decisionPoints) {
        if(dp.category == "table_alias_style") {
            hasDp = true;
        }
    }
    REQUIRE(hasDp);
}

TEST_CASE("codegen: SELECT with FROM schema qualifier warns") {
    REQUIRE(generateFull("SELECT name FROM main.users") ==
        CodeGenResult{"auto rows = storage.select(&Users::name);",
                      {columnRefStyleDp(1, "&Users::name")},
                      std::vector<CodegenWarning>{
                          "FROM clause schema qualifier 'main' for table 'users' is not represented in sqlite_orm "
                          "mapping"}});
}

TEST_CASE("codegen: comma-separated FROM is cross join") {
    REQUIRE(generateFull("SELECT * FROM users, posts") ==
        CodeGenResult{"auto rows = storage.get_all<Users>(cross_join<Posts>());",
                      {apiLevelStarSelectDp(1, "Users", "cross_join<Posts>()")},
                      {}});
}

TEST_CASE("codegen: INNER JOIN ON") {
    REQUIRE(generate("SELECT * FROM users INNER JOIN posts ON users.id = posts.user_id") ==
        "auto rows = storage.get_all<Users>(inner_join<Posts>(on(c(&Users::id) == &Posts::user_id)));");
}

TEST_CASE("codegen: LEFT JOIN and JOIN plain") {
    REQUIRE(generate("SELECT * FROM users LEFT JOIN posts ON users.id = posts.user_id") ==
        "auto rows = storage.get_all<Users>(left_join<Posts>(on(c(&Users::id) == &Posts::user_id)));");
    REQUIRE(generate("SELECT * FROM users JOIN posts ON users.id = posts.user_id") ==
        "auto rows = storage.get_all<Users>(join<Posts>(on(c(&Users::id) == &Posts::user_id)));");
}

TEST_CASE("codegen: JOIN ON merges expr_style decision point with is_equal alternative") {
    std::vector<DecisionPoint> joinDps;
    joinDps.push_back(apiLevelStarSelectDp(
        1, "Users", "left_join<Posts>(on(c(&Users::id) == &Posts::user_id))"));
    auto onExpr = expectedBinaryLeaf("&Users::id", "&Posts::user_id", " == ", "is_equal", 2);
    joinDps.insert(joinDps.end(), onExpr.decisionPoints.begin(), onExpr.decisionPoints.end());
    REQUIRE(generateFull("SELECT * FROM users LEFT JOIN posts ON users.id = posts.user_id") ==
        CodeGenResult{
            "auto rows = storage.get_all<Users>(left_join<Posts>(on(c(&Users::id) == &Posts::user_id)));",
            std::move(joinDps),
            {}});
}

TEST_CASE("codegen: parenthesized join in FROM") {
    REQUIRE(generate("SELECT * FROM (t1 INNER JOIN t2 ON t1.id = t2.t1_id)") ==
        "auto rows = storage.get_all<T1>(inner_join<T2>(on(c(&T1::id) == &T2::t1_id)));");
}

TEST_CASE("codegen: LEFT OUTER JOIN CROSS JOIN NATURAL JOIN") {
    REQUIRE(generate("SELECT * FROM users LEFT OUTER JOIN posts ON users.id = posts.user_id") ==
        "auto rows = storage.get_all<Users>(left_outer_join<Posts>(on(c(&Users::id) == &Posts::user_id)));");
    REQUIRE(generate("SELECT * FROM users CROSS JOIN posts") ==
        "auto rows = storage.get_all<Users>(cross_join<Posts>());");
    REQUIRE(generate("SELECT * FROM users NATURAL JOIN posts") ==
        "auto rows = storage.get_all<Users>(natural_join<Posts>());");
}

TEST_CASE("codegen: INNER JOIN USING one column") {
    REQUIRE(generate("SELECT * FROM users INNER JOIN posts USING (user_id)") ==
        "auto rows = storage.get_all<Users>(inner_join<Posts>(using_(&Posts::user_id)));");
}

TEST_CASE("codegen: INNER JOIN USING multiple columns") {
    REQUIRE(generate("SELECT * FROM t1 INNER JOIN t2 USING (a, b)") ==
        "auto rows = storage.get_all<T1>(inner_join<T2>(on(c(&T1::a) == c(&T2::a) and c(&T1::b) == c(&T2::b))));");
}

TEST_CASE("codegen: SELECT column FROM table") {
    auto result = generate("SELECT name FROM users");
    REQUIRE(result == "auto rows = storage.select(&Users::name);");
}

TEST_CASE("codegen: SELECT multiple columns") {
    auto result = generate("SELECT id, name FROM users");
    REQUIRE(result == "auto rows = storage.select(columns(&Users::id, &Users::name));");
}

TEST_CASE("codegen: SELECT with WHERE") {
    auto result = generate("SELECT * FROM users WHERE age > 18");
    REQUIRE(result == "auto rows = storage.get_all<Users>(where(c(&Users::age) > 18));");
}

TEST_CASE("codegen: SELECT column with WHERE") {
    auto result = generate("SELECT name FROM users WHERE id = 1");
    REQUIRE(result == "auto rows = storage.select(&Users::name, where(c(&Users::id) == 1));");
}

TEST_CASE("codegen: SELECT DISTINCT") {
    auto result = generate("SELECT DISTINCT name FROM users");
    REQUIRE(result == "auto rows = storage.select(distinct(&Users::name));");
}

TEST_CASE("codegen: SELECT DISTINCT multiple columns") {
    auto result = generate("SELECT DISTINCT id, name FROM users");
    REQUIRE(result == "auto rows = storage.select(distinct(columns(&Users::id, &Users::name)));");
}

TEST_CASE("codegen: SELECT expression with WHERE") {
    auto result = generate("SELECT id, name FROM users WHERE age >= 18 AND active = 1");
    REQUIRE(result == "auto rows = storage.select(columns(&Users::id, &Users::name), where(c(&Users::age) >= 18 and c(&Users::active) == 1));");
}

TEST_CASE("codegen: SELECT with ORDER BY") {
    auto result = generate("SELECT * FROM users ORDER BY name");
    REQUIRE(result == "auto rows = storage.get_all<Users>(order_by(&Users::name));");
}

TEST_CASE("codegen: SELECT with ORDER BY ASC") {
    auto result = generate("SELECT * FROM users ORDER BY name ASC");
    REQUIRE(result == "auto rows = storage.get_all<Users>(order_by(&Users::name).asc());");
}

TEST_CASE("codegen: SELECT with ORDER BY DESC") {
    auto result = generate("SELECT * FROM users ORDER BY age DESC");
    REQUIRE(result == "auto rows = storage.get_all<Users>(order_by(&Users::age).desc());");
}

TEST_CASE("codegen: SELECT with multiple ORDER BY") {
    auto result = generate("SELECT * FROM users ORDER BY name ASC, age DESC");
    REQUIRE(result == "auto rows = storage.get_all<Users>(multi_order_by(order_by(&Users::name).asc(), order_by(&Users::age).desc()));");
}

TEST_CASE("codegen: SELECT column with ORDER BY") {
    auto result = generate("SELECT name FROM users ORDER BY name DESC");
    REQUIRE(result == "auto rows = storage.select(&Users::name, order_by(&Users::name).desc());");
}

TEST_CASE("codegen: SELECT with LIMIT") {
    auto result = generate("SELECT * FROM users LIMIT 10");
    REQUIRE(result == "auto rows = storage.get_all<Users>(limit(10));");
}

TEST_CASE("codegen: SELECT with LIMIT OFFSET") {
    auto result = generate("SELECT * FROM users LIMIT 10 OFFSET 5");
    REQUIRE(result == "auto rows = storage.get_all<Users>(limit(10, offset(5)));");
}

// `LIMIT -1` is how SQLite spells "no limit"; it used to leave `limit(...)` out of the generated
// code altogether while reporting a parse error on the minus sign.
TEST_CASE("codegen: SELECT with negative LIMIT") {
    auto result = generate("SELECT * FROM users LIMIT -1");
    REQUIRE(result == "auto rows = storage.get_all<Users>(limit(-1));");
}

TEST_CASE("codegen: SELECT with negative OFFSET") {
    auto result = generate("SELECT * FROM users LIMIT 10 OFFSET -1");
    REQUIRE(result == "auto rows = storage.get_all<Users>(limit(10, offset(-1)));");
}

TEST_CASE("codegen: SELECT with negative LIMIT and OFFSET") {
    auto result = generate("SELECT * FROM users LIMIT -1 OFFSET 2");
    REQUIRE(result == "auto rows = storage.get_all<Users>(limit(-1, offset(2)));");
}

TEST_CASE("codegen: SELECT with computed LIMIT") {
    auto result = generate("SELECT * FROM users LIMIT 2 * 3");
    REQUIRE(result == "auto rows = storage.get_all<Users>(limit(c(2) * 3));");
}

// SQLite's `LIMIT <offset>, <count>` maps to sqlite_orm's explicit offset form.
TEST_CASE("codegen: SELECT with LIMIT offset comma count") {
    auto result = generate("SELECT * FROM users LIMIT 5, 10");
    REQUIRE(result == "auto rows = storage.get_all<Users>(limit(10, offset(5)));");
}

// The clause takes a whole expression now, so the expression generator's diagnostics have to reach
// the statement: generating it outside them left a silent `limit(/* (SELECT ...) */)` behind.
TEST_CASE("codegen: warnings from the LIMIT expression reach the statement") {
    REQUIRE(generateFull("SELECT name FROM users LIMIT 1 COLLATE NOCASE") ==
            CodeGenResult{"auto rows = storage.select(&Users::name, limit(1));",
                          {columnRefStyleDp(1, "&Users::name")},
                          {"COLLATE NOCASE on expressions is not directly supported in sqlite_orm codegen"}});
}

TEST_CASE("codegen: warnings from the OFFSET expression reach the statement") {
    REQUIRE(generateFull("SELECT name FROM users LIMIT 1 OFFSET 2 COLLATE NOCASE") ==
            CodeGenResult{"auto rows = storage.select(&Users::name, limit(1, offset(2)));",
                          {columnRefStyleDp(1, "&Users::name")},
                          {"COLLATE NOCASE on expressions is not directly supported in sqlite_orm codegen"}});
}

// A subquery LIMIT is generated by a second code path with its own diagnostics list.
TEST_CASE("codegen: warnings from a subquery LIMIT expression reach the statement") {
    REQUIRE(generateFull("SELECT (SELECT name FROM users LIMIT 1 COLLATE NOCASE) FROM users") ==
            CodeGenResult{"auto rows = storage.select(select(&Users::name, limit(1)));",
                          {columnRefStyleDp(1, "&Users::name")},
                          {"COLLATE NOCASE on expressions is not directly supported in sqlite_orm codegen"}});
}

// `limit()` with an empty argument list does not compile, so the unmapped subquery has to be said
// out loud instead of being emitted as clean-looking code with nothing on stderr.
TEST_CASE("codegen: an unmapped subquery in LIMIT is warned about") {
    REQUIRE(generateFull("SELECT name FROM users LIMIT (SELECT a FROM users GROUP BY a)") ==
            CodeGenResult{"auto rows = storage.select(&Users::name, limit(/* (SELECT ...) */));",
                          {columnRefStyleDp(1, "&Users::name")},
                          {"scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen",
                           "GROUP BY in subquery is not yet mapped to sqlite_orm select(...)"}});
}

TEST_CASE("codegen: SELECT with GROUP BY") {
    auto result = generate("SELECT name, count(*) FROM users GROUP BY name");
    REQUIRE(result == "auto rows = storage.select(columns(&Users::name, count<Users>()), group_by(&Users::name));");
}

TEST_CASE("codegen: SELECT with GROUP BY HAVING") {
    auto result = generate("SELECT name, count(*) FROM users GROUP BY name HAVING count(*) > 1");
    REQUIRE(result ==
            "auto rows = storage.select(columns(&Users::name, count<Users>()), "
            "group_by(&Users::name).having(count<Users>() > 1));");
}

TEST_CASE("codegen: SELECT with WHERE + ORDER BY + LIMIT") {
    auto result = generate("SELECT * FROM users WHERE age > 18 ORDER BY name LIMIT 10");
    REQUIRE(result == "auto rows = storage.get_all<Users>(where(c(&Users::age) > 18), order_by(&Users::name), limit(10));");
}

TEST_CASE("codegen: EXISTS (SELECT *)") {
    REQUIRE(generate("EXISTS (SELECT * FROM users)") == "exists(select(asterisk<Users>()))");
}

TEST_CASE("codegen: scalar subquery with COUNT(*)") {
    REQUIRE(generate("(SELECT COUNT(*) FROM t)") == "select(count<T>())");
}

TEST_CASE("codegen: IN with subquery") {
    REQUIRE(generate("id IN (SELECT id FROM users)") == "in(&User::id, select(&Users::id))");
}

TEST_CASE("codegen: comparison to scalar MAX subquery") {
    auto result = generateFull("id > (SELECT MAX(x) FROM t)");
    REQUIRE(result.code == "c(&User::id) > select(max(&T::x))");
}

TEST_CASE("codegen: UNION two literal SELECTs") {
    REQUIRE(generate("SELECT 1 UNION SELECT 2") == "auto rows = storage.select(union_(select(1), select(2)));");
}

TEST_CASE("codegen: UNION ALL") {
    REQUIRE(generate("SELECT 1 UNION ALL SELECT 2") ==
            "auto rows = storage.select(union_all(select(1), select(2)));");
}

TEST_CASE("codegen: INTERSECT") {
    REQUIRE(generate("SELECT 1 INTERSECT SELECT 2") ==
            "auto rows = storage.select(intersect(select(1), select(2)));");
}

TEST_CASE("codegen: EXCEPT") {
    REQUIRE(generate("SELECT 1 EXCEPT SELECT 2") == "auto rows = storage.select(except(select(1), select(2)));");
}

TEST_CASE("codegen: derived FROM emits stub and warning") {
    REQUIRE(generateFull("SELECT n FROM (SELECT 1 AS n) t") ==
            CodeGenResult{"/* SELECT with derived FROM */",
                          {},
                          {"subselect in FROM is not supported in sqlite_orm codegen"}});
}

TEST_CASE("codegen: ORDER BY COLLATE") {
    REQUIRE(generate("SELECT * FROM users ORDER BY name COLLATE NOCASE;") ==
        "auto rows = storage.get_all<Users>(order_by(&Users::name).collate_nocase());");
}

TEST_CASE("codegen: VALUES standalone") {
    REQUIRE(generate("VALUES (1, 'a'), (2, 'b');") ==
        "auto rows = storage.select(columns(1, \"a\"));");
}

TEST_CASE("codegen: SELECT single column with alias") {
    auto result = generateFull("SELECT name AS user_name FROM users");
    REQUIRE(result.code ==
        "struct User_nameAlias : sqlite_orm::alias_tag {\n"
        "    static const std::string& get() {\n"
        "        static const std::string res = \"user_name\";\n"
        "        return res;\n"
        "    }\n"
        "};\n"
        "auto rows = storage.select(as<User_nameAlias>(&Users::name));");
    REQUIRE(!result.warnings.empty());
}

TEST_CASE("codegen: SELECT multiple columns with one alias") {
    auto result = generate("SELECT id, name AS user_name FROM users");
    REQUIRE(result ==
        "struct User_nameAlias : sqlite_orm::alias_tag {\n"
        "    static const std::string& get() {\n"
        "        static const std::string res = \"user_name\";\n"
        "        return res;\n"
        "    }\n"
        "};\n"
        "auto rows = storage.select(columns(&Users::id, as<User_nameAlias>(&Users::name)));");
}

TEST_CASE("codegen: SELECT column with string literal alias") {
    auto result = generate("SELECT name AS 'UserName' FROM users");
    REQUIRE(result ==
        "struct UserNameAlias : sqlite_orm::alias_tag {\n"
        "    static const std::string& get() {\n"
        "        static const std::string res = \"UserName\";\n"
        "        return res;\n"
        "    }\n"
        "};\n"
        "auto rows = storage.select(as<UserNameAlias>(&Users::name));");
}

TEST_CASE("codegen: SELECT DISTINCT column with alias") {
    auto result = generate("SELECT DISTINCT name AS user_name FROM users");
    REQUIRE(result ==
        "struct User_nameAlias : sqlite_orm::alias_tag {\n"
        "    static const std::string& get() {\n"
        "        static const std::string res = \"user_name\";\n"
        "        return res;\n"
        "    }\n"
        "};\n"
        "auto rows = storage.select(distinct(as<User_nameAlias>(&Users::name)));");
}

TEST_CASE("codegen: SELECT DISTINCT multiple columns with aliases") {
    auto result = generate("SELECT DISTINCT id AS ID, name AS user_name FROM users");
    REQUIRE(result ==
        "struct IDAlias : sqlite_orm::alias_tag {\n"
        "    static const std::string& get() {\n"
        "        static const std::string res = \"ID\";\n"
        "        return res;\n"
        "    }\n"
        "};\n"
        "struct User_nameAlias : sqlite_orm::alias_tag {\n"
        "    static const std::string& get() {\n"
        "        static const std::string res = \"user_name\";\n"
        "        return res;\n"
        "    }\n"
        "};\n"
        "auto rows = storage.select(distinct(columns(as<IDAlias>(&Users::id), as<User_nameAlias>(&Users::name))));");
}

TEST_CASE("codegen: SELECT without alias is unchanged") {
    auto result = generate("SELECT id, name FROM users");
    REQUIRE(result == "auto rows = storage.select(columns(&Users::id, &Users::name));");
}

TEST_CASE("codegen: SELECT implicit column alias (without AS)") {
    auto result = generateFull("SELECT name user_name FROM users");
    REQUIRE(result.code ==
        "struct User_nameAlias : sqlite_orm::alias_tag {\n"
        "    static const std::string& get() {\n"
        "        static const std::string res = \"user_name\";\n"
        "        return res;\n"
        "    }\n"
        "};\n"
        "auto rows = storage.select(as<User_nameAlias>(&Users::name));");
}

TEST_CASE("codegen: SELECT builtin colalias for single-letter alias") {
    auto result = generateFull("SELECT name AS i FROM users");
    REQUIRE(result.code == "auto rows = storage.select(as<colalias_i>(&Users::name));");
    bool hasBuiltinWarning = false;
    for(const auto& w : result.warnings) {
        if(w.message.find("colalias_") != std::string::npos) hasBuiltinWarning = true;
    }
    REQUIRE(hasBuiltinWarning);
}

TEST_CASE("codegen: SELECT alias referenced in WHERE and ORDER BY") {
    auto result = generate(
        "SELECT name, instr(abilities, 'o') i "
        "FROM marvel "
        "WHERE i > 0 "
        "ORDER BY i");
    REQUIRE(result ==
        "auto rows = storage.select("
        "columns(&Marvel::name, as<colalias_i>(as_optional(instr(&Marvel::abilities, \"o\")))), "
        "where(c(get<colalias_i>()) > 0), "
        "order_by(get<colalias_i>()));");
}

TEST_CASE("codegen: SELECT alias referenced in ORDER BY with custom struct") {
    auto result = generate(
        "SELECT name AS user_name FROM users ORDER BY user_name");
    REQUIRE(result ==
        "struct User_nameAlias : sqlite_orm::alias_tag {\n"
        "    static const std::string& get() {\n"
        "        static const std::string res = \"user_name\";\n"
        "        return res;\n"
        "    }\n"
        "};\n"
        "auto rows = storage.select(as<User_nameAlias>(&Users::name), "
        "order_by(get<User_nameAlias>()));");
}

TEST_CASE("codegen: column_alias_style decision point offers C++20 alternative") {
    const std::string chosenCode = "auto rows = storage.select(as<colalias_i>(&Users::name));";
    const std::string cpp20AltCode = "constexpr orm_column_alias auto i = \"i\"_col;\n"
                                     "auto rows = storage.select(as<i>(&Users::name));";
    REQUIRE(generateFull("SELECT name AS i FROM users") ==
            CodeGenResult{
                chosenCode,
                {columnRefStyleDp(1, "&Users::name"),
                 DecisionPoint{2,
                               "column_alias_style",
                               "alias_tag",
                               chosenCode,
                               {Option{"alias_tag", chosenCode,
                                           "alias_tag / colalias_* / generated struct (default; wider compiler "
                                           "support)"},
                                Option{"cpp20_literal",
                                           cpp20AltCode,
                                           "C++20 literal aliases (`orm_column_alias`, `_col`)",
                                           false,
                                           {std::string(kExpectedCpp20ColumnAliasComment)},
                                           20}}}},
                {"SELECT column alias uses sqlite_orm built-in colalias_* types; requires `using namespace sqlite_orm`"},
                {},
                {}});
}

TEST_CASE("codegen: column_alias_style cpp20_literal policy") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["column_alias_style"] = "cpp20_literal";
    const char* sql =
        "SELECT name, instr(abilities, 'o') i "
        "FROM marvel "
        "WHERE i > 0 "
        "ORDER BY i";

    const std::string mainCode =
        "constexpr orm_column_alias auto i = \"i\"_col;\n"
        "auto rows = storage.select(columns(&Marvel::name, as<i>(as_optional(instr(&Marvel::abilities, \"o\")))), "
        "where(i > 0), order_by(i));";
    const std::string aliasTagAltCode =
        "auto rows = storage.select(columns(&Marvel::name, "
        "as<colalias_i>(as_optional(instr(&Marvel::abilities, \"o\")))), "
        "where(c(get<colalias_i>()) > 0), order_by(get<colalias_i>()));";

    REQUIRE(generateWithPolicy(sql, policy) ==
            CodeGenResult{
                mainCode,
                {columnRefStyleDp(1, "&Marvel::name"),
                 columnRefStyleDp(2, "&Marvel::abilities"),
                 DecisionPoint{3,
                               "expr_style",
                               "operator_wrap_left",
                               "i > 0",
                               {Option{"operator_wrap_left", "i > 0", "wrap left operand"},
                                Option{"operator_wrap_right", "i > c(0)", "wrap right operand"},
                                Option{"functional", "greater_than(i, 0)", "functional style"},
                                Option{"operator_wrap_both", "i > c(0)", "wrap both operands", true}}},
                 DecisionPoint{4,
                               "column_alias_style",
                               "cpp20_literal",
                               mainCode,
                               {Option{"alias_tag",
                                           aliasTagAltCode,
                                           "alias_tag / colalias_* / generated struct (default; wider compiler "
                                           "support)"},
                                Option{"cpp20_literal", mainCode,
                                           "C++20 literal aliases (`orm_column_alias`, `_col`)",
                                           false, {}, 20}}}},
                {},
                {},
                 {std::string(kExpectedCpp20ColumnAliasComment)}});
}

TEST_CASE("codegen: SELECT with window function OVER(), bind params in WHERE and LIMIT") {
    auto result = generate(
        "SELECT id, firstName, lastName, count(id) OVER() FROM user_profile WHERE id > :refId ORDER BY id LIMIT :resultperpage;");
    REQUIRE(result == "auto rows = storage.select(columns(&UserProfile::id, &UserProfile::firstName, &UserProfile::lastName, count(&UserProfile::id).over()), where(c(&UserProfile::id) > refId), order_by(&UserProfile::id), limit(resultperpage));");
}

TEST_CASE("codegen: MATCH against a column") {
    auto result = generate("SELECT * FROM docs WHERE body MATCH 'sqlite'");
    REQUIRE(result == "auto rows = storage.get_all<Docs>(where(match(&Docs::body, \"sqlite\")));");
}

TEST_CASE("codegen: MATCH against the FTS5 table name uses the hidden any column") {
    auto result = generateFull("SELECT * FROM docs_search WHERE docs_search MATCH 'sqlite'");
    REQUIRE(result.code ==
            "auto rows = storage.get_all<DocsSearch>(where(match(c<DocsSearch>()->*&fts5::hidden::any, "
            "\"sqlite\")));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{"MATCH against table \"docs_search\" maps to the hidden FTS5 'any' "
                                     "column; requires an FTS5 virtual table mapped as DocsSearch"});
}

TEST_CASE("codegen: MATCH against an aliased FTS5 table name") {
    auto result = generateFull("SELECT d.* FROM docs d JOIN docs_search s ON d.id = s.rowid WHERE docs_search MATCH 'word'");
    REQUIRE(result.code.find("match(c<DocsSearch>()->*&fts5::hidden::any, \"word\")") != std::string::npos);
}

namespace {
    const sqlite2orm::DecisionPoint* findDecisionPoint(const sqlite2orm::CodeGenResult& result,
                                                       std::string_view category) {
        for(const auto& dp : result.decisionPoints) {
            if(dp.category == category) {
                return &dp;
            }
        }
        return nullptr;
    }

    bool hasOptionValue(const sqlite2orm::DecisionPoint& dp, std::string_view value) {
        for(const auto& option : dp.options) {
            if(option.value == value) {
                return true;
            }
        }
        return false;
    }
}  // namespace

TEST_CASE("codegen: targetCppStandard 17 drops the C++20 column_alias option") {
    CodeGenPolicy policy;
    policy.targetCppStandard = 17;
    auto result = generateWithPolicy("SELECT name AS i FROM users", policy);
    const auto* dp = findDecisionPoint(result, "column_alias_style");
    REQUIRE(dp != nullptr);
    CHECK(dp->chosenValue == "alias_tag");
    CHECK(hasOptionValue(*dp, "alias_tag"));
    CHECK_FALSE(hasOptionValue(*dp, "cpp20_literal"));
    for(const auto& option : dp->options) {
        CHECK(option.minCppStandard <= 17);
    }
}

TEST_CASE("codegen: default targetCppStandard 20 keeps the C++20 column_alias option") {
    auto result = generateFull("SELECT name AS i FROM users");
    const auto* dp = findDecisionPoint(result, "column_alias_style");
    REQUIRE(dp != nullptr);
    CHECK(hasOptionValue(*dp, "cpp20_literal"));
}

TEST_CASE("codegen: targetCppStandard 17 drops the C++20 table_alias option") {
    CodeGenPolicy policy;
    policy.targetCppStandard = 17;
    auto result = generateWithPolicy("SELECT u.id FROM users u", policy);
    const auto* dp = findDecisionPoint(result, "table_alias_style");
    REQUIRE(dp != nullptr);
    CHECK(dp->chosenValue == "pre_cpp20");
    CHECK_FALSE(hasOptionValue(*dp, "cpp20"));
}

TEST_CASE("codegen: explicit C++20 table_alias policy is overridden by targetCppStandard 17") {
    CodeGenPolicy policy;
    policy.targetCppStandard = 17;
    policy.chosenAlternativeValueByCategory["table_alias_style"] = "cpp20";
    auto result = generateWithPolicy("SELECT u.id FROM users u", policy);
    const auto* dp = findDecisionPoint(result, "table_alias_style");
    REQUIRE(dp != nullptr);
    CHECK(dp->chosenValue == "pre_cpp20");
    CHECK_FALSE(hasOptionValue(*dp, "cpp20"));
}

// sqlite_orm types a binary operator from the operator alone — `double` for the arithmetic ones,
// `bool` for a comparison, `std::string` for `||` — and none of those can hold a NULL, so a NULL
// row used to reach the caller as 0 / false / "". `as_optional` leaves the SQL untouched and hands
// back a `std::optional` instead. A unary operator is typed the same way, whether it keeps its
// shape (`~x`) or becomes the subtraction `(c(0) - x)` a negation is generated as. Values checked
// against sqlite3 3.51 in "runtime: a result column that can be NULL reads the NULL back". The CAST
// inside the `~a` column is the int64 widening of
// "codegen: a bitwise result column is cast to an int64_t", which the widening rule here is
// independent of.
TEST_CASE("codegen: a result column that can be NULL is generated as as_optional") {
    REQUIRE(generate("SELECT a + 1 FROM users;") ==
            "auto rows = storage.select(as_optional(c(&Users::a) + 1));");
    REQUIRE(generate("SELECT a * 2 FROM users;") ==
            "auto rows = storage.select(as_optional(c(&Users::a) * 2));");
    REQUIRE(generate("SELECT 0 - a FROM users;") ==
            "auto rows = storage.select(as_optional(c(0) - &Users::a));");
    REQUIRE(generate("SELECT -a FROM users;") ==
            "auto rows = storage.select(as_optional((c(0) - c(&Users::a))));");
    REQUIRE(generate("SELECT ~a FROM users;") ==
            "auto rows = storage.select(as_optional(cast<int64_t>(~c(&Users::a))));");
    REQUIRE(generate("SELECT a > 0 FROM users;") ==
            "auto rows = storage.select(as_optional(c(&Users::a) > 0));");
    REQUIRE(generate("SELECT a AND 1 FROM users;") ==
            "auto rows = storage.select(as_optional(c(&Users::a) and 1));");
    REQUIRE(generate("SELECT a || 'x' FROM users;") ==
            "auto rows = storage.select(as_optional(c(&Users::a) || \"x\"));");
    REQUIRE(generate("SELECT NULL + 1;") == "auto rows = storage.select(as_optional(c(nullptr) + 1));");
    // SQLite answers a division by zero with NULL rather than an error, so even an expression whose
    // operands are both spelled out in the SQL can come back NULL.
    REQUIRE(generate("SELECT 1 / 0;") == "auto rows = storage.select(as_optional(c(1) / 0));");
    REQUIRE(generate("SELECT 1 % 0;") == "auto rows = storage.select(as_optional(c(1) % 0));");
}

// A COLLATE has no sqlite_orm form, so the result column comes out as the node under it comes out —
// and it is that node the widening is decided from. Looking at the COLLATE instead left the column
// unwidened, and a NULL row reached the caller as 0 where sqlite3 3.51 answers NULL. A NULL test or
// a plain column under the COLLATE is not widened, for the same reason it is not widened bare.
// Values checked in "runtime: a result column under a dropped COLLATE reads the NULL back".
TEST_CASE("codegen: a result column under a dropped COLLATE is widened like the node under it") {
    REQUIRE(generate("SELECT (a + 1) COLLATE BINARY FROM users;") ==
            "auto rows = storage.select(as_optional(c(&Users::a) + 1));");
    REQUIRE(generate("SELECT (a || 'x') COLLATE NOCASE FROM users;") ==
            "auto rows = storage.select(as_optional(c(&Users::a) || \"x\"));");
    REQUIRE(generate("SELECT (-a) COLLATE BINARY FROM users;") ==
            "auto rows = storage.select(as_optional((c(0) - c(&Users::a))));");
    REQUIRE(generate("SELECT (a IS NULL) COLLATE BINARY FROM users;") ==
            "auto rows = storage.select(is_null(&Users::a));");
    REQUIRE(generate("SELECT a COLLATE BINARY FROM users;") == "auto rows = storage.select(&Users::a);");
}

// Only what sqlite_orm cannot type nullably on its own is widened: a column carries its field's
// type, `abs(...)` is a `std::unique_ptr`, NULL itself is a `std::nullptr_t`, `IS NULL` is the test
// for a NULL and never is one, and an operator over literals alone has no NULL to report.
TEST_CASE("codegen: a result column that cannot be NULL keeps the type sqlite_orm gives it") {
    REQUIRE(generate("SELECT 1 + 2;") == "auto rows = storage.select(c(1) + 2);");
    REQUIRE(generate("SELECT 'a' || 'b';") == "auto rows = storage.select(c(\"a\") || \"b\");");
    REQUIRE(generate("SELECT a FROM users;") == "auto rows = storage.select(&Users::a);");
    REQUIRE(generate("SELECT NULL;") == "auto rows = storage.select(nullptr);");
    REQUIRE(generate("SELECT abs(a) FROM users;") == "auto rows = storage.select(abs(&Users::a));");
    REQUIRE(generate("SELECT a IS NULL FROM users;") == "auto rows = storage.select(is_null(&Users::a));");
}

// A NaN is the one value SQLite has no storage class for, so it stores one as NULL: `+`, `-` and
// `*` answer NULL as soon as the double they compute in runs into one — `SELECT typeof(0 * (1e300
// * 1e300))` and `SELECT typeof(1e300 * 1e300 - 1e300 * 1e300)` are both null in sqlite3 3.51 —
// however spelled out their operands are, the way `1 / 0` is. sqlite_orm types the arithmetic
// operators `double`, so that row used to reach the caller as 0. It takes an infinity to reach a
// NaN, and only a value the SQL no longer spells out, one a computation can overflow into, or a
// decimal literal whose own text runs past the double range, counts as one: an INTEGER answer is
// finite whatever its magnitude, and a finite number spelled out stays finite, so `1e300 * 1e300`
// (Inf, a REAL SQLite carries), `9e999`, `0 * 1e300`, `1.5 + 2.5`, `(1 + 2) * 0` and a zero times a
// comparison or a bitwise result are left plain. Values checked in "runtime: an arithmetic result
// column that overflows into a NaN reads the NULL back".
TEST_CASE("codegen: an arithmetic result column that can overflow into a NaN is widened") {
    REQUIRE(generate("SELECT 0 * (1e300 * 1e300);") ==
            "auto rows = storage.select(as_optional(c(0) * (c(1e300) * 1e300)));");
    REQUIRE(generate("SELECT 0.0 * (1e300 * 1e300);") ==
            "auto rows = storage.select(as_optional(c(0.0) * (c(1e300) * 1e300)));");
    REQUIRE(generate("SELECT (1e300 * 1e300) * 0;") ==
            "auto rows = storage.select(as_optional(c(1e300) * 1e300 * 0));");
    REQUIRE(generate("SELECT 1e300 * 1e300 - 1e300 * 1e300;") ==
            "auto rows = storage.select(as_optional(c(1e300) * 1e300 - c(1e300) * 1e300));");
    REQUIRE(generate("SELECT 1e300 * 1e300 + -1e300 * 1e300;") ==
            "auto rows = storage.select(as_optional(c(1e300) * 1e300 + c(-1e300) * 1e300));");
    REQUIRE(generate("SELECT (1e300 * 1e300) / (1e300 * 1e300);") ==
            "auto rows = storage.select(as_optional(c(1e300) * 1e300 / (c(1e300) * 1e300)));");
    // SQLite reads a number off the bytes of a blob the way it reads one off a string, and the
    // bytes of `x'41'` read back as 0, so this column is NULL too.
    REQUIRE(generate("SELECT x'41' * (1e300 * 1e300);") ==
            "auto rows = storage.select(as_optional(c(std::vector<char>{'\\x41'}) * (c(1e300) * 1e300)));");
    // A decimal literal whose exponent runs past the double range is an infinity of its own, and
    // it is read out of the literal's text rather than out of an int64 it does not fit, so these
    // pin the text-to-double path the two cells above reach through a magnitude bound instead.
    REQUIRE(generate("SELECT 9e999 - 9e999;") == "auto rows = storage.select(as_optional(c(9e999) - 9e999));");
    REQUIRE(generate("SELECT 0 * 9e999;") == "auto rows = storage.select(as_optional(c(0) * 9e999));");
    REQUIRE(generate("SELECT 1.0e400 - 1.0e400;") == "auto rows = storage.select(as_optional(c(1.0e400) - 1.0e400));");
    REQUIRE(generate("SELECT -9e999 + 9e999;") == "auto rows = storage.select(as_optional(c(-9e999) + 9e999));");
    // COLLATE decides how a value compares, not what the value is, so the literal under one is
    // read as the literal it is: a zero next to an infinity still reaches the NaN, a one does not.
    REQUIRE(generate("SELECT (0 COLLATE BINARY) * (1e300 * 1e300);") ==
            "auto rows = storage.select(as_optional(c(0) * (c(1e300) * 1e300)));");
    REQUIRE(generate("SELECT 0 * (9e999 COLLATE BINARY);") ==
            "auto rows = storage.select(as_optional(c(0) * 9e999));");
    REQUIRE(generate("SELECT (1 COLLATE BINARY) * (1e300 * 1e300);") ==
            "auto rows = storage.select(c(1) * (c(1e300) * 1e300));");
    REQUIRE(generate("SELECT 1e300 * 1e300;") == "auto rows = storage.select(c(1e300) * 1e300);");
    REQUIRE(generate("SELECT 9e999;") == "auto rows = storage.select(9e999);");
    REQUIRE(generate("SELECT 0 * 1e300;") == "auto rows = storage.select(c(0) * 1e300);");
    REQUIRE(generate("SELECT 1.5 + 2.5;") == "auto rows = storage.select(c(1.5) + 2.5);");
    REQUIRE(generate("SELECT (1 + 2) * 0;") == "auto rows = storage.select((c(1) + 2) * 0);");
    REQUIRE(generate("SELECT 0 * (1 < 2);") == "auto rows = storage.select(c(0) * (c(1) < 2));");
    REQUIRE(generate("SELECT 0 * (1 & 2);") == "auto rows = storage.select(c(0) * (c(1) & 2));");
}

// A NULL test and an EXISTS answer over a NULL operand too, so an operator built on one of them has
// no NULL to report either and keeps the type sqlite_orm gives it. Checked against sqlite3 3.45.1
// over `users(a INTEGER)` holding one NULL row: 2, 1, 0, 2, '1x', -2 — no NULL among them. (The
// `~` form has no sqlite_orm overload and does not compile, which is a separate gap and not what
// this widening rule decides.) The CAST around a NULL test under a binary operator is the grouping
// the serialized SQL needs, spelled out in
// "codegen: a predicate under an operator is cast to stay one SQL term"; the one around the whole
// `~` column is the int64 widening of "codegen: a bitwise result column is cast to an int64_t".
TEST_CASE("codegen: an operator over a NULL test or an EXISTS is not widened") {
    REQUIRE(generate("SELECT (a IS NULL) + 1 FROM users;") ==
            "auto rows = storage.select(cast<int64_t>(is_null(&Users::a)) + 1);");
    REQUIRE(generate("SELECT (a IS NOT NULL) + 1 FROM users;") ==
            "auto rows = storage.select(cast<int64_t>(is_not_null(&Users::a)) + 1);");
    REQUIRE(generate("SELECT NOT (a IS NULL) FROM users;") ==
            "auto rows = storage.select(not (is_null(&Users::a)));");
    REQUIRE(generate("SELECT EXISTS(SELECT 1) + 1 FROM users;") ==
            "auto rows = storage.select(exists(select(1)) + 1);");
    REQUIRE(generate("SELECT (a IS NULL) || 'x' FROM users;") ==
            "auto rows = storage.select(cast<int64_t>(is_null(&Users::a)) || \"x\");");
    REQUIRE(generate("SELECT ~(a IS NULL) FROM users;") ==
            "auto rows = storage.select(cast<int64_t>(~(is_null(&Users::a))));");
}

// sqlite_orm types a BETWEEN, an IN, a LIKE and a GLOB — and the `negated_condition_t` a NOT form
// comes out as — `bool`, a CAST the very type the CAST asks for, and a call of a built-in function
// the return type that function declares. None of those holds a NULL, so a NULL row reached the
// caller as false / "" / 0 where sqlite3 3.51 answers NULL. Values checked in "runtime: a result
// column typed by a predicate, a CAST or a function call reads the NULL back".
TEST_CASE("codegen: a result column typed by a predicate, a CAST or a function call is widened") {
    REQUIRE(generate("SELECT a BETWEEN 1 AND 9 FROM users;") ==
            "auto rows = storage.select(as_optional(between(&Users::a, 1, 9)));");
    REQUIRE(generate("SELECT a NOT BETWEEN 1 AND 9 FROM users;") ==
            "auto rows = storage.select(as_optional(!between(&Users::a, 1, 9)));");
    REQUIRE(generate("SELECT a IN (1, 7) FROM users;") ==
            "auto rows = storage.select(as_optional(in(&Users::a, {1, 7})));");
    REQUIRE(generate("SELECT a NOT IN (1, 7) FROM users;") ==
            "auto rows = storage.select(as_optional(not_in(&Users::a, {1, 7})));");
    // What a subquery holds is not spelled out in the SQL, so the right-hand side alone makes the
    // test nullable: `1 IN (SELECT a)` is NULL over a NULL row.
    REQUIRE(generate("SELECT a IN (SELECT 1) FROM users;") ==
            "auto rows = storage.select(as_optional(in(&Users::a, select(1))));");
    REQUIRE(generate("SELECT a LIKE 'x' FROM users;") ==
            "auto rows = storage.select(as_optional(like(&Users::a, \"x\")));");
    REQUIRE(generate("SELECT a GLOB 'x' FROM users;") ==
            "auto rows = storage.select(as_optional(glob(&Users::a, \"x\")));");
    REQUIRE(generate("SELECT CAST(a AS TEXT) FROM users;") ==
            "auto rows = storage.select(as_optional(cast<std::string>(&Users::a)));");
    REQUIRE(generate("SELECT CAST(a AS INTEGER) FROM users;") ==
            "auto rows = storage.select(as_optional(cast<int64_t>(&Users::a)));");
    REQUIRE(generate("SELECT length(a) FROM users;") ==
            "auto rows = storage.select(as_optional(length(&Users::a)));");
    REQUIRE(generate("SELECT upper(a) FROM users;") ==
            "auto rows = storage.select(as_optional(upper(&Users::a)));");
    // An aggregate SQLite answers NULL for over an empty rowset is widened whatever its argument
    // holds: `SELECT avg(1) FROM users` over no rows is NULL.
    REQUIRE(generate("SELECT avg(1) FROM users;") == "auto rows = storage.select(as_optional(avg(1)));");
    REQUIRE(generate("SELECT group_concat(a) FROM users;") ==
            "auto rows = storage.select(as_optional(group_concat(&Users::a)));");
    // Only the top-level node of a result column decides the type the row is read back with, and a
    // function call SQLite propagates a NULL through makes the operator over it nullable too.
    REQUIRE(generate("SELECT length(a) + 1 FROM users;") ==
            "auto rows = storage.select(as_optional(length(&Users::a) + 1));");
}

// A built-in answers NULL over arguments that hold none far more often than it merely propagates
// one, so a call is ruled nullable unless the function is known to answer NULL for no reason other
// than a NULL argument. Every expression here is NULL in libsqlite3 3.45.1, the version this
// project links, over arguments the SQL spells out; the math ones need that library rather than the
// `sqlite3` CLI in the image, which is built without the math functions. Read back in "runtime: a
// built-in that answers NULL over spelled-out arguments reads the NULL back".
TEST_CASE("codegen: a call of a built-in SQLite answers NULL for over spelled-out arguments is widened") {
    REQUIRE(generate("SELECT nullif(1, 1) + 1;") == "auto rows = storage.select(as_optional(nullif(1, 1) + 1));");
    REQUIRE(generate("SELECT date('bogus');") == "auto rows = storage.select(as_optional(date(\"bogus\")));");
    REQUIRE(generate("SELECT date('bogus') || 'x';") ==
            "auto rows = storage.select(as_optional(date(\"bogus\") || \"x\"));");
    REQUIRE(generate("SELECT julianday('bogus');") ==
            "auto rows = storage.select(as_optional(julianday(\"bogus\")));");
    REQUIRE(generate("SELECT strftime('%Y', 'bogus');") ==
            "auto rows = storage.select(as_optional(strftime(\"%Y\", \"bogus\")));");
    REQUIRE(generate("SELECT unicode('');") == "auto rows = storage.select(as_optional(unicode(\"\")));");
    REQUIRE(generate("SELECT unicode('') + 1;") ==
            "auto rows = storage.select(as_optional(unicode(\"\") + 1));");
    REQUIRE(generate("SELECT sign('abc');") == "auto rows = storage.select(as_optional(sign(\"abc\")));");
    REQUIRE(generate("SELECT json_extract('{}', '$.a');") ==
            "auto rows = storage.select(as_optional(json_extract(\"{}\", \"$.a\")));");
    REQUIRE(generate("SELECT sqrt(-1);") == "auto rows = storage.select(as_optional(sqrt(-1)));");
    // `substr(x'', 1)` is NULL rather than an empty blob, and `printf('')` NULL rather than an
    // empty string, so neither is a function that only propagates a NULL argument.
    REQUIRE(generate("SELECT substr('abc', 1, 1);") ==
            "auto rows = storage.select(as_optional(substr(\"abc\", 1, 1)));");
    REQUIRE(generate("SELECT printf('') || 'x';") ==
            "auto rows = storage.select(as_optional(printf(\"\") || \"x\"));");
    // An aggregate is NULL over an empty rowset whatever its argument holds. `sum`, `max` and `min`
    // are left plain as a result column — sqlite_orm declares them `std::unique_ptr` — but an
    // operator over one is typed by the operator alone and has to carry the NULL itself.
    REQUIRE(generate("SELECT sum(1) + 1;") == "auto rows = storage.select(as_optional(sum(1) + 1));");
    REQUIRE(generate("SELECT avg(1) + 1;") == "auto rows = storage.select(as_optional(avg(1) + 1));");
}

// SQLite 3.48 added `iif(X, Y)` as a spelling of `iif(X, Y, NULL)`, so the short form answers NULL
// whenever X is false whatever its arguments hold — `SELECT iif(0, 1)` is NULL in sqlite3 3.51,
// while libsqlite3 3.45.1, the version this project links, refuses the call — and only the
// three-argument form propagates a NULL argument and nothing else. The name alone therefore does
// not answer whether the call can be NULL, in either position: a result column and an operand are
// both widened for the short form and both left plain for the long one. No runtime case reads the
// short form back: sqlite_orm declares the three-argument `iif` alone, so `iif(0, 1)` does not
// compile whether it is widened or not.
TEST_CASE("codegen: an iif without its ELSE argument is widened, the three-argument one is not") {
    REQUIRE(generate("SELECT iif(0, 1);") == "auto rows = storage.select(as_optional(iif(0, 1)));");
    REQUIRE(generate("SELECT iif(0, 1) + 1;") == "auto rows = storage.select(as_optional(iif(0, 1) + 1));");
    REQUIRE(generate("SELECT iif(0, 1, 2);") == "auto rows = storage.select(iif(0, 1, 2));");
    REQUIRE(generate("SELECT iif(0, 1, 2) + 1;") == "auto rows = storage.select(iif(0, 1, 2) + 1);");
    // The name is matched without regard to case, the way SQLite resolves it.
    REQUIRE(generate("SELECT IIF(0, 1) || 'x';") ==
            "auto rows = storage.select(as_optional(iif(0, 1) || \"x\"));");
}

// The widening stops where SQLite never answers NULL and where sqlite_orm reports a nullable type
// already. Checked against sqlite3 3.51 over a NULL argument: `hex(NULL)` is the empty text,
// `quote(NULL)` the text 'NULL', `count(NULL)` 0, `total(NULL)` 0.0, `iif(NULL, 1, 2)` 2 and
// `NULL IN ()` 0, and a predicate, a CAST or a function call over operands the SQL spells out has
// no NULL to report either, as long as the function answers NULL for no reason other than a NULL
// argument — `abs`, `instr`, `length`, `round`, `upper` and the rest of the list in
// `sqliteFunctionOnlyPropagatesANullArgument`. `abs`, `max`, `min` and `sum` are declared
// `std::unique_ptr` in sqlite_orm and `coalesce`, `ifnull`, `nullif`, `iif`, `likely`, `unlikely`
// and `likelihood` as the result of an argument, so widening one of those would nest a second
// nullable around the first. That answer is given over the name alone, and the two `length(a)`
// cases are what it leaves behind: `length` is typed `int` however NULL the row is, so
// `iif(1, length(a), 2)` and `likely(length(a))` are typed `int` too and still read a NULL back as
// 0 — a known hole with a card of its own, not one this widening reaches. A window function is
// left alone as well: `row_number` and the other ranks are never NULL, and `lag`, `lead`,
// `first_value`, `last_value` and `nth_value` are typed as their argument, which leaves the NULL
// an empty window answers over a NOT NULL argument — `lag(b) OVER (ORDER BY b)` is NULL on the
// first row — also carded. A MATCH has no widening either: SQLite refuses `a MATCH 'x'` as a
// result column outside an FTS table, and sqlite_orm has no result type for `match_t`.
// A user-defined function is left alone too — it is called through the generated struct's
// `operator()`, and the row carries back the type that operator declares — which
// "codegen: an argument under a dropped COLLATE keeps its name and type" spells out in full.
TEST_CASE("codegen: a predicate, a CAST or a function call that cannot be NULL keeps the type sqlite_orm gives it") {
    REQUIRE(generate("SELECT 1 BETWEEN 2 AND 3;") == "auto rows = storage.select(between(1, 2, 3));");
    REQUIRE(generate("SELECT a IN () FROM users;") == "auto rows = storage.select(in(&Users::a, {}));");
    REQUIRE(generate("SELECT CAST(1 AS TEXT);") == "auto rows = storage.select(cast<std::string>(1));");
    REQUIRE(generate("SELECT length('x');") == "auto rows = storage.select(length(\"x\"));");
    REQUIRE(generate("SELECT upper('a') || 'x';") == "auto rows = storage.select(upper(\"a\") || \"x\");");
    REQUIRE(generate("SELECT instr('abc', 'b');") == "auto rows = storage.select(instr(\"abc\", \"b\"));");
    REQUIRE(generate("SELECT json_valid('{}');") == "auto rows = storage.select(json_valid(\"{}\"));");
    REQUIRE(generate("SELECT round(1.5);") == "auto rows = storage.select(round(1.5));");
    REQUIRE(generate("SELECT pi();") == "auto rows = storage.select(pi());");
    REQUIRE(generate("SELECT hex(a) FROM users;") == "auto rows = storage.select(hex(&Users::a));");
    REQUIRE(generate("SELECT quote(a) FROM users;") == "auto rows = storage.select(quote(&Users::a));");
    REQUIRE(generate("SELECT count(a) FROM users;") == "auto rows = storage.select(count(&Users::a));");
    REQUIRE(generate("SELECT total(a) FROM users;") == "auto rows = storage.select(total(&Users::a));");
    REQUIRE(generate("SELECT abs(a) FROM users;") == "auto rows = storage.select(abs(&Users::a));");
    REQUIRE(generate("SELECT max(a) FROM users;") == "auto rows = storage.select(max(&Users::a));");
    REQUIRE(generate("SELECT sum(a) FROM users;") == "auto rows = storage.select(sum(&Users::a));");
    REQUIRE(generate("SELECT coalesce(a, 1) FROM users;") ==
            "auto rows = storage.select(coalesce(&Users::a, 1));");
    REQUIRE(generate("SELECT iif(a, 1, 2) FROM users;") == "auto rows = storage.select(iif(&Users::a, 1, 2));");
    REQUIRE(generate("SELECT iif(1, length(a), 2) FROM users;") ==
            "auto rows = storage.select(iif(1, length(&Users::a), 2));");
    REQUIRE(generate("SELECT likely(length(a)) FROM users;") ==
            "auto rows = storage.select(likely(length(&Users::a)));");
    REQUIRE(generate("SELECT lag(a) OVER () FROM users;") == "auto rows = storage.select(lag(&Users::a).over());");
    REQUIRE(generate("SELECT lag(a) OVER (ORDER BY a) FROM users;") ==
            "auto rows = storage.select(lag(&Users::a).over(order_by(&Users::a)));");
    REQUIRE(generate("SELECT row_number() OVER () FROM users;") ==
            "auto rows = storage.select(row_number().over());");
    REQUIRE(generate("SELECT a MATCH 'x' FROM users;") == "auto rows = storage.select(match(&Users::a, \"x\"));");
    REQUIRE(generate("SELECT count(*) FROM users;") == "auto rows = storage.select(count<Users>());");
}

// A WHERE or an ORDER BY is not read back, so the expression generator stays as it was: only the
// result column of a select is widened.
TEST_CASE("codegen: as_optional is confined to the result columns of a select") {
    REQUIRE(generate("SELECT * FROM users WHERE a + 1 > 2;") ==
            "auto rows = storage.get_all<Users>(where(c(&Users::a) + 1 > 2));");
    REQUIRE(generate("SELECT id FROM users ORDER BY a + 1;") ==
            "auto rows = storage.select(&Users::id, order_by(c(&Users::a) + 1));");
    REQUIRE(generate("a + 1") == "c(&User::a) + 1");
}

TEST_CASE("codegen: as_optional reaches every form a result column is generated in") {
    REQUIRE(generate("SELECT a + 1 AS total FROM users;") ==
            "struct TotalAlias : sqlite_orm::alias_tag {\n"
            "    static const std::string& get() {\n"
            "        static const std::string res = \"total\";\n"
            "        return res;\n"
            "    }\n"
            "};\n"
            "auto rows = storage.select(as<TotalAlias>(as_optional(c(&Users::a) + 1)));");
    REQUIRE(generate("SELECT DISTINCT a + 1 FROM users;") ==
            "auto rows = storage.select(distinct(as_optional(c(&Users::a) + 1)));");
    REQUIRE(generate("SELECT a + 1, a * 2 FROM users;") ==
            "auto rows = storage.select(columns(as_optional(c(&Users::a) + 1), "
            "as_optional(c(&Users::a) * 2)));");
    REQUIRE(generate("SELECT DISTINCT a + 1, a * 2 FROM users;") ==
            "auto rows = storage.select(distinct(columns(as_optional(c(&Users::a) + 1), "
            "as_optional(c(&Users::a) * 2))));");
}

// sqlite_orm types `&`, `|`, `<<`, `>>` and `~` as `int`, so the int64 SQLite computes reached the
// caller truncated: `9223372036854775807 & -1` came back as -1. SQLite answers a bitwise operator
// with an INTEGER or a NULL whatever its operands hold — checked over 1463 operand pairs against
// sqlite3 3.51, none of them REAL or TEXT — and a CAST to INTEGER keeps both, `typeof` included,
// so the CAST only widens the C++ type. Values checked in "runtime: a bitwise result column reads
// the whole int64 back".
TEST_CASE("codegen: a bitwise result column is cast to an int64_t") {
    REQUIRE(generate("SELECT a & -1 FROM users;") ==
            "auto rows = storage.select(as_optional(cast<int64_t>(c(&Users::a) & -1)));");
    REQUIRE(generate("SELECT a | 0 FROM users;") ==
            "auto rows = storage.select(as_optional(cast<int64_t>(c(&Users::a) | 0)));");
    REQUIRE(generate("SELECT a << 1 FROM users;") ==
            "auto rows = storage.select(as_optional(cast<int64_t>(c(&Users::a) << 1)));");
    REQUIRE(generate("SELECT a >> 1 FROM users;") ==
            "auto rows = storage.select(as_optional(cast<int64_t>(c(&Users::a) >> 1)));");
    REQUIRE(generate("SELECT ~a FROM users;") ==
            "auto rows = storage.select(as_optional(cast<int64_t>(~c(&Users::a))));");
    // A unary plus generates its operand's code, so the operand is what carries the CAST.
    REQUIRE(generate("SELECT +(a & -1) FROM users;") ==
            "auto rows = storage.select(as_optional(cast<int64_t>(c(&Users::a) & -1)));");
    // An operator over literals alone needs no `as_optional`, and still needs the CAST.
    REQUIRE(generate("SELECT 9223372036854775807 & -1;") ==
            "auto rows = storage.select(cast<int64_t>(c(9223372036854775807) & -1));");
    // Only the top-level operator of a result column decides the type the row is read back with.
    REQUIRE(generate("SELECT (a & -1) + 1 FROM users;") ==
            "auto rows = storage.select(as_optional((c(&Users::a) & -1) + 1));");
}

// The CAST is what a result column is read back with, so an expression nobody reads back — a
// WHERE, an ORDER BY, a GROUP BY — keeps the code the expression generator gives it.
TEST_CASE("codegen: the int64 cast is confined to the result columns of a select") {
    REQUIRE(generate("SELECT * FROM users WHERE a & 1;") ==
            "auto rows = storage.get_all<Users>(where(c(&Users::a) & 1));");
    REQUIRE(generate("SELECT a FROM users ORDER BY a & 1;") ==
            "auto rows = storage.select(&Users::a, order_by(c(&Users::a) & 1));");
}

// The CAST reaches a result column in every shape a select spells one, the way `as_optional` does.
TEST_CASE("codegen: the int64 cast reaches every form a result column is generated in") {
    REQUIRE(generate("SELECT a & 1 AS masked FROM users;") ==
            "struct MaskedAlias : sqlite_orm::alias_tag {\n"
            "    static const std::string& get() {\n"
            "        static const std::string res = \"masked\";\n"
            "        return res;\n"
            "    }\n"
            "};\n"
            "auto rows = storage.select(as<MaskedAlias>(as_optional(cast<int64_t>(c(&Users::a) & 1))));");
    REQUIRE(generate("SELECT DISTINCT a & 1 FROM users;") ==
            "auto rows = storage.select(distinct(as_optional(cast<int64_t>(c(&Users::a) & 1))));");
    REQUIRE(generate("SELECT a & 1, a | 2 FROM users;") ==
            "auto rows = storage.select(columns(as_optional(cast<int64_t>(c(&Users::a) & 1)), "
            "as_optional(cast<int64_t>(c(&Users::a) | 2))));");
}

// `+`, `-`, `*`, `/` and `%` have no such CAST: SQLite answers them with a REAL as soon as an
// operand is one, and a CAST would truncate that REAL instead of carrying it. sqlite_orm types
// them `double`, which holds every integer up to 2^53 and rounds the ones past it, so the column
// is generated as it was and the loss is reported. The underline covers the operator token, which
// is what the message names.
TEST_CASE("codegen: an arithmetic result column reports the double it is read back through") {
    REQUIRE(generateFull("SELECT a + 0 FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 10}, 1}});
    REQUIRE(generateFull("SELECT a - 1 FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `-` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 10}, 1}});
    REQUIRE(generateFull("SELECT a * 2 FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `*` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 10}, 1}});
    REQUIRE(generateFull("SELECT a / 2 FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `/` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 10}, 1}});
    REQUIRE(generateFull("SELECT a % a FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `%` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 10}, 1}});
    // Several columns computed with the same operator report once, anchored at the first of them.
    REQUIRE(generateFull("SELECT a + 1, a + 2 FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 10}, 1}});
    // A unary plus generates its operand's code, so the operand is the one the warning names and
    // is anchored at: the `+` of `a + 0`, at column 12, not the one the column starts with.
    REQUIRE(generateFull("SELECT +(a + 0) FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 12}, 1}});
    // A negation reaches sqlite_orm as the subtraction `0 - x`, which is typed `double` too; the
    // minus sign of the SQL is the token the message names.
    REQUIRE(generateFull("SELECT -a FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `-` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 8}, 1}});
}

// A `double` holds every integer up to 2^53, so an expression whose operands the SQL spells out is
// reported only when the arithmetic can leave that range. The bound is an upper bound on the
// magnitude of an INTEGER answer, and it is computed in the int64 domain SQLite computes in:
// carrying it in the `double` the warning is about would round it down past 2^53 and lose the very
// values it looks for. Each step saturates instead of growing, and an INTEGER SQLite cannot hold is
// a REAL, which reaches the caller exactly.
TEST_CASE("codegen: an arithmetic result column a double carries is not reported") {
    REQUIRE(generateFull("SELECT 1 + 2;").warnings.empty());
    REQUIRE(generateFull("SELECT 1000000 * 1000000;").warnings.empty());
    REQUIRE(generateFull("SELECT 1.5 * 2;").warnings.empty());
    REQUIRE(generateFull("SELECT 9223372036854775807 % 2;").warnings.empty());
    // A remainder is smaller than the divisor, so a spelled-out divisor bounds it whatever the
    // dividend is.
    REQUIRE(generateFull("SELECT a % 2 FROM users;").warnings.empty());
    // An integer division never grows the dividend, so a spelled-out one bounds the quotient.
    REQUIRE(generateFull("SELECT 1000 / a FROM users;").warnings.empty());
    REQUIRE(generateFull("SELECT 99999999999999999999 + 1;").warnings.empty());
    // 2^53 itself is the largest magnitude a double still holds, so it is not reported and the
    // next integer up is.
    REQUIRE(generateFull("SELECT 9007199254740992 + 0;").warnings.empty());
    // A factor of zero answers zero however large the other side is.
    REQUIRE(generateFull("SELECT a * 0 FROM users;").warnings.empty());
    // A WHERE or an ORDER BY is not read back, so there is nothing to lose there.
    REQUIRE(generateFull("SELECT * FROM users WHERE a + 1 > 2;").warnings.empty());
    REQUIRE(generateFull("SELECT a FROM users ORDER BY a + 1;").warnings.empty());
}

// `+`, `-`, `*`, `/` and `%` answer a REAL as soon as an operand is one and a NULL as soon as an
// operand is one — checked over 1710 operand pairs against sqlite3 3.51, none of which answered an
// INTEGER — so such a column has no INTEGER to lose: a `double` carries a REAL as it is and the
// `as_optional` already generated carries the NULL. Reporting them would underline an `a * 1.0` in
// the playground and in Studio over a loss the SQL cannot produce.
TEST_CASE("codegen: an arithmetic result column with a REAL or a NULL operand is not reported") {
    REQUIRE(generateFull("SELECT a + 1.5 FROM users;").warnings.empty());
    REQUIRE(generateFull("SELECT 1.5 - a FROM users;").warnings.empty());
    REQUIRE(generateFull("SELECT a * 1e300 FROM users;").warnings.empty());
    REQUIRE(generateFull("SELECT a / 1.5 FROM users;").warnings.empty());
    REQUIRE(generateFull("SELECT a % 1.5 FROM users;").warnings.empty());
    REQUIRE(generateFull("SELECT NULL + a FROM users;").warnings.empty());
    REQUIRE(generateFull("SELECT a - NULL FROM users;").warnings.empty());
    REQUIRE(generateFull("SELECT a * NULL FROM users;").warnings.empty());
    REQUIRE(generateFull("SELECT a / NULL FROM users;").warnings.empty());
    REQUIRE(generateFull("SELECT a % NULL FROM users;").warnings.empty());
    // A REAL or a NULL reaches the outer operator through the inner one the same way.
    REQUIRE(generateFull("SELECT (a + 1.5) * 9223372036854775807 FROM users;").warnings.empty());
}

// The first integers a `double` no longer holds, which a bound carried in a `double` rounded back
// down into range and stayed quiet about: `static_cast<double>(9007199254740993)` is
// 9007199254740992.0, and `9007199254740992.0 + 1.0` is 9007199254740992.0 as well. sqlite3 3.51
// answers all four of these with an INTEGER — 9007199254740993, 9007199254740993, 9007199254740993
// and -9007199254740994 — and the generated column reads the first three back as
// 9007199254740992.
TEST_CASE("codegen: an arithmetic result column is reported at the first integer past 2^53") {
    REQUIRE(generateFull("SELECT 9007199254740993 + 0;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 25}, 1}});
    REQUIRE(generateFull("SELECT 9007199254740992 + 1;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 25}, 1}});
    REQUIRE(generateFull("SELECT 4503599627370496 * 2 + 1;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 29}, 1}});
    // `~` casts its operand to an INTEGER first, so a REAL operand is not carried through it the
    // way it is through the arithmetic operators: this one answers the INTEGER -9007199254740995.
    REQUIRE(generateFull("SELECT 1 + ~9007199254740994.0;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 10}, 1}});
    // A magnitude past 2^53 is reported even when both operands are literals.
    REQUIRE(generateFull("SELECT 9223372036854775807 + 0;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 28}, 1}});
    REQUIRE(generateFull("SELECT 100000000 * 100000000;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `*` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 18}, 1}});
}
