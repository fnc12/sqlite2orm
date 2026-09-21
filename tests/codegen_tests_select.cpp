#include "codegen_tests_common.hpp"

namespace {

    /** Must match `kCommentCpp20ColumnAliases` in codegen.cpp (C++20 `_col` aliases). */
    const char* const kExpectedCpp20ColumnAliasComment =
        "C++20 literal column aliases (`orm_column_alias`, string literal `_col`) require sqlite_orm to be "
        "built with the preprocessor macro SQLITE_ORM_WITH_CPP20_ALIASES defined. Your project may enable "
        "that via CMake target_compile_definitions, compiler `-D`, a config header, or any other suitable "
        "mechanism.";

    /** Must match `kCommentAliasedFromSources` in codegen_utils.cpp. */
    const std::string kAliasedFromSourcesComment =
        "A select over aliased FROM sources names them with `from<...>()`: left to itself sqlite_orm "
        "builds the FROM out of the recordsets the arguments mention, and the two forms that would "
        "stand for such a source there drop its alias — `cross_join<alias_b<T>>()` serializes as a "
        "plain `CROSS JOIN \"t\"` (only a join carrying an ON writes an alias out), and "
        "`count<alias_a<T>>()` names no table at all. The named list is the same FROM the SQL wrote, "
        "in the same order, a comma between two sources reading as the CROSS JOIN it stands for.";

}  // namespace

TEST_CASE("codegen: SELECT * FROM table") {
    REQUIRE(generateFull("SELECT * FROM users") ==
            CodeGenResult{"auto rows = storage.get_all<Users>();", {apiLevelStarSelectDp(1, "Users", "")}, {}});
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
    REQUIRE(
        result.warnings ==
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

// The comma names both sources: `cross_join<alias_b<Users>>()` serializes as a plain
// `CROSS JOIN "users"`, sqlite_orm writing an alias out only for a join that carries an ON.
TEST_CASE("codegen: self-join with aliases") {
    auto result = generate("SELECT a.name, b.name FROM users a, users b");
    REQUIRE(result == "auto rows = storage.select(columns(alias_column<alias_a<Users>>(&Users::name), "
                      "alias_column<alias_b<Users>>(&Users::name)), from<alias_a<Users>, alias_b<Users>>());");
}

TEST_CASE("codegen: SELECT column via FROM table alias C++20 style") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["table_alias_style"] = "cpp20";
    auto result = generateWithPolicy("SELECT u.name FROM users u", pol);
    REQUIRE(result.code == "constexpr orm_table_alias auto u = \"u\"_alias.for_<Users>();\n"
                           "auto rows = storage.select(u->*&Users::name);");
}

TEST_CASE("codegen: SELECT t.* FROM alias C++20 style") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["table_alias_style"] = "cpp20";
    auto result = generateWithPolicy("SELECT t.* FROM users t", pol);
    REQUIRE(result.code == "constexpr orm_table_alias auto t = \"t\"_alias.for_<Users>();\n"
                           "auto rows = storage.select(asterisk<t>());");
}

TEST_CASE("codegen: self-join C++20 style") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["table_alias_style"] = "cpp20";
    auto result = generateWithPolicy("SELECT a.name, b.name FROM users a, users b", pol);
    REQUIRE(result.code == "constexpr orm_table_alias auto a = \"a\"_alias.for_<Users>();\n"
                           "constexpr orm_table_alias auto b = \"b\"_alias.for_<Users>();\n"
                           "auto rows = storage.select(columns(a->*&Users::name, b->*&Users::name), from<a, b>());");
}

TEST_CASE("codegen: table_alias_style decision point present when alias used") {
    auto result = generateFull("SELECT u.name FROM users u");
    bool hasDp = false;
    for (const auto& dp: result.decisionPoints) {
        if (dp.category == "table_alias_style") {
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
    joinDps.push_back(apiLevelStarSelectDp(1, "Users", "left_join<Posts>(on(c(&Users::id) == &Posts::user_id))"));
    auto onExpr = expectedBinaryLeaf("&Users::id", "&Posts::user_id", " == ", "is_equal", 2);
    joinDps.insert(joinDps.end(), onExpr.decisionPoints.begin(), onExpr.decisionPoints.end());
    REQUIRE(generateFull("SELECT * FROM users LEFT JOIN posts ON users.id = posts.user_id") ==
            CodeGenResult{"auto rows = storage.get_all<Users>(left_join<Posts>(on(c(&Users::id) == &Posts::user_id)));",
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
    REQUIRE(result == "auto rows = storage.select(columns(&Users::id, &Users::name), where(c(&Users::age) >= 18 and "
                      "c(&Users::active) == 1));");
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
    REQUIRE(result == "auto rows = storage.get_all<Users>(multi_order_by(order_by(&Users::name).asc(), "
                      "order_by(&Users::age).desc()));");
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

// `limit()` with an empty argument list does not compile, and neither does the placeholder that
// stands for the subquery, so the statement is left out whole and the warnings are what say why.
TEST_CASE("codegen: an unmapped subquery in LIMIT is warned about") {
    REQUIRE(generateFull("SELECT name FROM users LIMIT (SELECT a FROM users GROUP BY a)") ==
            CodeGenResult{{},
                          {},
                          {CodegenWarning{"scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen",
                                          SourceLocation{1, 30},
                                          32},
                           "GROUP BY in subquery is not yet mapped to sqlite_orm select(...)",
                           kStatementNotGenerated}});
}

TEST_CASE("codegen: SELECT with GROUP BY") {
    auto result = generate("SELECT name, count(*) FROM users GROUP BY name");
    REQUIRE(result == "auto rows = storage.select(columns(&Users::name, count<Users>()), group_by(&Users::name));");
}

TEST_CASE("codegen: SELECT with GROUP BY HAVING") {
    auto result = generate("SELECT name, count(*) FROM users GROUP BY name HAVING count(*) > 1");
    REQUIRE(result == "auto rows = storage.select(columns(&Users::name, count<Users>()), "
                      "group_by(&Users::name).having(count<Users>() > 1));");
}

TEST_CASE("codegen: SELECT with WHERE + ORDER BY + LIMIT") {
    auto result = generate("SELECT * FROM users WHERE age > 18 ORDER BY name LIMIT 10");
    REQUIRE(result ==
            "auto rows = storage.get_all<Users>(where(c(&Users::age) > 18), order_by(&Users::name), limit(10));");
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
    REQUIRE(generate("SELECT 1 UNION ALL SELECT 2") == "auto rows = storage.select(union_all(select(1), select(2)));");
}

TEST_CASE("codegen: INTERSECT") {
    REQUIRE(generate("SELECT 1 INTERSECT SELECT 2") == "auto rows = storage.select(intersect(select(1), select(2)));");
}

TEST_CASE("codegen: EXCEPT") {
    REQUIRE(generate("SELECT 1 EXCEPT SELECT 2") == "auto rows = storage.select(except(select(1), select(2)));");
}

TEST_CASE("codegen: derived FROM emits stub and warning") {
    REQUIRE(
        generateFull("SELECT n FROM (SELECT 1 AS n) t") ==
        CodeGenResult{
            "/* SELECT with derived FROM */",
            {},
            {CodegenWarning{"subselect in FROM is not supported in sqlite_orm codegen", SourceLocation{1, 16}, 13}}});
}

TEST_CASE("codegen: ORDER BY COLLATE") {
    REQUIRE(generate("SELECT * FROM users ORDER BY name COLLATE NOCASE;") ==
            "auto rows = storage.get_all<Users>(order_by(&Users::name).collate_nocase());");
}

TEST_CASE("codegen: VALUES standalone") {
    REQUIRE(generate("VALUES (1, 'a'), (2, 'b');") == "auto rows = storage.select(columns(1, \"a\"));");
}

TEST_CASE("codegen: SELECT single column with alias") {
    auto result = generateFull("SELECT name AS user_name FROM users");
    REQUIRE(result.code == "struct User_nameAlias : sqlite_orm::alias_tag {\n"
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
    REQUIRE(result == "struct User_nameAlias : sqlite_orm::alias_tag {\n"
                      "    static const std::string& get() {\n"
                      "        static const std::string res = \"user_name\";\n"
                      "        return res;\n"
                      "    }\n"
                      "};\n"
                      "auto rows = storage.select(columns(&Users::id, as<User_nameAlias>(&Users::name)));");
}

TEST_CASE("codegen: SELECT column with string literal alias") {
    auto result = generate("SELECT name AS 'UserName' FROM users");
    REQUIRE(result == "struct UserNameAlias : sqlite_orm::alias_tag {\n"
                      "    static const std::string& get() {\n"
                      "        static const std::string res = \"UserName\";\n"
                      "        return res;\n"
                      "    }\n"
                      "};\n"
                      "auto rows = storage.select(as<UserNameAlias>(&Users::name));");
}

TEST_CASE("codegen: SELECT DISTINCT column with alias") {
    auto result = generate("SELECT DISTINCT name AS user_name FROM users");
    REQUIRE(result == "struct User_nameAlias : sqlite_orm::alias_tag {\n"
                      "    static const std::string& get() {\n"
                      "        static const std::string res = \"user_name\";\n"
                      "        return res;\n"
                      "    }\n"
                      "};\n"
                      "auto rows = storage.select(distinct(as<User_nameAlias>(&Users::name)));");
}

TEST_CASE("codegen: SELECT DISTINCT multiple columns with aliases") {
    auto result = generate("SELECT DISTINCT id AS ID, name AS user_name FROM users");
    REQUIRE(
        result ==
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
    REQUIRE(result.code == "struct User_nameAlias : sqlite_orm::alias_tag {\n"
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
    for (const auto& w: result.warnings) {
        if (w.message.find("colalias_") != std::string::npos)
            hasBuiltinWarning = true;
    }
    REQUIRE(hasBuiltinWarning);
}

TEST_CASE("codegen: SELECT alias referenced in WHERE and ORDER BY") {
    auto result = generate("SELECT name, instr(abilities, 'o') i "
                           "FROM marvel "
                           "WHERE i > 0 "
                           "ORDER BY i");
    REQUIRE(result == "auto rows = storage.select("
                      "columns(&Marvel::name, as<colalias_i>(as_optional(instr(&Marvel::abilities, \"o\")))), "
                      "where(c(get<colalias_i>()) > 0), "
                      "order_by(get<colalias_i>()));");
}

TEST_CASE("codegen: SELECT alias referenced in ORDER BY with custom struct") {
    auto result = generate("SELECT name AS user_name FROM users ORDER BY user_name");
    REQUIRE(result == "struct User_nameAlias : sqlite_orm::alias_tag {\n"
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
    REQUIRE(
        generateFull("SELECT name AS i FROM users") ==
        CodeGenResult{
            chosenCode,
            {columnRefStyleDp(1, "&Users::name"),
             DecisionPoint{2,
                           "column_alias_style",
                           "alias_tag",
                           chosenCode,
                           {Option{"alias_tag",
                                   chosenCode,
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
    const char* sql = "SELECT name, instr(abilities, 'o') i "
                      "FROM marvel "
                      "WHERE i > 0 "
                      "ORDER BY i";

    const std::string mainCode =
        "constexpr orm_column_alias auto i = \"i\"_col;\n"
        "auto rows = storage.select(columns(&Marvel::name, as<i>(as_optional(instr(&Marvel::abilities, \"o\")))), "
        "where(i > 0), order_by(i));";
    const std::string aliasTagAltCode = "auto rows = storage.select(columns(&Marvel::name, "
                                        "as<colalias_i>(as_optional(instr(&Marvel::abilities, \"o\")))), "
                                        "where(c(get<colalias_i>()) > 0), order_by(get<colalias_i>()));";

    REQUIRE(generateWithPolicy(sql, policy) ==
            CodeGenResult{mainCode,
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
                                          Option{"cpp20_literal",
                                                 mainCode,
                                                 "C++20 literal aliases (`orm_column_alias`, `_col`)",
                                                 false,
                                                 {},
                                                 20}}}},
                          {},
                          {},
                          {std::string(kExpectedCpp20ColumnAliasComment)}});
}

TEST_CASE("codegen: SELECT with window function OVER(), bind params in WHERE and LIMIT") {
    auto result = generate("SELECT id, firstName, lastName, count(id) OVER() FROM user_profile WHERE id > :refId ORDER "
                           "BY id LIMIT :resultperpage;");
    REQUIRE(result == "auto rows = storage.select(columns(&UserProfile::id, &UserProfile::firstName, "
                      "&UserProfile::lastName, count(&UserProfile::id).over()), where(c(&UserProfile::id) > refId), "
                      "order_by(&UserProfile::id), limit(resultperpage));");
}

TEST_CASE("codegen: MATCH against a column") {
    auto result = generate("SELECT * FROM docs WHERE body MATCH 'sqlite'");
    REQUIRE(result == "auto rows = storage.get_all<Docs>(where(match(&Docs::body, \"sqlite\")));");
}

TEST_CASE("codegen: MATCH against the FTS5 table name uses the hidden any column") {
    auto result = generateFull("SELECT * FROM docs_search WHERE docs_search MATCH 'sqlite'");
    REQUIRE(result.code == "auto rows = storage.get_all<DocsSearch>(where(match(c<DocsSearch>()->*&fts5::hidden::any, "
                           "\"sqlite\")));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{"MATCH against table \"docs_search\" maps to the hidden FTS5 'any' "
                                        "column; requires an FTS5 virtual table mapped as DocsSearch"});
}

TEST_CASE("codegen: MATCH against an aliased FTS5 table name") {
    auto result =
        generateFull("SELECT d.* FROM docs d JOIN docs_search s ON d.id = s.rowid WHERE docs_search MATCH 'word'");
    REQUIRE(result.code.find("match(c<DocsSearch>()->*&fts5::hidden::any, \"word\")") != std::string::npos);
}

namespace {
    const sqlite2orm::DecisionPoint* findDecisionPoint(const sqlite2orm::CodeGenResult& result,
                                                       std::string_view category) {
        for (const auto& dp: result.decisionPoints) {
            if (dp.category == category) {
                return &dp;
            }
        }
        return nullptr;
    }

    bool hasOptionValue(const sqlite2orm::DecisionPoint& dp, std::string_view value) {
        for (const auto& option: dp.options) {
            if (option.value == value) {
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
    for (const auto& option: dp->options) {
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
    REQUIRE(generate("SELECT a + 1 FROM users;") == "auto rows = storage.select(as_optional(c(&Users::a) + 1));");
    REQUIRE(generate("SELECT a * 2 FROM users;") == "auto rows = storage.select(as_optional(c(&Users::a) * 2));");
    REQUIRE(generate("SELECT 0 - a FROM users;") == "auto rows = storage.select(as_optional(c(0) - &Users::a));");
    REQUIRE(generate("SELECT -a FROM users;") == "auto rows = storage.select(as_optional((c(0) - c(&Users::a))));");
    REQUIRE(generate("SELECT ~a FROM users;") ==
            "auto rows = storage.select(as_optional(cast<int64_t>(~c(&Users::a))));");
    REQUIRE(generate("SELECT a > 0 FROM users;") == "auto rows = storage.select(as_optional(c(&Users::a) > 0));");
    REQUIRE(generate("SELECT a AND 1 FROM users;") == "auto rows = storage.select(as_optional(c(&Users::a) and 1));");
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

// A scalar subquery is read back through the result column of the nested `select(...)`: sqlite_orm
// types a `select_t` as the column list it carries, so `(SELECT 1 / 0)` reached the caller as the 0
// of the `double` the division is typed as, where sqlite3 3.51 answers NULL. The widening wraps the
// subquery rather than the column inside it — `as_optional` serializes its operand and nothing
// else, so the SQL stays the same byte for byte, and the nested `select(...)` is generated by the
// path a view body, a CTE, an IN and an INSERT ... SELECT share, none of which hands a result
// column to a caller. Values checked in "runtime: a scalar subquery result column reads the NULL
// back".
TEST_CASE("codegen: a scalar subquery whose result column can be NULL is generated as as_optional") {
    REQUIRE(generate("SELECT (SELECT 1 / 0);") == "auto rows = storage.select(as_optional(select(c(1) / 0)));");
    REQUIRE(generate("SELECT (SELECT 0 * (1e300 * 1e300));") ==
            "auto rows = storage.select(as_optional(select(c(0) * (c(1e300) * 1e300))));");
    REQUIRE(generate("SELECT (SELECT a + 1 FROM users);") ==
            "auto rows = storage.select(as_optional(select(c(&Users::a) + 1)));");
    REQUIRE(generate("SELECT (SELECT a > 0 FROM users);") ==
            "auto rows = storage.select(as_optional(select(c(&Users::a) > 0)));");
    // The question is asked of the nested result column however deep the nesting runs, and one
    // `as_optional` at the outermost subquery is what the caller reads the value through.
    REQUIRE(generate("SELECT (SELECT (SELECT 1 / 0));") ==
            "auto rows = storage.select(as_optional(select(select(c(1) / 0))));");
    REQUIRE(generate("SELECT DISTINCT (SELECT 1 / 0);") ==
            "auto rows = storage.select(distinct(as_optional(select(c(1) / 0))));");
    REQUIRE(generate("SELECT (SELECT 1 / 0), 5;") ==
            "auto rows = storage.select(columns(as_optional(select(c(1) / 0)), 5));");
}

// The widening is the one asked of the nested result column, so a subquery over a column sqlite_orm
// already types nullably is left plain, and so is one whose column holds no NULL. A subquery in a
// WHERE is no result column at all: nothing reads its value back.
TEST_CASE("codegen: a scalar subquery that needs no widening keeps the type sqlite_orm gives it") {
    REQUIRE(generate("SELECT (SELECT 1);") == "auto rows = storage.select(select(1));");
    REQUIRE(generate("SELECT (SELECT max(a) FROM users);") == "auto rows = storage.select(select(max(&Users::a)));");
    REQUIRE(generate("SELECT (SELECT abs(a) FROM users);") == "auto rows = storage.select(select(abs(&Users::a)));");
    // The NULL SQLite answers a scalar subquery over an empty rowset with is a hole of its own:
    // `(SELECT a FROM users)` is NULL on an empty table however the column is declared, and
    // whether the field it is read into already holds an optional is the table's to answer, which
    // no schema reaches this layer to. Carded separately.
    REQUIRE(generate("SELECT (SELECT a FROM users);") == "auto rows = storage.select(select(&Users::a));");
    REQUIRE(generate("SELECT a FROM users WHERE (SELECT 1 / 0);") ==
            "auto rows = storage.select(&Users::a, where(select(c(1) / 0)));");
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
    REQUIRE(generate("SELECT 9e999 - 9e999;") ==
            "auto rows = storage.select(as_optional(c(std::numeric_limits<double>::infinity()) - "
            "std::numeric_limits<double>::infinity()));");
    REQUIRE(generate("SELECT 0 * 9e999;") ==
            "auto rows = storage.select(as_optional(c(0) * std::numeric_limits<double>::infinity()));");
    REQUIRE(generate("SELECT 1.0e400 - 1.0e400;") ==
            "auto rows = storage.select(as_optional(c(std::numeric_limits<double>::infinity()) - "
            "std::numeric_limits<double>::infinity()));");
    REQUIRE(generate("SELECT -9e999 + 9e999;") ==
            "auto rows = storage.select(as_optional(c(-std::numeric_limits<double>::infinity()) + "
            "std::numeric_limits<double>::infinity()));");
    // COLLATE decides how a value compares, not what the value is, so the literal under one is
    // read as the literal it is: a zero next to an infinity still reaches the NaN, a one does not.
    REQUIRE(generate("SELECT (0 COLLATE BINARY) * (1e300 * 1e300);") ==
            "auto rows = storage.select(as_optional(c(0) * (c(1e300) * 1e300)));");
    REQUIRE(generate("SELECT 0 * (9e999 COLLATE BINARY);") ==
            "auto rows = storage.select(as_optional(c(0) * std::numeric_limits<double>::infinity()));");
    REQUIRE(generate("SELECT (1 COLLATE BINARY) * (1e300 * 1e300);") ==
            "auto rows = storage.select(c(1) * (c(1e300) * 1e300));");
    REQUIRE(generate("SELECT 1e300 * 1e300;") == "auto rows = storage.select(c(1e300) * 1e300);");
    REQUIRE(generate("SELECT 9e999;") == "auto rows = storage.select(std::numeric_limits<double>::infinity());");
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
    REQUIRE(generate("SELECT NOT (a IS NULL) FROM users;") == "auto rows = storage.select(not (is_null(&Users::a)));");
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
    REQUIRE(generate("SELECT length(a) FROM users;") == "auto rows = storage.select(as_optional(length(&Users::a)));");
    REQUIRE(generate("SELECT upper(a) FROM users;") == "auto rows = storage.select(as_optional(upper(&Users::a)));");
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
    REQUIRE(generate("SELECT julianday('bogus');") == "auto rows = storage.select(as_optional(julianday(\"bogus\")));");
    REQUIRE(generate("SELECT strftime('%Y', 'bogus');") ==
            "auto rows = storage.select(as_optional(strftime(\"%Y\", \"bogus\")));");
    REQUIRE(generate("SELECT unicode('');") == "auto rows = storage.select(as_optional(unicode(\"\")));");
    REQUIRE(generate("SELECT unicode('') + 1;") == "auto rows = storage.select(as_optional(unicode(\"\") + 1));");
    REQUIRE(generate("SELECT sign('abc');") == "auto rows = storage.select(as_optional(sign(\"abc\")));");
    REQUIRE(generate("SELECT json_extract('{}', '$.a');") ==
            "auto rows = storage.select(as_optional(json_extract<std::string>(\"{}\", \"$.a\")));");
    REQUIRE(generate("SELECT sqrt(-1);") == "auto rows = storage.select(as_optional(sqrt(-1)));");
    // `substr(x'', 1)` is NULL rather than an empty blob, and `printf('')` NULL rather than an
    // empty string, so neither is a function that only propagates a NULL argument.
    REQUIRE(generate("SELECT substr('abc', 1, 1);") ==
            "auto rows = storage.select(as_optional(substr(\"abc\", 1, 1)));");
    REQUIRE(generate("SELECT printf('') || 'x';") == "auto rows = storage.select(as_optional(printf(\"\") || \"x\"));");
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
    REQUIRE(generate("SELECT IIF(0, 1) || 'x';") == "auto rows = storage.select(as_optional(iif(0, 1) || \"x\"));");
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
    REQUIRE(generate("SELECT coalesce(a, 1) FROM users;") == "auto rows = storage.select(coalesce(&Users::a, 1));");
    REQUIRE(generate("SELECT iif(a, 1, 2) FROM users;") == "auto rows = storage.select(iif(&Users::a, 1, 2));");
    REQUIRE(generate("SELECT iif(1, length(a), 2) FROM users;") ==
            "auto rows = storage.select(iif(1, length(&Users::a), 2));");
    REQUIRE(generate("SELECT likely(length(a)) FROM users;") ==
            "auto rows = storage.select(likely(length(&Users::a)));");
    REQUIRE(generate("SELECT lag(a) OVER () FROM users;") == "auto rows = storage.select(lag(&Users::a).over());");
    REQUIRE(generate("SELECT lag(a) OVER (ORDER BY a) FROM users;") ==
            "auto rows = storage.select(lag(&Users::a).over(order_by(&Users::a)));");
    REQUIRE(generate("SELECT row_number() OVER () FROM users;") == "auto rows = storage.select(row_number().over());");
    REQUIRE(generate("SELECT a MATCH 'x' FROM users;") == "auto rows = storage.select(match(&Users::a, \"x\"));");
    REQUIRE(generate("SELECT count(*) FROM users;") == "auto rows = storage.select(count<Users>());");
}

// sqlite_orm types `case_t<R, …>` as R, and R is the type inferred for the first branch's result —
// `int` for a branch holding a NULL — so a CASE that answers NULL reached the caller as 0. SQLite
// answers a CASE with the result of the branch it takes, with the ELSE result where no branch
// matches, and with a NULL where none matches and no ELSE is written: `SELECT typeof(CASE WHEN 1
// THEN NULL ELSE 0 END)` and `SELECT typeof(CASE WHEN 0 THEN 1 END)` are both null in sqlite3 3.51.
// The inference that picks R never names a nullable type, so there is no `case_` already widened.
// Values checked in "runtime: a CASE result column that can be NULL reads the NULL back".
TEST_CASE("codegen: a CASE result column that can be NULL is generated as as_optional") {
    REQUIRE(generate("SELECT CASE WHEN 1 THEN NULL ELSE 0 END;") ==
            "auto rows = storage.select(as_optional(case_<int>().when(1, then(nullptr)).else_(0).end()));");
    REQUIRE(generate("SELECT CASE WHEN 1 THEN NULL END;") ==
            "auto rows = storage.select(as_optional(case_<int>().when(1, then(nullptr)).end()));");
    REQUIRE(generate("SELECT CASE WHEN 0 THEN 1 ELSE NULL END;") ==
            "auto rows = storage.select(as_optional(case_<int>().when(0, then(1)).else_(nullptr).end()));");
    // No ELSE: the CASE answers NULL for every row no branch matches, whatever the branches hold.
    REQUIRE(generate("SELECT CASE WHEN 0 THEN 1 END;") ==
            "auto rows = storage.select(as_optional(case_<int>().when(0, then(1)).end()));");
    REQUIRE(generate("SELECT CASE a WHEN 1 THEN 2 END FROM users;") ==
            "auto rows = storage.select(as_optional(case_<int>(&Users::a).when(1, then(2)).end()));");
    // A branch that always matches leaves no row for the missing ELSE — `SELECT CASE WHEN 1 THEN 2
    // END` is 2 in sqlite3 3.51, never NULL — and it is widened all the same: telling those apart
    // means evaluating what SQLite makes of the condition, and an answer of `always` that is wrong
    // reads a NULL back as 0 again, while one widened for nothing only carries an `std::optional`
    // that is always engaged.
    REQUIRE(generate("SELECT CASE WHEN 1 THEN 2 END;") ==
            "auto rows = storage.select(as_optional(case_<int>().when(1, then(2)).end()));");
    // A branch other than the first is asked as well, and so is a result that is not a NULL
    // literal: a nullable column, and a division SQLite answers with NULL rather than an error.
    REQUIRE(generate("SELECT CASE WHEN 0 THEN 1 WHEN 1 THEN a ELSE 2 END FROM users;") ==
            "auto rows = storage.select(as_optional(case_<int>().when(0, then(1)).when(1, "
            "then(&Users::a)).else_(2).end()));");
    REQUIRE(generate("SELECT CASE WHEN 1 THEN 1 / 0 ELSE 2 END;") ==
            "auto rows = storage.select(as_optional(case_<int64_t>().when(1, then(c(1) / 0)).else_(2).end()));");
    // The CASE under a dropped COLLATE is the node the result column comes out as, so it is the
    // one the widening is decided from, the way it is for an operator.
    REQUIRE(generate("SELECT (CASE WHEN 1 THEN NULL ELSE 0 END) COLLATE BINARY;") ==
            "auto rows = storage.select(as_optional(case_<int>().when(1, then(nullptr)).else_(0).end()));");
}

// The operand a simple CASE compares and the conditions of a searched one pick the branch rather
// than fill it, so a NULL among them takes no branch and leaves the value to the ELSE:
// `SELECT CASE NULL WHEN 1 THEN 2 ELSE 3 END` and `SELECT CASE WHEN NULL THEN 2 ELSE 3 END` are
// both 3 in sqlite3 3.51, and `CASE WHEN a THEN 'x' ELSE 'y' END` over a NULL `a` is 'y'. With an
// ELSE written and no result that can be NULL, `case_<R>` carries every value the CASE answers.
TEST_CASE("codegen: a CASE result column that cannot be NULL keeps the type sqlite_orm gives it") {
    REQUIRE(generate("SELECT CASE WHEN 1 THEN 2 ELSE 3 END;") ==
            "auto rows = storage.select(case_<int>().when(1, then(2)).else_(3).end());");
    REQUIRE(generate("SELECT CASE NULL WHEN 1 THEN 2 ELSE 3 END;") ==
            "auto rows = storage.select(case_<int>(nullptr).when(1, then(2)).else_(3).end());");
    REQUIRE(generate("SELECT CASE WHEN NULL THEN 2 ELSE 3 END;") ==
            "auto rows = storage.select(case_<int>().when(nullptr, then(2)).else_(3).end());");
    REQUIRE(generate("SELECT CASE a WHEN 1 THEN 2 ELSE 3 END FROM users;") ==
            "auto rows = storage.select(case_<int>(&Users::a).when(1, then(2)).else_(3).end());");
    REQUIRE(generate("SELECT CASE WHEN a THEN 'x' ELSE 'y' END FROM users;") ==
            "auto rows = storage.select(case_<std::string>().when(&Users::a, then(\"x\")).else_(\"y\").end());");
    // The same answer is given for a CASE standing as an argument: `length` only propagates a NULL
    // its argument holds, and this CASE holds none, so the call is not widened either.
    REQUIRE(
        generate("SELECT length(CASE WHEN a THEN 'x' ELSE 'y' END) FROM users;") ==
        "auto rows = storage.select(length(case_<std::string>().when(&Users::a, then(\"x\")).else_(\"y\").end()));");
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
                               SourceLocation{1, 10},
                               1}});
    REQUIRE(generateFull("SELECT a - 1 FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `-` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 10},
                               1}});
    REQUIRE(generateFull("SELECT a * 2 FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `*` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 10},
                               1}});
    REQUIRE(generateFull("SELECT a / 2 FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `/` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 10},
                               1}});
    REQUIRE(generateFull("SELECT a % a FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `%` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 10},
                               1}});
    // Several columns computed with the same operator report once, anchored at the first of them.
    REQUIRE(generateFull("SELECT a + 1, a + 2 FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 10},
                               1}});
    // A unary plus generates its operand's code, so the operand is the one the warning names and
    // is anchored at: the `+` of `a + 0`, at column 12, not the one the column starts with.
    REQUIRE(generateFull("SELECT +(a + 0) FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 12},
                               1}});
    // A negation reaches sqlite_orm as the subtraction `0 - x`, which is typed `double` too; the
    // minus sign of the SQL is the token the message names.
    REQUIRE(generateFull("SELECT -a FROM users;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `-` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 8},
                               1}});
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
                               SourceLocation{1, 25},
                               1}});
    REQUIRE(generateFull("SELECT 9007199254740992 + 1;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 25},
                               1}});
    REQUIRE(generateFull("SELECT 4503599627370496 * 2 + 1;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 29},
                               1}});
    // `~` casts its operand to an INTEGER first, so a REAL operand is not carried through it the
    // way it is through the arithmetic operators: this one answers the INTEGER -9007199254740995.
    REQUIRE(generateFull("SELECT 1 + ~9007199254740994.0;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 10},
                               1}});
    // A magnitude past 2^53 is reported even when both operands are literals.
    REQUIRE(generateFull("SELECT 9223372036854775807 + 0;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `+` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 28},
                               1}});
    REQUIRE(generateFull("SELECT 100000000 * 100000000;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"result column computed with `*` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 18},
                               1}});
}

// A unary plus is the identity SQLite treats it as, so a statement carrying one is generated
// rather than rejected — `sqlite2orm -e 'SELECT +a;'` used to exit 1 on `unary plus (+expr) is
// not supported in sqlite_orm` — and it is generated as the statement over the bare operand.
// sqlite3 3.51 over `users(a INTEGER)` holding 7 answers `+a`, `+a + 1`, `1 + +a` and `+upper(a)`
// with 7, 8, 8 and 7.
TEST_CASE("codegen: a statement with a unary plus generates what the bare operand generates") {
    REQUIRE(generate("SELECT +a FROM users;") == "auto rows = storage.select(&Users::a);");
    REQUIRE(generate("SELECT a FROM users;") == "auto rows = storage.select(&Users::a);");
    REQUIRE(generate("SELECT +a + 1 FROM users;") == "auto rows = storage.select(as_optional(c(&Users::a) + 1));");
    REQUIRE(generate("SELECT a + 1 FROM users;") == "auto rows = storage.select(as_optional(c(&Users::a) + 1));");
    REQUIRE(generate("SELECT 1 + +a FROM users;") == "auto rows = storage.select(as_optional(c(1) + &Users::a));");
    REQUIRE(generate("SELECT 1 + a FROM users;") == "auto rows = storage.select(as_optional(c(1) + &Users::a));");
    REQUIRE(generate("SELECT +upper(a) FROM users;") == "auto rows = storage.select(as_optional(upper(&Users::a)));");
    REQUIRE(generate("SELECT upper(a) FROM users;") == "auto rows = storage.select(as_optional(upper(&Users::a)));");
    // A plus over a plus is the same identity twice, and so is one over a parenthesised operand.
    REQUIRE(generate("SELECT + +a FROM users;") == "auto rows = storage.select(&Users::a);");
    REQUIRE(generate("SELECT +(a) FROM users;") == "auto rows = storage.select(&Users::a);");
    // Every clause holds an expression, and the plus is dropped in each of them the same way.
    REQUIRE(generate("SELECT a FROM users WHERE +a ORDER BY +a LIMIT +1;") ==
            "auto rows = storage.select(&Users::a, where(&Users::a), order_by(&Users::a), limit(1));");
}

// Dropping the plus is not enough on its own: what the generation around it asks of its operand
// has to reach that operand too. `operator!` is the one sqlite_orm operator that keeps the `c(...)`
// its operand carries, and the walk that collects a statement's tables stops at such a wrapper, so
// a column under a NOT is generated as `column<T>(&T::a)` instead — a request the plus used to
// reset on its way down, which put `not c(&Users::a)` back and ran the select with no FROM clause
// at all (`SQL logic error`, the very bug #49 fixed). Read back in "runtime: a unary plus under a
// NOT keeps the column form the table walk reads".
TEST_CASE("codegen: a unary plus passes on what the generation around it asks of its operand") {
    REQUIRE(generate("SELECT NOT a FROM users;") ==
            "auto rows = storage.select(as_optional(not column<Users>(&Users::a)));");
    REQUIRE(generate("SELECT NOT +a FROM users;") ==
            "auto rows = storage.select(as_optional(not column<Users>(&Users::a)));");
    REQUIRE(generate("SELECT NOT +(a) FROM users;") ==
            "auto rows = storage.select(as_optional(not column<Users>(&Users::a)));");
    REQUIRE(generate("SELECT NOT + +a FROM users;") ==
            "auto rows = storage.select(as_optional(not column<Users>(&Users::a)));");
    REQUIRE(generate("SELECT * FROM users WHERE NOT +a;") ==
            "auto rows = storage.get_all<Users>(where(not column<Users>(&Users::a)));");
    // A column that names a SELECT alias is generated as `get<Alias>()` whatever is asked for, and
    // the plus is transparent for that answer as well.
    REQUIRE(generate("SELECT a AS i FROM users WHERE NOT +i;") ==
            "auto rows = storage.select(as<colalias_i>(&Users::a), where(not c(get<colalias_i>())));");
}

// SQLite's parser drops a unary plus before it reads the sign above it, so the plus does not break
// a chain of signs: sqlite3 3.51 answers `SELECT -+1` with the INTEGER -1 and `typeof(-+1)` with
// `integer`, where the `0 - expr` subtraction the generator falls back to would read the row back
// through a double. A COLLATE does break it — `SELECT -+(0x8000000000000000 COLLATE BINARY)` is
// the negation 9.22337203685478e+18 SQLite computes, not a literal it refuses — so only the
// pluses are stepped through.
TEST_CASE("codegen: a sign over a unary plus is folded the way SQLite folds it") {
    REQUIRE(generate("SELECT -1 FROM users;") == "auto rows = storage.select(-1);");
    REQUIRE(generate("SELECT -+1 FROM users;") == "auto rows = storage.select(-1);");
    REQUIRE(generate("SELECT - + + 1 FROM users;") == "auto rows = storage.select(-1);");
    REQUIRE(generate("SELECT -+1.5 FROM users;") == "auto rows = storage.select(-1.5);");
    REQUIRE(generate("SELECT -+0x10 FROM users;") == "auto rows = storage.select(-0x10);");
    REQUIRE(generate("SELECT -+2147483648 FROM users;") == "auto rows = storage.select(-2147483648);");
    REQUIRE(generate("SELECT -+9223372036854775807 FROM users;") ==
            "auto rows = storage.select(-9223372036854775807);");
    // A COLLATE under the plus stops the fold, exactly as it stops it without one.
    REQUIRE(generate("SELECT -+(1 COLLATE BINARY) FROM users;") == "auto rows = storage.select((c(0) - c(1)));");
    REQUIRE(generate("SELECT -(1 COLLATE BINARY) FROM users;") == "auto rows = storage.select((c(0) - c(1)));");
}

// The one thing a unary plus carries that its operand does not: SQLite applies the affinity of a
// column to the other side of a comparison, and `+a` is not a column reference, so the comparison
// runs without it. Over `t(a TEXT)` holding '1', sqlite3 3.51 answers `a = 1` with 1 and `+a = 1`
// with 0; the same pair of answers comes out of `<`, `<>` and the other comparisons, and a table
// qualifier changes nothing — `+t.a = 1` is 0 as well. sqlite_orm has no form that takes an
// affinity away, so the generated comparison is the plus-less one and the loss is reported.
TEST_CASE("codegen: a compared unary plus reports the column affinity it takes away") {
    REQUIRE(generateFull("SELECT * FROM users WHERE +a = 1;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"unary plus over column `a` is dropped: it takes the column's affinity "
                               "out of the comparison, and sqlite_orm has no form that does. Where that "
                               "affinity carries — a TEXT column `t` holding '1' — SQLite answers "
                               "`t = 1` with 1 and `+t = 1` with 0, while the generated comparison is "
                               "the one without the plus either way. Whether this column is one of "
                               "those depends on the affinity it was declared with, which is not read "
                               "here",
                               SourceLocation{1, 27},
                               1}});
    // Both operands are read for their affinity, and both are reported, in the order written.
    REQUIRE(generateFull("SELECT * FROM users WHERE +a <> +b;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"unary plus over column `a` is dropped: it takes the column's affinity "
                               "out of the comparison, and sqlite_orm has no form that does. Where that "
                               "affinity carries — a TEXT column `t` holding '1' — SQLite answers "
                               "`t = 1` with 1 and `+t = 1` with 0, while the generated comparison is "
                               "the one without the plus either way. Whether this column is one of "
                               "those depends on the affinity it was declared with, which is not read "
                               "here",
                               SourceLocation{1, 27},
                               1},
                CodegenWarning{"unary plus over column `b` is dropped: it takes the column's affinity "
                               "out of the comparison, and sqlite_orm has no form that does. Where that "
                               "affinity carries — a TEXT column `t` holding '1' — SQLite answers "
                               "`t = 1` with 1 and `+t = 1` with 0, while the generated comparison is "
                               "the one without the plus either way. Whether this column is one of "
                               "those depends on the affinity it was declared with, which is not read "
                               "here",
                               SourceLocation{1, 33},
                               1}});
    // A table qualifier is a column reference all the same, and the affinity it loses is the
    // same one: sqlite3 3.51 answers `+t.a = 1` the way it answers `+a = 1`.
    REQUIRE(generateFull("SELECT * FROM users WHERE +users.a >= 1;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"unary plus over column `a` is dropped: it takes the column's affinity "
                               "out of the comparison, and sqlite_orm has no form that does. Where that "
                               "affinity carries — a TEXT column `t` holding '1' — SQLite answers "
                               "`t = 1` with 1 and `+t = 1` with 0, while the generated comparison is "
                               "the one without the plus either way. Whether this column is one of "
                               "those depends on the affinity it was declared with, which is not read "
                               "here",
                               SourceLocation{1, 27},
                               1}});
    // This anchor is one of the three that write their length inline instead of measuring it from
    // source text (see `underlineLengthOf`): the plus is a single ASCII character, which is one
    // character in either count. What the tokenizer hands it is not ASCII-only, though, so the
    // column it is anchored at is in characters — sqlite3 3.51 takes the comment below and answers
    // the two comparisons 1 and 0 all the same — and the inline 1 still underlines just the plus.
    REQUIRE(generateFull("/* «üü» */ SELECT * FROM users WHERE +a = 1;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"unary plus over column `a` is dropped: it takes the column's affinity "
                               "out of the comparison, and sqlite_orm has no form that does. Where that "
                               "affinity carries — a TEXT column `t` holding '1' — SQLite answers "
                               "`t = 1` with 1 and `+t = 1` with 0, while the generated comparison is "
                               "the one without the plus either way. Whether this column is one of "
                               "those depends on the affinity it was declared with, which is not read "
                               "here",
                               SourceLocation{1, 38},
                               1}});
    // A COLLATE keeps the affinity of the column under it — sqlite3 3.51 answers
    // `(a COLLATE BINARY) = 1` the way it answers `a = 1` — and a plus over one takes it away
    // just the same, so the column under both is the one reported.
    REQUIRE(generateFull("SELECT * FROM users WHERE +(a COLLATE BINARY) > 1;").warnings ==
            std::vector<CodegenWarning>{
                CodegenWarning{"COLLATE BINARY on expressions is not directly supported in sqlite_orm "
                               "codegen"},
                CodegenWarning{"unary plus over column `a` is dropped: it takes the column's affinity "
                               "out of the comparison, and sqlite_orm has no form that does. Where that "
                               "affinity carries — a TEXT column `t` holding '1' — SQLite answers "
                               "`t = 1` with 1 and `+t = 1` with 0, while the generated comparison is "
                               "the one without the plus either way. Whether this column is one of "
                               "those depends on the affinity it was declared with, which is not read "
                               "here",
                               SourceLocation{1, 27},
                               1}});
}

// Only a comparison against a column reference reads an affinity, so a plus anywhere else is
// dropped without changing an answer and is not reported. sqlite3 3.51 over `t(a TEXT)` holding
// '1' answers `a + 1 = 2` and `+a + 1 = 2` with 1, `(a || '') = 1` and `(+a || '') = 1` with 0,
// `a = +1` and `a = 1` with 1, and `a LIKE 1` and `+a LIKE 1` with 1 — LIKE, GLOB and MATCH apply
// no column affinity at all. Two slots that do read one are silent all the same: the BETWEEN and
// IN branches build their result without any codegen warning (card 1867000382920590460), and so
// `+a BETWEEN 1 AND 2` and `+a IN (1, 2)` are generated plus-less and unreported although sqlite3
// answers them 0 where the plus-less form answers 1.
TEST_CASE("codegen: a unary plus that costs no affinity is not reported") {
    REQUIRE(generateFull("SELECT +a FROM users;").warnings.empty());
    REQUIRE(generateFull("SELECT * FROM users WHERE +a + 1 = 2;").warnings.empty());
    REQUIRE(generateFull("SELECT * FROM users WHERE a = +1;").warnings.empty());
    REQUIRE(generateFull("SELECT * FROM users WHERE +1 = 1;").warnings.empty());
    REQUIRE(generateFull("SELECT * FROM users WHERE +upper(a) = 'A';").warnings.empty());
    REQUIRE(generateFull("SELECT * FROM users WHERE +a LIKE 'x';").warnings.empty());
    REQUIRE(generateFull("SELECT * FROM users WHERE +a GLOB 'x';").warnings.empty());
    REQUIRE(generateFull("SELECT * FROM users WHERE +a BETWEEN 1 AND 2;").warnings.empty());
    REQUIRE(generateFull("SELECT * FROM users WHERE +a IN (1, 2);").warnings.empty());
}

// A column the SQL leaves unqualified belongs to the FROM source all the same, and an aliased
// source answers to its alias only: written as `&T::x`, the column names the plain table, which
// sqlite_orm then puts in the FROM it infers next to the alias — `SELECT "Album"."Title" FROM
// "Album", "Album" "a" LEFT JOIN …`, a cartesian product the SQL never asked for (card
// 1868205961584313865). The form is the one a column the SQL qualified with that alias takes.
TEST_CASE("codegen: an unqualified column of an aliased source is generated through the alias") {
    REQUIRE(generate("SELECT name FROM users u;") ==
            "auto rows = storage.select(alias_column<alias_a<Users>>(&Users::name));");
}

TEST_CASE("codegen: an unqualified column of an aliased source in WHERE and ORDER BY") {
    REQUIRE(generate("SELECT name FROM users u WHERE id > 5 ORDER BY id;") ==
            "auto rows = storage.select(alias_column<alias_a<Users>>(&Users::name), "
            "where(c(alias_column<alias_a<Users>>(&Users::id)) > 5), "
            "order_by(alias_column<alias_a<Users>>(&Users::id)));");
}

TEST_CASE("codegen: an unqualified column of an aliased source C++20 style") {
    CodeGenPolicy pol;
    pol.chosenAlternativeValueByCategory["table_alias_style"] = "cpp20";
    auto result = generateWithPolicy("SELECT name FROM users u", pol);
    REQUIRE(result.code == "constexpr orm_table_alias auto u = \"u\"_alias.for_<Users>();\n"
                           "auto rows = storage.select(u->*&Users::name);");
}

// The card's LEFT JOIN: the result column is the one source the SQL never names, and only the
// alias keeps the join two tables wide.
TEST_CASE("codegen: an unqualified column of the left source of a LEFT JOIN") {
    REQUIRE(generate("SELECT name FROM users a LEFT JOIN orders b ON a.id = b.id WHERE b.id IS NULL "
                     "ORDER BY a.id;") == "auto rows = storage.select(alias_column<alias_a<Users>>(&Users::name), "
                                          "left_join<alias_b<Orders>>(on(alias_column<alias_a<Users>>(&Users::id) == "
                                          "alias_column<alias_b<Orders>>(&Orders::id))), "
                                          "where(is_null(alias_column<alias_b<Orders>>(&Orders::id))), "
                                          "order_by(alias_column<alias_a<Users>>(&Users::id)));");
}

// A source with no alias is still read from the plain struct: nothing about it is aliased, and the
// FROM sqlite_orm infers names it exactly once.
TEST_CASE("codegen: an unqualified column of an unaliased source keeps the member pointer") {
    REQUIRE(generate("SELECT name FROM users;") == "auto rows = storage.select(&Users::name);");
    REQUIRE(generate("SELECT name FROM users JOIN orders ON users.id = orders.id;") ==
            "auto rows = storage.select(&Users::name, join<Orders>(on(c(&Users::id) == &Orders::id)));");
}

// `count<alias_a<T>>()` names no table in the FROM sqlite_orm infers — `count<T>()` names the
// plain table, the one the alias replaced — so the sources are written out for it.
TEST_CASE("codegen: COUNT(*) over an aliased source names the source") {
    REQUIRE(generate("SELECT COUNT(*) FROM users u;") ==
            "auto rows = storage.select(count<alias_a<Users>>(), from<alias_a<Users>>());");
    REQUIRE(generate("SELECT COUNT(*) FROM users u WHERE id > 5;") ==
            "auto rows = storage.select(count<alias_a<Users>>(), from<alias_a<Users>>(), "
            "where(c(alias_column<alias_a<Users>>(&Users::id)) > 5));");
}

TEST_CASE("codegen: COUNT(*) over an unaliased source keeps the table type") {
    REQUIRE(generate("SELECT COUNT(*) FROM users;") == "auto rows = storage.select(count<Users>());");
}

// A comma or CROSS JOIN list of aliased sources cannot go through `cross_join<alias_b<T>>()`:
// sqlite_orm writes that out as a plain `CROSS JOIN "users"`, the alias only being serialized for
// a join that carries an ON. The sources are named instead, which is the same FROM in the same
// order — `FROM "users" "a", "users" "b"`.
TEST_CASE("codegen: a self-join over a comma names both sources") {
    REQUIRE(generate("SELECT a.name, b.name FROM users a, users b") ==
            "auto rows = storage.select(columns(alias_column<alias_a<Users>>(&Users::name), "
            "alias_column<alias_b<Users>>(&Users::name)), from<alias_a<Users>, alias_b<Users>>());");
}

TEST_CASE("codegen: a CROSS JOIN over aliased sources names both sources") {
    REQUIRE(generate("SELECT a.name FROM users a CROSS JOIN orders b WHERE a.id = b.id;") ==
            "auto rows = storage.select(alias_column<alias_a<Users>>(&Users::name), "
            "from<alias_a<Users>, alias_b<Orders>>(), "
            "where(alias_column<alias_a<Users>>(&Users::id) == alias_column<alias_b<Orders>>(&Orders::id)));");
}

TEST_CASE("codegen: a comma join of unaliased sources keeps cross_join") {
    REQUIRE(generate("SELECT users.name FROM users, orders") ==
            "auto rows = storage.select(&Users::name, cross_join<Orders>());");
}

// One aliased source among plain ones is still a source of its own, and the list names each of
// them the way the SQL wrote it.
TEST_CASE("codegen: a comma join names a plain source next to an aliased one") {
    REQUIRE(generate("SELECT a.name FROM users a, orders") ==
            "auto rows = storage.select(alias_column<alias_a<Users>>(&Users::name), from<alias_a<Users>, Orders>());");
}

// A join that carries an ON writes its own alias out, so only the comma run ahead of it is named.
TEST_CASE("codegen: a named comma run keeps a constrained join of its own") {
    REQUIRE(generate("SELECT a.name FROM users a, users b JOIN orders c ON c.id = a.id;") ==
            "auto rows = storage.select(alias_column<alias_a<Users>>(&Users::name), "
            "from<alias_a<Users>, alias_b<Users>>(), "
            "join<alias_c<Orders>>(on(alias_column<alias_c<Orders>>(&Orders::id) == "
            "alias_column<alias_a<Users>>(&Users::id))));");
}

// `SELECT *` reads its row from the plain struct — `asterisk<T>()` names no alias — so naming the
// sources would leave the star reading a table the select no longer selects from, which SQLite
// refuses outright. The star keeps the form it had until that slot is fixed too.
TEST_CASE("codegen: a star over aliased sources is left as it was") {
    REQUIRE(generate("SELECT * FROM users u;") == "auto rows = storage.get_all<Users>();");
    REQUIRE(generate("SELECT * FROM users a, users b") ==
            "auto rows = storage.get_all<Users>(cross_join<alias_b<Users>>());");
}

// A subquery answers for its own sources the same way; the enclosing select is untouched by it.
TEST_CASE("codegen: a subquery over aliased sources names its own sources") {
    REQUIRE(generate("SELECT name FROM users WHERE id IN (SELECT COUNT(*) FROM orders o);") ==
            "auto rows = storage.select(&Users::name, where(in(&Users::id, select(count<alias_a<Orders>>(), "
            "from<alias_a<Orders>>()))));");
}

// The named FROM is a form the SQL did not write, so the generated code explains itself.
TEST_CASE("codegen: naming the sources of an aliased select carries its comment") {
    REQUIRE(generateFull("SELECT COUNT(*) FROM users u;").comments ==
            std::vector<std::string>{kAliasedFromSourcesComment});
    REQUIRE(generateFull("SELECT a.name FROM users a, users b").comments ==
            std::vector<std::string>{kAliasedFromSourcesComment});
    REQUIRE(generateFull("SELECT u.name FROM users u").comments.empty());
}

// Whether the sources have to be named is only settled once every clause is generated: a
// `count(*)` standing in a HAVING or an ORDER BY names the aliased source just as one among the
// result columns does, and `count<alias_a<T>>()` carries no table into an inferred FROM. Deciding
// before the tail clauses left such a select with no FROM at all — `SELECT 1 GROUP BY 1 HAVING
// COUNT(*) > 1` — so the clause is written at one point, after them.
TEST_CASE("codegen: COUNT(*) in HAVING over an aliased source names the source") {
    REQUIRE(generate("SELECT 1 FROM users u GROUP BY 1 HAVING COUNT(*) > 1;") ==
            "auto rows = storage.select(1, from<alias_a<Users>>(), group_by(1).having(count<alias_a<Users>>() > 1));");
}

TEST_CASE("codegen: COUNT(*) in ORDER BY over an aliased source names the source") {
    REQUIRE(generate("SELECT name FROM users u GROUP BY name ORDER BY COUNT(*) DESC;") ==
            "auto rows = storage.select(alias_column<alias_a<Users>>(&Users::name), from<alias_a<Users>>(), "
            "group_by(alias_column<alias_a<Users>>(&Users::name)), order_by(count<alias_a<Users>>()).desc());");
}

// The named FROM stands ahead of the clauses that follow it, whichever of them the `count(*)` was
// generated into.
TEST_CASE("codegen: a FROM named for a HAVING stands before the clauses that follow") {
    REQUIRE(generate("SELECT 1 FROM users u GROUP BY 1 HAVING COUNT(*) > 1 LIMIT 1;") ==
            "auto rows = storage.select(1, from<alias_a<Users>>(), group_by(1).having(count<alias_a<Users>>() > 1), "
            "limit(1));");
}

// A subquery in the HAVING answers for its own sources, and the outer select still answers for
// its own: left unnamed, the outer FROM took in the source of the subquery and gave two sources
// the one alias `"a"`, which SQLite refuses.
TEST_CASE("codegen: a subquery in HAVING leaves the outer FROM named") {
    REQUIRE(generate("SELECT uid FROM orders o GROUP BY uid HAVING COUNT(*) > "
                     "(SELECT COUNT(*) FROM users u WHERE u.id > 2);") ==
            "auto rows = storage.select(alias_column<alias_a<Orders>>(&Orders::uid), from<alias_a<Orders>>(), "
            "group_by(alias_column<alias_a<Orders>>(&Orders::uid)).having(count<alias_a<Orders>>() > "
            "select(count<alias_a<Users>>(), from<alias_a<Users>>(), "
            "where(alias_column<alias_a<Users>>(&Users::id) > 2))));");
}

// A join carrying an ON writes its own alias out, so only the source the `count(*)` stands for is
// named — and the join keeps its place after it.
TEST_CASE("codegen: COUNT(*) over an aliased source next to a constrained join") {
    REQUIRE(generate("SELECT COUNT(*) FROM users u JOIN orders o ON u.id = o.uid;") ==
            "auto rows = storage.select(count<alias_a<Users>>(), from<alias_a<Users>>(), "
            "join<alias_b<Orders>>(on(alias_column<alias_a<Users>>(&Users::id) == "
            "alias_column<alias_b<Orders>>(&Orders::uid))));");
}

TEST_CASE("codegen: naming the sources for a COUNT(*) in HAVING carries its comment") {
    REQUIRE(generateFull("SELECT 1 FROM users u GROUP BY 1 HAVING COUNT(*) > 1;").comments ==
            std::vector<std::string>{kAliasedFromSourcesComment});
}
