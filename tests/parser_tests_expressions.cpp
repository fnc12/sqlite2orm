#include "parser_tests_common.hpp"
using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: integer literal") {
    SECTION("simple") {
        auto parseResult = parse("42");
        REQUIRE(requireNode<IntegerLiteralNode>(parseResult) == IntegerLiteralNode("42", {}));
    }
    SECTION("zero") {
        auto parseResult = parse("0");
        REQUIRE(requireNode<IntegerLiteralNode>(parseResult) == IntegerLiteralNode("0", {}));
    }
    SECTION("hex") {
        auto parseResult = parse("0xFF");
        REQUIRE(requireNode<IntegerLiteralNode>(parseResult) == IntegerLiteralNode("0xFF", {}));
    }
    SECTION("preserves source location") {
        auto parseResult = parse("42");
        REQUIRE(parseResult);
        REQUIRE(parseResult.astNodePointer->location.line == 1);
        REQUIRE(parseResult.astNodePointer->location.column == 1);
    }
}

TEST_CASE("parser: real literal") {
    SECTION("with decimal") {
        auto parseResult = parse("3.14");
        REQUIRE(requireNode<RealLiteralNode>(parseResult) == RealLiteralNode("3.14", {}));
    }
    SECTION("starting with dot") {
        auto parseResult = parse(".5");
        REQUIRE(requireNode<RealLiteralNode>(parseResult) == RealLiteralNode(".5", {}));
    }
    SECTION("with exponent") {
        auto parseResult = parse("1e10");
        REQUIRE(requireNode<RealLiteralNode>(parseResult) == RealLiteralNode("1e10", {}));
    }
}

TEST_CASE("parser: string literal") {
    SECTION("simple") {
        auto parseResult = parse("'hello'");
        REQUIRE(requireNode<StringLiteralNode>(parseResult) == StringLiteralNode("'hello'", {}));
    }
    SECTION("escaped quote") {
        auto parseResult = parse("'it''s'");
        REQUIRE(requireNode<StringLiteralNode>(parseResult) == StringLiteralNode("'it''s'", {}));
    }
    SECTION("empty") {
        auto parseResult = parse("''");
        REQUIRE(requireNode<StringLiteralNode>(parseResult) == StringLiteralNode("''", {}));
    }
}

TEST_CASE("parser: null literal") {
    auto input = GENERATE("NULL", "null", "Null");
    auto parseResult = parse(input);
    requireNode<NullLiteralNode>(parseResult);
}

TEST_CASE("parser: bool literal") {
    SECTION("true") {
        auto parseResult = parse("TRUE");
        REQUIRE(requireNode<BoolLiteralNode>(parseResult) == BoolLiteralNode(true, {}));
    }
    SECTION("false") {
        auto parseResult = parse("FALSE");
        REQUIRE(requireNode<BoolLiteralNode>(parseResult) == BoolLiteralNode(false, {}));
    }
}

TEST_CASE("parser: blob literal") {
    auto parseResult = parse("X'48656C6C6F'");
    REQUIRE(requireNode<BlobLiteralNode>(parseResult) == BlobLiteralNode("X'48656C6C6F'", {}));
}

TEST_CASE("parser: column ref") {
    SECTION("unqualified") {
        auto parseResult = parse("name");
        REQUIRE(requireNode<ColumnRefNode>(parseResult) == ColumnRefNode("name", {}));
    }
    SECTION("quoted identifier") {
        auto parseResult = parse(R"("my column")");
        REQUIRE(requireNode<ColumnRefNode>(parseResult) == ColumnRefNode(R"("my column")", {}));
    }
}

TEST_CASE("parser: qualified column ref") {
    SECTION("table.column") {
        auto parseResult = parse("users.name");
        REQUIRE(requireNode<QualifiedColumnRefNode>(parseResult) == QualifiedColumnRefNode("users", "name", {}));
    }
    SECTION("quoted table and column") {
        auto parseResult = parse(R"("my table"."my column")");
        REQUIRE(requireNode<QualifiedColumnRefNode>(parseResult) == QualifiedColumnRefNode(R"("my table")", R"("my column")", {}));
    }
    SECTION("schema.table.column") {
        auto parseResult = parse("main.users.id");
        REQUIRE(requireNode<QualifiedColumnRefNode>(parseResult) ==
                QualifiedColumnRefNode("main", "users", "id", {}));
    }
}

TEST_CASE("parser: CURRENT_TIME / CURRENT_DATE / CURRENT_TIMESTAMP") {
    REQUIRE(requireNode<CurrentDatetimeLiteralNode>(parse("CURRENT_TIME")) ==
            CurrentDatetimeLiteralNode(CurrentDatetimeKind::time, {}));
    REQUIRE(requireNode<CurrentDatetimeLiteralNode>(parse("CURRENT_DATE")) ==
            CurrentDatetimeLiteralNode(CurrentDatetimeKind::date, {}));
    REQUIRE(requireNode<CurrentDatetimeLiteralNode>(parse("CURRENT_TIMESTAMP")) ==
            CurrentDatetimeLiteralNode(CurrentDatetimeKind::timestamp, {}));
}

TEST_CASE("parser: window function OVER") {
    auto parseResult = parse("row_number() OVER (PARTITION BY a ORDER BY b DESC)");
    REQUIRE(parseResult);
    FunctionCallNode expected("row_number", std::vector<AstNodePointer>{}, false, false, SourceLocation{});
    expected.over = std::make_unique<OverClause>();
    expected.over->partitionBy.push_back(makeNode<ColumnRefNode>("a"));
    expected.over->orderBy.push_back(
        OrderByTerm{makeSharedNode<ColumnRefNode>("b"), SortDirection::desc});
    REQUIRE(requireNode<FunctionCallNode>(parseResult) == expected);
}

TEST_CASE("parser: aggregate FILTER (WHERE …) before OVER") {
    auto parseResult = parse("count(*) FILTER (WHERE x > 0) OVER (ORDER BY y)");
    REQUIRE(parseResult);
    FunctionCallNode expected("count", std::vector<AstNodePointer>{}, false, true, SourceLocation{});
    expected.filterWhere = std::make_unique<BinaryOperatorNode>(
        BinaryOperator::greaterThan,
        makeNode<ColumnRefNode>("x"),
        makeNode<IntegerLiteralNode>("0"),
        SourceLocation{});
    expected.over = std::make_unique<OverClause>();
    expected.over->orderBy.push_back(OrderByTerm{makeSharedNode<ColumnRefNode>("y"), SortDirection::none});
    REQUIRE(requireNode<FunctionCallNode>(parseResult) == expected);
}

TEST_CASE("parser: NEW ref") {
    auto input = GENERATE("NEW.col", "new.col", "New.col");
    auto parseResult = parse(input);
    REQUIRE(requireNode<NewRefNode>(parseResult) == NewRefNode("col", {}));
}

TEST_CASE("parser: OLD ref") {
    auto input = GENERATE("OLD.col", "old.col", "Old.col");
    auto parseResult = parse(input);
    REQUIRE(requireNode<OldRefNode>(parseResult) == OldRefNode("col", {}));
}

TEST_CASE("parser: binary comparison") {
    SECTION("equals") {
        auto parseResult = parse("a = 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::equals, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("double equals") {
        auto parseResult = parse("a == 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::equals, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("not equals !=") {
        auto parseResult = parse("a != 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::notEquals, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("not equals <>") {
        auto parseResult = parse("a <> 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::notEquals, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("less than") {
        auto parseResult = parse("a < 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::lessThan, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("less or equal") {
        auto parseResult = parse("a <= 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::lessOrEqual, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("greater than") {
        auto parseResult = parse("a > 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::greaterThan, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("greater or equal") {
        auto parseResult = parse("a >= 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::greaterOrEqual, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("column vs string") {
        auto parseResult = parse("name = 'hello'");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::equals, makeNode<ColumnRefNode>("name"), makeNode<StringLiteralNode>("'hello'"), {}));
    }
    SECTION("qualified column ref as operand") {
        auto parseResult = parse("users.id = 42");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::equals, makeNode<QualifiedColumnRefNode>("users", "id"), makeNode<IntegerLiteralNode>("42"), {}));
    }
    SECTION("standalone literal without operator remains literal") {
        auto parseResult = parse("42");
        requireNode<IntegerLiteralNode>(parseResult);
    }
}

TEST_CASE("parser: arithmetic operators") {
    SECTION("add") {
        auto parseResult = parse("a + 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::add, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("subtract") {
        auto parseResult = parse("a - 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::subtract, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("multiply") {
        auto parseResult = parse("a * 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::multiply, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("divide") {
        auto parseResult = parse("a / 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::divide, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("modulo") {
        auto parseResult = parse("a % 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::modulo, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
}

TEST_CASE("parser: concatenation") {
    auto parseResult = parse("a || b");
    REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::concatenate, makeNode<ColumnRefNode>("a"), makeNode<ColumnRefNode>("b"), {}));
}

TEST_CASE("parser: bitwise operators") {
    SECTION("bitwise and") {
        auto parseResult = parse("a & 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::bitwiseAnd, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("bitwise or") {
        auto parseResult = parse("a | 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::bitwiseOr, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("shift left") {
        auto parseResult = parse("a << 2");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::shiftLeft, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("2"), {}));
    }
    SECTION("shift right") {
        auto parseResult = parse("a >> 2");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::shiftRight, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("2"), {}));
    }
}

TEST_CASE("parser: unary operators") {
    SECTION("unary minus on literal") {
        auto parseResult = parse("-5");
        REQUIRE(requireNode<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
            UnaryOperator::minus, makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("unary minus on column") {
        auto parseResult = parse("-a");
        REQUIRE(requireNode<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
            UnaryOperator::minus, makeNode<ColumnRefNode>("a"), {}));
    }
    SECTION("unary plus") {
        auto parseResult = parse("+5");
        REQUIRE(requireNode<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
            UnaryOperator::plus, makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("bitwise not") {
        auto parseResult = parse("~5");
        REQUIRE(requireNode<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
            UnaryOperator::bitwiseNot, makeNode<IntegerLiteralNode>("5"), {}));
    }
    SECTION("double unary minus") {
        auto parseResult = parse("- -5");
        REQUIRE(requireNode<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
            UnaryOperator::minus,
            std::make_unique<UnaryOperatorNode>(UnaryOperator::minus, makeNode<IntegerLiteralNode>("5"), SourceLocation{}),
            {}));
    }
}

TEST_CASE("parser: operator precedence") {
    SECTION("multiply before add: a + b * c") {
        auto parseResult = parse("a + b * c");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::add,
            makeNode<ColumnRefNode>("a"),
            std::make_unique<BinaryOperatorNode>(BinaryOperator::multiply,
                makeNode<ColumnRefNode>("b"), makeNode<ColumnRefNode>("c"), SourceLocation{}),
            {}));
    }
    SECTION("multiply before add: a * b + c") {
        auto parseResult = parse("a * b + c");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::add,
            std::make_unique<BinaryOperatorNode>(BinaryOperator::multiply,
                makeNode<ColumnRefNode>("a"), makeNode<ColumnRefNode>("b"), SourceLocation{}),
            makeNode<ColumnRefNode>("c"),
            {}));
    }
    SECTION("comparison lower than arithmetic: a = b + 5") {
        auto parseResult = parse("a = b + 5");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::equals,
            makeNode<ColumnRefNode>("a"),
            std::make_unique<BinaryOperatorNode>(BinaryOperator::add,
                makeNode<ColumnRefNode>("b"), makeNode<IntegerLiteralNode>("5"), SourceLocation{}),
            {}));
    }
    SECTION("concat highest binary: a || b * c is (a || b) * c") {
        auto parseResult = parse("a || b * c");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::multiply,
            std::make_unique<BinaryOperatorNode>(BinaryOperator::concatenate,
                makeNode<ColumnRefNode>("a"), makeNode<ColumnRefNode>("b"), SourceLocation{}),
            makeNode<ColumnRefNode>("c"),
            {}));
    }
    SECTION("unary minus binds tighter than binary: -a + b") {
        auto parseResult = parse("-a + b");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::add,
            std::make_unique<UnaryOperatorNode>(UnaryOperator::minus, makeNode<ColumnRefNode>("a"), SourceLocation{}),
            makeNode<ColumnRefNode>("b"),
            {}));
    }
    SECTION("binary minus with unary minus operand: a - -b") {
        auto parseResult = parse("a - -b");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::subtract,
            makeNode<ColumnRefNode>("a"),
            std::make_unique<UnaryOperatorNode>(UnaryOperator::minus, makeNode<ColumnRefNode>("b"), SourceLocation{}),
            {}));
    }
}

TEST_CASE("parser: left associativity") {
    SECTION("a + b + c = (a + b) + c") {
        auto parseResult = parse("a + b + c");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::add,
            std::make_unique<BinaryOperatorNode>(BinaryOperator::add,
                makeNode<ColumnRefNode>("a"), makeNode<ColumnRefNode>("b"), SourceLocation{}),
            makeNode<ColumnRefNode>("c"),
            {}));
    }
    SECTION("a || b || c = (a || b) || c") {
        auto parseResult = parse("a || b || c");
        REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::concatenate,
            std::make_unique<BinaryOperatorNode>(BinaryOperator::concatenate,
                makeNode<ColumnRefNode>("a"), makeNode<ColumnRefNode>("b"), SourceLocation{}),
            makeNode<ColumnRefNode>("c"),
            {}));
    }
}

TEST_CASE("parser: logical AND") {
    auto parseResult = parse("a = 1 AND b = 2");
    REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::logicalAnd,
        std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
            makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("1"), SourceLocation{}),
        std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
            makeNode<ColumnRefNode>("b"), makeNode<IntegerLiteralNode>("2"), SourceLocation{}),
        {}));
}

TEST_CASE("parser: logical OR") {
    auto parseResult = parse("a = 1 OR b = 2");
    REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::logicalOr,
        std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
            makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("1"), SourceLocation{}),
        std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
            makeNode<ColumnRefNode>("b"), makeNode<IntegerLiteralNode>("2"), SourceLocation{}),
        {}));
}

TEST_CASE("parser: logical NOT") {
    auto parseResult = parse("NOT a");
    REQUIRE(requireNode<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
        UnaryOperator::logicalNot, makeNode<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: NOT case insensitive") {
    auto input = GENERATE("NOT a", "not a", "Not a");
    auto parseResult = parse(input);
    REQUIRE(requireNode<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
        UnaryOperator::logicalNot, makeNode<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: AND binds tighter than OR") {
    auto parseResult = parse("a = 1 OR b = 2 AND c = 3");
    REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::logicalOr,
        std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
            makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("1"), SourceLocation{}),
        std::make_unique<BinaryOperatorNode>(BinaryOperator::logicalAnd,
            std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
                makeNode<ColumnRefNode>("b"), makeNode<IntegerLiteralNode>("2"), SourceLocation{}),
            std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
                makeNode<ColumnRefNode>("c"), makeNode<IntegerLiteralNode>("3"), SourceLocation{}),
            SourceLocation{}),
        {}));
}

TEST_CASE("parser: NOT binds tighter than AND") {
    auto parseResult = parse("NOT a AND b");
    REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::logicalAnd,
        std::make_unique<UnaryOperatorNode>(UnaryOperator::logicalNot, makeNode<ColumnRefNode>("a"), SourceLocation{}),
        makeNode<ColumnRefNode>("b"),
        {}));
}

TEST_CASE("parser: IS NULL") {
    auto parseResult = parse("a IS NULL");
    REQUIRE(requireNode<IsNullNode>(parseResult) == IsNullNode(
        makeNode<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: IS NOT NULL") {
    auto parseResult = parse("a IS NOT NULL");
    REQUIRE(requireNode<IsNotNullNode>(parseResult) == IsNotNullNode(
        makeNode<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: ISNULL keyword") {
    auto parseResult = parse("a ISNULL");
    REQUIRE(requireNode<IsNullNode>(parseResult) == IsNullNode(
        makeNode<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: NOTNULL keyword") {
    auto parseResult = parse("a NOTNULL");
    REQUIRE(requireNode<IsNotNullNode>(parseResult) == IsNotNullNode(
        makeNode<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: NOT NULL (two keywords)") {
    auto parseResult = parse("a NOT NULL");
    REQUIRE(requireNode<IsNotNullNode>(parseResult) == IsNotNullNode(
        makeNode<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: BETWEEN") {
    auto parseResult = parse("a BETWEEN 1 AND 10");
    REQUIRE(requireNode<BetweenNode>(parseResult) == BetweenNode(
        makeNode<ColumnRefNode>("a"),
        makeNode<IntegerLiteralNode>("1"),
        makeNode<IntegerLiteralNode>("10"),
        false, {}));
}

TEST_CASE("parser: NOT BETWEEN") {
    auto parseResult = parse("a NOT BETWEEN 1 AND 10");
    REQUIRE(requireNode<BetweenNode>(parseResult) == BetweenNode(
        makeNode<ColumnRefNode>("a"),
        makeNode<IntegerLiteralNode>("1"),
        makeNode<IntegerLiteralNode>("10"),
        true, {}));
}

TEST_CASE("parser: BETWEEN with AND precedence") {
    auto parseResult = parse("a BETWEEN 1 AND 10 AND b > 5");
    REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::logicalAnd,
        std::make_unique<BetweenNode>(
            makeNode<ColumnRefNode>("a"),
            makeNode<IntegerLiteralNode>("1"),
            makeNode<IntegerLiteralNode>("10"),
            false, SourceLocation{}),
        std::make_unique<BinaryOperatorNode>(BinaryOperator::greaterThan,
            makeNode<ColumnRefNode>("b"), makeNode<IntegerLiteralNode>("5"), SourceLocation{}),
        {}));
}

TEST_CASE("parser: IN with values") {
    auto parseResult = parse("a IN (1, 2, 3)");
    std::vector<AstNodePointer> values;
    values.push_back(makeNode<IntegerLiteralNode>("1"));
    values.push_back(makeNode<IntegerLiteralNode>("2"));
    values.push_back(makeNode<IntegerLiteralNode>("3"));
    REQUIRE(requireNode<InNode>(parseResult) ==
            InNode(makeNode<ColumnRefNode>("a"), std::move(values), nullptr, false, {}));
}

TEST_CASE("parser: NOT IN") {
    auto parseResult = parse("a NOT IN (1, 2)");
    std::vector<AstNodePointer> values;
    values.push_back(makeNode<IntegerLiteralNode>("1"));
    values.push_back(makeNode<IntegerLiteralNode>("2"));
    REQUIRE(requireNode<InNode>(parseResult) ==
            InNode(makeNode<ColumnRefNode>("a"), std::move(values), nullptr, true, {}));
}

TEST_CASE("parser: IN with empty list") {
    auto parseResult = parse("a IN ()");
    REQUIRE(requireNode<InNode>(parseResult) ==
            InNode(makeNode<ColumnRefNode>("a"), {}, nullptr, false, {}));
}


TEST_CASE("parser: LIKE") {
    auto parseResult = parse("name LIKE '%foo%'");
    REQUIRE(requireNode<LikeNode>(parseResult) ==
            LikeNode(makeNode<ColumnRefNode>("name"), makeNode<StringLiteralNode>("'%foo%'"), nullptr, false, {}));
}

TEST_CASE("parser: LIKE with ESCAPE") {
    auto parseResult = parse("name LIKE '%\\%%' ESCAPE '\\'");
    REQUIRE(requireNode<LikeNode>(parseResult) ==
            LikeNode(makeNode<ColumnRefNode>("name"),
                     makeNode<StringLiteralNode>("'%\\%%'"),
                     makeNode<StringLiteralNode>("'\\'"),
                     false,
                     {}));
}

TEST_CASE("parser: NOT LIKE") {
    auto parseResult = parse("name NOT LIKE '%foo%'");
    REQUIRE(requireNode<LikeNode>(parseResult) ==
            LikeNode(makeNode<ColumnRefNode>("name"), makeNode<StringLiteralNode>("'%foo%'"), nullptr, true, {}));
}

TEST_CASE("parser: GLOB") {
    auto parseResult = parse("name GLOB '*foo*'");
    REQUIRE(requireNode<GlobNode>(parseResult) ==
            GlobNode(makeNode<ColumnRefNode>("name"), makeNode<StringLiteralNode>("'*foo*'"), false, {}));
}

TEST_CASE("parser: NOT GLOB") {
    auto parseResult = parse("name NOT GLOB '*foo*'");
    REQUIRE(requireNode<GlobNode>(parseResult) ==
            GlobNode(makeNode<ColumnRefNode>("name"), makeNode<StringLiteralNode>("'*foo*'"), true, {}));
}

TEST_CASE("parser: IS NULL in AND expression") {
    auto parseResult = parse("a IS NULL AND b IS NOT NULL");
    REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::logicalAnd,
        std::make_unique<IsNullNode>(makeNode<ColumnRefNode>("a"), SourceLocation{}),
        std::make_unique<IsNotNullNode>(makeNode<ColumnRefNode>("b"), SourceLocation{}),
        {}));
}

// --- Phase 7: Functions ---

TEST_CASE("parser: function call - no args") {
    auto parseResult = parse("random()");
    REQUIRE(requireNode<FunctionCallNode>(parseResult) ==
            *makeFunc("random", false, false));
}

TEST_CASE("parser: function call - one arg") {
    auto parseResult = parse("abs(a)");
    REQUIRE(requireNode<FunctionCallNode>(parseResult) ==
            *makeFunc("abs", false, false, makeNode<ColumnRefNode>("a")));
}

TEST_CASE("parser: function call - multiple args") {
    auto parseResult = parse("coalesce(a, b, 0)");
    REQUIRE(requireNode<FunctionCallNode>(parseResult) ==
            *makeFunc("coalesce", false, false,
                        makeNode<ColumnRefNode>("a"),
                        makeNode<ColumnRefNode>("b"),
                        makeNode<IntegerLiteralNode>("0")));
}

TEST_CASE("parser: function call - count(*)") {
    auto parseResult = parse("count(*)");
    REQUIRE(requireNode<FunctionCallNode>(parseResult) ==
            *makeFunc("count", false, true));
}

TEST_CASE("parser: function call - count(DISTINCT expr)") {
    auto parseResult = parse("count(DISTINCT name)");
    REQUIRE(requireNode<FunctionCallNode>(parseResult) ==
            *makeFunc("count", true, false, makeNode<ColumnRefNode>("name")));
}

TEST_CASE("parser: function call - nested") {
    auto parseResult = parse("abs(round(x, 2))");
    REQUIRE(requireNode<FunctionCallNode>(parseResult) ==
            *makeFunc("abs", false, false,
                        makeFunc("round", false, false,
                                   makeNode<ColumnRefNode>("x"),
                                   makeNode<IntegerLiteralNode>("2"))));
}

TEST_CASE("parser: function call - expression arg") {
    auto parseResult = parse("round(a + b, 2)");
    REQUIRE(requireNode<FunctionCallNode>(parseResult) ==
            *makeFunc("round", false, false,
                        makeNode<BinaryOperatorNode>(BinaryOperator::add,
                            makeNode<ColumnRefNode>("a"), makeNode<ColumnRefNode>("b")),
                        makeNode<IntegerLiteralNode>("2")));
}

TEST_CASE("parser: keyword as function - replace") {
    auto parseResult = parse("replace(name, 'foo', 'bar')");
    REQUIRE(requireNode<FunctionCallNode>(parseResult) ==
            *makeFunc("replace", false, false,
                        makeNode<ColumnRefNode>("name"),
                        makeNode<StringLiteralNode>("'foo'"),
                        makeNode<StringLiteralNode>("'bar'")));
}

TEST_CASE("parser: keyword as function - case insensitive", "[function]") {
    auto funcName = GENERATE("ABS", "Abs", "abs");
    CAPTURE(funcName);
    std::string sql = std::string(funcName) + "(x)";
    auto parseResult = parse(sql);
    REQUIRE(requireNode<FunctionCallNode>(parseResult) ==
            *makeFunc(funcName, false, false, makeNode<ColumnRefNode>("x")));
}

TEST_CASE("parser: parenthesized expression") {
    auto parseResult = parse("(a + b) * c");
    REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::multiply,
        makeNode<BinaryOperatorNode>(BinaryOperator::add,
            makeNode<ColumnRefNode>("a"), makeNode<ColumnRefNode>("b")),
        makeNode<ColumnRefNode>("c"),
        {}));
}

TEST_CASE("parser: nested parentheses") {
    auto parseResult = parse("((a))");
    REQUIRE(requireNode<ColumnRefNode>(parseResult) == ColumnRefNode("a", {}));
}

TEST_CASE("parser: function in expression") {
    auto parseResult = parse("abs(a) + length(b)");
    REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::add,
        makeFunc("abs", false, false, makeNode<ColumnRefNode>("a")),
        makeFunc("length", false, false, makeNode<ColumnRefNode>("b")),
        {}));
}

TEST_CASE("parser: function with IS NULL") {
    auto parseResult = parse("length(name) IS NULL");
    REQUIRE(requireNode<IsNullNode>(parseResult) == IsNullNode(
        makeFunc("length", false, false, makeNode<ColumnRefNode>("name")),
        {}));
}

// --- Phase 8: CAST and CASE ---

TEST_CASE("parser: CAST with simple type") {
    auto parseResult = parse("CAST(a AS INTEGER)");
    REQUIRE(requireNode<CastNode>(parseResult) == CastNode(
        makeNode<ColumnRefNode>("a"), "INTEGER", {}));
}

TEST_CASE("parser: CAST with type and size") {
    auto parseResult = parse("CAST(name AS VARCHAR(255))");
    REQUIRE(requireNode<CastNode>(parseResult) == CastNode(
        makeNode<ColumnRefNode>("name"), "VARCHAR(255)", {}));
}

TEST_CASE("parser: CAST with multi-word type") {
    auto parseResult = parse("CAST(x AS UNSIGNED BIG INT)");
    REQUIRE(requireNode<CastNode>(parseResult) == CastNode(
        makeNode<ColumnRefNode>("x"), "UNSIGNED BIG INT", {}));
}

TEST_CASE("parser: CAST with expression operand") {
    auto parseResult = parse("CAST(a + b AS REAL)");
    REQUIRE(requireNode<CastNode>(parseResult) ==
            CastNode(makeNode<BinaryOperatorNode>(BinaryOperator::add,
                         makeNode<ColumnRefNode>("a"),
                         makeNode<ColumnRefNode>("b")),
                     "REAL",
                     {}));
}

TEST_CASE("parser: searched CASE") {
    auto parseResult = parse("CASE WHEN a > 0 THEN 'pos' ELSE 'neg' END");
    std::vector<CaseBranch> branches;
    branches.push_back(CaseBranch{
        makeNode<BinaryOperatorNode>(BinaryOperator::greaterThan, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("0")),
        makeNode<StringLiteralNode>("'pos'")});
    REQUIRE(requireNode<CaseNode>(parseResult) ==
            CaseNode(nullptr, std::move(branches), makeNode<StringLiteralNode>("'neg'"), {}));
}

TEST_CASE("parser: simple CASE") {
    auto parseResult = parse("CASE status WHEN 1 THEN 'on' WHEN 0 THEN 'off' END");
    std::vector<CaseBranch> branches;
    branches.push_back(CaseBranch{makeNode<IntegerLiteralNode>("1"), makeNode<StringLiteralNode>("'on'")});
    branches.push_back(CaseBranch{makeNode<IntegerLiteralNode>("0"), makeNode<StringLiteralNode>("'off'")});
    REQUIRE(requireNode<CaseNode>(parseResult) ==
            CaseNode(makeNode<ColumnRefNode>("status"), std::move(branches), nullptr, {}));
}

TEST_CASE("parser: CASE with multiple WHEN and ELSE") {
    auto parseResult = parse("CASE WHEN a = 1 THEN 'one' WHEN a = 2 THEN 'two' ELSE 'other' END");
    std::vector<CaseBranch> branches;
    branches.push_back(CaseBranch{
        makeNode<BinaryOperatorNode>(BinaryOperator::equals, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("1")),
        makeNode<StringLiteralNode>("'one'")});
    branches.push_back(CaseBranch{
        makeNode<BinaryOperatorNode>(BinaryOperator::equals, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("2")),
        makeNode<StringLiteralNode>("'two'")});
    REQUIRE(requireNode<CaseNode>(parseResult) ==
            CaseNode(nullptr, std::move(branches), makeNode<StringLiteralNode>("'other'"), {}));
}

TEST_CASE("parser: CASE in expression") {
    auto parseResult = parse("CASE WHEN a > 0 THEN 1 ELSE 0 END + 10");
    std::vector<CaseBranch> branches;
    branches.push_back(CaseBranch{
        makeNode<BinaryOperatorNode>(BinaryOperator::greaterThan, makeNode<ColumnRefNode>("a"), makeNode<IntegerLiteralNode>("0")),
        makeNode<IntegerLiteralNode>("1")});
    REQUIRE(requireNode<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::add,
        makeNode<CaseNode>(nullptr, std::move(branches), makeNode<IntegerLiteralNode>("0")),
        makeNode<IntegerLiteralNode>("10"),
        {}));
}

// --- Errors ---

TEST_CASE("parser: error on unexpected token") {
    auto parseResult = parse("DELETE");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

TEST_CASE("parser: error on trailing tokens") {
    auto parseResult = parse("42 hello");
    REQUIRE_FALSE(parseResult);
    REQUIRE(parseResult.errors.size() == 1);
}

// --- IS / IS NOT / IS [NOT] DISTINCT FROM ---

TEST_CASE("parser: IS expr") {
    auto parseResult = parse("SELECT 1 IS 2");
    SelectNode expected({});
    expected.columns = {SelectColumn{std::make_shared<BinaryOperatorNode>(
        BinaryOperator::isOp,
        makeNode<IntegerLiteralNode>("1"),
        makeNode<IntegerLiteralNode>("2"), SourceLocation{}), ""}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: IS NOT expr") {
    auto parseResult = parse("SELECT a IS NOT b FROM t");
    SelectNode expected({});
    expected.columns = {SelectColumn{std::make_shared<BinaryOperatorNode>(
        BinaryOperator::isNot,
        makeNode<ColumnRefNode>("a"),
        makeNode<ColumnRefNode>("b"), SourceLocation{}), ""}};
    expected.fromClause = fromOne("t");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: IS DISTINCT FROM") {
    auto parseResult = parse("SELECT a IS DISTINCT FROM b FROM t");
    SelectNode expected({});
    expected.columns = {SelectColumn{std::make_shared<BinaryOperatorNode>(
        BinaryOperator::isDistinctFrom,
        makeNode<ColumnRefNode>("a"),
        makeNode<ColumnRefNode>("b"), SourceLocation{}), ""}};
    expected.fromClause = fromOne("t");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: IS NOT DISTINCT FROM") {
    auto parseResult = parse("SELECT a IS NOT DISTINCT FROM b FROM t");
    SelectNode expected({});
    expected.columns = {SelectColumn{std::make_shared<BinaryOperatorNode>(
        BinaryOperator::isNotDistinctFrom,
        makeNode<ColumnRefNode>("a"),
        makeNode<ColumnRefNode>("b"), SourceLocation{}), ""}};
    expected.fromClause = fromOne("t");
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}

// --- COLLATE ---

TEST_CASE("parser: expr COLLATE name") {
    auto parseResult = parse("SELECT name COLLATE NOCASE FROM t");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.columns.size() == 1);
    auto* collate = dynamic_cast<const CollateNode*>(sel.columns.at(0).expression.get());
    REQUIRE(collate != nullptr);
    REQUIRE(*collate == CollateNode(makeNode<ColumnRefNode>("name"), "NOCASE", {}));
}

// --- JSON operators ---

TEST_CASE("parser: JSON -> operator") {
    auto parseResult = parse("SELECT data -> '$.name' FROM t");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.columns.size() == 1);
    auto* binOp = dynamic_cast<const BinaryOperatorNode*>(sel.columns.at(0).expression.get());
    REQUIRE(binOp != nullptr);
    REQUIRE(*binOp == BinaryOperatorNode(
        BinaryOperator::jsonArrow, makeNode<ColumnRefNode>("data"), makeNode<StringLiteralNode>("'$.name'"), {}));
}

TEST_CASE("parser: JSON ->> operator") {
    auto parseResult = parse("SELECT data ->> '$.name' FROM t");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.columns.size() == 1);
    auto* binOp = dynamic_cast<const BinaryOperatorNode*>(sel.columns.at(0).expression.get());
    REQUIRE(binOp != nullptr);
    REQUIRE(*binOp == BinaryOperatorNode(
        BinaryOperator::jsonArrow2, makeNode<ColumnRefNode>("data"), makeNode<StringLiteralNode>("'$.name'"), {}));
}

// --- Bind parameters ---

TEST_CASE("parser: bind parameter ?") {
    auto parseResult = parse("SELECT ? FROM t");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.columns.size() == 1);
    auto* bind = dynamic_cast<const BindParameterNode*>(sel.columns.at(0).expression.get());
    REQUIRE(bind != nullptr);
    REQUIRE(*bind == BindParameterNode("?", {}));
}

TEST_CASE("parser: bind parameter :name") {
    auto parseResult = parse("SELECT :id FROM t");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    REQUIRE(sel.columns.size() == 1);
    auto* bind = dynamic_cast<const BindParameterNode*>(sel.columns.at(0).expression.get());
    REQUIRE(bind != nullptr);
    REQUIRE(*bind == BindParameterNode(":id", {}));
}

// --- IN table-name ---

TEST_CASE("parser: IN table-name") {
    auto parseResult = parse("SELECT * FROM t WHERE x IN tbl");
    REQUIRE(parseResult);
    const auto& sel = requireNode<SelectNode>(parseResult);
    auto* inNode = dynamic_cast<const InNode*>(sel.whereClause.get());
    REQUIRE(inNode != nullptr);
    REQUIRE(*inNode == InNode(makeNode<ColumnRefNode>("x"), std::string("tbl"), false, {}));
}
