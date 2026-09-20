#include "codegen_tests_common.hpp"

#include <sqlite2orm/parser.h>
#include <sqlite2orm/tokenizer.h>

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

    // The hint attached to every AND and OR operand the generator hands over `c()`-wrapped;
    // asserted on its own in "codegen: an AND or an OR with a quoted operand carries its comment".
    const std::string kAndOrQuotedOperandComment =
        "An operand of an AND or an OR that sqlite_orm does not recognize is generated as "
        "`c(operand)`: `or_()` and `and_()` assert that both arguments are bindable values or "
        "operands sqlite_orm knows, and `operator&&` is declared only where one of the operands "
        "is a condition or an operator argument. A MATCH, a CURRENT_DATE / CURRENT_TIME / "
        "CURRENT_TIMESTAMP, a window call and a FILTERed aggregate are none of those, and a pair "
        "like `a + 1 AND a + 2` is neither, so `b MATCH 'x' OR b MATCH 'y'` and `a + 1 AND a + 2` "
        "would not compile at all. `c()` hands the expression over as it stands: `or_()`, `and_()` "
        "and `operator&&` all unwrap the `quoted_expression_t` back to the expression it holds, so "
        "the condition built — and the SQL it serializes to — is the one the bare operand would "
        "have built.";

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

    // The hint attached to every generated view; asserted on its own in
    // "codegen: CREATE VIEW - reflection comment attached".
    const std::string kViewReflectionComment =
        "SQL views map to sqlite_orm's reflection-based `make_view<T>()`: the struct's fields and the "
        "`[[= \"…\"_orm_name]]` annotation require a C++26 compiler with reflection (P2996/P3394). "
        "sqlite_orm detects support automatically (SQLITE_ORM_REFLECTION_SUPPORTED enables "
        "SQLITE_ORM_WITH_VIEW); on older compilers this code does not compile.";

    // The report every `->` carries, whatever it stands in; asserted with its anchor in
    // "codegen: JSON -> operator".
    const std::string kJsonTextArrowWarning =
        "`->` is generated as a JSON_EXTRACT call, which answers the SQL value at the path where "
        "`->` answers the JSON text of that value: a string comes back unquoted, and `true`, "
        "`false` and `null` come back as 1, 0 and NULL. sqlite_orm has no form for the operator "
        "itself";

    // The report a result column read back through a single-path JSON_EXTRACT carries; asserted
    // with its anchor in "codegen: JSON -> operator" and the cases around it.
    const std::string kJsonExtractResultTypeWarning =
        "result column taken from JSON_EXTRACT over one path comes back as text: SQLite answers "
        "the value at the path, of whatever storage class the JSON holds, and sqlite_orm deduces "
        "no result type for the call, so it is generated as `json_extract<std::string>(…)` — a "
        "JSON number comes back as its digits. Spell the result type the value at the path has "
        "where it is known";

    // The report a JSON arrow carries whose path operand cannot be expanded here; asserted with
    // its anchor in "codegen: a JSON arrow whose path is no literal is reported at the operator".
    const std::string kJsonArrowPathNotExpandedWarning =
        "the path operand of a JSON arrow is not a text or an integer literal, so the `$` path "
        "SQLite expands it into cannot be spelled out here: the JSON_EXTRACT call the operator is "
        "generated as takes the operand as written, and SQLite refuses a path that does not start "
        "with `$` with `bad JSON path`";

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

// A literal past the range of a double is an Inf to SQLite — `SELECT 9e999` answers Inf — while
// [lex.fcon] makes the same spelling ill-formed in C++: g++ 13.3 warns `floating constant exceeds
// range of 'double'` under `-Woverflow`, which a consumer building the generated code with
// `-Werror` reads as an error. `std::numeric_limits<double>::infinity()` is that value spelled in
// a way C++ accepts. The largest finite double keeps its own spelling. Checked against sqlite3 3.51.
TEST_CASE("codegen: real literal past the double range becomes an infinity") {
    REQUIRE(generate("9e999") == "std::numeric_limits<double>::infinity()");
    REQUIRE(generate("1e309") == "std::numeric_limits<double>::infinity()");
    REQUIRE(generate("1.0e400") == "std::numeric_limits<double>::infinity()");
    REQUIRE(generate("1.7976931348623159e308") == "std::numeric_limits<double>::infinity()");
    REQUIRE(generate("9e99_9") == "std::numeric_limits<double>::infinity()");
    REQUIRE(generate("1.7976931348623157e308") == "1.7976931348623157e308");
    REQUIRE(generate("1e308") == "1e308");
    REQUIRE(generate("SELECT 9e999;") == "auto rows = storage.select(std::numeric_limits<double>::infinity());");
    // The sign stands on the value, and a minus in front of an infinity is one C++ spells too.
    REQUIRE(generate("SELECT -9e999;") == "auto rows = storage.select(-std::numeric_limits<double>::infinity());");
}

// A decimal integer far enough past the int64 range runs out of double as well: a 1 followed by
// 309 zeros is an Inf to SQLite, so the `.0` that carries the rest of the range cannot carry this
// one. One zero less and the value is 1.0e+308, which a double holds.
TEST_CASE("codegen: integer literal past the double range becomes an infinity") {
    REQUIRE(generate(std::string("1") + std::string(309, '0')) == "std::numeric_limits<double>::infinity()");
    REQUIRE(generate(std::string(310, '9')) == "std::numeric_limits<double>::infinity()");
    REQUIRE(generate(std::string("1") + std::string(308, '0')) == "1" + std::string(308, '0') + ".0");
}

// The other end of the range warns the same way: g++ 13.3 answers `1e-400` with `floating constant
// truncated to zero`. The zero it is truncated to is the value SQLite reads as well — `SELECT
// 1e-400` answers 0.0 — so the generated literal is that zero. A subnormal is a value a double
// still holds, and a literal written as a zero was never rounded to one, so both keep their
// spelling. What the value is, is read the way a compiler reads it: `2.5e-324` rounds up to the
// smallest subnormal rather than down to a zero, and g++ takes that spelling without a word.
// Checked against sqlite3 3.51.
TEST_CASE("codegen: real literal below the double range becomes a zero") {
    REQUIRE(generate("1e-400") == "0.0");
    REQUIRE(generate("1e-500") == "0.0");
    REQUIRE(generate("1e-330") == "0.0");
    REQUIRE(generate("1e-40_0") == "0.0");
    REQUIRE(generate("4.9e-324") == "4.9e-324");
    REQUIRE(generate("1e-320") == "1e-320");
    REQUIRE(generate("0.0e999") == "0.0e999");
    REQUIRE(generate("0.0") == "0.0");
    REQUIRE(generate("0.000") == "0.000");
    REQUIRE(generate("SELECT 1e-400;") == "auto rows = storage.select(0.0);");
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
        REQUIRE(
            result ==
            CodeGenResult{
                "c(&User::a) || &User::b || &User::c",
                {
                    columnRefStyleDp(1, "&User::a"),
                    columnRefStyleDp(2, "&User::b"),
                    DecisionPoint{
                        3,
                        "expr_style",
                        "operator_wrap_left",
                        "c(&User::a) || &User::b",
                        {
                            Option{"operator_wrap_left", "c(&User::a) || &User::b", "wrap left operand"},
                            Option{"operator_wrap_right", "&User::a || c(&User::b)", "wrap right operand"},
                            Option{"functional", "conc(&User::a, &User::b)", "functional style"},
                            Option{"operator_wrap_both", "c(&User::a) || c(&User::b)", "wrap both operands", true},
                        }},
                    columnRefStyleDp(4, "&User::c"),
                    DecisionPoint{
                        5,
                        "expr_style",
                        "operator_wrap_left",
                        "c(&User::a) || &User::b || &User::c",
                        {
                            Option{"operator_wrap_left", "c(&User::a) || &User::b || &User::c", "wrap left operand"},
                            Option{"operator_wrap_right",
                                   "c(&User::a) || &User::b || c(&User::c)",
                                   "wrap right operand"},
                            Option{"functional", "conc(c(&User::a) || &User::b, &User::c)", "functional style"},
                            Option{"operator_wrap_both",
                                   "c(&User::a) || &User::b || c(&User::c)",
                                   "wrap both operands",
                                   true},
                        }},
                }});
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
        REQUIRE(result ==
                CodeGenResult{"(c(0) - c(&User::a))",
                              {
                                  columnRefStyleDp(1, "&User::a"),
                                  DecisionPoint{2,
                                                "expr_style",
                                                "operator",
                                                "(c(0) - c(&User::a))",
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
// "codegen: a bitwise result column is cast to an int64_t" on top of that, and the BETWEEN column
// the nullable widening of "codegen: a result column typed by a predicate, a CAST or a function
// call is widened".
TEST_CASE("codegen: negative literal as an operand") {
    REQUIRE(generate("SELECT -2;") == "auto rows = storage.select(-2);");
    REQUIRE(generate("SELECT 100 / -2;") == "auto rows = storage.select(as_optional(c(100) / -2));");
    REQUIRE(generate("SELECT -2 + 3;") == "auto rows = storage.select(c(-2) + 3);");
    REQUIRE(generate("SELECT a * -2;") == "auto rows = storage.select(as_optional(c(&User::a) * -2));");
    REQUIRE(generate("SELECT ~ -2;") == "auto rows = storage.select(cast<int64_t>(~c(-2)));");
    REQUIRE(generate("SELECT a BETWEEN -1 AND 5;") ==
            "auto rows = storage.select(as_optional(between(&User::a, -1, 5)));");
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
                 SourceLocation{1, 1},
                 1}});
    // Every neighbouring value stays folded: sqlite3 3.51 gives 9223372036854775807 and 1 for these.
    REQUIRE(generateFull("-0x8000000000000001") == CodeGenResult{"-static_cast<int64_t>(0x8000000000000001)", {}});
    REQUIRE(generateFull("-0xFFFFFFFFFFFFFFFF") == CodeGenResult{"-static_cast<int64_t>(0xFFFFFFFFFFFFFFFF)", {}});
    // The literal on its own is -9223372036854775808 in SQLite and needs no guard.
    REQUIRE(generateFull("0x8000000000000000") == CodeGenResult{"static_cast<int64_t>(0x8000000000000000)", {}});
    // The last of the three anchors whose length is written inline rather than measured from source
    // text (see `underlineLengthOf`): the minus is one ASCII character however the rest of the SQL
    // is written, while the column it is anchored at counts the characters before it, not bytes.
    auto shifted = generateFull("/* «üü» */ -0x8000000000000000");
    REQUIRE(shifted.code == "(c(0) - c(static_cast<int64_t>(0x8000000000000000)))");
    REQUIRE(shifted.warnings == std::vector<CodegenWarning>{{tooBig, SourceLocation{1, 12}, 1}});
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
        REQUIRE(result == CodeGenResult{"~c(5)",
                                        {DecisionPoint{1,
                                                       "expr_style",
                                                       "operator",
                                                       "~c(5)",
                                                       {Option{"operator", "~c(5)", "operator style"},
                                                        Option{"functional", "bitwise_not(5)", "functional style"}}}}});
    }
    SECTION("~a") {
        auto result = generateFull("~a");
        REQUIRE(result ==
                CodeGenResult{"~c(&User::a)",
                              {
                                  columnRefStyleDp(1, "&User::a"),
                                  DecisionPoint{2,
                                                "expr_style",
                                                "operator",
                                                "~c(&User::a)",
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
        expectedDps.push_back(DecisionPoint{
            5,
            "expr_style",
            "operator_wrap_left",
            "c(&User::a) == 1 and c(&User::b) == 2",
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
        REQUIRE(generateFull("a OR b") ==
                CodeGenResult{"or_(&User::a, &User::b)",
                              {
                                  columnRefStyleDp(1, "&User::a"),
                                  columnRefStyleDp(2, "&User::b"),
                                  DecisionPoint{3,
                                                "expr_style",
                                                "functional",
                                                "or_(&User::a, &User::b)",
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
        expectedDps.push_back(DecisionPoint{
            5,
            "expr_style",
            "operator_wrap_left",
            "c(&User::a) == 1 or c(&User::b) == 2",
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
        // `match_t` is the one predicate sqlite_orm does not class as a condition, and it is not
        // an operand `or_()` accepts either — it derives from nothing at all. The call hands it
        // over `c()`-wrapped, which is what makes it one; the OR built is the same either way.
        REQUIRE(generate("a MATCH 'x' OR b") == R"(or_(c(match(&User::a, "x")), &User::b))");
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
        REQUIRE(result ==
                CodeGenResult{
                    "not column<User>(&User::a)",
                    {
                        DecisionPoint{1,
                                      "expr_style",
                                      "operator",
                                      "not column<User>(&User::a)",
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
        REQUIRE(result ==
                CodeGenResult{"not (c(0) + 1)",
                              {
                                  DecisionPoint{1,
                                                "expr_style",
                                                "operator",
                                                "not (c(0) + 1)",
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
        REQUIRE(result ==
                CodeGenResult{
                    "not (c(0) - c(&User::a))",
                    {
                        columnRefStyleDp(1, "&User::a"),
                        DecisionPoint{2,
                                      "expr_style",
                                      "operator",
                                      "(c(0) - c(&User::a))",
                                      {Option{"operator", "(c(0) - c(&User::a))", "operator style"},
                                       Option{"functional", "sub(0, &User::a)", "functional style"}}},
                        DecisionPoint{3,
                                      "expr_style",
                                      "operator",
                                      "not (c(0) - c(&User::a))",
                                      {
                                          Option{"operator", "not (c(0) - c(&User::a))", "operator style"},
                                          Option{"operator_excl", "!(c(0) - c(&User::a))", "use ! instead of not"},
                                      }},
                    },
                    {},
                    {},
                    {kZeroMinusComment}});
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
    REQUIRE(result.code == "struct AlAlias : sqlite_orm::alias_tag {\n"
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
    REQUIRE(generate("SELECT NOT current_timestamp;") == "auto rows = storage.select(not c(current_timestamp()));");
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
    REQUIRE(generate("SELECT NOT (a IS NULL) FROM users;") == "auto rows = storage.select(not (is_null(&Users::a)));");
    REQUIRE(generate("SELECT NOT (a NOT IN (1, 2)) FROM users;") ==
            "auto rows = storage.select(as_optional(not (not_in(&Users::a, {1, 2}))));");
}

TEST_CASE("codegen: a NOT over a NOT carries its comment") {
    auto result = generateFull("SELECT NOT NOT a FROM users;");
    REQUIRE(result.comments == std::vector<std::string>{kNotColumnPointerComment, kNegatedConditionCastComment});
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
    REQUIRE(result.warnings == std::vector<CodegenWarning>{
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
    REQUIRE(result ==
            CodeGenResult{"(c(0) - (c(0) - c(&User::a)))",
                          {
                              columnRefStyleDp(1, "&User::a"),
                              DecisionPoint{2,
                                            "expr_style",
                                            "operator",
                                            "(c(0) - c(&User::a))",
                                            {Option{"operator", "(c(0) - c(&User::a))", "operator style"},
                                             Option{"functional", "sub(0, &User::a)", "functional style"}}},
                              DecisionPoint{3,
                                            "expr_style",
                                            "operator",
                                            "(c(0) - (c(0) - c(&User::a)))",
                                            {Option{"operator", "(c(0) - (c(0) - c(&User::a)))", "operator style"},
                                             Option{"functional", "sub(0, (c(0) - c(&User::a)))", "functional style"}}},
                          },
                          {},
                          {},
                          {kZeroMinusComment}});
}

// sqlite_orm has no working unary minus, so a negation of anything but a numeric constant is
// generated as a subtraction from zero: SQLite computes `0 - x` exactly like `-x` for every operand
// kind, value and typeof alike. Values checked against sqlite3 3.51 in
// "runtime: a negation over a general operand keeps its value".
TEST_CASE("codegen: unary minus over a general operand becomes a subtraction from zero") {
    REQUIRE(generate("SELECT -(2+3);") == "auto rows = storage.select((c(0) - (c(2) + 3)));");
    REQUIRE(generate("SELECT - ~2;") == "auto rows = storage.select((c(0) - (~c(2))));");
    // `length('abc')` is spelled out in the SQL and SQLite propagates a NULL argument, so the
    // subtraction over it has no NULL to report and keeps the type sqlite_orm gives it.
    REQUIRE(generate("SELECT -length('abc');") == "auto rows = storage.select((c(0) - (length(\"abc\"))));");
    REQUIRE(generate("SELECT -x'31';") == "auto rows = storage.select((c(0) - c(std::vector<char>{'\\x31'})));");
    REQUIRE(generate("SELECT -(SELECT 1);") == "auto rows = storage.select(as_optional((c(0) - (select(1)))));");
    REQUIRE(generate("SELECT -a FROM users;") == "auto rows = storage.select(as_optional((c(0) - c(&Users::a))));");
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
    REQUIRE(generateWithPolicy("SELECT -(2+3);", policy).code == "auto rows = storage.select(sub(0, add(2, 3)));");
}

// A predicate is the one operand the subtraction cannot carry: sqlite_orm serializes
// `a BETWEEN 1 AND 9` without parentheses and SQLite binds it looser than a binary `-`, so
// `0 - a BETWEEN 1 AND 9` would read as `(0 - a) BETWEEN 1 AND 9`. The unary form stays and warns.
TEST_CASE("codegen: unary minus over a predicate warns instead") {
    auto check = [](std::string_view sql,
                    const std::string& code,
                    const std::string& predicate,
                    SourceLocation location = SourceLocation{1, 8}) {
        auto result = generateFull(sql);
        REQUIRE(result.code == code);
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"unary minus over a predicate (" + predicate +
                         ") has no working sqlite_orm form; the generated negation does not reproduce "
                         "what SQLite computes and does not compile",
                     location,
                     1}});
    };
    check("SELECT -(a IN (1,2)) FROM users;", "auto rows = storage.select(-(in(&Users::a, {1, 2})));", "IN");
    check("SELECT -(a BETWEEN 1 AND 9) FROM users;",
          "auto rows = storage.select(-(between(&Users::a, 1, 9)));",
          "BETWEEN");
    check("SELECT -(a LIKE 'x') FROM users;", "auto rows = storage.select(-(like(&Users::a, \"x\")));", "LIKE");
    check("SELECT -(a GLOB 'x') FROM users;", "auto rows = storage.select(-(glob(&Users::a, \"x\")));", "GLOB");
    check("SELECT -(a MATCH 'x') FROM users;", "auto rows = storage.select(-(match(&Users::a, \"x\")));", "MATCH");
    check("SELECT -(a IS NULL) FROM users;", "auto rows = storage.select(-(is_null(&Users::a)));", "IS NULL");
    check("SELECT -(a NOTNULL) FROM users;", "auto rows = storage.select(-(is_not_null(&Users::a)));", "IS NOT NULL");
    check("SELECT - NOT a FROM users;", "auto rows = storage.select(-(not column<Users>(&Users::a)));", "NOT");
    // The length here is written inline rather than measured from source text (see
    // `underlineLengthOf`), which holds because the minus is a single ASCII character. The column
    // it is anchored at is counted in characters, so a comment that is not ASCII-only moves it by
    // the characters it is written with, not by its bytes, and the inline 1 still covers the minus.
    check("/* «üü» */ SELECT -(a IN (1,2)) FROM users;",
          "auto rows = storage.select(-(in(&Users::a, {1, 2})));",
          "IN",
          SourceLocation{1, 19});
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
    REQUIRE(generate("a -> (b IS NULL)") == "json_extract<std::string>(&User::a, is_null(&User::b))");
}

// `||`, `->` and `->>` share one left-associative level in SQLite, so the concatenation is the
// operand the arrow reads from rather than the other way round. sqlite3 3.51 answers 5 to
// `SELECT '{"x' || '":5}' ->> 'x'` and rejects `SELECT '":5}' ->> 'x'` as malformed JSON.
TEST_CASE("codegen: the JSON arrows read a whole concatenation") {
    REQUIRE(generate("a || b ->> 'x'") == "json_extract<std::string>(c(&User::a) || &User::b, \"$.x\")");
    REQUIRE(generate("a || b -> 'x'") == "json_extract<std::string>(c(&User::a) || &User::b, \"$.x\")");
    REQUIRE(generate("a ->> 'x' || b") == "json_extract<std::string>(&User::a, \"$.x\") || &User::b");
    REQUIRE(generate("a * b ->> 'x'") == "c(&User::a) * json_extract<std::string>(&User::b, \"$.x\")");
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
    REQUIRE(generateFull("SELECT 1 - (a IS NULL);").comments == std::vector<std::string>{kPredicateCastComment});
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
        REQUIRE(generate("(a OR b) IN (0, 1)") == "in(cast<int64_t>(or_(&User::a, &User::b)), {0, 1})");
        REQUIRE(generate("(a OR b) NOT IN (0, 1)") == "not_in(cast<int64_t>(or_(&User::a, &User::b)), {0, 1})");
        REQUIRE(generate("(a OR b) BETWEEN 0 AND 1") == "between(cast<int64_t>(or_(&User::a, &User::b)), 0, 1)");
        // `between(A, T, T)` deduces one type for both bounds, so these two do not compile — nor
        // did they on master, where the bound was a `conc_t` — but the SQL a bound serializes into
        // runs into the `AND` of the BETWEEN itself, so the CAST belongs there all the same.
        REQUIRE(generate("1 BETWEEN (1 OR 0) AND 3") == "between(1, cast<int64_t>(or_(1, 0)), 3)");
        REQUIRE(generate("1 BETWEEN 0 AND (1 OR 0)") == "between(1, 0, cast<int64_t>(or_(1, 0)))");
        REQUIRE(generate("(a OR b) LIKE 'x'") == R"(like(cast<int64_t>(or_(&User::a, &User::b)), "x"))");
        REQUIRE(generate("'x' LIKE (a OR b)") == R"(like("x", cast<int64_t>(or_(&User::a, &User::b))))");
        REQUIRE(generate("(a OR b) NOT LIKE 'x'") == R"(!like(cast<int64_t>(or_(&User::a, &User::b)), "x"))");
        REQUIRE(generate("(a OR b) GLOB 'x'") == R"(glob(cast<int64_t>(or_(&User::a, &User::b)), "x"))");
        REQUIRE(generate("a MATCH (b OR c)") == "match(&User::a, cast<int64_t>(or_(&User::b, &User::c)))");
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

// sqlite_orm reads the operand types to decide what an AND or an OR may build at all. `operator&&`
// is declared only where one of the operands is a condition or an operator argument, so a pair that
// is neither — `a + 1 AND a + 2` — finds no overload; `or_()` and `and_()` assert that both of
// their arguments are operands sqlite_orm recognizes, and a MATCH, a CURRENT_DATE / CURRENT_TIME /
// CURRENT_TIMESTAMP, a window call and a FILTERed aggregate are none of them. The operand that has
// to carry the pair is handed over `c()`-wrapped: `or_()`, `and_()` and `operator&&` all unwrap the
// `quoted_expression_t` right back to the expression it holds, so the condition built is the one
// that was written. What the generated code answers is pinned in "runtime: an AND or an OR over
// operands sqlite_orm does not recognize returns the values SQLite computes".
TEST_CASE("codegen: an AND or an OR quotes an operand sqlite_orm does not recognize") {
    SECTION("a MATCH, in both of the spellings that generate `match_t`") {
        REQUIRE(generate("a MATCH 'x' OR b MATCH 'y'") == R"(or_(c(match(&User::a, "x")), c(match(&User::b, "y"))))");
        REQUIRE(generate("match(a, 'x') OR match(b, 'y')") ==
                R"(or_(c(match(&User::a, "x")), c(match(&User::b, "y"))))");
        REQUIRE(generate("a MATCH 'x' AND b MATCH 'y'") == R"(c(match(&User::a, "x")) and match(&User::b, "y"))");
    }
    SECTION("a CURRENT_DATE, CURRENT_TIME or CURRENT_TIMESTAMP") {
        REQUIRE(generate("CURRENT_TIMESTAMP OR a") == "or_(c(current_timestamp()), &User::a)");
        REQUIRE(generate("a OR CURRENT_DATE") == "or_(&User::a, c(current_date()))");
        // The AND takes the quote the wrapping variant puts on a leaf operand anyway, and one
        // quoted operand is all `operator&&` asks for.
        REQUIRE(generate("CURRENT_TIME AND CURRENT_DATE") == "c(current_time()) and current_date()");
    }
    SECTION("a window call and a FILTERed aggregate") {
        REQUIRE(generate("(row_number() OVER ()) OR a") == "or_(c(row_number().over()), &User::a)");
        REQUIRE(generate("(count(a) FILTER (WHERE a > 0)) AND (a + 1)") ==
                "c(count(&User::a).filter(where(c(&User::a) > 0))) and c(&User::a) + 1");
    }
    SECTION("a pair `operator&&` is declared for neither way round") {
        REQUIRE(generate("a + 1 AND a + 2") == "c(c(&User::a) + 1) and c(&User::a) + 2");
        REQUIRE(generate("(a || 'x') AND (b || 'y')") == R"(c(c(&User::a) || "x") and (c(&User::b) || "y"))");
        REQUIRE(generate("~a AND -a") == "c(~c(&User::a)) and (c(0) - c(&User::a))");
        REQUIRE(generate("(SELECT 1) AND a") == "c(select(1)) and &User::a");
    }
    SECTION("an operand beside a condition or an operator argument is left as it was written") {
        REQUIRE(generate("a MATCH 'x' OR b = 1") == R"(match(&User::a, "x") or c(&User::b) == 1)");
        REQUIRE(generate("b = 1 AND a MATCH 'x'") == R"(c(&User::b) == 1 and match(&User::a, "x"))");
        REQUIRE(generate("CURRENT_DATE AND abs(a)") == "c(current_date()) and abs(&User::a)");
        REQUIRE(generate("abs(a) AND a + 1") == "abs(&User::a) and c(&User::a) + 1");
        REQUIRE(generate("CAST(a AS INTEGER) AND a + 1") == "cast<int64_t>(&User::a) and c(&User::a) + 1");
        REQUIRE(generate("a AND b") == "c(&User::a) and &User::b");
        REQUIRE(generate("a = 1 AND b = 2") == "c(&User::a) == 1 and c(&User::b) == 2");
        REQUIRE(generate("a OR b") == "or_(&User::a, &User::b)");
    }
    SECTION("every variant the decision point offers is quoted the way its own spelling needs") {
        const std::vector<Option> options = generateFull("a + 1 AND a + 2").decisionPoints.back().options;
        REQUIRE(options.size() == 4);
        REQUIRE(options[0].code == "c(c(&User::a) + 1) and c(&User::a) + 2");
        REQUIRE(options[1].code == "c(&User::a) + 1 and c(c(&User::a) + 2)");
        REQUIRE(options[2].code == "and_(c(&User::a) + 1, c(&User::a) + 2)");
        REQUIRE(options[3].code == "c(c(&User::a) + 1) and c(c(&User::a) + 2)");
        // The call is the only spelling offered for an OR over operands that are not conditions,
        // and there both arguments carry the quote their own form needs.
        const std::vector<Option> matchOptions =
            generateFull("a MATCH 'x' OR b MATCH 'y'").decisionPoints.back().options;
        REQUIRE(matchOptions.size() == 1);
        REQUIRE(matchOptions[0].code == R"(or_(c(match(&User::a, "x")), c(match(&User::b, "y"))))");
        // The operator spelling needs no quote where a condition stands beside the MATCH, and the
        // call spelling, offered beside it, needs one all the same.
        const std::vector<Option> mixedOptions = generateFull("a MATCH 'x' OR b = 1").decisionPoints.back().options;
        REQUIRE(mixedOptions.size() == 4);
        REQUIRE(mixedOptions[0].code == R"(match(&User::a, "x") or c(&User::b) == 1)");
        REQUIRE(mixedOptions[2].code == R"(or_(c(match(&User::a, "x")), c(&User::b) == 1))");
    }
}

TEST_CASE("codegen: an AND or an OR with a quoted operand carries its comment") {
    REQUIRE(generateFull("SELECT a MATCH 'x' OR b MATCH 'y';").comments ==
            std::vector<std::string>{kOrTokenCallSpellingComment, kAndOrQuotedOperandComment});
    REQUIRE(generateFull("SELECT a + 1 AND a + 2;").comments == std::vector<std::string>{kAndOrQuotedOperandComment});
    // The quote a wrapping variant puts on a leaf operand is the spelling of every AND and OR over
    // one, so it speaks for itself; only an operand whose own form forces the quote carries the
    // hint.
    REQUIRE(generateFull("SELECT a AND b;").comments.empty());
    REQUIRE(generateFull("SELECT a MATCH 'x' OR b = 1;").comments.empty());
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

// sqlite_orm's `between(A, T, T)` deduces one `T` from both bounds, and C++ types an integer
// constant by its magnitude, so `between(&User::a, 1, 3000000000)` is an `int` next to a 64-bit
// constant and does not compile — while sqlite3 3.45.1 and 3.51 both take the SQL and answer 1.
// The cast gives the two bounds one type; the values it carries are pinned in
// "runtime: BETWEEN bounds of two integer widths read back as SQLite computes them".
TEST_CASE("codegen: BETWEEN bounds of two integer widths are widened to one") {
    // The cast goes on both bounds rather than on the narrower one: a 64-bit constant is given
    // the first of `long` and `long long` it fits in, which is the `long` that is not the
    // `long long` an `int64_t` is on macOS, so casting only the `int` leaves two types behind.
    REQUIRE(generate("a BETWEEN 1 AND 3000000000") ==
            "between(&User::a, static_cast<int64_t>(1), static_cast<int64_t>(3000000000))");
    REQUIRE(generate("a BETWEEN 3000000000 AND 1") ==
            "between(&User::a, static_cast<int64_t>(3000000000), static_cast<int64_t>(1))");
    REQUIRE(generate("a BETWEEN -1 AND 3000000000") ==
            "between(&User::a, static_cast<int64_t>(-1), static_cast<int64_t>(3000000000))");
    REQUIRE(generate("a NOT BETWEEN 1 AND 3000000000") ==
            "!between(&User::a, static_cast<int64_t>(1), static_cast<int64_t>(3000000000))");
    // TRUE is the integer 1 for SQLite, so a `bool` bound widens with the rest of them.
    REQUIRE(generate("a BETWEEN 1 AND TRUE") ==
            "between(&User::a, static_cast<int64_t>(1), static_cast<int64_t>(true))");
    // A hex literal C++ would make unsigned already carries the cast that types it, and the other
    // bound joins it there rather than taking a second cast of its own.
    REQUIRE(generate("a BETWEEN 1 AND 0xFFFFFFFF") ==
            "between(&User::a, static_cast<int64_t>(1), static_cast<int64_t>(0xFFFFFFFF))");
    // A 64-bit constant and a cast hex literal are 64 bits wide both, and still two types: the
    // `long` of the one is not the `int64_t` of the other wherever `int64_t` is a `long long`.
    REQUIRE(generate("a BETWEEN 3000000000 AND 0xFFFFFFFF") ==
            "between(&User::a, static_cast<int64_t>(3000000000), static_cast<int64_t>(0xFFFFFFFF))");
    // A hex literal that stays signed is typed by its digits rather than by a cast, and nine
    // significant digits is where it stops being an `int`, so both sides of that line are pinned
    // here: `0x100000000` next to an `int` is two types and takes the cast…
    REQUIRE(generate("a BETWEEN 1 AND 0x100000000") ==
            "between(&User::a, static_cast<int64_t>(1), static_cast<int64_t>(0x100000000))");
    // …while next to a 64-bit decimal constant it is the same type already — both are the first
    // of `long` and `long long` that holds them — so that pair is left alone.
    REQUIRE(generate("a BETWEEN 0x100000000 AND 3000000000") == "between(&User::a, 0x100000000, 3000000000)");
    // Bounds that are one type already are left as written, whichever type that is.
    REQUIRE(generate("a BETWEEN 1 AND 10") == "between(&User::a, 1, 10)");
    REQUIRE(generate("a BETWEEN 1 AND 0x7FFFFFFF") == "between(&User::a, 1, 0x7FFFFFFF)");
    REQUIRE(generate("a BETWEEN 3000000000 AND 4000000000") == "between(&User::a, 3000000000, 4000000000)");
    REQUIRE(generate("a BETWEEN 0xFFFFFFFF AND 0xFFFFFFFF") ==
            "between(&User::a, static_cast<int64_t>(0xFFFFFFFF), static_cast<int64_t>(0xFFFFFFFF))");
}

// Two bounds with no C++ type to widen to have no working form at all: `between(A, T, T)` takes
// one type, and an integer next to a text, a real, a NULL, a blob, a column pointer or an
// expression node is two. SQLite takes every one of these (`SELECT 1 BETWEEN 1 AND 'x'` answers 1
// on 3.45.1 and on 3.51), so the statement is generated and the warning says what the code does.
TEST_CASE("codegen: BETWEEN bounds with no common C++ type warn") {
    const auto boundsWarning = [](const std::string& low, const std::string& high, size_t length) {
        return std::vector<CodegenWarning>{
            {"sqlite_orm's between(A, T, T) deduces one C++ type from both bounds of a BETWEEN, and " + low +
                 " next to " + high + " is not one type, so the generated code does not compile",
             SourceLocation{1, 1},
             length}};
    };
    auto result = generateFull("a BETWEEN 1 AND 'x'");
    REQUIRE(result.code == "between(&User::a, 1, \"x\")");
    REQUIRE(result.warnings == boundsWarning("an `int`", "a `const char*`", 19));

    result = generateFull("a BETWEEN 1 AND 2.5");
    REQUIRE(result.code == "between(&User::a, 1, 2.5)");
    REQUIRE(result.warnings == boundsWarning("an `int`", "a `double`", 19));

    result = generateFull("a BETWEEN NULL AND 1");
    REQUIRE(result.code == "between(&User::a, nullptr, 1)");
    REQUIRE(result.warnings == boundsWarning("a `std::nullptr_t`", "an `int`", 20));

    result = generateFull("a BETWEEN x'41' AND 1");
    REQUIRE(result.code == "between(&User::a, std::vector<char>{'\\x41'}, 1)");
    REQUIRE(result.warnings == boundsWarning("a `std::vector<char>`", "an `int`", 21));

    result = generateFull("a BETWEEN b AND 3");
    REQUIRE(result.code == "between(&User::a, &User::b, 3)");
    REQUIRE(result.warnings == boundsWarning("a pointer to a column", "an `int`", 17));

    result = generateFull("a BETWEEN 1 AND abs(b)");
    REQUIRE(result.code == "between(&User::a, 1, abs(&User::b))");
    REQUIRE(result.warnings == boundsWarning("an `int`", "a sqlite_orm expression", 22));

    // A pointer to a member is none of the node types sqlite_orm builds an expression out of,
    // whichever column and whichever expression the two bounds stand for.
    result = generateFull("a BETWEEN b AND abs(b)");
    REQUIRE(result.code == "between(&User::a, &User::b, abs(&User::b))");
    REQUIRE(result.warnings == boundsWarning("a pointer to a column", "a sqlite_orm expression", 22));

    // A decimal literal past the int64 range is a REAL for SQLite and a `double` here, so it is
    // the integer bound beside it that has nothing to meet.
    result = generateFull("a BETWEEN 1 AND 99999999999999999999");
    REQUIRE(result.code == "between(&User::a, 1, 99999999999999999999.0)");
    REQUIRE(result.warnings == boundsWarning("an `int`", "a `double`", 36));

    // The two 64-bit integer types are named apart, because they are two types: a constant past
    // the `int` range is given the first of `long` and `long long` it fits in, while the cast a
    // hex literal carries names `int64_t` itself.
    result = generateFull("a BETWEEN 3000000000 AND 2.5");
    REQUIRE(result.code == "between(&User::a, 3000000000, 2.5)");
    REQUIRE(result.warnings == boundsWarning("a 64-bit integer constant", "a `double`", 28));

    result = generateFull("a BETWEEN 0xFFFFFFFF AND 'x'");
    REQUIRE(result.code == "between(&User::a, static_cast<int64_t>(0xFFFFFFFF), \"x\")");
    REQUIRE(result.warnings == boundsWarning("an `int64_t`", "a `const char*`", 28));
}

// Bounds whose type this cannot know are left alone rather than warned about: two columns are
// typed by the schema and two expression nodes by what they are built over — `abs(&User::a)` and
// `abs(&User::b)` are the same type when the two columns are — and a bind parameter is typed by
// the caller, who declares `bindParam1` himself.
TEST_CASE("codegen: BETWEEN bounds this cannot type are left as written") {
    REQUIRE(generateFull("a BETWEEN b AND b").warnings.empty());
    REQUIRE(generateFull("a BETWEEN b AND c").warnings.empty());
    REQUIRE(generateFull("a BETWEEN abs(b) AND abs(c)").warnings.empty());
    REQUIRE(generateFull("a BETWEEN 'x' AND 'y'").warnings.empty());
    REQUIRE(generate("a BETWEEN 1 AND ?") == "between(&User::a, 1, bindParam1)");
    REQUIRE(generateFull("a BETWEEN 1 AND ?").warnings ==
            std::vector<CodegenWarning>{"bind parameter ? -> C++ variable 'bindParam1'; for prepared statements use "
                                        "storage.prepare() + get<N>(stmt)"});
}

// What a bound generates warns for itself too, and that warning used to be dropped on the way out
// of the BETWEEN: the three operands were generated and only their code was kept.
TEST_CASE("codegen: a warning from a BETWEEN operand reaches the caller") {
    auto result = generateFull("a BETWEEN (1 COLLATE BINARY) AND 3");
    REQUIRE(result.code == "between(&User::a, 1, 3)");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{"COLLATE BINARY on expressions is not directly supported in sqlite_orm "
                                        "codegen"});
    result = generateFull("(a COLLATE NOCASE) BETWEEN 1 AND 3");
    REQUIRE(result.code == "between(&User::a, 1, 3)");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{"COLLATE NOCASE on expressions is not directly supported in sqlite_orm "
                                        "codegen"});
}

TEST_CASE("codegen: IN") {
    REQUIRE(generateFull("a IN (1, 2, 3)") ==
            CodeGenResult{"in(&User::a, {1, 2, 3})", {columnRefStyleDp(1, "&User::a")}, {}});
}

TEST_CASE("codegen: NOT IN") {
    REQUIRE(generateFull("a NOT IN (1, 2)") ==
            CodeGenResult{"not_in(&User::a, {1, 2})",
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
    REQUIRE(generateWithPolicy("SELECT 1 - (2 - 3);", policy).code == "auto rows = storage.select(sub(1, sub(2, 3)));");
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
    REQUIRE(prefixFor("x BETWEEN 3000000000 AND 4000000000") == "struct User {\n    int64_t x = 0;\n};");
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
    REQUIRE(generateFull("SELECT 1 IS 2 FROM users;") == CodeGenResult{{},
                                                                       {},
                                                                       {},
                                                                       {"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
                                                                        "is not supported in sqlite_orm"}});
}

TEST_CASE("codegen: IS NOT expr returns error") {
    REQUIRE(generateFull("SELECT 1 IS NOT 2 FROM users;") ==
            CodeGenResult{{},
                          {},
                          {},
                          {"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
                           "is not supported in sqlite_orm"}});
}

TEST_CASE("codegen: IS DISTINCT FROM returns error") {
    REQUIRE(generateFull("SELECT a IS DISTINCT FROM b FROM t;") ==
            CodeGenResult{{},
                          {},
                          {},
                          {"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
                           "is not supported in sqlite_orm"}});
}

TEST_CASE("codegen: IS NOT DISTINCT FROM returns error") {
    REQUIRE(generateFull("SELECT a IS NOT DISTINCT FROM b FROM t;") ==
            CodeGenResult{{},
                          {},
                          {},
                          {"binary IS / IS NOT / IS [NOT] DISTINCT FROM "
                           "is not supported in sqlite_orm"}});
}

// `->` answers the JSON text of the value at the path — a string comes back quoted, `true` comes
// back as `true` — where the JSON_EXTRACT call it is generated as answers the value itself, which
// sqlite3 3.51 confirms: `'{"a":"s"}' -> '$.a'` is `"s"` and `json_extract('{"a":"s"}', '$.a')` is
// `s`. sqlite_orm has no form for the operator, so the divergence is reported rather than fixed.
TEST_CASE("codegen: JSON -> operator") {
    REQUIRE(
        generateFull("SELECT data -> '$.name' FROM users;") ==
        CodeGenResult{
            "auto rows = storage.select(as_optional(json_extract<std::string>(&Users::data, \"$.name\")));",
            {columnRefStyleDp(1, "&Users::data"),
             DecisionPoint{
                 2,
                 "expr_style",
                 "functional",
                 "json_extract<std::string>(&Users::data, \"$.name\")",
                 {Option{"functional", "json_extract<std::string>(&Users::data, \"$.name\")", "functional style"}}}},
            {CodegenWarning{kJsonTextArrowWarning, SourceLocation{1, 13}, 2}}});
}

// The result type report belongs to `->>` and to the call, not to `->`. Whatever the JSON holds,
// `->` answers its JSON text, so what it answers is a text already and the `std::string` the call
// is read back through costs it nothing — sqlite3 3.51 answers `'{"n":42}' -> '$.n'` with the text
// `42`, `'{"f":1.5}' -> '$.f'` with the text `1.5` and `'{"o":{"k":1}}' -> '$.o'` with the text
// `{"k":1}`, each of which the generated code reads back unchanged. Where `->` does lose something
// is where it differs from the call at all, which `kJsonTextArrowWarning` reports at the very same
// two characters, so a second report there would underline them twice and say something untrue.
TEST_CASE("codegen: `->` carries no result type report and `->>` carries nothing else") {
    REQUIRE(generateFull("SELECT data -> '$.name' FROM users;").warnings ==
            std::vector<CodegenWarning>{CodegenWarning{kJsonTextArrowWarning, SourceLocation{1, 13}, 2}});
    REQUIRE(generateFull("SELECT data ->> '$.name' FROM users;").warnings ==
            std::vector<CodegenWarning>{CodegenWarning{kJsonExtractResultTypeWarning, SourceLocation{1, 13}, 3}});
    // The same split holds where the arrow reads a whole concatenation, which is the form the two
    // operators share a precedence level with.
    REQUIRE(generateFull("SELECT data || data -> '$.name' FROM users;").warnings ==
            std::vector<CodegenWarning>{CodegenWarning{kJsonTextArrowWarning, SourceLocation{1, 21}, 2}});
    REQUIRE(generateFull("SELECT data || data ->> '$.name' FROM users;").warnings ==
            std::vector<CodegenWarning>{CodegenWarning{kJsonExtractResultTypeWarning, SourceLocation{1, 21}, 3}});
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
        for (const auto& option: dp.options) {
            if (option.value == dp.chosenValue) {
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

// `->>` over the path it is generated with is the JSON_EXTRACT call it is generated as, value for
// value and type for type — sqlite3 3.51 answers the same storage class and the same value for
// both over every kind of JSON value — so the only thing left to report is the result type the
// call is read back through. What the operator does that the call does not is expand an
// abbreviated path, which is pinned in "codegen: a JSON arrow expands the path it is written with".
TEST_CASE("codegen: JSON ->> operator") {
    REQUIRE(
        generateFull("SELECT data ->> '$.name' FROM users;") ==
        CodeGenResult{
            "auto rows = storage.select(as_optional(json_extract<std::string>(&Users::data, \"$.name\")));",
            {columnRefStyleDp(1, "&Users::data"),
             DecisionPoint{
                 2,
                 "expr_style",
                 "functional",
                 "json_extract<std::string>(&Users::data, \"$.name\")",
                 {Option{"functional", "json_extract<std::string>(&Users::data, \"$.name\")", "functional style"}}}},
            {CodegenWarning{kJsonExtractResultTypeWarning, SourceLocation{1, 13}, 3}}});
}

// sqlite_orm declares `json_extract` and `json_quote` with a result type parameter that has no
// default, so a call generated without one does not compile at all — the generated code names the
// type the row is read back through. `json_quote` answers text whatever it is given and
// JSON_EXTRACT over two paths or more answers the JSON array of what it found, also text, so only
// the single-path form is reported.
TEST_CASE("codegen: json_extract() and json_quote() calls name the result type") {
    REQUIRE(generateFull("SELECT json_extract(data, '$.name') FROM users;") ==
            CodeGenResult{"auto rows = storage.select(as_optional(json_extract<std::string>(&Users::data, "
                          "\"$.name\")));",
                          {columnRefStyleDp(1, "&Users::data")},
                          {CodegenWarning{kJsonExtractResultTypeWarning, SourceLocation{1, 8}, 12}}});
    REQUIRE(generateFull("SELECT json_extract(data, '$.a', '$.b') FROM users;") ==
            CodeGenResult{"auto rows = storage.select(as_optional(json_extract<std::string>(&Users::data, \"$.a\", "
                          "\"$.b\")));",
                          {columnRefStyleDp(1, "&Users::data")}});
    REQUIRE(generateFull("SELECT json_quote(data) FROM users;") ==
            CodeGenResult{"auto rows = storage.select(json_quote<std::string>(&Users::data));",
                          {columnRefStyleDp(1, "&Users::data")}});
}

// The result type is what the row is read back into, and a call standing anywhere but in a result
// column is never read back: the type is spelled out all the same, since the call does not compile
// without it, and nothing is reported.
TEST_CASE("codegen: a JSON_EXTRACT call outside a result column is typed without a report") {
    REQUIRE(generate("SELECT data FROM users WHERE data ->> '$.a' = 5;") ==
            "auto rows = storage.select(&Users::data, where(json_extract<std::string>(&Users::data, \"$.a\") == 5));");
    REQUIRE(generateFull("SELECT data FROM users WHERE data ->> '$.a' = 5;").warnings == std::vector<CodegenWarning>{});
    REQUIRE(generate("SELECT data FROM users ORDER BY json_extract(data, '$.a');") ==
            "auto rows = storage.select(&Users::data, order_by(json_extract<std::string>(&Users::data, \"$.a\")));");
    REQUIRE(generateFull("SELECT data FROM users ORDER BY json_extract(data, '$.a');").warnings ==
            std::vector<CodegenWarning>{});
}

// `->` and `->>` take an abbreviated path the JSON_EXTRACT call they are generated as does not:
// SQLite expands the operand into a `$` path before it looks anything up, so `'{"x":5}' ->> 'x'`
// is 5 while `json_extract('{"x":5}', 'x')` is the error `bad JSON path: 'x'`. Every expansion
// below is the one sqlite3 3.51 builds in `jsonExtractFunc`, cross-checked by running
// `<json> ->> <operand>` against `json_extract(<json>, '<expansion>')` over objects and arrays.
TEST_CASE("codegen: a JSON arrow expands the path it is written with") {
    const std::string prefix = "auto rows = storage.select(as_optional(json_extract<std::string>(&Users::data, ";
    // A label of nothing but ASCII letters, digits and `_` goes in unquoted, and so it does under
    // `->`, which is expanded the same way and only differs in what it answers at the path.
    REQUIRE(generate("SELECT data ->> 'name' FROM users;") == prefix + "\"$.name\")));");
    REQUIRE(generate("SELECT data -> 'name' FROM users;") == prefix + "\"$.name\")));");
    // Every other label goes in quoted — the dot of `a.b` is part of the name, not a step down —
    // and pasted in unescaped, exactly as SQLite pastes it.
    REQUIRE(generate("SELECT data ->> 'a.b' FROM users;") == prefix + "\"$.\\\"a.b\\\"\")));");
    REQUIRE(generate("SELECT data ->> 'it''s' FROM users;") == prefix + "\"$.\\\"it's\\\"\")));");
    // An empty label expands to `$.`, which SQLite refuses as a bad path — as it refuses the bare
    // `''` the operator was written with, naming the other of the two in the message.
    REQUIRE(generate("SELECT data ->> '' FROM users;") == prefix + "\"$.\")));");
    // An integer is an array index, a negative one counted from the right of the array, and `TRUE`
    // and a hexadecimal literal are integers as much as `1` is.
    REQUIRE(generate("SELECT data ->> 1 FROM users;") == prefix + "\"$[1]\")));");
    REQUIRE(generate("SELECT data ->> -1 FROM users;") == prefix + "\"$[#-1]\")));");
    REQUIRE(generate("SELECT data ->> TRUE FROM users;") == prefix + "\"$[1]\")));");
    REQUIRE(generate("SELECT data ->> 0x1 FROM users;") == prefix + "\"$[1]\")));");
    // A path the operand already spells out is used as written, whether it starts with the `$` or
    // with the subscript SQLite puts one in front of.
    REQUIRE(generate("SELECT data ->> '$.name' FROM users;") == prefix + "\"$.name\")));");
    REQUIRE(generate("SELECT data ->> '[1]' FROM users;") == prefix + "\"$[1]\")));");
    // A NULL path needs no expansion: SQLite answers NULL for it, and so does the generated call.
    REQUIRE(generate("SELECT data ->> NULL FROM users;") == prefix + "nullptr)));");
}

// An operand SQLite expands while it runs the statement cannot be expanded while the statement is
// generated: a column, a bind parameter and an expression are only known then, and a REAL or a
// BLOB expands through the text SQLite renders it as. The call is generated over the operand as
// written — which is what the operator answers wherever the operand already holds a `$` path —
// and the divergence is reported at the operator, so that a consumer underlines it.
TEST_CASE("codegen: a JSON arrow whose path is no literal is reported at the operator") {
    REQUIRE(generate("SELECT data FROM users WHERE data ->> name = 5;") ==
            "auto rows = storage.select(&Users::data, where(json_extract<std::string>(&Users::data, &Users::name) == "
            "5));");
    REQUIRE(generateFull("SELECT data FROM users WHERE data ->> name = 5;").warnings ==
            std::vector<CodegenWarning>{CodegenWarning{kJsonArrowPathNotExpandedWarning, SourceLocation{1, 35}, 3}});
    REQUIRE(generateFull("SELECT data FROM users WHERE data -> ? = 5;").warnings ==
            std::vector<CodegenWarning>{CodegenWarning{"bind parameter ? -> C++ variable 'bindParam1'; "
                                                       "for prepared statements use storage.prepare() + get<N>(stmt)"},
                                        CodegenWarning{kJsonArrowPathNotExpandedWarning, SourceLocation{1, 35}, 2},
                                        CodegenWarning{kJsonTextArrowWarning, SourceLocation{1, 35}, 2}});
    REQUIRE(generateFull("SELECT data FROM users WHERE data ->> 1.0 = 5;").warnings ==
            std::vector<CodegenWarning>{CodegenWarning{kJsonArrowPathNotExpandedWarning, SourceLocation{1, 35}, 3}});
    REQUIRE(generateFull("SELECT data FROM users WHERE data ->> x'78' = 5;").warnings ==
            std::vector<CodegenWarning>{CodegenWarning{kJsonArrowPathNotExpandedWarning, SourceLocation{1, 35}, 3}});
    // Past the int64 range SQLite reads the literal as a REAL, which is a label and no index.
    REQUIRE(generateFull("SELECT data FROM users WHERE data ->> 9223372036854775808 = 5;").warnings ==
            std::vector<CodegenWarning>{CodegenWarning{kJsonArrowPathNotExpandedWarning, SourceLocation{1, 35}, 3}});
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
    REQUIRE(generate("SELECT (a COLLATE BINARY) + 1;") == "auto rows = storage.select(as_optional(c(&User::a) + 1));");
    REQUIRE(generate("SELECT a + (a COLLATE BINARY);") ==
            "auto rows = storage.select(as_optional(c(&User::a) + &User::a));");
    REQUIRE(generate("SELECT a + 1;") == "auto rows = storage.select(as_optional(c(&User::a) + 1));");
    // The CAST around the whole column is the int64 widening of
    // "codegen: a bitwise result column is cast to an int64_t", which the COLLATE is inside of.
    REQUIRE(generate("SELECT ~('a' COLLATE NOCASE);") == "auto rows = storage.select(cast<int64_t>(~c(\"a\")));");
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
    REQUIRE(prefixFor("SELECT (a COLLATE NOCASE) BETWEEN 'x' AND 'y';") == "struct User {\n    std::string a;\n};");
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
    REQUIRE(generate("SELECT myfunc(a) FROM users;") == "struct Myfunc {\n"
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
                               SourceLocation{1, 8},
                               1}});
}

// A predicate under the COLLATE is still a predicate sqlite_orm serializes without parentheses, so
// the negation cannot become `0 - is_null(a)`: SQLite would read that as `(0 - a) IS NULL`, which
// answers 1 over a NULL row where `-((a IS NULL) COLLATE BINARY)` answers -1.
TEST_CASE("codegen: a minus over a predicate under a COLLATE warns as the bare predicate does") {
    auto result = generateFull("SELECT -((a IS NULL) COLLATE BINARY);");
    REQUIRE(result.code == "auto rows = storage.select(-(is_null(&User::a)));");
    REQUIRE(result.warnings == std::vector<CodegenWarning>{
                                   "COLLATE BINARY on expressions is not directly supported in sqlite_orm codegen",
                                   CodegenWarning{"unary minus over a predicate (IS NULL) has no working sqlite_orm "
                                                  "form; the generated negation does not reproduce what SQLite "
                                                  "computes and does not compile",
                                                  SourceLocation{1, 8},
                                                  1}});
}

namespace {
    const sqlite2orm::DecisionPoint* findDp(const sqlite2orm::CodeGenResult& result, std::string_view category) {
        for (const auto& dp: result.decisionPoints) {
            if (dp.category == category)
                return &dp;
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

// A tree at the depth limit still has to survive every recursive pass over it — the validator and
// the generator included - since that limit is exactly the promise the parser makes to them.
TEST_CASE("codegen: an expression at the depth limit still generates") {
    std::string sql = "1";
    std::string expected = "c(1)";
    for (size_t i = 1; i < kMaxExpressionDepth; ++i) {
        sql += " + 1";
        expected += " + 1";
    }
    REQUIRE(generate(sql) == expected);
}

// A comment explains why the generator picked the form it did, and a consumer reads it from the
// statement it belongs to — `statements[].comments` of `--db --json`, `comments` of the generated
// header. Every clause there is generates expressions, not only a SELECT's result column, so a
// comment recorded while generating a CHECK, a column DEFAULT, a generated column, an index, a
// trigger's WHEN or any DML clause belongs to that statement just the same.
TEST_CASE("codegen: an expression's comment reaches the statement whose body generated it") {
    const std::vector<std::string> predicateCastOnly{kPredicateCastComment};

    REQUIRE(generateFull("CREATE TABLE q (a INTEGER, b TEXT, CHECK(1 - (b LIKE 'x')));").comments == predicateCastOnly);
    REQUIRE(generateFull("CREATE TABLE q (a INTEGER DEFAULT (1 - (0 LIKE 'x')));").comments == predicateCastOnly);
    REQUIRE(generateFull("CREATE TABLE q (a INTEGER, b AS (1 - (a LIKE 'x')));").comments == predicateCastOnly);
    REQUIRE(generateFull("CREATE INDEX i ON t (1 - (b LIKE 'x'));").comments == predicateCastOnly);
    REQUIRE(
        generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN 1 - (new.b LIKE 'x') BEGIN SELECT 1; END;").comments ==
        predicateCastOnly);
    REQUIRE(generateFull("INSERT INTO t (a) VALUES (1 - (0 LIKE 'x'));").comments == predicateCastOnly);
    REQUIRE(generateFull("UPDATE t SET a = 1 - (b LIKE 'x');").comments == predicateCastOnly);
    REQUIRE(generateFull("DELETE FROM t WHERE 1 - (b LIKE 'x');").comments == predicateCastOnly);
    REQUIRE(generateFull("WITH c AS (SELECT 1 - (b LIKE 'x') AS z FROM t) SELECT z FROM c;").comments ==
            predicateCastOnly);
    REQUIRE(generateFull("SELECT * FROM t WHERE a IN (SELECT 1 - (b LIKE 'x') FROM t);").comments == predicateCastOnly);
}

// The comments belong to a statement, but an embedder does not only ask for statements: the
// playground and SQLite ORM Studio reach the codegen entry points directly, and an entry point
// answers with the comments recorded while it ran — the ones of the node it was handed, and not the
// ones the statement around it recorded before or after.
TEST_CASE("codegen: an entry point reports the comments of the node it was handed") {
    Tokenizer tokenizer;
    Parser parser;
    auto parseResult = parser.parse(tokenizer.tokenize("SELECT 1 - (b LIKE 'x'), -a FROM t;"));
    REQUIRE(parseResult);
    const auto* selectNode = dynamic_cast<const SelectNode*>(parseResult.astNodePointer.get());
    REQUIRE(selectNode != nullptr);
    const AstNode& predicateUnderOperator = *selectNode->columns.at(0).expression;
    const AstNode& negation = *selectNode->columns.at(1).expression;

    CodeGenerator codeGenerator;
    REQUIRE(codeGenerator.generateNode(predicateUnderOperator).comments ==
            std::vector<std::string>{kPredicateCastComment});
    // The same generator, a second node: the first node's comment stays recorded for the statement
    // the two belong to, and what comes back here is this node's own.
    REQUIRE(codeGenerator.generateNode(negation).comments == std::vector<std::string>{kZeroMinusComment});
    REQUIRE(codeGenerator.generateStoredExpression(predicateUnderOperator).comments ==
            std::vector<std::string>{kPredicateCastComment});
    // The SELECT the two stand in is generated through the subexpression entry point a view body
    // and a trigger step go through, and it answers with the comments of the whole body.
    REQUIRE(codeGenerator.tryCodegenSqliteSelectSubexpression(*selectNode).comments ==
            std::vector<std::string>{kPredicateCastComment, kZeroMinusComment});

    // The three entry points left: a compound SELECT, the subquery form a scalar `(SELECT …)` goes
    // through, and a trigger step. Each of them answers with the comments of the body it generated.
    auto compoundParseResult = parser.parse(tokenizer.tokenize("SELECT 1 - (b LIKE 'x') FROM t UNION "
                                                               "SELECT -a FROM t;"));
    REQUIRE(compoundParseResult);
    const auto* compoundSelectNode = dynamic_cast<const CompoundSelectNode*>(compoundParseResult.astNodePointer.get());
    REQUIRE(compoundSelectNode != nullptr);
    REQUIRE(codeGenerator.tryCodegenCompoundSelectSubexpression(*compoundSelectNode).comments ==
            std::vector<std::string>{kPredicateCastComment, kZeroMinusComment});

    auto subqueryParseResult = parser.parse(tokenizer.tokenize("SELECT -a FROM t;"));
    REQUIRE(subqueryParseResult);
    REQUIRE(codeGenerator.tryCodegenSelectLikeSubquery(*subqueryParseResult.astNodePointer).comments ==
            std::vector<std::string>{kZeroMinusComment});

    auto triggerStepParseResult = parser.parse(tokenizer.tokenize("UPDATE t SET a = -a;"));
    REQUIRE(triggerStepParseResult);
    REQUIRE(codeGenerator.generateTriggerStep(*triggerStepParseResult.astNodePointer, "T").comments ==
            std::vector<std::string>{kZeroMinusComment});
}

// A whole statement takes its comments out of the context, and an embedder holding one generator
// generates statement after statement through it: the take has to leave what was recorded before
// the statement alone, or a CREATE TABLE and a CREATE VIEW answer with the comment of whatever node
// the generator was handed before them.
TEST_CASE("codegen: a statement entry point takes only the comments its own body recorded") {
    Tokenizer tokenizer;
    Parser parser;
    auto expressionParseResult = parser.parse(tokenizer.tokenize("SELECT 1 - (b LIKE 'x') FROM t;"));
    REQUIRE(expressionParseResult);
    const auto* selectNode = dynamic_cast<const SelectNode*>(expressionParseResult.astNodePointer.get());
    REQUIRE(selectNode != nullptr);
    const AstNode& predicateUnderOperator = *selectNode->columns.at(0).expression;

    auto viewParseResult = parser.parse(tokenizer.tokenize("CREATE VIEW v AS SELECT -a FROM t;"));
    REQUIRE(viewParseResult);
    const auto* createViewNode = dynamic_cast<const CreateViewNode*>(viewParseResult.astNodePointer.get());
    REQUIRE(createViewNode != nullptr);

    auto tableParseResult = parser.parse(tokenizer.tokenize("CREATE TABLE q (a INTEGER, b TEXT, CHECK(-a));"));
    REQUIRE(tableParseResult);
    const auto* createTableNode = dynamic_cast<const CreateTableNode*>(tableParseResult.astNodePointer.get());
    REQUIRE(createTableNode != nullptr);

    const std::vector<std::string> viewComments{kZeroMinusComment, kViewReflectionComment};
    const std::vector<std::string> tableComments{kZeroMinusComment};

    CodeGenerator freshGenerator;
    REQUIRE(freshGenerator.createViewParts(*createViewNode).comments == viewComments);
    CodeGenerator freshTableGenerator;
    REQUIRE(freshTableGenerator.createTableParts(*createTableNode).comments == tableComments);

    // The same lists on a generator that was handed an unrelated expression first: its CAST comment
    // belongs to the statement that expression came from, and neither statement reports it.
    CodeGenerator reusedGenerator;
    REQUIRE(reusedGenerator.generateNode(predicateUnderOperator).comments ==
            std::vector<std::string>{kPredicateCastComment});
    REQUIRE(reusedGenerator.createViewParts(*createViewNode).comments == viewComments);
    REQUIRE(reusedGenerator.createTableParts(*createTableNode).comments == tableComments);
}

// A comment explains the form some generated code took, so it belongs with the code a consumer is
// given. A fragment the generator throws away — a subquery sqlite_orm has no form for, an index it
// cannot map at all — takes the comments recorded while generating it with it, and the code the
// statement did generate around that fragment keeps its own.
TEST_CASE("codegen: a fragment that is thrown away takes its comments with it") {
    // sqlite_orm deduces the table an index is made for from its columns and `make_unique_index`
    // has no overload naming that table, so a UNIQUE index over an expression generates the
    // expression and then leaves the whole statement out: nothing the comment explains is emitted.
    const CodeGenResult uniqueIndex = generateFull("CREATE UNIQUE INDEX i ON t (-a);");
    REQUIRE(uniqueIndex.code.empty());
    REQUIRE(uniqueIndex.comments == std::vector<std::string>{});

    // The first arm of a compound SELECT is generated before the second one turns out not to be
    // mapped, and the placeholder that stands for the compound stands for that arm as well.
    const CodeGenResult compound = generateFull("SELECT -a FROM t UNION SELECT -a FROM t GROUP BY a;");
    REQUIRE(compound.code == "/* compound SELECT */");
    REQUIRE(compound.comments == std::vector<std::string>{});

    // The operand of an IN is generated before its subquery turns out not to be mapped, and the
    // placeholder replaces the whole predicate, the operand included. The placeholder stands in an
    // expression slot, so the statement goes with it and the comment has nothing left to explain.
    const CodeGenResult inOperand = generateFull("SELECT b FROM t WHERE -a IN (SELECT b FROM t GROUP BY b);");
    REQUIRE(inOperand.code.empty());
    REQUIRE(inOperand.comments == std::vector<std::string>{});

    // The statement around such a placeholder goes the same way, comments and all — what the
    // generators hand each other still carries them, which is the channel the entry point below is.
    const CodeGenResult aroundPlaceholder =
        generateFull("SELECT 1 - (b LIKE 'x') FROM t WHERE a IN (SELECT -a FROM t UNION SELECT -a FROM t GROUP BY a);");
    REQUIRE(aroundPlaceholder.code.empty());
    REQUIRE(aroundPlaceholder.comments == std::vector<std::string>{});

    Tokenizer tokenizer;
    Parser parser;
    auto parseResult = parser.parse(
        tokenizer.tokenize("SELECT 1 - (b LIKE 'x') FROM t WHERE a IN (SELECT -a FROM t UNION SELECT -a FROM t "
                           "GROUP BY a);"));
    REQUIRE(parseResult);
    CodeGenerator nodeGenerator;
    const CodeGenResult node = nodeGenerator.generateNode(*parseResult.astNodePointer);
    REQUIRE(node.code == "auto rows = storage.select(as_optional(c(1) - cast<int64_t>(like(&T::b, \"x\"))), "
                         "where(/* IN (SELECT ...) */));");
    REQUIRE(node.comments == std::vector<std::string>{kPredicateCastComment});

    // The same comment on both sides of the line: the negation of the trigger's WHEN clause is
    // generated and keeps it, and the negation of the step that is replaced by a placeholder does
    // not — a thrown-away fragment loses its own comments and no more than those. The trigger the
    // two stand in holds a placeholder in a `begin(...)` argument, so it is not generated at all.
    const CodeGenResult triggerWhen = generateFull(
        "CREATE TRIGGER tr AFTER INSERT ON t WHEN -a BEGIN SELECT -a FROM t UNION SELECT a FROM t GROUP BY a; END;");
    REQUIRE(triggerWhen.code.empty());
    REQUIRE(triggerWhen.comments == std::vector<std::string>{});

    // The two DDL statements reach their placeholder by another road: the clauses around the one
    // that gives the statement up generate and record as usual, and the parts producer then hands
    // back an empty make-expression. A STORED generated column holding a hex literal past int64 is
    // what gives the table up here, and the CHECK beside it is generated before that is known.
    const CodeGenResult ungeneratableTable =
        generateLastOfBatch("CREATE TABLE t (a INTEGER, b TEXT);\n"
                            "CREATE TABLE q (a INTEGER CHECK(NOT a), g AS (0x1FFFFFFFFFFFFFFFFF) STORED);");
    REQUIRE(ungeneratableTable.code == "/* CREATE TABLE q — not supported for sqlite_orm */");
    REQUIRE(ungeneratableTable.comments == std::vector<std::string>{});

    // The same for a view: SQLite stores a body holding that literal and refuses every query
    // against it, so the view is not generated although its SELECT list generated the negation.
    const CodeGenResult ungeneratableView =
        generateLastOfBatch("CREATE TABLE t (a INTEGER, b TEXT);\n"
                            "CREATE VIEW v AS SELECT -a, 0x1FFFFFFFFFFFFFFFFF FROM t;");
    REQUIRE(ungeneratableView.code == "/* CREATE VIEW v — not supported for sqlite_orm */");
    REQUIRE(ungeneratableView.comments == std::vector<std::string>{});
}
