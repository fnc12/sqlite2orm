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

    // The hint attached to every column a NOT stands over; asserted on its own in
    // "codegen: a column under a NOT carries its comment".
    const std::string kNotColumnPointerComment =
        "A column under a NOT is generated as `column<T>(&T::x)`: `operator!` is the one sqlite_orm "
        "operator that keeps the `c(...)` its operand carries instead of unwrapping it, and the walker "
        "that collects the tables a statement reads stops at such a wrapper — `select(not c(&T::x))` "
        "comes out with no FROM clause at all and throws `SQL logic error`. The column pointer names "
        "the same column and serializes to the same SQL.";

    // The hint attached to every value a NOT stands over; asserted on its own in
    // "codegen: a value under a NOT carries its comment".
    const std::string kNotValueAddedToZeroComment =
        "A value under a NOT is generated as `(c(0) + value)`: sqlite_orm binds the values of a "
        "statement by walking its expression tree, and that walk stops at the `c(...)` a NOT keeps "
        "over its operand — the value of `select(not c(0))` is never bound, so the statement runs "
        "with an empty parameter and answers NULL. A binary operator unwraps what it is given, and "
        "`0 + x` is the numeric coercion SQLite applies to `x` in a boolean context anyway, so "
        "`NOT x` and `NOT (0 + x)` answer alike.";

    // The hint attached to every NOT the generator delimits with a CAST; asserted on its own in
    // "codegen: a NOT over a NOT carries its comment".
    const std::string kNegatedConditionCastComment =
        "A NOT over a NOT is generated as `not cast<int64_t>(not …)`: sqlite_orm's `negated_condition_t` "
        "— what a NOT and the `!predicate` spelling of a negated BETWEEN, LIKE, GLOB and MATCH produce — "
        "is neither negatable nor an operator argument, so a second NOT over it does not compile. The "
        "CAST leaves what the inner NOT stands for alone: it is 0, 1 or NULL, and a CAST to INTEGER "
        "keeps all three.";

    // The hint attached to every NOT the generator delimits with a CAST to REAL; asserted on its
    // own in "codegen: a NOT over a concatenation carries its comment".
    const std::string kConcatenationCastComment =
        "A NOT over a concatenation is generated as `not cast<double>(…)`: sqlite_orm's `conc_t` is "
        "`binary_operator<L, R, conc_string>` and nothing else — neither negatable nor an operator "
        "argument — so `not (c(&T::a) || \"x\")` does not compile. A concatenation answers TEXT or "
        "NULL, and SQLite reads the truth of a text value through its real value: `NOT ('0' || '.5')` "
        "is 0, where `NOT CAST('0' || '.5' AS INTEGER)` is 1. A CAST to REAL parses the text exactly "
        "as that truth test does, so `NOT x` and `NOT CAST(x AS REAL)` answer alike.";

    // The hint attached to every operator spelled as a call because the C++ `||` token would build
    // the other node; asserted on its own in "codegen: an operator spelled as a call carries its
    // comment".
    const std::string kOrTokenCallSpellingComment =
        "`OR` is generated as `or_(left, right)` and `||` as `conc(left, right)`: C++ spells both "
        "of them `||`, and sqlite_orm picks between the two by the operands — `operator||` builds "
        "the OR condition only when one of them is a condition (a comparison, AND, OR, IN, BETWEEN, "
        "LIKE, GLOB, IS [NOT] NULL, EXISTS or NOT) and the concatenation otherwise. So `1 OR 0` "
        "spelled `c(1) or 0` runs as `1 || 0` and answers '10', and `(a = 1) || 'x'` spelled "
        "`c(&T::a) == 1 || \"x\"` runs as `(a = 1) OR 'x'`. The call names the node it builds.";

    // The hint attached to every AND and OR the generator delimits in a predicate's argument slot;
    // asserted on its own in "codegen: an AND or an OR cast in a predicate argument carries its
    // comment".
    const std::string kAndOrPredicateArgumentCastComment =
        "An AND or an OR in the argument of a predicate is generated as `cast<int64_t>(…)`: "
        "sqlite_orm serializes IN, BETWEEN, LIKE, GLOB, MATCH and IS [NOT] NULL with no "
        "parentheses around their arguments, and SQLite binds AND and OR looser than every "
        "predicate, so `is_null(or_(1, 0))` would be read back as `1 OR (0 IS NULL)` and "
        "`between(1, or_(1, 0), 3)` would not even parse. The CAST delimits the argument and "
        "leaves what it stands for alone — an AND and an OR are 0, 1 or NULL, and a CAST to "
        "INTEGER keeps all three, typeof included.";

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
// The `~` column carries the int64 widening of
// "codegen: a bitwise result column is cast to an int64_t" on top of that.
TEST_CASE("codegen: negative literal as an operand") {
    REQUIRE(generate("SELECT -2;") == "auto rows = storage.select(-2);");
    REQUIRE(generate("SELECT 100 / -2;") == "auto rows = storage.select(as_optional(c(100) / -2));");
    REQUIRE(generate("SELECT -2 + 3;") == "auto rows = storage.select(c(-2) + 3);");
    REQUIRE(generate("SELECT a * -2;") == "auto rows = storage.select(as_optional(c(&User::a) * -2));");
    REQUIRE(generate("SELECT ~ -2;") == "auto rows = storage.select(cast<int64_t>(~c(-2)));");
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
        // Neither operand is a condition, so `c(&User::a) or &User::b` would be the concatenation
        // `operator||` and not an OR at all; the call is the only form there.
        REQUIRE(generateFull("a OR b") == CodeGenResult{
            "or_(&User::a, &User::b)",
            {
                columnRefStyleDp(1, "&User::a"),
                columnRefStyleDp(2, "&User::b"),
                DecisionPoint{3, "expr_style", "functional", "or_(&User::a, &User::b)",
                    {
                        Option{"functional", "or_(&User::a, &User::b)", "functional style"},
                    }},
            },
            {},
            {},
            {kOrTokenCallSpellingComment}});
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

// C++ spells a logical OR and a concatenation with the same token, `||`, and sqlite_orm's two
// `operator||` overloads pick between the `or_condition_t` and the `conc_t` by the operands: the OR
// only when one of them is a condition, the concatenation only when neither is. `or_()` and
// `conc()` name the node they build, so the call is the form for every operator the token would
// misread. The values each form answers with are pinned in "runtime: an OR and a concatenation
// return the values SQLite computes".
TEST_CASE("codegen: an operator the C++ `||` token misreads is spelled as a call") {
    SECTION("an OR over operands that are not conditions") {
        // `c(1) or 0` is `1 || 0`, which SQLite answers with '10' where `1 OR 0` is 1.
        REQUIRE(generate("1 OR 0") == "or_(1, 0)");
        REQUIRE(generate("a OR 0") == "or_(&User::a, 0)");
        REQUIRE(generate("a OR b") == "or_(&User::a, &User::b)");
        REQUIRE(generate("-(1 OR 0)") == "(c(0) - (or_(1, 0)))");
        // `match_t` is the one predicate sqlite_orm does not class as a condition. The call names
        // the OR that was written, but it does not compile either: `match_t` derives from nothing
        // at all, so `or_()` does not accept it as an operand either: the call is the OR that was
        // written, but it does not compile — `or_() arguments must be bindable values or
        // sqlite_orm-recognized operands`. Neither did the operator spelling it replaces, which
        // had no `operator||` to pick at all, so this is what the form looks like and not a form
        // that works yet.
        REQUIRE(generate("a MATCH 'x' OR b") == R"(or_(match(&User::a, "x"), &User::b))");
    }
    SECTION("an OR with a condition among its operands") {
        REQUIRE(generate("a = 1 OR b") == "c(&User::a) == 1 or &User::b");
        REQUIRE(generate("a OR b = 1") == "c(&User::a) or c(&User::b) == 1");
        REQUIRE(generate("a AND b OR c") == "c(&User::a) and &User::b or &User::c");
        REQUIRE(generate("(a IS NULL) OR b") == "is_null(&User::a) or &User::b");
        REQUIRE(generate("a IN (1, 2) OR b") == "in(&User::a, {1, 2}) or &User::b");
        REQUIRE(generate("a BETWEEN 1 AND 9 OR b") == "between(&User::a, 1, 9) or &User::b");
        REQUIRE(generate("a LIKE 'x' OR b") == R"(like(&User::a, "x") or &User::b)");
        REQUIRE(generate("NOT a OR b") == "not column<User>(&User::a) or &User::b");
    }
    SECTION("an operand spelled as a call takes the chain with it") {
        REQUIRE(generate("1 OR 0 OR 1") == "or_(or_(1, 0), 1)");
        REQUIRE(generate("1 OR 0 OR a = 1") == "or_(or_(1, 0), c(&User::a) == 1)");
        // The chain that starts with a condition keeps the operator spelling throughout.
        REQUIRE(generate("a = 1 OR b OR c") == "c(&User::a) == 1 or &User::b or &User::c");
    }
    SECTION("a concatenation over a condition") {
        // `c(&User::a) == 1 || "x"` is `(a = 1) OR 'x'`, where the concatenation is '1x'.
        REQUIRE(generate("(a = 1) || 'x'") == R"(conc(c(&User::a) == 1, "x"))");
        REQUIRE(generate("'x' || (a = 1)") == R"(conc("x", c(&User::a) == 1))");
        REQUIRE(generate("(1 OR 0) || 'x'") == R"(conc(or_(1, 0), "x"))");
        REQUIRE(generate("'a' || (a = 1) || 'c'") == R"(conc(conc("a", c(&User::a) == 1), "c"))");
        REQUIRE(generate("EXISTS(SELECT 1) || 'x'") == R"(conc(exists(select(1)), "x"))");
    }
    SECTION("a concatenation over operands that are not conditions") {
        REQUIRE(generate("a || b") == "c(&User::a) || &User::b");
        REQUIRE(generate("'a' || 'b'") == R"(c("a") || "b")");
        REQUIRE(generate("a || b || c") == "c(&User::a) || &User::b || &User::c");
        // A predicate is delimited with a CAST already, and a `cast_t` is not a condition, so the
        // operator spelling is the concatenation it reads as.
        REQUIRE(generate("(a IS NULL) || 'x'") == R"(cast<int64_t>(is_null(&User::a)) || "x")");
        REQUIRE(generate("(a IN (1, 2)) || 'x'") == R"(cast<int64_t>(in(&User::a, {1, 2})) || "x")");
        REQUIRE(generate("(NOT a) || 'x'") == R"(cast<int64_t>(not column<User>(&User::a)) || "x")");
    }
}

TEST_CASE("codegen: an operator spelled as a call carries its comment") {
    REQUIRE(generateFull("1 OR 0").comments == std::vector<std::string>{kOrTokenCallSpellingComment});
    REQUIRE(generateFull("(a = 1) || 'x'").comments == std::vector<std::string>{kOrTokenCallSpellingComment});
    REQUIRE(generateFull("a = 1 OR b").comments.empty());
    REQUIRE(generateFull("a || b").comments.empty());
}

TEST_CASE("codegen: logical NOT") {
    SECTION("leaf operand") {
        auto result = generateFull("NOT a");
        REQUIRE(result == CodeGenResult{"not column<User>(&User::a)",
            {
                DecisionPoint{1, "expr_style", "operator", "not column<User>(&User::a)",
                              {
                                  Option{"operator", "not column<User>(&User::a)", "operator style"},
                                  Option{"operator_excl", "!column<User>(&User::a)", "use ! instead of not"},
                              }},
            },
            {},
            {},
            {kNotColumnPointerComment}});
    }
    SECTION("value operand") {
        auto result = generateFull("NOT 1");
        REQUIRE(result == CodeGenResult{"not (c(0) + 1)",
            {
                DecisionPoint{1, "expr_style", "operator", "not (c(0) + 1)",
                              {
                                  Option{"operator", "not (c(0) + 1)", "operator style"},
                                  Option{"operator_excl", "!(c(0) + 1)", "use ! instead of not"},
                              }},
            },
            {},
            {},
            {kNotValueAddedToZeroComment}});
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

// `operator!` is the one sqlite_orm operator that keeps the `c(...)` its operand carries instead of
// unwrapping it, and the walker that collects the tables a statement reads stops at such a wrapper.
// A column a NOT is the only mention of was invisible to it, so `select(not c(&Users::a))` came out
// as `SELECT NOT "users"."a"` with no FROM clause at all and threw `SQL logic error`. The
// column-pointer form names the same column and serializes to the same SQL. The forms that already
// carry their table — an aliased column, a CTE column — are left as they were. The values the
// generated code answers with are pinned in
// "runtime: a NOT over a column returns the value SQLite computes".
TEST_CASE("codegen: a column under a NOT is generated as a column pointer") {
    REQUIRE(generate("SELECT NOT a FROM users;") ==
            "auto rows = storage.select(as_optional(not column<Users>(&Users::a)));");
    REQUIRE(generate("SELECT NOT users.a FROM users;") ==
            "auto rows = storage.select(as_optional(not column<Users>(&Users::a)));");
    REQUIRE(generate("SELECT NOT (a COLLATE NOCASE) FROM users;") ==
            "auto rows = storage.select(as_optional(not column<Users>(&Users::a)));");
    REQUIRE(generate("SELECT 1 FROM users WHERE NOT a;") ==
            "auto rows = storage.select(1, where(not column<Users>(&Users::a)));");
    REQUIRE(generate("CREATE TABLE t(a INTEGER CHECK(NOT a));") ==
            "struct T {\n"
            "    std::optional<int64_t> a;\n"
            "};\n"
            "\n"
            "auto storage = make_storage(\"\",\n"
            "    make_table(\"t\",\n"
            "        make_column(\"a\", &T::a, check(not column<T>(&T::a)))));");
    REQUIRE(generate("SELECT NOT x.a FROM users AS x;") ==
            "auto rows = storage.select(as_optional(not alias_column<alias_a<Users>>(&Users::a)));");
    // An operand that is not a leaf is not wrapped in `c(...)` to begin with: a binary operator
    // unwraps what it is given, so the column under one stays a member pointer.
    REQUIRE(generate("SELECT NOT (a + 1) FROM users;") ==
            "auto rows = storage.select(as_optional(not (c(&Users::a) + 1)));");
    REQUIRE(generate("SELECT NOT -a FROM users;") ==
            "auto rows = storage.select(as_optional(not (c(0) - c(&Users::a))));");
}

TEST_CASE("codegen: a column under a NOT carries its comment") {
    auto result = generateFull("SELECT NOT a FROM users;");
    REQUIRE(result.comments == std::vector<std::string>{kNotColumnPointerComment});
}

// A column that names a SELECT alias is generated as `get<Alias>()` whatever a NOT over it asks
// for: the alias carries the statement it was declared in, so the walk that collects the tables
// reads it through the `c(...)` wrapper and there is no column pointer to put in its place. The
// form is the one it had before a NOT ever rewrote a column, and the explanation of a rewrite that
// did not happen stays off it.
TEST_CASE("codegen: a SELECT alias under a NOT keeps the form it had") {
    auto result = generateFull("SELECT a AS al FROM users WHERE NOT al;");
    REQUIRE(result.code ==
            "struct AlAlias : sqlite_orm::alias_tag {\n"
            "    static const std::string& get() {\n"
            "        static const std::string res = \"al\";\n"
            "        return res;\n"
            "    }\n"
            "};\n"
            "auto rows = storage.select(as<AlAlias>(&Users::a), where(not c(get<AlAlias>())));");
    REQUIRE(result.comments == std::vector<std::string>{});
}

// The same wrapper hides a value from the walk that binds one: the literal of `select(not c(0))`
// never reached a parameter, so the statement ran with an empty one and answered NULL where SQLite
// answers 1. A binary operator unwraps what it is given, and `0 + x` is the numeric coercion SQLite
// applies to `x` in a boolean context anyway. A leaf that names something instead of carrying a
// value — NEW/OLD in a trigger, a datetime function — is serialized into the SQL and needs nothing.
TEST_CASE("codegen: a value under a NOT is added to zero") {
    REQUIRE(generate("SELECT NOT 0;") == "auto rows = storage.select(not (c(0) + 0));");
    REQUIRE(generate("SELECT NOT 0.5;") == "auto rows = storage.select(not (c(0) + 0.5));");
    REQUIRE(generate("SELECT NOT 'abc';") == "auto rows = storage.select(not (c(0) + \"abc\"));");
    REQUIRE(generate("SELECT NOT TRUE;") == "auto rows = storage.select(not (c(0) + true));");
    REQUIRE(generate("SELECT NOT NULL;") == "auto rows = storage.select(as_optional(not (c(0) + nullptr)));");
    REQUIRE(generate("SELECT NOT -2;") == "auto rows = storage.select(not (c(0) + -2));");
    REQUIRE(generate("SELECT NOT x'3132';") ==
            "auto rows = storage.select(not (c(0) + std::vector<char>{'\\x31', '\\x32'}));");
    REQUIRE(generate("SELECT NOT current_timestamp;") ==
            "auto rows = storage.select(not c(current_timestamp()));");
}

TEST_CASE("codegen: a value under a NOT carries its comment") {
    auto result = generateFull("SELECT NOT 0;");
    REQUIRE(result.comments == std::vector<std::string>{kNotValueAddedToZeroComment});
}

// sqlite_orm classifies `negated_condition_t` — what a NOT and the `!predicate` spelling of a
// negated BETWEEN, LIKE, GLOB and MATCH generate — as neither negatable nor an operator argument,
// so a second NOT over one does not compile at all. A CAST to INTEGER makes it an operand again and
// keeps its value: a predicate is 0, 1 or NULL, and a CAST to INTEGER keeps all three. The
// predicates sqlite_orm does class as negatable need nothing.
TEST_CASE("codegen: a NOT over a NOT is delimited by a CAST") {
    REQUIRE(generate("SELECT NOT NOT a FROM users;") ==
            "auto rows = storage.select(as_optional(not cast<int64_t>(not column<Users>(&Users::a))));");
    REQUIRE(generate("SELECT NOT NOT NOT a FROM users;") ==
            "auto rows = storage.select(as_optional(not cast<int64_t>(not cast<int64_t>(not "
            "column<Users>(&Users::a)))));");
    REQUIRE(generate("SELECT NOT (a NOT BETWEEN 1 AND 9) FROM users;") ==
            "auto rows = storage.select(as_optional(not cast<int64_t>(!between(&Users::a, 1, 9))));");
    REQUIRE(generate("SELECT NOT (a NOT LIKE 'x') FROM users;") ==
            "auto rows = storage.select(as_optional(not cast<int64_t>(!like(&Users::a, \"x\"))));");
    REQUIRE(generate("SELECT NOT (a NOT GLOB 'x') FROM users;") ==
            "auto rows = storage.select(as_optional(not cast<int64_t>(!glob(&Users::a, \"x\"))));");
    // BETWEEN, LIKE, GLOB, IN and the NULL tests are negatable on their own, and `not_in` is an
    // `in` with its negation inside rather than a NOT over one.
    REQUIRE(generate("SELECT NOT (a BETWEEN 1 AND 9) FROM users;") ==
            "auto rows = storage.select(as_optional(not (between(&Users::a, 1, 9))));");
    REQUIRE(generate("SELECT NOT (a LIKE 'x') FROM users;") ==
            "auto rows = storage.select(as_optional(not (like(&Users::a, \"x\"))));");
    REQUIRE(generate("SELECT NOT (a IS NULL) FROM users;") ==
            "auto rows = storage.select(not (is_null(&Users::a)));");
    REQUIRE(generate("SELECT NOT (a NOT IN (1, 2)) FROM users;") ==
            "auto rows = storage.select(as_optional(not (not_in(&Users::a, {1, 2}))));");
}

TEST_CASE("codegen: a NOT over a NOT carries its comment") {
    auto result = generateFull("SELECT NOT NOT a FROM users;");
    REQUIRE(result.comments ==
            std::vector<std::string>{kNotColumnPointerComment, kNegatedConditionCastComment});
}

// A concatenation carries the same CAST as a NOT does: sqlite_orm's `conc_t` is
// `binary_operator<L, R, conc_string>` and nothing else — neither negatable nor an operator
// argument — so `not (c(&Users::a) || "x")` stops at `no match for operator!`. The CAST has to be
// one to REAL, not the CAST to INTEGER a nested NOT takes: a concatenation answers TEXT or NULL,
// and SQLite reads the truth of a text value through its real value, so `NOT ('0' || '.5')` is 0
// where `NOT CAST('0' || '.5' AS INTEGER)` is 1. The values are pinned in
// "runtime: a NOT over a concatenation returns the value SQLite computes".
TEST_CASE("codegen: a NOT over a concatenation is delimited by a CAST to REAL") {
    auto check = [](std::string_view sql, const std::string& code) {
        auto result = generateFull(sql);
        REQUIRE(result.code == code);
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    };
    check("SELECT NOT (a || 'x') FROM users;",
          "auto rows = storage.select(as_optional(not cast<double>(c(&Users::a) || \"x\")));");
    check("SELECT NOT ('a' || 'b');", "auto rows = storage.select(not cast<double>(c(\"a\") || \"b\"));");
    check("SELECT NOT ('a' || 'b' || 'c');",
          "auto rows = storage.select(not cast<double>(c(\"a\") || \"b\" || \"c\"));");
    // The call spelling a condition operand takes is the same `conc_t`, and so is the one a
    // predicate delimited with a CAST keeps.
    check("SELECT NOT ((a = 1) || 'x') FROM users;",
          "auto rows = storage.select(as_optional(not cast<double>(conc(c(&Users::a) == 1, \"x\"))));");
    check("SELECT NOT ((a IS NULL) || 'x') FROM users;",
          "auto rows = storage.select(not cast<double>(cast<int64_t>(is_null(&Users::a)) || \"x\"));");
    check("SELECT 1 FROM users WHERE NOT (a || 'x');",
          "auto rows = storage.select(1, where(not cast<double>(c(&Users::a) || \"x\")));");
    // A NOT over the NOT keeps the CAST to INTEGER its own operand needs: the inner NOT answers
    // 0, 1 or NULL, which a CAST to INTEGER holds.
    check("SELECT NOT NOT (a || 'x') FROM users;",
          "auto rows = storage.select(as_optional(not cast<int64_t>(not cast<double>(c(&Users::a) || "
          "\"x\"))));");
}

// A COLLATE generates its operand and nothing else, so the concatenation under one is the operand
// the NOT really stands over in the generated code.
TEST_CASE("codegen: a NOT over a collated concatenation is delimited as well") {
    auto result = generateFull("SELECT NOT (('a' || 'b') COLLATE NOCASE);");
    REQUIRE(result.code == "auto rows = storage.select(not cast<double>(c(\"a\") || \"b\"));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"COLLATE NOCASE on expressions is not directly supported in sqlite_orm codegen"}});
}

TEST_CASE("codegen: a NOT over a concatenation carries its comment") {
    auto result = generateFull("SELECT NOT ('a' || 'b');");
    REQUIRE(result.comments == std::vector<std::string>{kConcatenationCastComment});
}

// An OR needs nothing: sqlite_orm spells `or` and the concatenation with the same `operator||`, and
// an OR is generated as the call whenever the operator would pick the `conc_t` — an `or_condition_t`
// either way, which sqlite_orm does negate. The values are pinned in
// "runtime: a NOT over an OR returns the value SQLite computes".
TEST_CASE("codegen: a NOT over an OR is negated as it stands") {
    auto check = [](std::string_view sql, const std::string& code) {
        auto result = generateFull(sql);
        REQUIRE(result.code == code);
        REQUIRE(result.warnings == std::vector<CodegenWarning>{});
    };
    check("SELECT NOT (a OR b) FROM users;",
          "auto rows = storage.select(as_optional(not (or_(&Users::a, &Users::b))));");
    check("SELECT NOT (1 OR 0);", "auto rows = storage.select(not (or_(1, 0)));");
    check("SELECT NOT (a = 1 OR b = 2) FROM users;",
          "auto rows = storage.select(as_optional(not (c(&Users::a) == 1 or c(&Users::b) == 2)));");
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
    check("SELECT - NOT a FROM users;", "auto rows = storage.select(-(not column<Users>(&Users::a)));",
          "NOT");
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
    REQUIRE(generate("1 - (NOT a)") == "c(1) - cast<int64_t>(not column<User>(&User::a))");
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
    REQUIRE(generate("(NOT a) AND 1") == "not column<User>(&User::a) and 1");
    REQUIRE(generate("(NOT a) = 1") == "cast<int64_t>(not column<User>(&User::a)) == 1");
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

// A predicate serializer parenthesizes none of its arguments, and SQLite binds AND and OR looser
// than every predicate, so an AND or an OR standing there bare takes the predicate into itself:
// `1 OR 0 IS NULL` is `1 OR (0 IS NULL)` and answers 1 where `(1 OR 0) IS NULL` answers 0, and
// `1 BETWEEN 1 OR 0 AND 3` is not a statement SQLite parses at all. The same CAST that delimits a
// predicate under an operator delimits them. Checked against sqlite3 3.51.
TEST_CASE("codegen: an AND or an OR in a predicate argument is cast to stay one SQL term") {
    SECTION("every argument slot of every predicate") {
        REQUIRE(generate("(1 OR 0) IS NULL") == "is_null(cast<int64_t>(or_(1, 0)))");
        REQUIRE(generate("(1 OR 0) NOT NULL") == "is_not_null(cast<int64_t>(or_(1, 0)))");
        REQUIRE(generate("(a OR b) IN (0, 1)") ==
                "in(cast<int64_t>(or_(&User::a, &User::b)), {0, 1})");
        REQUIRE(generate("(a OR b) NOT IN (0, 1)") ==
                "not_in(cast<int64_t>(or_(&User::a, &User::b)), {0, 1})");
        REQUIRE(generate("(a OR b) BETWEEN 0 AND 1") ==
                "between(cast<int64_t>(or_(&User::a, &User::b)), 0, 1)");
        // `between(A, T, T)` deduces one type for both bounds, so these two do not compile — nor
        // did they on master, where the bound was a `conc_t` — but the SQL a bound serializes into
        // runs into the `AND` of the BETWEEN itself, so the CAST belongs there all the same.
        REQUIRE(generate("1 BETWEEN (1 OR 0) AND 3") == "between(1, cast<int64_t>(or_(1, 0)), 3)");
        REQUIRE(generate("1 BETWEEN 0 AND (1 OR 0)") == "between(1, 0, cast<int64_t>(or_(1, 0)))");
        REQUIRE(generate("(a OR b) LIKE 'x'") ==
                R"(like(cast<int64_t>(or_(&User::a, &User::b)), "x"))");
        REQUIRE(generate("'x' LIKE (a OR b)") ==
                R"(like("x", cast<int64_t>(or_(&User::a, &User::b))))");
        REQUIRE(generate("(a OR b) NOT LIKE 'x'") ==
                R"(!like(cast<int64_t>(or_(&User::a, &User::b)), "x"))");
        REQUIRE(generate("(a OR b) GLOB 'x'") ==
                R"(glob(cast<int64_t>(or_(&User::a, &User::b)), "x"))");
        REQUIRE(generate("a MATCH (b OR c)") ==
                "match(&User::a, cast<int64_t>(or_(&User::b, &User::c)))");
    }
    SECTION("an AND is bound the same way") {
        REQUIRE(generate("(1 AND 0) IS NULL") == "is_null(cast<int64_t>(c(1) and 0))");
        REQUIRE(generate("((a OR b) AND c) IN (0, 1)") ==
                "in(cast<int64_t>(or_(&User::a, &User::b) and &User::c), {0, 1})");
    }
    SECTION("an argument SQLite reads as one term is left bare") {
        // A comparison shares the rank of the predicates and SQLite reads them left-associatively,
        // so the argument slot is where it already groups: `"a" = 1 IS NULL` is `(a = 1) IS NULL`.
        REQUIRE(generate("(a = 1) IS NULL") == "is_null(c(&User::a) == 1)");
        REQUIRE(generate("(a + 1) IS NULL") == "is_null(c(&User::a) + 1)");
        REQUIRE(generate("(a IS NULL) IS NULL") == "is_null(is_null(&User::a))");
        // The values of an IN list are delimited by the commas around them. Such a list does not
        // compile, here or on master — its values have to share one C++ type and an `or_(…)` does
        // not have the type of a number — but the SQL it stands for needs no CAST.
        REQUIRE(generate("a IN (1 OR 0, 2)") == "in(&User::a, {or_(1, 0), 2})");
    }
}

TEST_CASE("codegen: an AND or an OR cast in a predicate argument carries its comment") {
    REQUIRE(generateFull("SELECT (1 OR 0) IS NULL;").comments ==
            std::vector<std::string>{kOrTokenCallSpellingComment, kAndOrPredicateArgumentCastComment});
    REQUIRE(generateFull("SELECT (1 AND 0) IS NULL;").comments ==
            std::vector<std::string>{kAndOrPredicateArgumentCastComment});
    REQUIRE(generateFull("SELECT (a = 1) IS NULL;").comments.empty());
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
        REQUIRE(generate("1 AND (2 OR 3)") == "c(1) and or_(2, 3)");
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

// A magnitude past the int32 range stays past it under either sign, and the operand of a unary
// plus or minus decides the field on its own.
TEST_CASE("codegen: prefix - inferred type looks through a unary sign") {
    REQUIRE(prefixFor("x > -3000000000") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > +3000000000") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > -5") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > -3.14") == "struct User {\n    double x = 0.0;\n};");
}

// A minus sign belongs to the literal standing behind it, so the width follows the value the two
// spell together rather than the magnitude alone. It only shows on the edges of the range, where
// the sign carries a value across: `sqlite3 :memory: "SELECT -0x80000000, -0xFFFFFFFF80000000"`
// prints -2147483648, which an `int` holds, and 2147483648, which it does not.
TEST_CASE("codegen: prefix - the width of a literal follows the sign folded into it") {
    REQUIRE(prefixFor("x > -2147483648") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > -2147483649") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > -0x80000000") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > -0x80000001") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > -0x100000000") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > -0xFFFFFFFF80000000") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > -0xFFFFFFFF80000001") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > -0xFFFFFFFFFFFFFFFF") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > +0xFFFFFFFF80000000") == "struct User {\n    int x = 0;\n};");
}

// Parentheses leave no node behind, so `-(-2147483648)` is a minus over a minus and every sign
// counts: `sqlite3 :memory: "SELECT -(-2147483648), -(-(-2147483648))"` prints 2147483648, which
// an `int` field reads back as -2147483648, and -2147483648, which it holds. A unary plus
// disappears from SQLite's parse tree altogether and does not interrupt the chain either:
// `SELECT -+2147483648` prints -2147483648 and `SELECT -(+(-2147483648))` prints 2147483648.
TEST_CASE("codegen: prefix - the width of a literal follows every sign folded into it") {
    REQUIRE(prefixFor("x > -(-2147483648)") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > -(-0x80000000)") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > -(-(-2147483648))") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > -(-2147483647)") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > -+2147483648") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > -(+(-2147483648))") == "struct User {\n    int64_t x = 0;\n};");
}

// A COLLATE between a minus and the literal stops SQLite's parser from folding the two together,
// so the sign left standing over it is a negation SQLite computes over 64 bits while it runs the
// statement. It takes a value out of the int32 range as readily as a folded sign does, and
// negating the int64 minimum leaves the integer range altogether:
// `sqlite3 :memory: "SELECT -(-2147483648 COLLATE BINARY), typeof(-(-9223372036854775808 COLLATE
// BINARY))"` prints 2147483648 and real.
TEST_CASE("codegen: prefix - a minus a COLLATE keeps from folding still widens the field") {
    REQUIRE(prefixFor("x > -(-2147483648 COLLATE BINARY)") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > -(-0x80000000 COLLATE BINARY)") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > -(-9223372036854775808 COLLATE BINARY)") == "struct User {\n    double x = 0.0;\n};");
    REQUIRE(prefixFor("x > -(-9223372036854775807 COLLATE BINARY)") == "struct User {\n    int64_t x = 0;\n};");
    // A COLLATE that does not stand between the sign and the literal leaves the folding alone:
    // `sqlite3 :memory: "SELECT -2147483648 COLLATE BINARY, -0xFFFFFFFF80000000 COLLATE BINARY"`
    // prints -2147483648 and 2147483648, the values the signs spell with the literals.
    REQUIRE(prefixFor("x > -2147483648 COLLATE BINARY") == "struct User {\n    int x = 0;\n};");
    REQUIRE(prefixFor("x > -0xFFFFFFFF80000000 COLLATE BINARY") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > (-(-2147483648) COLLATE BINARY)") == "struct User {\n    int64_t x = 0;\n};");
}

// Only the innermost sign folds into the literal; SQLite negates the value the outer ones stand
// on while it runs the statement, and negating the int64 minimum leaves the integer range:
// `sqlite3 :memory: "SELECT typeof(-9223372036854775808), typeof(-(-9223372036854775808)),
// typeof(-(-(-9223372036854775808)))"` prints integer, real and real.
TEST_CASE("codegen: prefix - the int64 minimum keeps its field until a second sign negates it") {
    REQUIRE(prefixFor("x > -9223372036854775808") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x > -(-9223372036854775808)") == "struct User {\n    double x = 0.0;\n};");
    REQUIRE(prefixFor("x > -(-(-9223372036854775808))") == "struct User {\n    double x = 0.0;\n};");
    REQUIRE(prefixFor("x > +9223372036854775808") == "struct User {\n    double x = 0.0;\n};");
}

// `~` complements over 64 bits, so it takes a value out of the int32 range as readily as it
// brings one back in: `sqlite3 :memory: "SELECT ~2147483648"` prints -2147483649, which an `int`
// field reads back as 2147483647.
TEST_CASE("codegen: prefix - inferred int64_t from a bitwise NOT") {
    REQUIRE(prefixFor("x = ~2147483648") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x = ~5") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x = ~0xFFFFFFFF80000000") == "struct User {\n    int64_t x = 0;\n};");
}

// Arithmetic and bit operations are computed over 64-bit integers, so the result leaves the int32
// range even where both operands sit inside it: `sqlite3 :memory: "SELECT 2147483647 + 1"` prints
// 2147483648, and `SELECT 3000000000 + 0` prints 3000000000 rather than the -1294967296 an `int`
// field reads back.
TEST_CASE("codegen: prefix - inferred int64_t from a binary arithmetic term") {
    REQUIRE(prefixFor("x = 2147483647 + 1") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x = 3000000000 + 0") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x = 5 - 1") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x = 100000 * 100000") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x = 7 / 2") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x = 7 % 2") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x = 3 & 1") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x = 3 | 1") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x = 1 << 40") == "struct User {\n    int64_t x = 0;\n};");
    REQUIRE(prefixFor("x = 1099511627776 >> 1") == "struct User {\n    int64_t x = 0;\n};");
    // A real operand makes the whole term a REAL, the way it does for a view column.
    REQUIRE(prefixFor("x = 3000000000 * 1.5") == "struct User {\n    double x = 0.0;\n};");
    REQUIRE(prefixFor("x = 1 / 2.0") == "struct User {\n    double x = 0.0;\n};");
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
                          {Option{"functional",
                              "json_extract(&Users::data, \"$.name\")", "functional style"}}}},
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
                          {Option{"functional",
                              "json_extract(&Users::data, \"$.name\")", "functional style"}}}},
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

// The COLLATE above is dropped, and the operand it stood over has to come out exactly as it would
// have come out without it. It did not: the operand lost the `c(…)` wrap a leaf operand of an
// operator gets, so `('a' COLLATE NOCASE) || 'b'` generated `"a" || "b"` — two `const char*` under
// C++'s own `||`, which is the constant `true` and not the `ab` sqlite3 3.51 answers with. Over a
// column the same operand generated `&User::a + 1`, arithmetic on a pointer to member that does
// not compile. Values checked in "runtime: an operand under a dropped COLLATE keeps its value".
TEST_CASE("codegen: a dropped COLLATE leaves the operand it stood over as it was") {
    REQUIRE(generate("SELECT ('a' COLLATE NOCASE) || 'b';") == "auto rows = storage.select(c(\"a\") || \"b\");");
    REQUIRE(generate("SELECT 'a' || ('b' COLLATE NOCASE);") == "auto rows = storage.select(c(\"a\") || \"b\");");
    REQUIRE(generate("SELECT 'a' || 'b';") == "auto rows = storage.select(c(\"a\") || \"b\");");
    REQUIRE(generate("SELECT (a COLLATE BINARY) + 1;") ==
            "auto rows = storage.select(as_optional(c(&User::a) + 1));");
    REQUIRE(generate("SELECT a + (a COLLATE BINARY);") ==
            "auto rows = storage.select(as_optional(c(&User::a) + &User::a));");
    REQUIRE(generate("SELECT a + 1;") == "auto rows = storage.select(as_optional(c(&User::a) + 1));");
    // The CAST around the whole column is the int64 widening of
    // "codegen: a bitwise result column is cast to an int64_t", which the COLLATE is inside of.
    REQUIRE(generate("SELECT ~('a' COLLATE NOCASE);") ==
            "auto rows = storage.select(cast<int64_t>(~c(\"a\")));");
}

// The grouping the generated operand needs is the grouping of the node under the COLLATE, and the
// field a compared column is given is the one the value under the COLLATE asks for.
TEST_CASE("codegen: a COLLATE'd operand keeps the grouping and the field of what it stands over") {
    REQUIRE(generate("SELECT ((1+2) COLLATE BINARY) * 3;") == "auto rows = storage.select((c(1) + 2) * 3);");
    REQUIRE(generate("SELECT (1+2) * 3;") == "auto rows = storage.select((c(1) + 2) * 3);");
    REQUIRE(generate("SELECT (1 COLLATE BINARY) + 2 * 3;") == "auto rows = storage.select(c(1) + c(2) * 3);");
    REQUIRE(generate("SELECT 2 * ((a + 1) COLLATE BINARY);") ==
            "auto rows = storage.select(as_optional(c(2) * (c(&User::a) + 1)));");
    REQUIRE(prefixFor("SELECT a = ('x' COLLATE NOCASE);") == "struct User {\n    std::string a;\n};");
}

// The bound side of a comparison is not the only thing that says what a column holds: the operand of
// BETWEEN / IN / LIKE / GLOB / MATCH and the first argument of a scalar function that reads text say
// it too, and so does an argument handed to a user-defined function. All of them are the node under
// a dropped COLLATE, so `(a COLLATE NOCASE) LIKE 'x%'` has to reach the same field `a LIKE 'x%'`
// does; it used to leave `a` an `int` the pattern is never compared against.
TEST_CASE("codegen: a column under a dropped COLLATE is still the operand of the predicate") {
    REQUIRE(prefixFor("SELECT (a COLLATE NOCASE) BETWEEN 'x' AND 'y';") ==
            "struct User {\n    std::string a;\n};");
    REQUIRE(prefixFor("SELECT a BETWEEN 'x' AND 'y';") == "struct User {\n    std::string a;\n};");
    REQUIRE(prefixFor("SELECT (a COLLATE NOCASE) IN ('x', 'y');") == "struct User {\n    std::string a;\n};");
    REQUIRE(prefixFor("SELECT (a COLLATE NOCASE) LIKE 'x%';") == "struct User {\n    std::string a;\n};");
    REQUIRE(prefixFor("SELECT (a COLLATE NOCASE) GLOB 'x*';") == "struct User {\n    std::string a;\n};");
    REQUIRE(prefixFor("SELECT (a COLLATE NOCASE) MATCH 'x';") == "struct User {\n    std::string a;\n};");
    REQUIRE(prefixFor("SELECT upper(a COLLATE NOCASE);") == "struct User {\n    std::string a;\n};");
}

// A user-defined function is generated from the arguments the call hands it, and a COLLATE over one
// of them changes neither the value nor the column it comes from: the parameter keeps the name and
// the type it has without the COLLATE. It used to fall back to the `arg0` / `int` a call over an
// expression gets.
TEST_CASE("codegen: an argument under a dropped COLLATE keeps its name and type") {
    REQUIRE(generate("SELECT myfunc(a COLLATE NOCASE) FROM users;") ==
            "struct Myfunc {\n"
            "    // TODO: implement this user-defined scalar function\n"
            "    int operator()(int a) const { return {}; }\n"
            "    static const char *name() { return \"myfunc\"; }\n"
            "};\n"
            "\n"
            "storage.create_scalar_function<Myfunc>();\n"
            "\n"
            "auto rows = storage.select(func<Myfunc>(&Users::a));");
    REQUIRE(generate("SELECT myfunc(a) FROM users;") ==
            "struct Myfunc {\n"
            "    // TODO: implement this user-defined scalar function\n"
            "    int operator()(int a) const { return {}; }\n"
            "    static const char *name() { return \"myfunc\"; }\n"
            "};\n"
            "\n"
            "storage.create_scalar_function<Myfunc>();\n"
            "\n"
            "auto rows = storage.select(func<Myfunc>(&Users::a));");
}

// SQLite's parser folds a minus into the literal it stands over through parentheses but not through
// a COLLATE: it refuses `-(0x8000000000000000)` with `hex literal too big` and answers
// `-(0x8000000000000000 COLLATE BINARY)` with 9.22337203685478e+18, which is what `0 - x` is.
// So a minus over a COLLATE keeps the subtraction form, and carries no `hex literal too big`.
TEST_CASE("codegen: a minus over a COLLATE is a negation, not a folded sign") {
    REQUIRE(generate("SELECT -(5 COLLATE BINARY);") == "auto rows = storage.select((c(0) - c(5)));");
    REQUIRE(generate("SELECT -((5) COLLATE BINARY) + 1;") == "auto rows = storage.select((c(0) - c(5)) + 1);");
    REQUIRE(generate("SELECT -(a COLLATE BINARY);") ==
            "auto rows = storage.select(as_optional((c(0) - c(&User::a))));");
    REQUIRE(generate("SELECT -(-3 COLLATE BINARY);") == "auto rows = storage.select(-(-3));");
    auto tooBig = generateFull("SELECT -(0x8000000000000000 COLLATE BINARY);");
    REQUIRE(tooBig.code == "auto rows = storage.select((c(0) - c(static_cast<int64_t>(0x8000000000000000))));");
    // The negation is read back through sqlite_orm's `double`, and the magnitude the bound carries
    // for the int64 minimum is past 2^53, so the column is reported as
    // "codegen: an arithmetic result column reports the double it is read back through" describes.
    REQUIRE(tooBig.warnings ==
            std::vector<CodegenWarning>{
                "COLLATE BINARY on expressions is not directly supported in sqlite_orm codegen",
                CodegenWarning{"result column computed with `-` is read back through a double: sqlite_orm "
                               "types `+`, `-`, `*`, `/` and `%` as `double`, so an INTEGER result past "
                               "2^53 comes back rounded (9223372036854775807 reads back as "
                               "9223372036854775808)",
                               SourceLocation{1, 8}, 1}});
}

// A predicate under the COLLATE is still a predicate sqlite_orm serializes without parentheses, so
// the negation cannot become `0 - is_null(a)`: SQLite would read that as `(0 - a) IS NULL`, which
// answers 1 over a NULL row where `-((a IS NULL) COLLATE BINARY)` answers -1.
TEST_CASE("codegen: a minus over a predicate under a COLLATE warns as the bare predicate does") {
    auto result = generateFull("SELECT -((a IS NULL) COLLATE BINARY);");
    REQUIRE(result.code == "auto rows = storage.select(-(is_null(&User::a)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                "COLLATE BINARY on expressions is not directly supported in sqlite_orm codegen",
                CodegenWarning{"unary minus over a predicate (IS NULL) has no working sqlite_orm "
                               "form; the generated negation does not reproduce what SQLite "
                               "computes and does not compile",
                               SourceLocation{1, 8}, 1}});
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
