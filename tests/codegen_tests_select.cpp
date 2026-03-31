#include "codegen_tests_common.hpp"

TEST_CASE("codegen: SELECT * FROM table") {
    auto result = generate("SELECT * FROM users");
    REQUIRE(result == "auto rows = storage.get_all<Users>();");
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
    auto result = generate_full("SELECT main.users.* FROM users");
    REQUIRE(result.code == "auto rows = storage.select(asterisk<Users>());");
    REQUIRE(result.warnings ==
        std::vector<std::string>{
            "schema-qualified SELECT result column main.users.* is not represented in sqlite_orm; generated code uses "
            "asterisk<Users>() (table type only)"});
}

TEST_CASE("codegen: SELECT column via FROM table alias") {
    auto result = generate("SELECT u.name FROM users u");
    REQUIRE(result == "auto rows = storage.select(&Users::name);");
}

TEST_CASE("codegen: SELECT with FROM schema qualifier warns") {
    REQUIRE(generate_full("SELECT name FROM main.users") ==
        CodeGenResult{"auto rows = storage.select(&Users::name);",
                      {column_ref_style_dp(1, "&Users::name")},
                      std::vector<std::string>{
                          "FROM clause schema qualifier 'main' for table 'users' is not represented in sqlite_orm "
                          "mapping"}});
}

TEST_CASE("codegen: comma-separated FROM is cross join") {
    REQUIRE(generate_full("SELECT * FROM users, posts") ==
        CodeGenResult{"auto rows = storage.get_all<Users>(cross_join<Posts>());",
                      {api_level_star_select_dp(1, "Users", "cross_join<Posts>()")},
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
    joinDps.push_back(api_level_star_select_dp(
        1, "Users", "left_join<Posts>(on(c(&Users::id) == &Posts::user_id))"));
    auto onExpr = expected_binary_leaf("&Users::id", "&Posts::user_id", " == ", "is_equal", 2);
    joinDps.insert(joinDps.end(), onExpr.decisionPoints.begin(), onExpr.decisionPoints.end());
    REQUIRE(generate_full("SELECT * FROM users LEFT JOIN posts ON users.id = posts.user_id") ==
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
    auto result = generate_full("id > (SELECT MAX(x) FROM t)");
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
    REQUIRE(generate_full("SELECT n FROM (SELECT 1 AS n) t") ==
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
    auto result = generate_full("SELECT name AS user_name FROM users");
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
    auto result = generate_full("SELECT name user_name FROM users");
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
    auto result = generate_full("SELECT name AS i FROM users");
    REQUIRE(result.code == "auto rows = storage.select(as<colalias_i>(&Users::name));");
    bool hasBuiltinWarning = false;
    for(const auto& w : result.warnings) {
        if(w.find("colalias_") != std::string::npos) hasBuiltinWarning = true;
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
        "columns(&Marvel::name, as<colalias_i>(instr(&Marvel::abilities, \"o\"))), "
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
