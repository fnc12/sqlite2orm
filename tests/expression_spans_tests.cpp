#include <sqlite2orm/process.h>

#include <catch2/catch_all.hpp>

#include <string>
#include <vector>

using namespace sqlite2orm;

namespace {

    CodeGenPolicy recordingExpressionSpans() {
        CodeGenPolicy policy;
        policy.recordExpressionSpans = true;
        return policy;
    }

}  // namespace

// The map is asked for: without it a result carries no expression span at all, and the code is
// the same either way — the policy only decides whether the map is recorded next to it.
TEST_CASE("expression spans: recorded only when the policy asks for them") {
    const std::string sql = "SELECT a + 1 FROM users;";
    const CodeGenPolicy plain;
    const CodeGenPolicy recording = recordingExpressionSpans();
    const ProcessSqlResult withoutMap = processSql(sql, &plain);
    const ProcessSqlResult withMap = processSql(sql, &recording);
    REQUIRE(withoutMap.codegen.code == "auto rows = storage.select(as_optional(c(&Users::a) + 1));");
    REQUIRE(withoutMap.codegen.expressionSpans == std::vector<GeneratedCodeSpan>{});
    REQUIRE(withMap.codegen.code == "auto rows = storage.select(as_optional(c(&Users::a) + 1));");
    REQUIRE(withMap.codegen.expressionSpans == std::vector<GeneratedCodeSpan>{{39, 16, 0, SourceLocation{1, 8}, 5},
                                                                              {41, 9, 0, SourceLocation{1, 8}, 1},
                                                                              {54, 1, 0, SourceLocation{1, 12}, 1}});
}

// Every expression of a SELECT is on the map, nested the way the expressions are: `a + 1` holds
// `a` and `1`, the AND holds both of its operands. An expression's code is its own and nothing
// around it — the `c(...)` the `+` quotes `a` with is the operator's, so the span of `a` is the
// member pointer alone, and the `as_optional(...)` a result column is widened with is the
// column's, so the span of `a + 1` starts inside it.
TEST_CASE("expression spans: a SELECT maps each expression onto its code") {
    const CodeGenPolicy policy = recordingExpressionSpans();
    const ProcessSqlResult result = processSql("SELECT a + 1 FROM users WHERE name LIKE 'A%' AND a > 2;", &policy);
    REQUIRE(result.codegen.code == "auto rows = storage.select(as_optional(c(&Users::a) + 1), "
                                   "where(like(&Users::name, \"A%\") and c(&Users::a) > 2));");
    REQUIRE(result.codegen.expressionSpans == std::vector<GeneratedCodeSpan>{{39, 16, 0, SourceLocation{1, 8}, 5},
                                                                             {41, 9, 0, SourceLocation{1, 8}, 1},
                                                                             {54, 1, 0, SourceLocation{1, 12}, 1},
                                                                             {64, 45, 0, SourceLocation{1, 31}, 24},
                                                                             {64, 24, 0, SourceLocation{1, 31}, 14},
                                                                             {69, 12, 0, SourceLocation{1, 31}, 4},
                                                                             {83, 4, 0, SourceLocation{1, 41}, 4},
                                                                             {93, 16, 0, SourceLocation{1, 50}, 5},
                                                                             {95, 9, 0, SourceLocation{1, 50}, 1},
                                                                             {108, 1, 0, SourceLocation{1, 54}, 1}});
}

// An UPDATE maps the value of every SET and its WHERE. A negation is generated as a subtraction
// from zero, and the parentheses that keep it one operand are its own code, so `-a` spans them;
// offsets count characters, so the `ü` of the literal moves what follows it by one and not by the
// two bytes it takes.
TEST_CASE("expression spans: an UPDATE maps its SET values and its WHERE") {
    const CodeGenPolicy policy = recordingExpressionSpans();
    const ProcessSqlResult result =
        processSql("UPDATE users SET a = -a + 2, name = 'ü' || name WHERE a BETWEEN 1 AND 9;", &policy);
    REQUIRE(result.codegen.code == "storage.update_all(set(c(&Users::a) = (c(0) - c(&Users::a)) + 2, c(&Users::name) = "
                                   "c(\"ü\") || &Users::name), where(between(&Users::a, 1, 9)));");
    REQUIRE(result.codegen.expressionSpans == std::vector<GeneratedCodeSpan>{{38, 25, 0, SourceLocation{1, 22}, 6},
                                                                             {38, 21, 0, SourceLocation{1, 22}, 2},
                                                                             {48, 9, 0, SourceLocation{1, 23}, 1},
                                                                             {62, 1, 0, SourceLocation{1, 27}, 1},
                                                                             {83, 22, 0, SourceLocation{1, 37}, 11},
                                                                             {85, 3, 0, SourceLocation{1, 37}, 3},
                                                                             {93, 12, 0, SourceLocation{1, 44}, 4},
                                                                             {114, 24, 0, SourceLocation{1, 55}, 17},
                                                                             {122, 9, 0, SourceLocation{1, 55}, 1},
                                                                             {133, 1, 0, SourceLocation{1, 65}, 1},
                                                                             {136, 1, 0, SourceLocation{1, 71}, 1}});
}

// A DELETE maps its WHERE. SQLite binds NOT looser than IN, so the NOT stands over the whole IN,
// and the parentheses it puts around its operand are its own code rather than the IN's.
TEST_CASE("expression spans: a DELETE maps its WHERE") {
    const CodeGenPolicy policy = recordingExpressionSpans();
    const ProcessSqlResult result = processSql("DELETE FROM users WHERE NOT a IN (1, 2);", &policy);
    REQUIRE(result.codegen.code == "storage.remove_all<Users>(where(not (in(&Users::a, {1, 2}))));");
    REQUIRE(result.codegen.expressionSpans == std::vector<GeneratedCodeSpan>{{32, 27, 0, SourceLocation{1, 25}, 15},
                                                                             {37, 21, 0, SourceLocation{1, 29}, 11},
                                                                             {40, 9, 0, SourceLocation{1, 29}, 1},
                                                                             {52, 1, 0, SourceLocation{1, 35}, 1},
                                                                             {55, 1, 0, SourceLocation{1, 38}, 1}});
}

// An INSERT maps every value of its rows. A NULL filling a field of the struct is written as the
// empty optional the field takes, and that is still the code the NULL generated.
TEST_CASE("expression spans: an INSERT maps its values, a NULL field included") {
    const CodeGenPolicy policy = recordingExpressionSpans();
    const ProcessSqlResult result = processSql("INSERT INTO users VALUES (CAST('7' AS INTEGER), NULL);", &policy);
    REQUIRE(result.codegen.code == "storage.insert(Users{cast<int64_t>(\"7\"), std::nullopt});");
    REQUIRE(result.codegen.expressionSpans == std::vector<GeneratedCodeSpan>{{21, 18, 0, SourceLocation{1, 27}, 20},
                                                                             {35, 3, 0, SourceLocation{1, 32}, 3},
                                                                             {41, 12, 0, SourceLocation{1, 49}, 4}});
}

// The map does not reach into a subquery yet: the IN standing over one is on it, code of the
// subquery included, and so is its operand, while the expressions of the subquery's own clauses
// are a gap rather than spans pointing anywhere else.
TEST_CASE("expression spans: a subquery is mapped as a whole and not inside") {
    const CodeGenPolicy policy = recordingExpressionSpans();
    const ProcessSqlResult result =
        processSql("SELECT a FROM users WHERE a IN (SELECT b FROM other WHERE b > 1);", &policy);
    REQUIRE(result.codegen.code == "auto rows = storage.select(&Users::a, from<Users>(), "
                                   "where(in(&Users::a, select(&Other::b, where(c(&Other::b) > 1)))));");
    REQUIRE(result.codegen.expressionSpans == std::vector<GeneratedCodeSpan>{{27, 9, 0, SourceLocation{1, 8}, 1},
                                                                             {59, 57, 0, SourceLocation{1, 27}, 38},
                                                                             {62, 9, 0, SourceLocation{1, 27}, 1}});
}

// The declarations a user-defined function needs are written in front of the statement, and the
// spans of its expressions move along with it. A minus folded into a constant is the code of both
// the negation and the constant: `-5` holds the `5` it was written with.
TEST_CASE("expression spans: the custom function preamble moves the spans after it") {
    const CodeGenPolicy policy = recordingExpressionSpans();
    const ProcessSqlResult result = processSql("SELECT my_fn(a, -5) FROM t;", &policy);
    REQUIRE(result.codegen.code == "struct MyFn {\n"
                                   "    // TODO: implement this user-defined scalar function\n"
                                   "    int operator()(int a, int arg1) const { return {}; }\n"
                                   "    static const char *name() { return \"my_fn\"; }\n"
                                   "};\n"
                                   "\n"
                                   "storage.create_scalar_function<MyFn>();\n"
                                   "\n"
                                   "auto rows = storage.select(func<MyFn>(&T::a, -5));");
    REQUIRE(result.codegen.expressionSpans == std::vector<GeneratedCodeSpan>{{250, 21, 0, SourceLocation{1, 8}, 12},
                                                                             {261, 5, 0, SourceLocation{1, 14}, 1},
                                                                             {268, 2, 0, SourceLocation{1, 17}, 2},
                                                                             {269, 1, 0, SourceLocation{1, 18}, 1}});
}

// A joined batch carries the expression spans of every statement, moved to where its code landed
// and named by the statement's index — inside a savepoint's lambda too, where the statement is
// indented. Every one of them lies within the statement-level span of its statement. A COLLATE is
// generated as its operand alone, so `'x' COLLATE nocase` and `'x'` share one stretch of code.
TEST_CASE("expression spans: a joined batch moves each statement's spans to where its code landed") {
    CodeGenPolicy policy = recordingExpressionSpans();
    policy.chosenAlternativeValueByCategory["savepoint_style"] = "functional";
    const auto results = processMultiSql("CREATE TABLE t (a INTEGER, b TEXT);\n"
                                         "SAVEPOINT s;\n"
                                         "DELETE FROM t WHERE a > 1;\n"
                                         "RELEASE s;\n"
                                         "SELECT b FROM t WHERE b = 'x' COLLATE nocase;",
                                         &policy);
    const JoinedGeneratedCode joined = joinGeneratedCodeWithSpans(results);
    REQUIRE(joined.code == "struct T {\n"
                           "    std::optional<int64_t> a;\n"
                           "    std::optional<std::string> b;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"a\", &T::a),\n"
                           "        make_column(\"b\", &T::b)));\n"
                           "\n"
                           "storage.savepoint(\"s\", [&] {\n"
                           "    storage.remove_all<T>(where(c(&T::a) > 1));\n"
                           "    return true;\n"
                           "});\n"
                           "auto rows = storage.select(&T::b, where(c(&T::b) == \"x\"));\n");
    REQUIRE(joined.spans == std::vector<GeneratedCodeSpan>{{0, 77, 0, SourceLocation{1, 1}, 34},
                                                           {115, 81, 0, SourceLocation{1, 1}, 34},
                                                           {200, 28, 1, SourceLocation{2, 1}, 11},
                                                           {233, 43, 2, SourceLocation{3, 1}, 25},
                                                           {281, 16, 3, SourceLocation{4, 1}, 9},
                                                           {298, 58, 4, SourceLocation{5, 1}, 44}});
    REQUIRE(joined.expressionSpans == std::vector<GeneratedCodeSpan>{{261, 12, 2, SourceLocation{3, 21}, 5},
                                                                     {263, 5, 2, SourceLocation{3, 21}, 1},
                                                                     {272, 1, 2, SourceLocation{3, 25}, 1},
                                                                     {325, 5, 4, SourceLocation{5, 8}, 1},
                                                                     {338, 15, 4, SourceLocation{5, 23}, 22},
                                                                     {340, 5, 4, SourceLocation{5, 23}, 1},
                                                                     {350, 3, 4, SourceLocation{5, 27}, 18},
                                                                     {350, 3, 4, SourceLocation{5, 27}, 3}});
}
