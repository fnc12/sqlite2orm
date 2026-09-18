#include "codegen_tests_common.hpp"

namespace {

    // The hint attached to every negation generated as a subtraction from zero; spelled out once
    // here and asserted on its own in "codegen: unary minus carries the zero-subtraction comment".
    const std::string kZeroMinusComment =
        "Unary minus is generated as `0 - expr`: sqlite_orm's own unary minus reports a wrong result "
        "type, so it hands the caller 0 (and throws over a column), while `0 - expr` is what SQLite "
        "computes for `-expr` — same value and same typeof for every operand kind.";

    // The hint attached to every predicate the generator delimits with a CAST; asserted on its own
    // in "codegen: a predicate cast under an operator carries its comment".
    const std::string kPredicateCastComment =
        "A predicate under an operator is generated as `cast<int64_t>(predicate)`: sqlite_orm "
        "serializes IN, BETWEEN, LIKE, GLOB, MATCH, IS [NOT] NULL and NOT without parentheses, and "
        "SQLite binds them looser than the operator around them, so `1 - (a IS NULL)` would be read "
        "back as `(1 - a) IS NULL`. The CAST delimits the predicate and leaves what it stands for "
        "alone — a predicate is 0, 1 or NULL, and a CAST to INTEGER keeps all three, typeof included.";

}  // namespace

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

TEST_CASE("codegen: digit separators become C++ separators") {
    REQUIRE(generate("SELECT 1_000_000;") == "auto rows = storage.select(1'000'000);");
    REQUIRE(generate("1_000_000") == "1'000'000");
    REQUIRE(generate("0x1_ffff") == "0x1'ffff");
    REQUIRE(generate("3.141_592") == "3.141'592");
    REQUIRE(generate("1e1_0") == "1e1'0");
}

// SQLite reads a leading zero as a plain decimal digit, C++ as an octal prefix: `010` would mean
// 8 and `0009` would not even compile. Values checked against sqlite3 3.51.
TEST_CASE("codegen: integer literal with leading zeros") {
    REQUIRE(generate("010") == "10");
    REQUIRE(generate("0009") == "9");
    REQUIRE(generate("08") == "8");
    REQUIRE(generate("00") == "0");
    REQUIRE(generate("0") == "0");
    REQUIRE(generate("SELECT 010;") == "auto rows = storage.select(10);");
    REQUIRE(generate("SELECT 1 LIMIT 010;") == "auto rows = storage.select(1, limit(10));");
}

// A separator standing between leading zeros goes away with them: SQLite reads `0_9` as 9, while
// C++ reads `0'9` as an octal constant with a bad digit and `01'0` as 8. The separators of the
// significant digits stay, as in every other literal.
TEST_CASE("codegen: integer literal with leading zeros and digit separators") {
    REQUIRE(generate("0_9") == "9");
    REQUIRE(generate("01_0") == "1'0");
    REQUIRE(generate("0_0") == "0");
    REQUIRE(generate("000_1") == "1");
    REQUIRE(generate("0_0_0") == "0");
    REQUIRE(generate("00_00") == "0");
    REQUIRE(generate("SELECT 01_0;") == "auto rows = storage.select(1'0);");
}

// A leading zero is harmless in the two literals C++ does not read as octal, so they keep their
// SQL spelling: `0x0FF` is 255 and `010.5` is 10.5 in both languages.
TEST_CASE("codegen: leading zeros kept in hexadecimal and real literals") {
    REQUIRE(generate("0x0FF") == "0x0FF");
    REQUIRE(generate("0x0_1f") == "0x0'1f");
    REQUIRE(generate("0X0FF") == "0X0FF");
    REQUIRE(generate("0X0_1F") == "0X0'1F");
    REQUIRE(generate("010.5") == "010.5");
    REQUIRE(generate("01e2") == "01e2");
    REQUIRE(generate("00.5") == "00.5");
    REQUIRE(generate("0_1.5") == "0'1.5");
}

// An int64 cannot hold these, so SQLite reads them as REAL: `9223372036854775808` comes back as
// 9.22337203685478e+18 and `99999999999999999999` as 1.0e+20. C++ would read the first spelling as
// an unsigned constant and reject the second as too large for any integer type, so the generated
// literal carries a fractional part and denotes the same double. Checked against sqlite3 3.51.
TEST_CASE("codegen: integer literal beyond int64 becomes a double") {
    REQUIRE(generate("9223372036854775807") == "9223372036854775807");
    REQUIRE(generate("9223372036854775808") == "9223372036854775808.0");
    REQUIRE(generate("99999999999999999999") == "99999999999999999999.0");
    REQUIRE(generate("18446744073709551616") == "18446744073709551616.0");
    REQUIRE(generate("0009223372036854775808") == "9223372036854775808.0");
    REQUIRE(generate("9_223_372_036_854_775_808") == "9'223'372'036'854'775'808.0");
    REQUIRE(generate("SELECT 9223372036854775808;") == "auto rows = storage.select(9223372036854775808.0);");
}

// SQLite wraps a hex literal around into a signed 64-bit integer, so `0xFFFFFFFFFFFFFFFF` is -1
// and `0x8000000000000000` is -9223372036854775808. C++ gives the same spelling the unsigned type
// it fits in, where it would mean 18446744073709551615, so the cast brings the value back.
TEST_CASE("codegen: hexadecimal literal past the int64 range wraps around") {
    REQUIRE(generate("0x7FFFFFFFFFFFFFFF") == "0x7FFFFFFFFFFFFFFF");
    REQUIRE(generate("0x8000000000000000") == "static_cast<int64_t>(0x8000000000000000)");
    REQUIRE(generate("0xFFFFFFFFFFFFFFFF") == "static_cast<int64_t>(0xFFFFFFFFFFFFFFFF)");
    REQUIRE(generate("0x0000FFFFFFFFFFFFFFFF") == "static_cast<int64_t>(0x0000FFFFFFFFFFFFFFFF)");
    REQUIRE(generate("0xFF_FF_FF_FF_FF_FF_FF_FF") == "static_cast<int64_t>(0xFF'FF'FF'FF'FF'FF'FF'FF)");
    REQUIRE(generate("SELECT 0xFFFFFFFFFFFFFFFF;") ==
            "auto rows = storage.select(static_cast<int64_t>(0xFFFFFFFFFFFFFFFF));");
}

// SQLite reads `0xDEADBEEF` as the integer 3735928559, but C++ hands that spelling an
// `unsigned int`, the only unsigned type it fits in below the int64 range. The value survives the
// difference, the arithmetic around it does not: g++ 13.3 makes `-1 > 0xDEADBEEF` true while
// sqlite3 3.51 makes it false. One digit more and C++ is back to a signed type, so `0x100000000`
// needs no cast. Checked against sqlite3 3.51.
TEST_CASE("codegen: hexadecimal literal in the unsigned int range keeps its signed type") {
    REQUIRE(generate("0x7FFFFFFF") == "0x7FFFFFFF");
    REQUIRE(generate("0x80000000") == "static_cast<int64_t>(0x80000000)");
    REQUIRE(generate("0xDEADBEEF") == "static_cast<int64_t>(0xDEADBEEF)");
    REQUIRE(generate("0xFFFFFFFF") == "static_cast<int64_t>(0xFFFFFFFF)");
    REQUIRE(generate("0X8000_0000") == "static_cast<int64_t>(0X8000'0000)");
    REQUIRE(generate("0x0080000000") == "static_cast<int64_t>(0x0080000000)");
    REQUIRE(generate("0xdeadbeef") == "static_cast<int64_t>(0xdeadbeef)");
    // Neither a separator nor a leading zero is one of the eight digits that overflow an `int`,
    // so `0x8000_000` is the seven-digit 134217728 and stays signed in C++ as it is in SQLite.
    REQUIRE(generate("0x8000_000") == "0x8000'000");
    // A ninth digit takes the literal past an `unsigned int` and back to a signed C++ type, so
    // the leading 8 of `0x800000000` means nothing here: it is 34359738368, a `long`.
    REQUIRE(generate("0x800000000") == "0x800000000");
    REQUIRE(generate("0xFFFFFFFFFFFFFFF") == "0xFFFFFFFFFFFFFFF");
    REQUIRE(generate("0x100000000") == "0x100000000");
    REQUIRE(generate("0x1FFFFFFFF") == "0x1FFFFFFFF");
    REQUIRE(generate("SELECT x > 0xDEADBEEF;") ==
            "auto rows = storage.select(as_optional(c(&User::x) > static_cast<int64_t>(0xDEADBEEF)));");
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
    REQUIRE(generateFull("42 = 5") == expectedBinaryLeaf("42", "5", " == ", "is_equal"));
    REQUIRE(generateFull("a == b") == expectedBinaryLeaf("&User::a", "&User::b", " == ", "is_equal"));
    REQUIRE(generateFull("a != 5") == expectedBinaryLeaf("&User::a", "5", " != ", "is_not_equal"));
    REQUIRE(generateFull("a <> 5") == expectedBinaryLeaf("&User::a", "5", " != ", "is_not_equal"));
    REQUIRE(generateFull("a < 5") == expectedBinaryLeaf("&User::a", "5", " < ", "lesser_than"));
    REQUIRE(generateFull("a <= 5") == expectedBinaryLeaf("&User::a", "5", " <= ", "lesser_or_equal"));
    REQUIRE(generateFull("a > 3.14") == expectedBinaryLeaf("&User::a", "3.14", " > ", "greater_than"));
    REQUIRE(generateFull("a >= 0") == expectedBinaryLeaf("&User::a", "0", " >= ", "greater_or_equal"));
    REQUIRE(generateFull("name = 'hello'") == expectedBinaryLeaf("&User::name", R"("hello")", " == ", "is_equal"));
    REQUIRE(generateFull("users.id = 42") == expectedBinaryLeaf("&Users::id", "42", " == ", "is_equal"));
}

TEST_CASE("codegen: arithmetic operators") {
    REQUIRE(generateFull("a + 5") == expectedBinaryLeaf("&User::a", "5", " + ", "add"));
    REQUIRE(generateFull("a - 5") == expectedBinaryLeaf("&User::a", "5", " - ", "sub"));
    REQUIRE(generateFull("a * 5") == expectedBinaryLeaf("&User::a", "5", " * ", "mul"));
    REQUIRE(generateFull("a / 5") == expectedBinaryLeaf("&User::a", "5", " / ", "div"));
    REQUIRE(generateFull("a % 5") == expectedBinaryLeaf("&User::a", "5", " % ", "mod"));
}

TEST_CASE("codegen: concatenation") {
    SECTION("simple") {
        REQUIRE(generateFull("a || b") == expectedBinaryLeaf("&User::a", "&User::b", " || ", "conc"));
    }
    SECTION("chained: (a || b) || c") {
        auto result = generateFull("a || b || c");
        REQUIRE(result == CodeGenResult{
            "c(&User::a) || &User::b || &User::c",
            {
                columnRefStyleDp(1, "&User::a"),
                columnRefStyleDp(2, "&User::b"),
                DecisionPoint{3, "expr_style", "operator_wrap_left", "c(&User::a) || &User::b",
                    {
                        Option{"operator_wrap_left", "c(&User::a) || &User::b", "wrap left operand"},
                        Option{"operator_wrap_right", "&User::a || c(&User::b)", "wrap right operand"},
                        Option{"functional", "conc(&User::a, &User::b)", "functional style"},
                        Option{"operator_wrap_both", "c(&User::a) || c(&User::b)", "wrap both operands", true},
                    }},
                columnRefStyleDp(4, "&User::c"),
                DecisionPoint{5, "expr_style", "operator_wrap_left", "c(&User::a) || &User::b || &User::c",
                    {
                        Option{"operator_wrap_left", "c(&User::a) || &User::b || &User::c", "wrap left operand"},
                        Option{"operator_wrap_right", "c(&User::a) || &User::b || c(&User::c)", "wrap right operand"},
                        Option{"functional", "conc(c(&User::a) || &User::b, &User::c)", "functional style"},
                        Option{"operator_wrap_both", "c(&User::a) || &User::b || c(&User::c)", "wrap both operands", true},
                    }},
            }
        });
    }
}

TEST_CASE("codegen: bitwise operators") {
    REQUIRE(generateFull("a & 5") == expectedBinaryLeaf("&User::a", "5", " & ", "bitwise_and"));
    REQUIRE(generateFull("a | 5") == expectedBinaryLeaf("&User::a", "5", " | ", "bitwise_or"));
    REQUIRE(generateFull("a << 2") == expectedBinaryLeaf("&User::a", "2", " << ", "bitwise_shift_left"));
    REQUIRE(generateFull("a >> 2") == expectedBinaryLeaf("&User::a", "2", " >> ", "bitwise_shift_right"));
}

// A minus glued to a numeric literal is folded into the literal, the way SQLite's own parser does
// it. sqlite_orm's unary_minus_t reports a wrong result type, so `-c(5)` serializes to the right
// SQL and still reads the row back as 0. Values checked against sqlite3 3.51.
TEST_CASE("codegen: unary minus") {
    SECTION("-5") {
        auto result = generateFull("-5");
        REQUIRE(result == CodeGenResult{"-5", {}});
    }
    SECTION("-2.5") {
        auto result = generateFull("-2.5");
        REQUIRE(result == CodeGenResult{"-2.5", {}});
    }
    SECTION("-0x10") {
        auto result = generateFull("-0x10");
        REQUIRE(result == CodeGenResult{"-0x10", {}});
    }
    SECTION("-1e3") {
        auto result = generateFull("-1e3");
        REQUIRE(result == CodeGenResult{"-1e3", {}});
    }
    SECTION("leading zeros are still stripped under the sign") {
        auto result = generateFull("-010");
        REQUIRE(result == CodeGenResult{"-10", {}});
    }
    SECTION("-a") {
        auto result = generateFull("-a");
        REQUIRE(result == CodeGenResult{"(c(0) - c(&User::a))",
            {
                columnRefStyleDp(1, "&User::a"),
                DecisionPoint{2, "expr_style", "operator", "(c(0) - c(&User::a))",
                              {Option{"operator", "(c(0) - c(&User::a))", "operator style"},
                               Option{"functional", "sub(0, &User::a)", "functional style"}}},
            },
            {},
            {},
            {kZeroMinusComment}});
    }
}

// The signed literal is a plain C++ value again, so an operator around it needs the usual `c()`.
TEST_CASE("codegen: negative literal as an operand") {
    REQUIRE(generate("SELECT -2;") == "auto rows = storage.select(-2);");
    REQUIRE(generate("SELECT 100 / -2;") == "auto rows = storage.select(as_optional(c(100) / -2));");
    REQUIRE(generate("SELECT -2 + 3;") == "auto rows = storage.select(c(-2) + 3);");
    REQUIRE(generate("SELECT a * -2;") == "auto rows = storage.select(as_optional(c(&User::a) * -2));");
    REQUIRE(generate("SELECT ~ -2;") == "auto rows = storage.select(~c(-2));");
    REQUIRE(generate("SELECT a BETWEEN -1 AND 5;") == "auto rows = storage.select(between(&User::a, -1, 5));");
}

// A second minus has no literal to fold into; parenthesizing keeps it out of `c()`, which would
// rebuild the unary_minus_t the fold exists to avoid. The constant it folds into stays a constant,
// so a third sign folds in too. `SELECT - -3` is 3 and `SELECT - - -3` is -3 in sqlite3 3.51.
TEST_CASE("codegen: double unary minus on a literal") {
    REQUIRE(generateFull("- -3") == CodeGenResult{"-(-3)", {}});
    REQUIRE(generateFull("- - -3") == CodeGenResult{"-(-(-3))", {}});
    REQUIRE(generateFull("- - - -3") == CodeGenResult{"-(-(-(-3)))", {}});
}

// `0x8000000000000000` is INT64_MIN, so the sign cannot be folded into the C++ constant the literal
// generates — `-static_cast<int64_t>(0x8000000000000000)` overflows int64_t, which gcc rejects
// outright in a constant expression. SQLite has no value for the SQL either: it refuses every
// statement that uses the expression with `hex literal too big` (checked on sqlite3 3.51), so the
// subtraction the other operands get carries a warning here. Only a DDL clause reaches codegen with
// this expression at all; the validator rejects the statements SQLite compiles.
TEST_CASE("codegen: the sign of INT64_MIN is not folded into the hex literal") {
    const std::string tooBig =
        "hex literal too big: -0x8000000000000000; SQLite refuses this expression wherever it is "
        "used, so the generated subtraction from zero does not reproduce it";
    auto result = generateFull("-0x8000000000000000");
    REQUIRE(result.code == "(c(0) - c(static_cast<int64_t>(0x8000000000000000)))");
    REQUIRE(result.warnings == std::vector<CodegenWarning>{{tooBig, SourceLocation{1, 1}, 1}});
    // The separators and the leading zeros name the same value, and SQLite spells the separators out.
    auto separated = generateFull("-0x0_8000_0000_0000_0000");
    REQUIRE(separated.code == "(c(0) - c(static_cast<int64_t>(0x0'8000'0000'0000'0000)))");
    REQUIRE(separated.warnings ==
            std::vector<CodegenWarning>{
                {"hex literal too big: -0x08000000000000000; SQLite refuses this expression wherever "
                 "it is used, so the generated subtraction from zero does not reproduce it",
                 SourceLocation{1, 1}, 1}});
    // Every neighbouring value stays folded: sqlite3 3.51 gives 9223372036854775807 and 1 for these.
    REQUIRE(generateFull("-0x8000000000000001") ==
            CodeGenResult{"-static_cast<int64_t>(0x8000000000000001)", {}});
    REQUIRE(generateFull("-0xFFFFFFFFFFFFFFFF") ==
            CodeGenResult{"-static_cast<int64_t>(0xFFFFFFFFFFFFFFFF)", {}});
    // The literal on its own is -9223372036854775808 in SQLite and needs no guard.
    REQUIRE(generateFull("0x8000000000000000") ==
            CodeGenResult{"static_cast<int64_t>(0x8000000000000000)", {}});
}

TEST_CASE("codegen: unary plus is no-op") {
    auto result5 = generateFull("+5");
    REQUIRE(result5 == CodeGenResult{"5", {}});
    auto resultA = generateFull("+a");
    REQUIRE(resultA == CodeGenResult{"&User::a", {columnRefStyleDp(1, "&User::a")}});
}

TEST_CASE("codegen: bitwise not") {
    SECTION("~5") {
        auto result = generateFull("~5");
        REQUIRE(result == CodeGenResult{"~c(5)", {DecisionPoint{1, "expr_style", "operator", "~c(5)",
            {Option{"operator", "~c(5)", "operator style"},
             Option{"functional", "bitwise_not(5)", "functional style"}}
        }}});
    }
    SECTION("~a") {
        auto result = generateFull("~a");
        REQUIRE(result == CodeGenResult{"~c(&User::a)",
            {
                columnRefStyleDp(1, "&User::a"),
                DecisionPoint{2, "expr_style", "operator", "~c(&User::a)",
                              {Option{"operator", "~c(&User::a)", "operator style"},
                               Option{"functional", "bitwise_not(&User::a)", "functional style"}}},
            }});
    }
}

TEST_CASE("codegen: logical AND") {
    SECTION("leaf operands") {
        REQUIRE(generateFull("a AND b") == expectedBinaryLeaf("&User::a", "&User::b", " and ", "and_"));
    }
    SECTION("compound operands: a = 1 AND b = 2") {
        auto result = generateFull("a = 1 AND b = 2");
        auto leftEq = expectedBinaryLeaf("&User::a", "1", " == ", "is_equal", 1);
        auto rightEq = expectedBinaryLeaf("&User::b", "2", " == ", "is_equal", 3);
        std::vector<DecisionPoint> expectedDps;
        expectedDps.insert(expectedDps.end(), leftEq.decisionPoints.begin(), leftEq.decisionPoints.end());
        expectedDps.insert(expectedDps.end(), rightEq.decisionPoints.begin(), rightEq.decisionPoints.end());
        expectedDps.push_back(DecisionPoint{5, "expr_style", "operator_wrap_left", "c(&User::a) == 1 and c(&User::b) == 2",
            {
                Option{"operator_wrap_left", "c(&User::a) == 1 and c(&User::b) == 2", "wrap left operand"},
                Option{"operator_wrap_right", "c(&User::a) == 1 and c(&User::b) == 2", "wrap right operand"},
                Option{"functional", "and_(c(&User::a) == 1, c(&User::b) == 2)", "functional style"},
                Option{"operator_wrap_both", "c(&User::a) == 1 and c(&User::b) == 2", "wrap both operands", true},
            }});
        REQUIRE(result == CodeGenResult{"c(&User::a) == 1 and c(&User::b) == 2", std::move(expectedDps)});
    }
}

TEST_CASE("codegen: logical OR") {
    SECTION("leaf operands") {
        REQUIRE(generateFull("a OR b") == expectedBinaryLeaf("&User::a", "&User::b", " or ", "or_"));
    }
    SECTION("compound operands: a = 1 OR b = 2") {
        auto result = generateFull("a = 1 OR b = 2");
        auto leftEq = expectedBinaryLeaf("&User::a", "1", " == ", "is_equal", 1);
        auto rightEq = expectedBinaryLeaf("&User::b", "2", " == ", "is_equal", 3);
        std::vector<DecisionPoint> expectedDps;
        expectedDps.insert(expectedDps.end(), leftEq.decisionPoints.begin(), leftEq.decisionPoints.end());
        expectedDps.insert(expectedDps.end(), rightEq.decisionPoints.begin(), rightEq.decisionPoints.end());
        expectedDps.push_back(DecisionPoint{5, "expr_style", "operator_wrap_left", "c(&User::a) == 1 or c(&User::b) == 2",
            {
                Option{"operator_wrap_left", "c(&User::a) == 1 or c(&User::b) == 2", "wrap left operand"},
                Option{"operator_wrap_right", "c(&User::a) == 1 or c(&User::b) == 2", "wrap right operand"},
                Option{"functional", "or_(c(&User::a) == 1, c(&User::b) == 2)", "functional style"},
                Option{"operator_wrap_both", "c(&User::a) == 1 or c(&User::b) == 2", "wrap both operands", true},
            }});
        REQUIRE(result == CodeGenResult{"c(&User::a) == 1 or c(&User::b) == 2", std::move(expectedDps)});
    }
}

TEST_CASE("codegen: logical NOT") {
    SECTION("leaf operand") {
        auto result = generateFull("NOT a");
        REQUIRE(result == CodeGenResult{"not c(&User::a)",
            {
                columnRefStyleDp(1, "&User::a"),
                DecisionPoint{2, "expr_style", "operator", "not c(&User::a)",
                              {
                                  Option{"operator", "not c(&User::a)", "operator style"},
                                  Option{"operator_excl", "!c(&User::a)", "use ! instead of not"},
                              }},
            }});
    }
    SECTION("compound operand: NOT -a") {
        auto result = generateFull("NOT -a");
        REQUIRE(result == CodeGenResult{
            "not (c(0) - c(&User::a))",
            {
                columnRefStyleDp(1, "&User::a"),
                DecisionPoint{2, "expr_style", "operator", "(c(0) - c(&User::a))",
                              {Option{"operator", "(c(0) - c(&User::a))", "operator style"},
                               Option{"functional", "sub(0, &User::a)", "functional style"}}},
                DecisionPoint{3, "expr_style", "operator", "not (c(0) - c(&User::a))",
                              {
                                  Option{"operator", "not (c(0) - c(&User::a))", "operator style"},
                                  Option{"operator_excl", "!(c(0) - c(&User::a))", "use ! instead of not"},
                              }},
            },
            {},
            {},
            {kZeroMinusComment}
        });
    }
}

TEST_CASE("codegen: double unary minus parenthesized") {
    auto result = generateFull("- -a");
    REQUIRE(result == CodeGenResult{
        "(c(0) - (c(0) - c(&User::a)))",
        {
            columnRefStyleDp(1, "&User::a"),
            DecisionPoint{2, "expr_style", "operator", "(c(0) - c(&User::a))",
                          {Option{"operator", "(c(0) - c(&User::a))", "operator style"},
                           Option{"functional", "sub(0, &User::a)", "functional style"}}},
            DecisionPoint{3, "expr_style", "operator", "(c(0) - (c(0) - c(&User::a)))",
                          {Option{"operator", "(c(0) - (c(0) - c(&User::a)))", "operator style"},
                           Option{"functional", "sub(0, (c(0) - c(&User::a)))", "functional style"}}},
        },
        {},
        {},
        {kZeroMinusComment}
    });
}

// sqlite_orm has no working unary minus, so a negation of anything but a numeric constant is
// generated as a subtraction from zero: SQLite computes `0 - x` exactly like `-x` for every operand
// kind, value and typeof alike. Values checked against sqlite3 3.51 in
// "runtime: a negation over a general operand keeps its value".
TEST_CASE("codegen: unary minus over a general operand becomes a subtraction from zero") {
    REQUIRE(generate("SELECT -(2+3);") == "auto rows = storage.select((c(0) - (c(2) + 3)));");
    REQUIRE(generate("SELECT - ~2;") == "auto rows = storage.select((c(0) - (~c(2))));");
    REQUIRE(generate("SELECT -length('abc');") ==
            "auto rows = storage.select(as_optional((c(0) - (length(\"abc\")))));");
    REQUIRE(generate("SELECT -x'31';") ==
            "auto rows = storage.select((c(0) - c(std::vector<char>{'\\x31'})));");
    REQUIRE(generate("SELECT -(SELECT 1);") ==
            "auto rows = storage.select(as_optional((c(0) - (select(1)))));");
    REQUIRE(generate("SELECT -a FROM users;") ==
            "auto rows = storage.select(as_optional((c(0) - c(&Users::a))));");
    REQUIRE(generate("SELECT -(a+1) FROM users;") ==
            "auto rows = storage.select(as_optional((c(0) - (c(&Users::a) + 1))));");
    REQUIRE(generate("SELECT -CAST(a AS INTEGER) FROM users;") ==
            "auto rows = storage.select(as_optional((c(0) - (cast<int64_t>(&Users::a)))));");
    // The subtraction carries its own parentheses, so it survives as one operand of another operator
    // where `c(1) - c(0) - (c(2) + 3)` would regroup into `(1 - 0) - 5`.
    REQUIRE(generate("SELECT 1 - -(2+3);") == "auto rows = storage.select(c(1) - (c(0) - (c(2) + 3)));");
}

TEST_CASE("codegen: unary minus carries the zero-subtraction comment") {
    auto result = generateFull("SELECT -a FROM users;");
    REQUIRE(result.comments == std::vector<std::string>{kZeroMinusComment});
}

TEST_CASE("codegen: unary minus under the functional expression style") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["expr_style"] = "functional";
    REQUIRE(generateWithPolicy("SELECT -a FROM users;", policy).code ==
            "auto rows = storage.select(as_optional(sub(0, &Users::a)));");
    REQUIRE(generateWithPolicy("SELECT -(2+3);", policy).code ==
            "auto rows = storage.select(sub(0, add(2, 3)));");
}

// A predicate is the one operand the subtraction cannot carry: sqlite_orm serializes
// `a BETWEEN 1 AND 9` without parentheses and SQLite binds it looser than a binary `-`, so
// `0 - a BETWEEN 1 AND 9` would read as `(0 - a) BETWEEN 1 AND 9`. The unary form stays and warns.
TEST_CASE("codegen: unary minus over a predicate warns instead") {
    auto check = [](std::string_view sql, const std::string& code, const std::string& predicate) {
        auto result = generateFull(sql);
        REQUIRE(result.code == code);
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"unary minus over a predicate (" + predicate +
                         ") has no working sqlite_orm form; the generated negation does not reproduce "
                         "what SQLite computes and does not compile",
                     SourceLocation{1, 8}, 1}});
    };
    check("SELECT -(a IN (1,2)) FROM users;", "auto rows = storage.select(-(in(&Users::a, {1, 2})));", "IN");
    check("SELECT -(a BETWEEN 1 AND 9) FROM users;",
          "auto rows = storage.select(-(between(&Users::a, 1, 9)));", "BETWEEN");
    check("SELECT -(a LIKE 'x') FROM users;", "auto rows = storage.select(-(like(&Users::a, \"x\")));",
          "LIKE");
    check("SELECT -(a GLOB 'x') FROM users;", "auto rows = storage.select(-(glob(&Users::a, \"x\")));",
          "GLOB");
    check("SELECT -(a MATCH 'x') FROM users;", "auto rows = storage.select(-(match(&Users::a, \"x\")));",
          "MATCH");
    check("SELECT -(a IS NULL) FROM users;", "auto rows = storage.select(-(is_null(&Users::a)));",
          "IS NULL");
    check("SELECT -(a NOTNULL) FROM users;", "auto rows = storage.select(-(is_not_null(&Users::a)));",
          "IS NOT NULL");
    check("SELECT - NOT a FROM users;", "auto rows = storage.select(-(not c(&Users::a)));", "NOT");
}

// sqlite_orm parenthesizes an operand it serializes only when that operand is a binary operator or
// condition of its own. A predicate comes out bare, and SQLite binds every predicate looser than
// the arithmetic, bit and comparison operators, so `c(1) - is_null(&User::a)` is one C++ term whose
// SQL reads `1 - "a" IS NULL` — that is, `(1 - "a") IS NULL`. A CAST to INTEGER delimits the
// predicate and keeps its value, which is 0, 1 or NULL either way. Grouping checked against
// sqlite3 3.51; the values the generated code answers with are pinned in
// "runtime: a predicate under an operator keeps the grouping it was written with".
TEST_CASE("codegen: a predicate under an operator is cast to stay one SQL term") {
    REQUIRE(generate("1 - (a IS NULL)") == "c(1) - cast<int64_t>(is_null(&User::a))");
    REQUIRE(generate("1 - (a NOT NULL)") == "c(1) - cast<int64_t>(is_not_null(&User::a))");
    REQUIRE(generate("1 - (a IN (1, 2))") == "c(1) - cast<int64_t>(in(&User::a, {1, 2}))");
    REQUIRE(generate("1 - (a BETWEEN 1 AND 9)") == "c(1) - cast<int64_t>(between(&User::a, 1, 9))");
    REQUIRE(generate("1 - (a LIKE 'x')") == "c(1) - cast<int64_t>(like(&User::a, \"x\"))");
    REQUIRE(generate("1 - (a GLOB 'x')") == "c(1) - cast<int64_t>(glob(&User::a, \"x\"))");
    REQUIRE(generate("1 - (a MATCH 'x')") == "c(1) - cast<int64_t>(match(&User::a, \"x\"))");
    REQUIRE(generate("1 - (NOT a)") == "c(1) - cast<int64_t>(not c(&User::a))");
    // Every rank that binds tighter than a predicate needs it, the left operand included.
    REQUIRE(generate("1 || (a IS NULL)") == "c(1) || cast<int64_t>(is_null(&User::a))");
    REQUIRE(generate("1 * (a IS NULL)") == "c(1) * cast<int64_t>(is_null(&User::a))");
    REQUIRE(generate("1 << (a IS NULL)") == "c(1) << cast<int64_t>(is_null(&User::a))");
    REQUIRE(generate("1 & (a IS NULL)") == "c(1) & cast<int64_t>(is_null(&User::a))");
    REQUIRE(generate("1 < (a IS NULL)") == "c(1) < cast<int64_t>(is_null(&User::a))");
    REQUIRE(generate("(a IS NULL) - 1") == "cast<int64_t>(is_null(&User::a)) - 1");
    REQUIRE(generate("(a IS NULL) || 'x'") == "cast<int64_t>(is_null(&User::a)) || \"x\"");
}

// `=` shares its rank with the predicates and SQLite reads it left-associatively, so only the right
// operand regroups: `a IS NULL = 1` is `(a IS NULL) = 1` already, while `1 = a IS NULL` is
// `(1 = a) IS NULL`. `AND` and `OR` are looser than any predicate, so neither operand regroups
// there — which is what keeps a plain WHERE clause free of casts. `NOT` is looser than `=` and
// tighter than `AND`, so it parts company with the other predicates on both. Checked against
// sqlite3 3.51.
TEST_CASE("codegen: an operator no looser than the predicate leaves it bare") {
    REQUIRE(generate("(a IS NULL) = 1") == "is_null(&User::a) == 1");
    REQUIRE(generate("(a IS NULL) <> 1") == "is_null(&User::a) != 1");
    REQUIRE(generate("(a IS NULL) AND 1") == "is_null(&User::a) and 1");
    REQUIRE(generate("1 AND (a IS NULL)") == "c(1) and is_null(&User::a)");
    REQUIRE(generate("(a IS NULL) OR 1") == "is_null(&User::a) or 1");
    REQUIRE(generate("1 OR (a IS NULL)") == "c(1) or is_null(&User::a)");
    REQUIRE(generate("(NOT a) AND 1") == "not c(&User::a) and 1");
    REQUIRE(generate("(NOT a) = 1") == "cast<int64_t>(not c(&User::a)) == 1");
    // The JSON operators are generated as a json_extract() call, whose own syntax delimits both
    // operands whatever SQLite's precedence says.
    REQUIRE(generate("a -> (b IS NULL)") == "json_extract(&User::a, is_null(&User::b))");
}

// The functional spelling changes the C++ and not the SQL — `sub(1, is_null(&User::a))` serializes
// as `1 - "a" IS NULL` just like the operator one — so the CAST belongs to both.
TEST_CASE("codegen: the predicate cast survives the functional expression style") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["expr_style"] = "functional";
    REQUIRE(generateWithPolicy("SELECT 1 - (a IS NULL);", policy).code ==
            "auto rows = storage.select(sub(1, cast<int64_t>(is_null(&User::a))));");
}

TEST_CASE("codegen: a predicate cast under an operator carries its comment") {
    REQUIRE(generateFull("SELECT 1 - (a IS NULL);").comments ==
            std::vector<std::string>{kPredicateCastComment});
    REQUIRE(generateFull("SELECT (a IS NULL) AND 1;").comments.empty());
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
    REQUIRE(generate("a NOT BETWEEN 1 AND 10") == "!between(&User::a, 1, 10)");
}

TEST_CASE("codegen: IN") {
    REQUIRE(generateFull("a IN (1, 2, 3)") ==
            CodeGenResult{"in(&User::a, {1, 2, 3})", {columnRefStyleDp(1, "&User::a")}, {}});
}

TEST_CASE("codegen: NOT IN") {
    REQUIRE(generateFull("a NOT IN (1, 2)") ==
            CodeGenResult{
                "not_in(&User::a, {1, 2})",
                {
                    columnRefStyleDp(1, "&User::a"),
                    DecisionPoint{2,
                                  "negation_style",
                                  "not_in",
                                  "not_in(&User::a, {1, 2})",
                                  {Option{"not_in", "not_in(&User::a, {1, 2})", "use not_in()"},
                                   Option{"operator_excl", "!in(&User::a, {1, 2})", "use the ! operator"}}},
                },
                {}});
}

TEST_CASE("codegen: NOT IN with the operator_excl negation policy") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["negation_style"] = "operator_excl";
    auto result = generateWithPolicy("a NOT IN (1, 2)", policy);
    REQUIRE(result.code == "!in(&User::a, {1, 2})");
    REQUIRE(result.decisionPoints.at(1) ==
            DecisionPoint{2,
                          "negation_style",
                          "operator_excl",
                          "!in(&User::a, {1, 2})",
                          {Option{"not_in", "not_in(&User::a, {1, 2})", "use not_in()"},
                           Option{"operator_excl", "!in(&User::a, {1, 2})", "use the ! operator"}}});
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
    REQUIRE(generate("name NOT LIKE '%foo%'") == "!like(&User::name, \"%foo%\")");
}

TEST_CASE("codegen: GLOB") {
    REQUIRE(generate("name GLOB '*foo*'") == "glob(&User::name, \"*foo*\")");
}

TEST_CASE("codegen: NOT GLOB") {
    REQUIRE(generate("name NOT GLOB '*foo*'") == "!glob(&User::name, \"*foo*\")");
}

TEST_CASE("codegen: MATCH") {
    REQUIRE(generate("body MATCH 'word'") == "match(&User::body, \"word\")");
}

TEST_CASE("codegen: NOT MATCH") {
    REQUIRE(generate("body NOT MATCH 'word'") == "!match(&User::body, \"word\")");
}

TEST_CASE("codegen: IS NULL in compound expression") {
    auto result = generateFull("a IS NULL AND b = 1");
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
    auto result = generateFull("abs(a) + length(b)");
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
    REQUIRE(generate("(a + b) * c") == "(c(&User::a) + &User::b) * &User::c");
    REQUIRE(generate("a + b * c") == "c(&User::a) + c(&User::b) * &User::c");
}

// The emitted operators are grouped by C++ precedence, which is not the SQL precedence the parser
// applied. Every operator here is left-associative in C++ too, so a nested right operand regroups
// even at equal precedence; and SQL binds `||`, `&` and `|` tighter than C++ does, so a nested left
// operand regroups as well. Both get parentheses. Values checked against sqlite3 3.51.
TEST_CASE("codegen: a nested operand keeps the grouping SQL gave it") {
    SECTION("right operand of equal precedence") {
        // `1 - (2 - 3)` is 2 in sqlite3, where `c(1) - c(2) - 3` reads as `(1 - 2) - 3` and is -4.
        REQUIRE(generate("1 - (2 - 3)") == "c(1) - (c(2) - 3)");
        REQUIRE(generate("20 / (4 / 2)") == "c(20) / (c(4) / 2)");
        REQUIRE(generate("10 % (7 % 4)") == "c(10) % (c(7) % 4)");
        REQUIRE(generate("1 + (2 - 3)") == "c(1) + (c(2) - 3)");
        REQUIRE(generate("2 * (3 % 4)") == "c(2) * (c(3) % 4)");
        REQUIRE(generate("1 << (2 << 3)") == "c(1) << (c(2) << 3)");
        REQUIRE(generate("a - (a - 1)") == "c(&User::a) - (c(&User::a) - 1)");
        REQUIRE(generate("1 - (0 - a)") == "c(1) - (c(0) - &User::a)");
        REQUIRE(generate("1 AND (2 OR 3)") == "c(1) and (c(2) or 3)");
    }
    SECTION("left operand C++ binds looser than SQL does") {
        // SQL groups `4 & 2 < 3` as `(4 & 2) < 3` and is 1; C++ binds `<` tighter than `&`.
        REQUIRE(generate("4 & 2 < 3") == "(c(4) & 2) < 3");
        // SQL gives `|` and `&` the same precedence, C++ binds `&` tighter.
        REQUIRE(generate("6 | 3 & 5") == "(c(6) | 3) & 5");
        // SQL binds `||` tightest of the binary operators, C++ loosest.
        REQUIRE(generate("'a' || 'b' = 'ab'") == R"((c("a") || "b") == "ab")");
        REQUIRE(generate("(1 + 2) * 3") == "(c(1) + 2) * 3");
        REQUIRE(generate("(a + 1) * 2") == "(c(&User::a) + 1) * 2");
    }
    SECTION("an operand C++ groups the same way is left alone") {
        REQUIRE(generate("1 - 2 - 3") == "c(1) - 2 - 3");
        REQUIRE(generate("1 + 2 * 3") == "c(1) + c(2) * 3");
        REQUIRE(generate("1 * 2 + 3") == "c(1) * 2 + 3");
        REQUIRE(generate("1 OR 2 AND 3") == "c(1) or c(2) and 3");
        REQUIRE(generate("1 = 2 AND 3") == "c(1) == 2 and 3");
    }
    SECTION("an operand that is one C++ term already") {
        // A negation is either a signed constant or the parenthesized `(c(0) - …)` subtraction.
        REQUIRE(generate("1 - -(2 - 3)") == "c(1) - (c(0) - (c(2) - 3))");
        REQUIRE(generate("-(1 - 2) - (3 - 4)") == "(c(0) - (c(1) - 2)) - (c(3) - 4)");
        // A unary plus emits its operand and nothing else, so the operand decides.
        REQUIRE(generate("1 - +(2 - 3)") == "c(1) - (c(2) - 3)");
        REQUIRE(generate("length(a) - (a - 1)") == "length(&User::a) - (c(&User::a) - 1)");
    }
}

// The functional style passes the operands as arguments, which group themselves.
TEST_CASE("codegen: the functional expression style needs no parentheses") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["expr_style"] = "functional";
    REQUIRE(generateWithPolicy("SELECT 1 - (2 - 3);", policy).code ==
            "auto rows = storage.select(sub(1, sub(2, 3)));");
    REQUIRE(generateWithPolicy("SELECT 'a' || 'b' = 'ab';", policy).code ==
            R"(auto rows = storage.select(is_equal(conc("a", "b"), "ab"));)");
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
    REQUIRE(prefixFor("42") == "");
    REQUIRE(prefixFor("'hello'") == "");
}

TEST_CASE("codegen: prefix - single column defaults to int") {
    REQUIRE(prefixFor("a > 5") == "struct User {\n    int a = 0;\n};");
}

TEST_CASE("codegen: prefix - inferred string from comparison") {
    REQUIRE(prefixFor("name = 'hello'") == "struct User {\n    std::string name;\n};");
}

TEST_CASE("codegen: prefix - inferred double from real") {
    REQUIRE(prefixFor("x > 3.14") == "struct User {\n    double x = 0.0;\n};");
}

// The literal SQLite reads as a REAL drags the synthesized column along with it, the same way a
// real literal does one line above.
TEST_CASE("codegen: prefix - inferred double from an integer literal beyond int64") {
    REQUIRE(prefixFor("x > 99999999999999999999") == "struct User {\n    double x = 0.0;\n};");
}

// An `int` field would truncate the literal the column is compared against, so the field the
// literal infers is only an `int` while an int32 holds it. The boundary values are the ones
// `sqlite3 :memory: "SELECT 2147483648"` prints back unchanged as an INTEGER.
TEST_CASE("codegen: prefix - inferred int64_t from an integer literal beyond int32") {
    REQUIRE(prefixFor("x > 2147483647") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > 2147483648") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > 3000000000") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > 9223372036854775807") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > 0_2147483648") == "struct User {\n    int64_t x = 0;\n};");
}

// SQLite reads a hex literal as a signed 64-bit integer and wraps it around, so the width the
// field needs follows the wrapped value: `0xFFFFFFFFFFFFFFFF` is -1 and an `int` holds it, while
// `0xFFFFFFFF7FFFFFFF` is -2147483649 and one does not.
TEST_CASE("codegen: prefix - inferred int64_t from a hexadecimal literal beyond int32") {
    REQUIRE(prefixFor("x > 0x7FFFFFFF") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > 0x80000000") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > 0xDEADBEEF") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > 0xFFFFFFFF7FFFFFFF") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > 0xFFFFFFFF80000000") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > 0xffffffffffffffff") == "struct User {\n    int x = 0;\n};");
    // Nine to fifteen digits sit past `0xFFFFFFFF` and short of the wrap-around, so the value is
    // positive and out of reach of an int32 whatever the digits are: `0x100000000` is 4294967296
    // and `0xFFFFFFFFFFFFFFF` is 1152921504606846975.
    REQUIRE(prefixFor("x > 0x100000000") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > 0xFFFFFFFFFFFFFFF") == "struct User {\n    int64_t x = 0;\n};");
}

// Neither the leading zeros nor the `_` separators SQLite allows carry any value, so the digits
// that decide the width are the significant ones: `00000000000005` and `0x00000000000000000005`
// are both 5, and `0x0000000080000000` is the 2147483648 of `0x80000000`.
TEST_CASE("codegen: prefix - the width of a literal follows its significant digits") {
    REQUIRE(prefixFor("x > 00000000000005") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > 0x00000000000000000005") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > 0x0000000080000000") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > 0_2147483647") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > 21474_83648") == "struct User {\n    int64_t x = 0;\n};");
}

// A sign does not bring the value back into an int32, and the operand of a unary plus or minus
// decides the field on its own.
TEST_CASE("codegen: prefix - inferred type looks through a unary sign") {
    REQUIRE(prefixFor("x > -3000000000") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > +3000000000") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > -5") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > -3.14") == "struct User {\n    double x = 0.0;\n};");
}

// The same inference answers for BETWEEN, for IN and for the result type of a CASE, so a literal
// beyond int32 widens the field there too.
TEST_CASE("codegen: int64_t literal widens BETWEEN, IN and CASE") {
    REQUIRE(prefixFor("x BETWEEN 3000000000 AND 4000000000") ==
            "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x IN (3000000000)") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(generate("CASE WHEN a > 0 THEN 3000000000 ELSE 0 END") ==
            "case_<int64_t>().when(c(&User::a) > 0, then(3000000000)).else_(0).end()");
}

// Every operand of a BETWEEN and every value of an IN list is compared against the column, so a
// literal beyond int32 widens the field wherever it stands, not only in the leading position.
TEST_CASE("codegen: int64_t literal widens BETWEEN and IN from a trailing position") {
    REQUIRE(prefixFor("x BETWEEN 1 AND 3000000000") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x IN (1, 3000000000)") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x IN (1, 2, 0xFFFFFFFF)") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x NOT BETWEEN 1 AND 3000000000") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x NOT IN (1, 3000000000)") == "struct User {\n    int64_t x = 0;\n};");
}

// A list that stays inside an int32 keeps the readable `int`, from either position.
TEST_CASE("codegen: BETWEEN and IN within int32 keep an int field") {
    REQUIRE(prefixFor("x BETWEEN 1 AND 5") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x IN (1, 2, 3)") == "struct User {\n    int x = 0;\n};");
}

TEST_CASE("codegen: prefix - LIKE infers string") {
    REQUIRE(prefixFor("name LIKE '%foo%'") == "struct User {\n    std::string name;\n};");
}

TEST_CASE("codegen: prefix - instr first argument infers string column") {
    REQUIRE(prefixFor("SELECT name, instr(abilities, 'o') FROM marvel ORDER BY 2") ==
            "struct Marvel {\n    std::string abilities;\n    std::string name;\n};");
}

TEST_CASE("codegen: prefix - synthetic column name heuristic for text") {
    REQUIRE(prefixFor("SELECT title FROM books") == "struct Books {\n    std::string title;\n};");
    REQUIRE(prefixFor("SELECT id FROM books") == "struct Books {\n    int id = 0;\n};");
}

TEST_CASE("codegen: prefix - qualified quoted columns populate synthetic struct") {
    REQUIRE(prefixFor(R"(SELECT "last_result"."id", "last_result"."stamp" FROM "last_result")") ==
            "struct LastResult {\n    int id = 0;\n    int stamp = 0;\n};");
}

TEST_CASE("codegen: prefix - multiple columns sorted") {
    auto result = prefixFor("a > 5 AND b = 'hello'");
    REQUIRE(result == "struct User {\n    int a = 0;\n    std::string b;\n};");
}

TEST_CASE("codegen: prefix - CASE with int return type") {
    REQUIRE(generate("CASE WHEN a > 0 THEN 1 ELSE 0 END") ==
            "case_<int>().when(c(&User::a) > 0, then(1)).else_(0).end()");
}

TEST_CASE("codegen: IS expr returns error") {
    REQUIRE(generateFull("SELECT 1 IS 2 FROM users;") ==
        CodeGenResult{{}, {}, {},
            {"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
             "is not supported in sqlite_orm"}});
}

TEST_CASE("codegen: IS NOT expr returns error") {
    REQUIRE(generateFull("SELECT 1 IS NOT 2 FROM users;") ==
        CodeGenResult{{}, {}, {},
            {"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
             "is not supported in sqlite_orm"}});
}

TEST_CASE("codegen: IS DISTINCT FROM returns error") {
    REQUIRE(generateFull("SELECT a IS DISTINCT FROM b FROM t;") ==
        CodeGenResult{{}, {}, {},
            {"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
             "is not supported in sqlite_orm"}});
}

TEST_CASE("codegen: IS NOT DISTINCT FROM returns error") {
    REQUIRE(generateFull("SELECT a IS NOT DISTINCT FROM b FROM t;") ==
        CodeGenResult{{}, {}, {},
            {"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
             "is not supported in sqlite_orm"}});
}

TEST_CASE("codegen: JSON -> operator") {
    REQUIRE(generateFull("SELECT data -> '$.name' FROM users;") ==
        CodeGenResult{"auto rows = storage.select(json_extract(&Users::data, \"$.name\"));",
                      {columnRefStyleDp(1, "&Users::data"),
                       DecisionPoint{2, "expr_style", "functional",
                          "json_extract(&Users::data, \"$.name\")",
                          {Option{"operator_wrap_left",
                              "c(&Users::data) -> \"$.name\"", "wrap left operand"},
                           Option{"operator_wrap_right",
                              "&Users::data -> c(\"$.name\")", "wrap right operand"},
                           Option{"functional",
                              "json_extract(&Users::data, \"$.name\")", "functional style"},
                           Option{"operator_wrap_both",
                              "c(&Users::data) -> c(\"$.name\")", "wrap both operands", true}}}},
                      {"JSON -> / ->> operator is mapped to json_extract() "
                       "— return type may differ from sqlite"}});
}

TEST_CASE("codegen: column_ref_style options list every variant without duplicating the chosen") {
    // options always carry the full set of variants (both member_pointer and column_pointer),
    // each exactly once, regardless of which one is chosen — so a consumer never sees a
    // duplicated entry / double checkmark, and can always switch to the other variant.
    auto check = [](const DecisionPoint& dp, const std::string& expectedChosen) {
        REQUIRE(dp.category == "column_ref_style");
        REQUIRE(dp.chosenValue == expectedChosen);
        REQUIRE(dp.options.size() == 2);
        REQUIRE(dp.options[0].value == "member_pointer");
        REQUIRE(dp.options[1].value == "column_pointer");
        // The chosen value appears in options exactly once (never duplicated).
        int chosenCount = 0;
        for(const auto& option : dp.options) {
            if(option.value == dp.chosenValue) {
                ++chosenCount;
            }
        }
        REQUIRE(chosenCount == 1);
    };
    check(generateFull("SELECT id FROM users;").decisionPoints.at(0), "member_pointer");

    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["column_ref_style"] = "column_pointer";
    check(generateWithPolicy("SELECT id FROM users;", policy).decisionPoints.at(0), "column_pointer");
}

TEST_CASE("codegen: JSON ->> operator") {
    REQUIRE(generateFull("SELECT data ->> '$.name' FROM users;") ==
        CodeGenResult{"auto rows = storage.select(json_extract(&Users::data, \"$.name\"));",
                      {columnRefStyleDp(1, "&Users::data"),
                       DecisionPoint{2, "expr_style", "functional",
                          "json_extract(&Users::data, \"$.name\")",
                          {Option{"operator_wrap_left",
                              "c(&Users::data) ->> \"$.name\"", "wrap left operand"},
                           Option{"operator_wrap_right",
                              "&Users::data ->> c(\"$.name\")", "wrap right operand"},
                           Option{"functional",
                              "json_extract(&Users::data, \"$.name\")", "functional style"},
                           Option{"operator_wrap_both",
                              "c(&Users::data) ->> c(\"$.name\")", "wrap both operands", true}}}},
                      {"JSON -> / ->> operator is mapped to json_extract() "
                       "— return type may differ from sqlite"}});
}

TEST_CASE("codegen: bind parameter anonymous") {
    REQUIRE(generateFull("SELECT ? FROM users;") ==
        CodeGenResult{"auto rows = storage.select(bindParam1);",
                      {},
                      {"bind parameter ? -> C++ variable 'bindParam1'; "
                       "for prepared statements use storage.prepare() + get<N>(stmt)"}});
}

TEST_CASE("codegen: bind parameter named") {
    REQUIRE(generateFull("SELECT :userId FROM users;") ==
        CodeGenResult{"auto rows = storage.select(userId);",
                      {},
                      {"bind parameter :userId -> C++ variable 'userId'; "
                       "for prepared statements use storage.prepare() + get<N>(stmt)"}});
}

TEST_CASE("codegen: expr COLLATE warning") {
    REQUIRE(generateFull("SELECT name COLLATE NOCASE FROM users;") ==
        CodeGenResult{"auto rows = storage.select(&Users::name);",
                      {columnRefStyleDp(1, "&Users::name")},
                      {"COLLATE NOCASE on expressions is not directly supported in sqlite_orm codegen"}});
}

namespace {
    const sqlite2orm::DecisionPoint* findDp(const sqlite2orm::CodeGenResult& result, std::string_view category) {
        for(const auto& dp : result.decisionPoints) {
            if(dp.category == category) return &dp;
        }
        return nullptr;
    }
}  // namespace

TEST_CASE("codegen: unknown function becomes a scalar user-defined function") {
    auto result = generateFull("SELECT * FROM transactions WHERE k = morton_encode(day, cat)");
    CHECK(result.code.find("struct MortonEncode {") != std::string::npos);
    CHECK(result.code.find("int operator()(int day, int cat) const { return {}; }") != std::string::npos);
    CHECK(result.code.find("static const char *name() { return \"morton_encode\"; }") != std::string::npos);
    CHECK(result.code.find("storage.create_scalar_function<MortonEncode>();") != std::string::npos);
    CHECK(result.code.find("func<MortonEncode>(&Transactions::day, &Transactions::cat)") != std::string::npos);
    CHECK(result.errors.empty());
}

TEST_CASE("codegen: custom function exposes a scalar/aggregate/func_only decision point") {
    auto result = generateFull("SELECT * FROM t WHERE k = my_fn(a)");
    const auto* dp = findDp(result, "custom_function");
    REQUIRE(dp != nullptr);
    CHECK(dp->chosenValue == "scalar");
    REQUIRE(dp->options.size() == 3);
    CHECK(dp->options[0].value == "scalar");
    CHECK(dp->options[1].value == "aggregate");
    CHECK(dp->options[2].value == "func_only");
    // The aggregate option renders step/fin; func_only renders a bare declaration and no registration.
    CHECK(dp->options[1].code.find("void step(") != std::string::npos);
    CHECK(dp->options[1].code.find("create_aggregate_function<MyFn>") != std::string::npos);
    CHECK(dp->options[2].code.find("const;") != std::string::npos);
    CHECK(dp->options[2].code.find("create_scalar_function") == std::string::npos);
}

TEST_CASE("codegen: custom_function_style aggregate policy") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["custom_function_style"] = "aggregate";
    auto result = generateWithPolicy("SELECT * FROM t WHERE k = my_fn(a)", policy);
    CHECK(result.code.find("void step(int a) {}") != std::string::npos);
    CHECK(result.code.find("int fin() const { return {}; }") != std::string::npos);
    CHECK(result.code.find("storage.create_aggregate_function<MyFn>();") != std::string::npos);
}

TEST_CASE("codegen: custom_function_style func_only policy (extension)") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["custom_function_style"] = "func_only";
    auto result = generateWithPolicy("SELECT * FROM t WHERE k = my_fn(a)", policy);
    CHECK(result.code.find("int operator()(int a) const;") != std::string::npos);
    CHECK(result.code.find("create_scalar_function") == std::string::npos);
    CHECK(result.code.find("create_aggregate_function") == std::string::npos);
    CHECK(result.code.find("func<MyFn>(&T::a)") != std::string::npos);
}

TEST_CASE("codegen: known functions are not turned into custom functions") {
    auto result = generateFull("SELECT abs(day) FROM t");
    CHECK(result.code.find("struct") == std::string::npos);
    CHECK(findDp(result, "custom_function") == nullptr);
    CHECK(result.code.find("abs(&T::day)") != std::string::npos);
}

// SQLite raises `hex literal too big` in codeInteger(), so an expression it compiles is refused
// whole: `SELECT 0x10000000000000000` and `SELECT * FROM t LIMIT 0x10000000000000000` are both
// `Error: in prepare, hex literal too big`. Checked against sqlite3 3.51.
TEST_CASE("codegen: a hex literal past the int64 range is refused in a compiled expression") {
    REQUIRE(generateFull("SELECT 0x10000000000000000;") ==
            CodeGenResult{{}, {}, {}, {"hex literal too big: 0x10000000000000000"}, {}});
    REQUIRE(generateFull("SELECT * FROM t WHERE x > 0x00001FFFFFFFFFFFFFFFF;") ==
            CodeGenResult{{}, {}, {}, {"hex literal too big: 0x00001FFFFFFFFFFFFFFFF"}, {}});
    // SQLite names the literal with the digit separators taken out, the leading zeros left in.
    REQUIRE(generateFull("SELECT 0x1_FF_FF_FF_FF_FF_FF_FF_FF;") ==
            CodeGenResult{{}, {}, {}, {"hex literal too big: 0x1FFFFFFFFFFFFFFFF"}, {}});
}

// Sixteen significant digits are the most an int64 holds, and leading zeros do not count.
TEST_CASE("codegen: the sixteen-digit hex literals around the boundary still generate") {
    REQUIRE(generate("0xFFFFFFFFFFFFFFFF") == "static_cast<int64_t>(0xFFFFFFFFFFFFFFFF)");
    REQUIRE(generate("0x0000FFFFFFFFFFFFFFFF") == "static_cast<int64_t>(0x0000FFFFFFFFFFFFFFFF)");
}
