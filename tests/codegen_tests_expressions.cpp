#include "codegen_tests_common.hpp"

TEST_CASE("codegen: integer literal") {
    REQUIRE(generate("42") == "42");
    REQUIRE(generate("0") == "0");
    REQUIRE(generate("999999") == "999999");
    REQUIRE(generate("0xFF") == "0xFF");
}

TEST_CASE("codegen: real literal") {
    REQUIRE(generate("3.14") == "3.14");
    REQUIRE(generate(".5") == ".5");
    REQUIRE(generate("1e10") == "1e10");
    REQUIRE(generate("3.14e-2") == "3.14e-2");
}

TEST_CASE("codegen: string literal") {
    REQUIRE(generate("'hello'") == "\"hello\"");
    REQUIRE(generate("'it''s'") == "\"it's\"");
    REQUIRE(generate("''") == "\"\"");
}

TEST_CASE("codegen: string literal with special chars") {
    REQUIRE(generate(R"('has "quotes"')") == R"("has \"quotes\"")");
    REQUIRE(generate(R"('back\slash')") == R"("back\\slash")");
}

TEST_CASE("codegen: null literal") {
    REQUIRE(generate("NULL") == "nullptr");
}

TEST_CASE("codegen: bool literal") {
    REQUIRE(generate("TRUE") == "true");
    REQUIRE(generate("FALSE") == "false");
}

TEST_CASE("codegen: column ref") {
    REQUIRE(generate("name") == "&User::name");
    REQUIRE(generate(R"("my column")") == "&User::my_column");
}

TEST_CASE("codegen: qualified column ref") {
    REQUIRE(generate("users.name") == "&Users::name");
    REQUIRE(generate(R"([my table].[my col])") == "&MyTable::my_col");
}

TEST_CASE("codegen: NEW ref") {
    REQUIRE(generate("NEW.col") == "new_(&User::col)");
}

TEST_CASE("codegen: OLD ref") {
    REQUIRE(generate("OLD.col") == "old(&User::col)");
}

TEST_CASE("codegen: comparison operators") {
    REQUIRE(generate_full("42 = 5") == expected_binary_leaf("42", "5", " == ", "is_equal"));
    REQUIRE(generate_full("a == b") == expected_binary_leaf("&User::a", "&User::b", " == ", "is_equal"));
    REQUIRE(generate_full("a != 5") == expected_binary_leaf("&User::a", "5", " != ", "is_not_equal"));
    REQUIRE(generate_full("a <> 5") == expected_binary_leaf("&User::a", "5", " != ", "is_not_equal"));
    REQUIRE(generate_full("a < 5") == expected_binary_leaf("&User::a", "5", " < ", "lesser_than"));
    REQUIRE(generate_full("a <= 5") == expected_binary_leaf("&User::a", "5", " <= ", "lesser_or_equal"));
    REQUIRE(generate_full("a > 3.14") == expected_binary_leaf("&User::a", "3.14", " > ", "greater_than"));
    REQUIRE(generate_full("a >= 0") == expected_binary_leaf("&User::a", "0", " >= ", "greater_or_equal"));
    REQUIRE(generate_full("name = 'hello'") == expected_binary_leaf("&User::name", R"("hello")", " == ", "is_equal"));
    REQUIRE(generate_full("users.id = 42") == expected_binary_leaf("&Users::id", "42", " == ", "is_equal"));
}

TEST_CASE("codegen: arithmetic operators") {
    REQUIRE(generate_full("a + 5") == expected_binary_leaf("&User::a", "5", " + ", "add"));
    REQUIRE(generate_full("a - 5") == expected_binary_leaf("&User::a", "5", " - ", "sub"));
    REQUIRE(generate_full("a * 5") == expected_binary_leaf("&User::a", "5", " * ", "mul"));
    REQUIRE(generate_full("a / 5") == expected_binary_leaf("&User::a", "5", " / ", "div"));
    REQUIRE(generate_full("a % 5") == expected_binary_leaf("&User::a", "5", " % ", "mod"));
}

TEST_CASE("codegen: concatenation") {
    SECTION("simple") {
        REQUIRE(generate_full("a || b") == expected_binary_leaf("&User::a", "&User::b", " || ", "conc"));
    }
    SECTION("chained: (a || b) || c") {
        auto result = generate_full("a || b || c");
        REQUIRE(result == CodeGenResult{
            "c(&User::a) || &User::b || &User::c",
            {
                column_ref_style_dp(1, "&User::a"),
                column_ref_style_dp(2, "&User::b"),
                DecisionPoint{3, "expr_style", "operator_wrap_left", "c(&User::a) || &User::b",
                    {
                        Alternative{"operator_wrap_right", "&User::a || c(&User::b)", "wrap right operand"},
                        Alternative{"functional", "conc(&User::a, &User::b)", "functional style"},
                        Alternative{"operator_wrap_both", "c(&User::a) || c(&User::b)", "wrap both operands", true},
                    }},
                column_ref_style_dp(4, "&User::c"),
                DecisionPoint{5, "expr_style", "operator_wrap_left", "c(&User::a) || &User::b || &User::c",
                    {
                        Alternative{"operator_wrap_right", "c(&User::a) || &User::b || c(&User::c)", "wrap right operand"},
                        Alternative{"functional", "conc(c(&User::a) || &User::b, &User::c)", "functional style"},
                        Alternative{"operator_wrap_both", "c(&User::a) || &User::b || c(&User::c)", "wrap both operands", true},
                    }},
            }
        });
    }
}

TEST_CASE("codegen: bitwise operators") {
    REQUIRE(generate_full("a & 5") == expected_binary_leaf("&User::a", "5", " & ", "bitwise_and"));
    REQUIRE(generate_full("a | 5") == expected_binary_leaf("&User::a", "5", " | ", "bitwise_or"));
    REQUIRE(generate_full("a << 2") == expected_binary_leaf("&User::a", "2", " << ", "bitwise_shift_left"));
    REQUIRE(generate_full("a >> 2") == expected_binary_leaf("&User::a", "2", " >> ", "bitwise_shift_right"));
}

TEST_CASE("codegen: unary minus") {
    SECTION("-5") {
        auto result = generate_full("-5");
        REQUIRE(result == CodeGenResult{"-c(5)", {DecisionPoint{1, "expr_style", "operator", "-c(5)",
            {Alternative{"functional", "minus(5)", "functional style"}}
        }}});
    }
    SECTION("-a") {
        auto result = generate_full("-a");
        REQUIRE(result == CodeGenResult{"-c(&User::a)",
            {
                column_ref_style_dp(1, "&User::a"),
                DecisionPoint{2, "expr_style", "operator", "-c(&User::a)",
                              {Alternative{"functional", "minus(&User::a)", "functional style"}}},
            }});
    }
}

TEST_CASE("codegen: unary plus is no-op") {
    auto result5 = generate_full("+5");
    REQUIRE(result5 == CodeGenResult{"5", {}});
    auto resultA = generate_full("+a");
    REQUIRE(resultA == CodeGenResult{"&User::a", {column_ref_style_dp(1, "&User::a")}});
}

TEST_CASE("codegen: bitwise not") {
    SECTION("~5") {
        auto result = generate_full("~5");
        REQUIRE(result == CodeGenResult{"~c(5)", {DecisionPoint{1, "expr_style", "operator", "~c(5)",
            {Alternative{"functional", "bitwise_not(5)", "functional style"}}
        }}});
    }
    SECTION("~a") {
        auto result = generate_full("~a");
        REQUIRE(result == CodeGenResult{"~c(&User::a)",
            {
                column_ref_style_dp(1, "&User::a"),
                DecisionPoint{2, "expr_style", "operator", "~c(&User::a)",
                              {Alternative{"functional", "bitwise_not(&User::a)", "functional style"}}},
            }});
    }
}

TEST_CASE("codegen: logical AND") {
    SECTION("leaf operands") {
        REQUIRE(generate_full("a AND b") == expected_binary_leaf("&User::a", "&User::b", " and ", "and_"));
    }
    SECTION("compound operands: a = 1 AND b = 2") {
        auto result = generate_full("a = 1 AND b = 2");
        auto leftEq = expected_binary_leaf("&User::a", "1", " == ", "is_equal", 1);
        auto rightEq = expected_binary_leaf("&User::b", "2", " == ", "is_equal", 3);
        std::vector<DecisionPoint> expectedDps;
        expectedDps.insert(expectedDps.end(), leftEq.decisionPoints.begin(), leftEq.decisionPoints.end());
        expectedDps.insert(expectedDps.end(), rightEq.decisionPoints.begin(), rightEq.decisionPoints.end());
        expectedDps.push_back(DecisionPoint{5, "expr_style", "operator_wrap_left", "c(&User::a) == 1 and c(&User::b) == 2",
            {
                Alternative{"operator_wrap_right", "c(&User::a) == 1 and c(&User::b) == 2", "wrap right operand"},
                Alternative{"functional", "and_(c(&User::a) == 1, c(&User::b) == 2)", "functional style"},
                Alternative{"operator_wrap_both", "c(&User::a) == 1 and c(&User::b) == 2", "wrap both operands", true},
            }});
        REQUIRE(result == CodeGenResult{"c(&User::a) == 1 and c(&User::b) == 2", std::move(expectedDps)});
    }
}

TEST_CASE("codegen: logical OR") {
    SECTION("leaf operands") {
        REQUIRE(generate_full("a OR b") == expected_binary_leaf("&User::a", "&User::b", " or ", "or_"));
    }
    SECTION("compound operands: a = 1 OR b = 2") {
        auto result = generate_full("a = 1 OR b = 2");
        auto leftEq = expected_binary_leaf("&User::a", "1", " == ", "is_equal", 1);
        auto rightEq = expected_binary_leaf("&User::b", "2", " == ", "is_equal", 3);
        std::vector<DecisionPoint> expectedDps;
        expectedDps.insert(expectedDps.end(), leftEq.decisionPoints.begin(), leftEq.decisionPoints.end());
        expectedDps.insert(expectedDps.end(), rightEq.decisionPoints.begin(), rightEq.decisionPoints.end());
        expectedDps.push_back(DecisionPoint{5, "expr_style", "operator_wrap_left", "c(&User::a) == 1 or c(&User::b) == 2",
            {
                Alternative{"operator_wrap_right", "c(&User::a) == 1 or c(&User::b) == 2", "wrap right operand"},
                Alternative{"functional", "or_(c(&User::a) == 1, c(&User::b) == 2)", "functional style"},
                Alternative{"operator_wrap_both", "c(&User::a) == 1 or c(&User::b) == 2", "wrap both operands", true},
            }});
        REQUIRE(result == CodeGenResult{"c(&User::a) == 1 or c(&User::b) == 2", std::move(expectedDps)});
    }
}

TEST_CASE("codegen: logical NOT") {
    SECTION("leaf operand") {
        auto result = generate_full("NOT a");
        REQUIRE(result == CodeGenResult{"not c(&User::a)",
            {
                column_ref_style_dp(1, "&User::a"),
                DecisionPoint{2, "expr_style", "operator", "not c(&User::a)",
                              {
                                  Alternative{"operator_excl", "!c(&User::a)", "use ! instead of not"},
                                  Alternative{"functional", "not_(&User::a)", "functional style"},
                              }},
            }});
    }
    SECTION("compound operand: NOT -a") {
        auto result = generate_full("NOT -a");
        REQUIRE(result == CodeGenResult{
            "not (-c(&User::a))",
            {
                column_ref_style_dp(1, "&User::a"),
                DecisionPoint{2, "expr_style", "operator", "-c(&User::a)",
                              {Alternative{"functional", "minus(&User::a)", "functional style"}}},
                DecisionPoint{3, "expr_style", "operator", "not (-c(&User::a))",
                              {
                                  Alternative{"operator_excl", "!(-c(&User::a))", "use ! instead of not"},
                                  Alternative{"functional", "not_(-c(&User::a))", "functional style"},
                              }},
            }
        });
    }
}

TEST_CASE("codegen: double unary minus parenthesized") {
    auto result = generate_full("- -a");
    REQUIRE(result == CodeGenResult{
        "-(-c(&User::a))",
        {
            column_ref_style_dp(1, "&User::a"),
            DecisionPoint{2, "expr_style", "operator", "-c(&User::a)",
                          {Alternative{"functional", "minus(&User::a)", "functional style"}}},
            DecisionPoint{3, "expr_style", "operator", "-(-c(&User::a))",
                          {Alternative{"functional", "minus(-c(&User::a))", "functional style"}}},
        }
    });
}

TEST_CASE("codegen: IS NULL") {
    REQUIRE(generate("a IS NULL") == "is_null(&User::a)");
    REQUIRE(generate("a ISNULL") == "is_null(&User::a)");
}

TEST_CASE("codegen: IS NOT NULL") {
    REQUIRE(generate("a IS NOT NULL") == "is_not_null(&User::a)");
    REQUIRE(generate("a NOTNULL") == "is_not_null(&User::a)");
    REQUIRE(generate("a NOT NULL") == "is_not_null(&User::a)");
}

TEST_CASE("codegen: BETWEEN") {
    REQUIRE(generate("a BETWEEN 1 AND 10") == "between(&User::a, 1, 10)");
}

TEST_CASE("codegen: NOT BETWEEN") {
    REQUIRE(generate("a NOT BETWEEN 1 AND 10") == "not_(between(&User::a, 1, 10))");
}

TEST_CASE("codegen: IN") {
    REQUIRE(generate_full("a IN (1, 2, 3)") ==
            CodeGenResult{"in(&User::a, {1, 2, 3})", {column_ref_style_dp(1, "&User::a")}, {}});
}

TEST_CASE("codegen: NOT IN") {
    REQUIRE(generate_full("a NOT IN (1, 2)") ==
            CodeGenResult{
                "not_in(&User::a, {1, 2})",
                {
                    column_ref_style_dp(1, "&User::a"),
                    DecisionPoint{2,
                                  "negation_style",
                                  "not_in",
                                  "not_in(&User::a, {1, 2})",
                                  {Alternative{"not_wrapper", "not_(in(&User::a, {1, 2}))", "use not_() wrapper"}}},
                },
                {}});
}

TEST_CASE("codegen: IN with empty list") {
    REQUIRE(generate("a IN ()") == "in(&User::a, {})");
}

TEST_CASE("codegen: LIKE") {
    REQUIRE(generate("name LIKE '%foo%'") == "like(&User::name, \"%foo%\")");
}

TEST_CASE("codegen: LIKE with ESCAPE") {
    REQUIRE(generate("name LIKE '%x%%' ESCAPE 'x'") == "like(&User::name, \"%x%%\", \"x\")");
}

TEST_CASE("codegen: NOT LIKE") {
    REQUIRE(generate("name NOT LIKE '%foo%'") == "not_(like(&User::name, \"%foo%\"))");
}

TEST_CASE("codegen: GLOB") {
    REQUIRE(generate("name GLOB '*foo*'") == "glob(&User::name, \"*foo*\")");
}

TEST_CASE("codegen: NOT GLOB") {
    REQUIRE(generate("name NOT GLOB '*foo*'") == "not_(glob(&User::name, \"*foo*\"))");
}

TEST_CASE("codegen: IS NULL in compound expression") {
    auto result = generate_full("a IS NULL AND b = 1");
    REQUIRE(result.code == "is_null(&User::a) and c(&User::b) == 1");
}

TEST_CASE("codegen: function - no args") {
    REQUIRE(generate("random()") == "random()");
}

TEST_CASE("codegen: function - one arg") {
    REQUIRE(generate("abs(a)") == "abs(&User::a)");
    REQUIRE(generate("length(name)") == "length(&User::name)");
    REQUIRE(generate("lower(name)") == "lower(&User::name)");
    REQUIRE(generate("upper(name)") == "upper(&User::name)");
}

TEST_CASE("codegen: function - multiple args") {
    REQUIRE(generate("coalesce(a, b, 0)") == "coalesce(&User::a, &User::b, 0)");
    REQUIRE(generate("substr(name, 1, 3)") == "substr(&User::name, 1, 3)");
    REQUIRE(generate("replace(name, 'foo', 'bar')") == "replace(&User::name, \"foo\", \"bar\")");
}

TEST_CASE("codegen: function - case insensitive name") {
    REQUIRE(generate("ABS(a)") == "abs(&User::a)");
    REQUIRE(generate("COUNT(*)") == "count()");
    REQUIRE(generate("Length(name)") == "length(&User::name)");
}

TEST_CASE("codegen: count(*)") {
    REQUIRE(generate("count(*)") == "count()");
}

TEST_CASE("codegen: count(DISTINCT expr)") {
    REQUIRE(generate("count(DISTINCT name)") == "count(distinct(&User::name))");
}

TEST_CASE("codegen: function - nested") {
    REQUIRE(generate("abs(round(x, 2))") == "abs(round(&User::x, 2))");
}

TEST_CASE("codegen: function in expression") {
    auto result = generate_full("abs(a) + length(b)");
    REQUIRE(result.code == "abs(&User::a) + length(&User::b)");
}

TEST_CASE("codegen: date/time functions") {
    REQUIRE(generate("date('now')") == "date(\"now\")");
    REQUIRE(generate("datetime('now', 'localtime')") == "datetime(\"now\", \"localtime\")");
}

TEST_CASE("codegen: parenthesized expression") {
    REQUIRE(generate("(42)") == "42");
}

TEST_CASE("codegen: parenthesized changes precedence") {
    auto result = generate_full("(a + b) * c");
    auto plain = generate_full("a + b * c");
    REQUIRE(result.code != plain.code);
}

TEST_CASE("codegen: CAST - INTEGER") {
    REQUIRE(generate("CAST(a AS INTEGER)") == "cast<int64_t>(&User::a)");
    REQUIRE(generate("CAST(a AS INT)") == "cast<int64_t>(&User::a)");
    REQUIRE(generate("CAST(a AS BIGINT)") == "cast<int64_t>(&User::a)");
}

TEST_CASE("codegen: CAST - TEXT") {
    REQUIRE(generate("CAST(a AS TEXT)") == "cast<std::string>(&User::a)");
    REQUIRE(generate("CAST(a AS VARCHAR(255))") == "cast<std::string>(&User::a)");
    REQUIRE(generate("CAST(a AS CHAR(10))") == "cast<std::string>(&User::a)");
}

TEST_CASE("codegen: CAST - REAL") {
    REQUIRE(generate("CAST(a AS REAL)") == "cast<double>(&User::a)");
    REQUIRE(generate("CAST(a AS DOUBLE)") == "cast<double>(&User::a)");
    REQUIRE(generate("CAST(a AS FLOAT)") == "cast<double>(&User::a)");
}

TEST_CASE("codegen: CAST - BLOB") {
    REQUIRE(generate("CAST(a AS BLOB)") == "cast<std::vector<char>>(&User::a)");
}

TEST_CASE("codegen: CAST - BOOLEAN") {
    REQUIRE(generate("CAST(a AS BOOLEAN)") == "cast<bool>(&User::a)");
}

TEST_CASE("codegen: CAST - NUMERIC") {
    REQUIRE(generate("CAST(a AS NUMERIC)") == "cast<double>(&User::a)");
}

TEST_CASE("codegen: searched CASE") {
    REQUIRE(generate("CASE WHEN a > 0 THEN 'pos' ELSE 'neg' END") ==
            "case_<std::string>().when(c(&User::a) > 0, then(\"pos\")).else_(\"neg\").end()");
}

TEST_CASE("codegen: simple CASE") {
    REQUIRE(generate("CASE status WHEN 1 THEN 'on' WHEN 0 THEN 'off' END") ==
            "case_<std::string>(&User::status).when(1, then(\"on\")).when(0, then(\"off\")).end()");
}

TEST_CASE("codegen: CASE without ELSE") {
    REQUIRE(generate("CASE WHEN a = 1 THEN 'one' END") ==
            "case_<std::string>().when(c(&User::a) == 1, then(\"one\")).end()");
}

TEST_CASE("codegen: blob literal") {
    REQUIRE(generate("X'48656C6C6F'") == "std::vector<char>{'\\x48', '\\x65', '\\x6C', '\\x6C', '\\x6F'}");
    REQUIRE(generate("x'AB'") == "std::vector<char>{'\\xAB'}");
    REQUIRE(generate("X''") == "std::vector<char>{}");
}

TEST_CASE("codegen: prefix - empty for literals") {
    REQUIRE(prefix_for("42") == "");
    REQUIRE(prefix_for("'hello'") == "");
}

TEST_CASE("codegen: prefix - single column defaults to int") {
    REQUIRE(prefix_for("a > 5") == "struct User {\n    int a = 0;\n};");
}

TEST_CASE("codegen: prefix - inferred string from comparison") {
    REQUIRE(prefix_for("name = 'hello'") == "struct User {\n    std::string name;\n};");
}

TEST_CASE("codegen: prefix - inferred double from real") {
    REQUIRE(prefix_for("x > 3.14") == "struct User {\n    double x = 0.0;\n};");
}

TEST_CASE("codegen: prefix - LIKE infers string") {
    REQUIRE(prefix_for("name LIKE '%foo%'") == "struct User {\n    std::string name;\n};");
}

TEST_CASE("codegen: prefix - instr first argument infers string column") {
    REQUIRE(prefix_for("SELECT name, instr(abilities, 'o') FROM marvel ORDER BY 2") ==
            "struct Marvel {\n    std::string abilities;\n    std::string name;\n};");
}

TEST_CASE("codegen: prefix - synthetic column name heuristic for text") {
    REQUIRE(prefix_for("SELECT title FROM books") == "struct Books {\n    std::string title;\n};");
    REQUIRE(prefix_for("SELECT id FROM books") == "struct Books {\n    int id = 0;\n};");
}

TEST_CASE("codegen: prefix - qualified quoted columns populate synthetic struct") {
    REQUIRE(prefix_for(R"(SELECT "last_result"."id", "last_result"."stamp" FROM "last_result")") ==
            "struct LastResult {\n    int id = 0;\n    int stamp = 0;\n};");
}

TEST_CASE("codegen: prefix - multiple columns sorted") {
    auto result = prefix_for("a > 5 AND b = 'hello'");
    REQUIRE(result == "struct User {\n    int a = 0;\n    std::string b;\n};");
}

TEST_CASE("codegen: prefix - CASE with int return type") {
    REQUIRE(generate("CASE WHEN a > 0 THEN 1 ELSE 0 END") ==
            "case_<int>().when(c(&User::a) > 0, then(1)).else_(0).end()");
}

TEST_CASE("codegen: IS expr returns error") {
    REQUIRE(generate_full("SELECT 1 IS 2 FROM users;") ==
        CodeGenResult{{}, {}, {},
            {"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
             "is not supported in sqlite_orm"}});
}

TEST_CASE("codegen: IS NOT expr returns error") {
    REQUIRE(generate_full("SELECT 1 IS NOT 2 FROM users;") ==
        CodeGenResult{{}, {}, {},
            {"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
             "is not supported in sqlite_orm"}});
}

TEST_CASE("codegen: IS DISTINCT FROM returns error") {
    REQUIRE(generate_full("SELECT a IS DISTINCT FROM b FROM t;") ==
        CodeGenResult{{}, {}, {},
            {"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
             "is not supported in sqlite_orm"}});
}

TEST_CASE("codegen: IS NOT DISTINCT FROM returns error") {
    REQUIRE(generate_full("SELECT a IS NOT DISTINCT FROM b FROM t;") ==
        CodeGenResult{{}, {}, {},
            {"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
             "is not supported in sqlite_orm"}});
}

TEST_CASE("codegen: JSON -> operator") {
    REQUIRE(generate_full("SELECT data -> '$.name' FROM users;") ==
        CodeGenResult{"auto rows = storage.select(json_extract(&Users::data, \"$.name\"));",
                      {column_ref_style_dp(1, "&Users::data"),
                       DecisionPoint{2, "expr_style", "functional",
                          "json_extract(&Users::data, \"$.name\")",
                          {Alternative{"operator_wrap_right",
                              "&Users::data -> c(\"$.name\")", "wrap right operand"},
                           Alternative{"functional",
                              "json_extract(&Users::data, \"$.name\")", "functional style"},
                           Alternative{"operator_wrap_both",
                              "c(&Users::data) -> c(\"$.name\")", "wrap both operands", true}}}},
                      {"JSON -> / ->> operator is mapped to json_extract() "
                       "— return type may differ from sqlite"}});
}

TEST_CASE("codegen: JSON ->> operator") {
    REQUIRE(generate_full("SELECT data ->> '$.name' FROM users;") ==
        CodeGenResult{"auto rows = storage.select(json_extract(&Users::data, \"$.name\"));",
                      {column_ref_style_dp(1, "&Users::data"),
                       DecisionPoint{2, "expr_style", "functional",
                          "json_extract(&Users::data, \"$.name\")",
                          {Alternative{"operator_wrap_right",
                              "&Users::data ->> c(\"$.name\")", "wrap right operand"},
                           Alternative{"functional",
                              "json_extract(&Users::data, \"$.name\")", "functional style"},
                           Alternative{"operator_wrap_both",
                              "c(&Users::data) ->> c(\"$.name\")", "wrap both operands", true}}}},
                      {"JSON -> / ->> operator is mapped to json_extract() "
                       "— return type may differ from sqlite"}});
}

TEST_CASE("codegen: bind parameter anonymous") {
    REQUIRE(generate_full("SELECT ? FROM users;") ==
        CodeGenResult{"auto rows = storage.select(bindParam1);",
                      {},
                      {"bind parameter ? -> C++ variable 'bindParam1'; "
                       "for prepared statements use storage.prepare() + get<N>(stmt)"}});
}

TEST_CASE("codegen: bind parameter named") {
    REQUIRE(generate_full("SELECT :userId FROM users;") ==
        CodeGenResult{"auto rows = storage.select(userId);",
                      {},
                      {"bind parameter :userId -> C++ variable 'userId'; "
                       "for prepared statements use storage.prepare() + get<N>(stmt)"}});
}

TEST_CASE("codegen: expr COLLATE warning") {
    REQUIRE(generate_full("SELECT name COLLATE NOCASE FROM users;") ==
        CodeGenResult{"auto rows = storage.select(&Users::name);",
                      {column_ref_style_dp(1, "&Users::name")},
                      {"COLLATE NOCASE on expressions is not directly supported in sqlite_orm codegen"}});
}
