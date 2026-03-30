#include "parser_tests_common.hpp"
using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: integer literal") {
    SECTION("simple") {
        auto parseResult = parse("42");
        REQUIRE(require_node<IntegerLiteralNode>(parseResult) == IntegerLiteralNode("42", {}));
    }
    SECTION("zero") {
        auto parseResult = parse("0");
        REQUIRE(require_node<IntegerLiteralNode>(parseResult) == IntegerLiteralNode("0", {}));
    }
    SECTION("hex") {
        auto parseResult = parse("0xFF");
        REQUIRE(require_node<IntegerLiteralNode>(parseResult) == IntegerLiteralNode("0xFF", {}));
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
        REQUIRE(require_node<RealLiteralNode>(parseResult) == RealLiteralNode("3.14", {}));
    }
    SECTION("starting with dot") {
        auto parseResult = parse(".5");
        REQUIRE(require_node<RealLiteralNode>(parseResult) == RealLiteralNode(".5", {}));
    }
    SECTION("with exponent") {
        auto parseResult = parse("1e10");
        REQUIRE(require_node<RealLiteralNode>(parseResult) == RealLiteralNode("1e10", {}));
    }
}

TEST_CASE("parser: string literal") {
    SECTION("simple") {
        auto parseResult = parse("'hello'");
        REQUIRE(require_node<StringLiteralNode>(parseResult) == StringLiteralNode("'hello'", {}));
    }
    SECTION("escaped quote") {
        auto parseResult = parse("'it''s'");
        REQUIRE(require_node<StringLiteralNode>(parseResult) == StringLiteralNode("'it''s'", {}));
    }
    SECTION("empty") {
        auto parseResult = parse("''");
        REQUIRE(require_node<StringLiteralNode>(parseResult) == StringLiteralNode("''", {}));
    }
}

TEST_CASE("parser: null literal") {
    auto input = GENERATE("NULL", "null", "Null");
    auto parseResult = parse(input);
    require_node<NullLiteralNode>(parseResult);
}

TEST_CASE("parser: bool literal") {
    SECTION("true") {
        auto parseResult = parse("TRUE");
        REQUIRE(require_node<BoolLiteralNode>(parseResult) == BoolLiteralNode(true, {}));
    }
    SECTION("false") {
        auto parseResult = parse("FALSE");
        REQUIRE(require_node<BoolLiteralNode>(parseResult) == BoolLiteralNode(false, {}));
    }
}

TEST_CASE("parser: blob literal") {
    auto parseResult = parse("X'48656C6C6F'");
    REQUIRE(require_node<BlobLiteralNode>(parseResult) == BlobLiteralNode("X'48656C6C6F'", {}));
}

TEST_CASE("parser: column ref") {
    SECTION("unqualified") {
        auto parseResult = parse("name");
        REQUIRE(require_node<ColumnRefNode>(parseResult) == ColumnRefNode("name", {}));
    }
    SECTION("quoted identifier") {
        auto parseResult = parse(R"("my column")");
        REQUIRE(require_node<ColumnRefNode>(parseResult) == ColumnRefNode(R"("my column")", {}));
    }
}

TEST_CASE("parser: qualified column ref") {
    SECTION("table.column") {
        auto parseResult = parse("users.name");
        REQUIRE(require_node<QualifiedColumnRefNode>(parseResult) == QualifiedColumnRefNode("users", "name", {}));
    }
    SECTION("quoted table and column") {
        auto parseResult = parse(R"("my table"."my column")");
        REQUIRE(require_node<QualifiedColumnRefNode>(parseResult) == QualifiedColumnRefNode(R"("my table")", R"("my column")", {}));
    }
    SECTION("schema.table.column") {
        auto parseResult = parse("main.users.id");
        REQUIRE(require_node<QualifiedColumnRefNode>(parseResult) ==
                QualifiedColumnRefNode("main", "users", "id", {}));
    }
}

TEST_CASE("parser: CURRENT_TIME / CURRENT_DATE / CURRENT_TIMESTAMP") {
    REQUIRE(require_node<CurrentDatetimeLiteralNode>(parse("CURRENT_TIME")) ==
            CurrentDatetimeLiteralNode(CurrentDatetimeKind::time, {}));
    REQUIRE(require_node<CurrentDatetimeLiteralNode>(parse("CURRENT_DATE")) ==
            CurrentDatetimeLiteralNode(CurrentDatetimeKind::date, {}));
    REQUIRE(require_node<CurrentDatetimeLiteralNode>(parse("CURRENT_TIMESTAMP")) ==
            CurrentDatetimeLiteralNode(CurrentDatetimeKind::timestamp, {}));
}

TEST_CASE("parser: window function OVER") {
    auto parseResult = parse("row_number() OVER (PARTITION BY a ORDER BY b DESC)");
    REQUIRE(parseResult);
    FunctionCallNode expected("row_number", std::vector<AstNodePointer>{}, false, false, SourceLocation{});
    expected.over = std::make_unique<OverClause>();
    expected.over->partitionBy.push_back(make_node<ColumnRefNode>("a"));
    expected.over->orderBy.push_back(
        OrderByTerm{make_shared_node<ColumnRefNode>("b"), SortDirection::desc});
    REQUIRE(require_node<FunctionCallNode>(parseResult) == expected);
}

TEST_CASE("parser: aggregate FILTER (WHERE …) before OVER") {
    auto parseResult = parse("count(*) FILTER (WHERE x > 0) OVER (ORDER BY y)");
    REQUIRE(parseResult);
    FunctionCallNode expected("count", std::vector<AstNodePointer>{}, false, true, SourceLocation{});
    expected.filterWhere = std::make_unique<BinaryOperatorNode>(
        BinaryOperator::greaterThan,
        make_node<ColumnRefNode>("x"),
        make_node<IntegerLiteralNode>("0"),
        SourceLocation{});
    expected.over = std::make_unique<OverClause>();
    expected.over->orderBy.push_back(OrderByTerm{make_shared_node<ColumnRefNode>("y"), SortDirection::none});
    REQUIRE(require_node<FunctionCallNode>(parseResult) == expected);
}

TEST_CASE("parser: NEW ref") {
    auto input = GENERATE("NEW.col", "new.col", "New.col");
    auto parseResult = parse(input);
    REQUIRE(require_node<NewRefNode>(parseResult) == NewRefNode("col", {}));
}

TEST_CASE("parser: OLD ref") {
    auto input = GENERATE("OLD.col", "old.col", "Old.col");
    auto parseResult = parse(input);
    REQUIRE(require_node<OldRefNode>(parseResult) == OldRefNode("col", {}));
}

TEST_CASE("parser: binary comparison") {
    SECTION("equals") {
        auto parseResult = parse("a = 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::equals, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("double equals") {
        auto parseResult = parse("a == 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::equals, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("not equals !=") {
        auto parseResult = parse("a != 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::notEquals, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("not equals <>") {
        auto parseResult = parse("a <> 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::notEquals, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("less than") {
        auto parseResult = parse("a < 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::lessThan, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("less or equal") {
        auto parseResult = parse("a <= 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::lessOrEqual, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("greater than") {
        auto parseResult = parse("a > 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::greaterThan, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("greater or equal") {
        auto parseResult = parse("a >= 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::greaterOrEqual, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("column vs string") {
        auto parseResult = parse("name = 'hello'");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::equals, make_node<ColumnRefNode>("name"), make_node<StringLiteralNode>("'hello'"), {}));
    }
    SECTION("qualified column ref as operand") {
        auto parseResult = parse("users.id = 42");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::equals, make_node<QualifiedColumnRefNode>("users", "id"), make_node<IntegerLiteralNode>("42"), {}));
    }
    SECTION("standalone literal without operator remains literal") {
        auto parseResult = parse("42");
        require_node<IntegerLiteralNode>(parseResult);
    }
}

TEST_CASE("parser: arithmetic operators") {
    SECTION("add") {
        auto parseResult = parse("a + 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::add, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("subtract") {
        auto parseResult = parse("a - 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::subtract, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("multiply") {
        auto parseResult = parse("a * 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::multiply, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("divide") {
        auto parseResult = parse("a / 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::divide, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("modulo") {
        auto parseResult = parse("a % 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::modulo, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
}

TEST_CASE("parser: concatenation") {
    auto parseResult = parse("a || b");
    REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::concatenate, make_node<ColumnRefNode>("a"), make_node<ColumnRefNode>("b"), {}));
}

TEST_CASE("parser: bitwise operators") {
    SECTION("bitwise and") {
        auto parseResult = parse("a & 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::bitwiseAnd, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("bitwise or") {
        auto parseResult = parse("a | 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::bitwiseOr, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("shift left") {
        auto parseResult = parse("a << 2");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::shiftLeft, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("2"), {}));
    }
    SECTION("shift right") {
        auto parseResult = parse("a >> 2");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::shiftRight, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("2"), {}));
    }
}

TEST_CASE("parser: unary operators") {
    SECTION("unary minus on literal") {
        auto parseResult = parse("-5");
        REQUIRE(require_node<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
            UnaryOperator::minus, make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("unary minus on column") {
        auto parseResult = parse("-a");
        REQUIRE(require_node<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
            UnaryOperator::minus, make_node<ColumnRefNode>("a"), {}));
    }
    SECTION("unary plus") {
        auto parseResult = parse("+5");
        REQUIRE(require_node<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
            UnaryOperator::plus, make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("bitwise not") {
        auto parseResult = parse("~5");
        REQUIRE(require_node<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
            UnaryOperator::bitwiseNot, make_node<IntegerLiteralNode>("5"), {}));
    }
    SECTION("double unary minus") {
        auto parseResult = parse("- -5");
        REQUIRE(require_node<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
            UnaryOperator::minus,
            std::make_unique<UnaryOperatorNode>(UnaryOperator::minus, make_node<IntegerLiteralNode>("5"), SourceLocation{}),
            {}));
    }
}

TEST_CASE("parser: operator precedence") {
    SECTION("multiply before add: a + b * c") {
        auto parseResult = parse("a + b * c");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::add,
            make_node<ColumnRefNode>("a"),
            std::make_unique<BinaryOperatorNode>(BinaryOperator::multiply,
                make_node<ColumnRefNode>("b"), make_node<ColumnRefNode>("c"), SourceLocation{}),
            {}));
    }
    SECTION("multiply before add: a * b + c") {
        auto parseResult = parse("a * b + c");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::add,
            std::make_unique<BinaryOperatorNode>(BinaryOperator::multiply,
                make_node<ColumnRefNode>("a"), make_node<ColumnRefNode>("b"), SourceLocation{}),
            make_node<ColumnRefNode>("c"),
            {}));
    }
    SECTION("comparison lower than arithmetic: a = b + 5") {
        auto parseResult = parse("a = b + 5");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::equals,
            make_node<ColumnRefNode>("a"),
            std::make_unique<BinaryOperatorNode>(BinaryOperator::add,
                make_node<ColumnRefNode>("b"), make_node<IntegerLiteralNode>("5"), SourceLocation{}),
            {}));
    }
    SECTION("concat highest binary: a || b * c is (a || b) * c") {
        auto parseResult = parse("a || b * c");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::multiply,
            std::make_unique<BinaryOperatorNode>(BinaryOperator::concatenate,
                make_node<ColumnRefNode>("a"), make_node<ColumnRefNode>("b"), SourceLocation{}),
            make_node<ColumnRefNode>("c"),
            {}));
    }
    SECTION("unary minus binds tighter than binary: -a + b") {
        auto parseResult = parse("-a + b");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::add,
            std::make_unique<UnaryOperatorNode>(UnaryOperator::minus, make_node<ColumnRefNode>("a"), SourceLocation{}),
            make_node<ColumnRefNode>("b"),
            {}));
    }
    SECTION("binary minus with unary minus operand: a - -b") {
        auto parseResult = parse("a - -b");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::subtract,
            make_node<ColumnRefNode>("a"),
            std::make_unique<UnaryOperatorNode>(UnaryOperator::minus, make_node<ColumnRefNode>("b"), SourceLocation{}),
            {}));
    }
}

TEST_CASE("parser: left associativity") {
    SECTION("a + b + c = (a + b) + c") {
        auto parseResult = parse("a + b + c");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::add,
            std::make_unique<BinaryOperatorNode>(BinaryOperator::add,
                make_node<ColumnRefNode>("a"), make_node<ColumnRefNode>("b"), SourceLocation{}),
            make_node<ColumnRefNode>("c"),
            {}));
    }
    SECTION("a || b || c = (a || b) || c") {
        auto parseResult = parse("a || b || c");
        REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
            BinaryOperator::concatenate,
            std::make_unique<BinaryOperatorNode>(BinaryOperator::concatenate,
                make_node<ColumnRefNode>("a"), make_node<ColumnRefNode>("b"), SourceLocation{}),
            make_node<ColumnRefNode>("c"),
            {}));
    }
}

TEST_CASE("parser: logical AND") {
    auto parseResult = parse("a = 1 AND b = 2");
    REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::logicalAnd,
        std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
            make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("1"), SourceLocation{}),
        std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
            make_node<ColumnRefNode>("b"), make_node<IntegerLiteralNode>("2"), SourceLocation{}),
        {}));
}

TEST_CASE("parser: logical OR") {
    auto parseResult = parse("a = 1 OR b = 2");
    REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::logicalOr,
        std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
            make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("1"), SourceLocation{}),
        std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
            make_node<ColumnRefNode>("b"), make_node<IntegerLiteralNode>("2"), SourceLocation{}),
        {}));
}

TEST_CASE("parser: logical NOT") {
    auto parseResult = parse("NOT a");
    REQUIRE(require_node<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
        UnaryOperator::logicalNot, make_node<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: NOT case insensitive") {
    auto input = GENERATE("NOT a", "not a", "Not a");
    auto parseResult = parse(input);
    REQUIRE(require_node<UnaryOperatorNode>(parseResult) == UnaryOperatorNode(
        UnaryOperator::logicalNot, make_node<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: AND binds tighter than OR") {
    auto parseResult = parse("a = 1 OR b = 2 AND c = 3");
    REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::logicalOr,
        std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
            make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("1"), SourceLocation{}),
        std::make_unique<BinaryOperatorNode>(BinaryOperator::logicalAnd,
            std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
                make_node<ColumnRefNode>("b"), make_node<IntegerLiteralNode>("2"), SourceLocation{}),
            std::make_unique<BinaryOperatorNode>(BinaryOperator::equals,
                make_node<ColumnRefNode>("c"), make_node<IntegerLiteralNode>("3"), SourceLocation{}),
            SourceLocation{}),
        {}));
}

TEST_CASE("parser: NOT binds tighter than AND") {
    auto parseResult = parse("NOT a AND b");
    REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::logicalAnd,
        std::make_unique<UnaryOperatorNode>(UnaryOperator::logicalNot, make_node<ColumnRefNode>("a"), SourceLocation{}),
        make_node<ColumnRefNode>("b"),
        {}));
}

TEST_CASE("parser: IS NULL") {
    auto parseResult = parse("a IS NULL");
    REQUIRE(require_node<IsNullNode>(parseResult) == IsNullNode(
        make_node<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: IS NOT NULL") {
    auto parseResult = parse("a IS NOT NULL");
    REQUIRE(require_node<IsNotNullNode>(parseResult) == IsNotNullNode(
        make_node<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: ISNULL keyword") {
    auto parseResult = parse("a ISNULL");
    REQUIRE(require_node<IsNullNode>(parseResult) == IsNullNode(
        make_node<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: NOTNULL keyword") {
    auto parseResult = parse("a NOTNULL");
    REQUIRE(require_node<IsNotNullNode>(parseResult) == IsNotNullNode(
        make_node<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: NOT NULL (two keywords)") {
    auto parseResult = parse("a NOT NULL");
    REQUIRE(require_node<IsNotNullNode>(parseResult) == IsNotNullNode(
        make_node<ColumnRefNode>("a"), {}));
}

TEST_CASE("parser: BETWEEN") {
    auto parseResult = parse("a BETWEEN 1 AND 10");
    REQUIRE(require_node<BetweenNode>(parseResult) == BetweenNode(
        make_node<ColumnRefNode>("a"),
        make_node<IntegerLiteralNode>("1"),
        make_node<IntegerLiteralNode>("10"),
        false, {}));
}

TEST_CASE("parser: NOT BETWEEN") {
    auto parseResult = parse("a NOT BETWEEN 1 AND 10");
    REQUIRE(require_node<BetweenNode>(parseResult) == BetweenNode(
        make_node<ColumnRefNode>("a"),
        make_node<IntegerLiteralNode>("1"),
        make_node<IntegerLiteralNode>("10"),
        true, {}));
}

TEST_CASE("parser: BETWEEN with AND precedence") {
    auto parseResult = parse("a BETWEEN 1 AND 10 AND b > 5");
    REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::logicalAnd,
        std::make_unique<BetweenNode>(
            make_node<ColumnRefNode>("a"),
            make_node<IntegerLiteralNode>("1"),
            make_node<IntegerLiteralNode>("10"),
            false, SourceLocation{}),
        std::make_unique<BinaryOperatorNode>(BinaryOperator::greaterThan,
            make_node<ColumnRefNode>("b"), make_node<IntegerLiteralNode>("5"), SourceLocation{}),
        {}));
}

TEST_CASE("parser: IN with values") {
    auto parseResult = parse("a IN (1, 2, 3)");
    std::vector<AstNodePointer> values;
    values.push_back(make_node<IntegerLiteralNode>("1"));
    values.push_back(make_node<IntegerLiteralNode>("2"));
    values.push_back(make_node<IntegerLiteralNode>("3"));
    REQUIRE(require_node<InNode>(parseResult) ==
            InNode(make_node<ColumnRefNode>("a"), std::move(values), nullptr, false, {}));
}

TEST_CASE("parser: NOT IN") {
    auto parseResult = parse("a NOT IN (1, 2)");
    std::vector<AstNodePointer> values;
    values.push_back(make_node<IntegerLiteralNode>("1"));
    values.push_back(make_node<IntegerLiteralNode>("2"));
    REQUIRE(require_node<InNode>(parseResult) ==
            InNode(make_node<ColumnRefNode>("a"), std::move(values), nullptr, true, {}));
}

TEST_CASE("parser: IN with empty list") {
    auto parseResult = parse("a IN ()");
    REQUIRE(require_node<InNode>(parseResult) ==
            InNode(make_node<ColumnRefNode>("a"), {}, nullptr, false, {}));
}


TEST_CASE("parser: LIKE") {
    auto parseResult = parse("name LIKE '%foo%'");
    REQUIRE(require_node<LikeNode>(parseResult) ==
            LikeNode(make_node<ColumnRefNode>("name"), make_node<StringLiteralNode>("'%foo%'"), nullptr, false, {}));
}

TEST_CASE("parser: LIKE with ESCAPE") {
    auto parseResult = parse("name LIKE '%\\%%' ESCAPE '\\'");
    REQUIRE(require_node<LikeNode>(parseResult) ==
            LikeNode(make_node<ColumnRefNode>("name"),
                     make_node<StringLiteralNode>("'%\\%%'"),
                     make_node<StringLiteralNode>("'\\'"),
                     false,
                     {}));
}

TEST_CASE("parser: NOT LIKE") {
    auto parseResult = parse("name NOT LIKE '%foo%'");
    REQUIRE(require_node<LikeNode>(parseResult) ==
            LikeNode(make_node<ColumnRefNode>("name"), make_node<StringLiteralNode>("'%foo%'"), nullptr, true, {}));
}

TEST_CASE("parser: GLOB") {
    auto parseResult = parse("name GLOB '*foo*'");
    REQUIRE(require_node<GlobNode>(parseResult) ==
            GlobNode(make_node<ColumnRefNode>("name"), make_node<StringLiteralNode>("'*foo*'"), false, {}));
}

TEST_CASE("parser: NOT GLOB") {
    auto parseResult = parse("name NOT GLOB '*foo*'");
    REQUIRE(require_node<GlobNode>(parseResult) ==
            GlobNode(make_node<ColumnRefNode>("name"), make_node<StringLiteralNode>("'*foo*'"), true, {}));
}

TEST_CASE("parser: IS NULL in AND expression") {
    auto parseResult = parse("a IS NULL AND b IS NOT NULL");
    REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::logicalAnd,
        std::make_unique<IsNullNode>(make_node<ColumnRefNode>("a"), SourceLocation{}),
        std::make_unique<IsNotNullNode>(make_node<ColumnRefNode>("b"), SourceLocation{}),
        {}));
}

// --- Phase 7: Functions ---

TEST_CASE("parser: function call - no args") {
    auto parseResult = parse("random()");
    REQUIRE(require_node<FunctionCallNode>(parseResult) ==
            *make_func("random", false, false));
}

TEST_CASE("parser: function call - one arg") {
    auto parseResult = parse("abs(a)");
    REQUIRE(require_node<FunctionCallNode>(parseResult) ==
            *make_func("abs", false, false, make_node<ColumnRefNode>("a")));
}

TEST_CASE("parser: function call - multiple args") {
    auto parseResult = parse("coalesce(a, b, 0)");
    REQUIRE(require_node<FunctionCallNode>(parseResult) ==
            *make_func("coalesce", false, false,
                        make_node<ColumnRefNode>("a"),
                        make_node<ColumnRefNode>("b"),
                        make_node<IntegerLiteralNode>("0")));
}

TEST_CASE("parser: function call - count(*)") {
    auto parseResult = parse("count(*)");
    REQUIRE(require_node<FunctionCallNode>(parseResult) ==
            *make_func("count", false, true));
}

TEST_CASE("parser: function call - count(DISTINCT expr)") {
    auto parseResult = parse("count(DISTINCT name)");
    REQUIRE(require_node<FunctionCallNode>(parseResult) ==
            *make_func("count", true, false, make_node<ColumnRefNode>("name")));
}

TEST_CASE("parser: function call - nested") {
    auto parseResult = parse("abs(round(x, 2))");
    REQUIRE(require_node<FunctionCallNode>(parseResult) ==
            *make_func("abs", false, false,
                        make_func("round", false, false,
                                   make_node<ColumnRefNode>("x"),
                                   make_node<IntegerLiteralNode>("2"))));
}

TEST_CASE("parser: function call - expression arg") {
    auto parseResult = parse("round(a + b, 2)");
    REQUIRE(require_node<FunctionCallNode>(parseResult) ==
            *make_func("round", false, false,
                        make_node<BinaryOperatorNode>(BinaryOperator::add,
                            make_node<ColumnRefNode>("a"), make_node<ColumnRefNode>("b")),
                        make_node<IntegerLiteralNode>("2")));
}

TEST_CASE("parser: keyword as function - replace") {
    auto parseResult = parse("replace(name, 'foo', 'bar')");
    REQUIRE(require_node<FunctionCallNode>(parseResult) ==
            *make_func("replace", false, false,
                        make_node<ColumnRefNode>("name"),
                        make_node<StringLiteralNode>("'foo'"),
                        make_node<StringLiteralNode>("'bar'")));
}

TEST_CASE("parser: keyword as function - case insensitive", "[function]") {
    auto funcName = GENERATE("ABS", "Abs", "abs");
    CAPTURE(funcName);
    std::string sql = std::string(funcName) + "(x)";
    auto parseResult = parse(sql);
    REQUIRE(require_node<FunctionCallNode>(parseResult) ==
            *make_func(funcName, false, false, make_node<ColumnRefNode>("x")));
}

TEST_CASE("parser: parenthesized expression") {
    auto parseResult = parse("(a + b) * c");
    REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::multiply,
        make_node<BinaryOperatorNode>(BinaryOperator::add,
            make_node<ColumnRefNode>("a"), make_node<ColumnRefNode>("b")),
        make_node<ColumnRefNode>("c"),
        {}));
}

TEST_CASE("parser: nested parentheses") {
    auto parseResult = parse("((a))");
    REQUIRE(require_node<ColumnRefNode>(parseResult) == ColumnRefNode("a", {}));
}

TEST_CASE("parser: function in expression") {
    auto parseResult = parse("abs(a) + length(b)");
    REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::add,
        make_func("abs", false, false, make_node<ColumnRefNode>("a")),
        make_func("length", false, false, make_node<ColumnRefNode>("b")),
        {}));
}

TEST_CASE("parser: function with IS NULL") {
    auto parseResult = parse("length(name) IS NULL");
    REQUIRE(require_node<IsNullNode>(parseResult) == IsNullNode(
        make_func("length", false, false, make_node<ColumnRefNode>("name")),
        {}));
}

// --- Phase 8: CAST and CASE ---

TEST_CASE("parser: CAST with simple type") {
    auto parseResult = parse("CAST(a AS INTEGER)");
    REQUIRE(require_node<CastNode>(parseResult) == CastNode(
        make_node<ColumnRefNode>("a"), "INTEGER", {}));
}

TEST_CASE("parser: CAST with type and size") {
    auto parseResult = parse("CAST(name AS VARCHAR(255))");
    REQUIRE(require_node<CastNode>(parseResult) == CastNode(
        make_node<ColumnRefNode>("name"), "VARCHAR(255)", {}));
}

TEST_CASE("parser: CAST with multi-word type") {
    auto parseResult = parse("CAST(x AS UNSIGNED BIG INT)");
    REQUIRE(require_node<CastNode>(parseResult) == CastNode(
        make_node<ColumnRefNode>("x"), "UNSIGNED BIG INT", {}));
}

TEST_CASE("parser: CAST with expression operand") {
    auto parseResult = parse("CAST(a + b AS REAL)");
    REQUIRE(require_node<CastNode>(parseResult) ==
            CastNode(make_node<BinaryOperatorNode>(BinaryOperator::add,
                         make_node<ColumnRefNode>("a"),
                         make_node<ColumnRefNode>("b")),
                     "REAL",
                     {}));
}

TEST_CASE("parser: searched CASE") {
    auto parseResult = parse("CASE WHEN a > 0 THEN 'pos' ELSE 'neg' END");
    std::vector<CaseBranch> branches;
    branches.push_back(CaseBranch{
        make_node<BinaryOperatorNode>(BinaryOperator::greaterThan, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("0")),
        make_node<StringLiteralNode>("'pos'")});
    REQUIRE(require_node<CaseNode>(parseResult) ==
            CaseNode(nullptr, std::move(branches), make_node<StringLiteralNode>("'neg'"), {}));
}

TEST_CASE("parser: simple CASE") {
    auto parseResult = parse("CASE status WHEN 1 THEN 'on' WHEN 0 THEN 'off' END");
    std::vector<CaseBranch> branches;
    branches.push_back(CaseBranch{make_node<IntegerLiteralNode>("1"), make_node<StringLiteralNode>("'on'")});
    branches.push_back(CaseBranch{make_node<IntegerLiteralNode>("0"), make_node<StringLiteralNode>("'off'")});
    REQUIRE(require_node<CaseNode>(parseResult) ==
            CaseNode(make_node<ColumnRefNode>("status"), std::move(branches), nullptr, {}));
}

TEST_CASE("parser: CASE with multiple WHEN and ELSE") {
    auto parseResult = parse("CASE WHEN a = 1 THEN 'one' WHEN a = 2 THEN 'two' ELSE 'other' END");
    std::vector<CaseBranch> branches;
    branches.push_back(CaseBranch{
        make_node<BinaryOperatorNode>(BinaryOperator::equals, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("1")),
        make_node<StringLiteralNode>("'one'")});
    branches.push_back(CaseBranch{
        make_node<BinaryOperatorNode>(BinaryOperator::equals, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("2")),
        make_node<StringLiteralNode>("'two'")});
    REQUIRE(require_node<CaseNode>(parseResult) ==
            CaseNode(nullptr, std::move(branches), make_node<StringLiteralNode>("'other'"), {}));
}

TEST_CASE("parser: CASE in expression") {
    auto parseResult = parse("CASE WHEN a > 0 THEN 1 ELSE 0 END + 10");
    std::vector<CaseBranch> branches;
    branches.push_back(CaseBranch{
        make_node<BinaryOperatorNode>(BinaryOperator::greaterThan, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("0")),
        make_node<IntegerLiteralNode>("1")});
    REQUIRE(require_node<BinaryOperatorNode>(parseResult) == BinaryOperatorNode(
        BinaryOperator::add,
        make_node<CaseNode>(nullptr, std::move(branches), make_node<IntegerLiteralNode>("0")),
        make_node<IntegerLiteralNode>("10"),
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
        make_node<IntegerLiteralNode>("1"),
        make_node<IntegerLiteralNode>("2"), SourceLocation{}), ""}};
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: IS NOT expr") {
    auto parseResult = parse("SELECT a IS NOT b FROM t");
    SelectNode expected({});
    expected.columns = {SelectColumn{std::make_shared<BinaryOperatorNode>(
        BinaryOperator::isNot,
        make_node<ColumnRefNode>("a"),
        make_node<ColumnRefNode>("b"), SourceLocation{}), ""}};
    expected.fromClause = from_one("t");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: IS DISTINCT FROM") {
    auto parseResult = parse("SELECT a IS DISTINCT FROM b FROM t");
    SelectNode expected({});
    expected.columns = {SelectColumn{std::make_shared<BinaryOperatorNode>(
        BinaryOperator::isDistinctFrom,
        make_node<ColumnRefNode>("a"),
        make_node<ColumnRefNode>("b"), SourceLocation{}), ""}};
    expected.fromClause = from_one("t");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

TEST_CASE("parser: IS NOT DISTINCT FROM") {
    auto parseResult = parse("SELECT a IS NOT DISTINCT FROM b FROM t");
    SelectNode expected({});
    expected.columns = {SelectColumn{std::make_shared<BinaryOperatorNode>(
        BinaryOperator::isNotDistinctFrom,
        make_node<ColumnRefNode>("a"),
        make_node<ColumnRefNode>("b"), SourceLocation{}), ""}};
    expected.fromClause = from_one("t");
    REQUIRE(require_node<SelectNode>(parseResult) == expected);
}

// --- COLLATE ---

TEST_CASE("parser: expr COLLATE name") {
    auto parseResult = parse("SELECT name COLLATE NOCASE FROM t");
    REQUIRE(parseResult);
    const auto& sel = require_node<SelectNode>(parseResult);
    REQUIRE(sel.columns.size() == 1);
    auto* collate = dynamic_cast<const CollateNode*>(sel.columns.at(0).expression.get());
    REQUIRE(collate != nullptr);
    REQUIRE(*collate == CollateNode(make_node<ColumnRefNode>("name"), "NOCASE", {}));
}

// --- JSON operators ---

TEST_CASE("parser: JSON -> operator") {
    auto parseResult = parse("SELECT data -> '$.name' FROM t");
    REQUIRE(parseResult);
    const auto& sel = require_node<SelectNode>(parseResult);
    REQUIRE(sel.columns.size() == 1);
    auto* binOp = dynamic_cast<const BinaryOperatorNode*>(sel.columns.at(0).expression.get());
    REQUIRE(binOp != nullptr);
    REQUIRE(*binOp == BinaryOperatorNode(
        BinaryOperator::jsonArrow, make_node<ColumnRefNode>("data"), make_node<StringLiteralNode>("'$.name'"), {}));
}

TEST_CASE("parser: JSON ->> operator") {
    auto parseResult = parse("SELECT data ->> '$.name' FROM t");
    REQUIRE(parseResult);
    const auto& sel = require_node<SelectNode>(parseResult);
    REQUIRE(sel.columns.size() == 1);
    auto* binOp = dynamic_cast<const BinaryOperatorNode*>(sel.columns.at(0).expression.get());
    REQUIRE(binOp != nullptr);
    REQUIRE(*binOp == BinaryOperatorNode(
        BinaryOperator::jsonArrow2, make_node<ColumnRefNode>("data"), make_node<StringLiteralNode>("'$.name'"), {}));
}

// --- Bind parameters ---

TEST_CASE("parser: bind parameter ?") {
    auto parseResult = parse("SELECT ? FROM t");
    REQUIRE(parseResult);
    const auto& sel = require_node<SelectNode>(parseResult);
    REQUIRE(sel.columns.size() == 1);
    auto* bind = dynamic_cast<const BindParameterNode*>(sel.columns.at(0).expression.get());
    REQUIRE(bind != nullptr);
    REQUIRE(*bind == BindParameterNode("?", {}));
}

TEST_CASE("parser: bind parameter :name") {
    auto parseResult = parse("SELECT :id FROM t");
    REQUIRE(parseResult);
    const auto& sel = require_node<SelectNode>(parseResult);
    REQUIRE(sel.columns.size() == 1);
    auto* bind = dynamic_cast<const BindParameterNode*>(sel.columns.at(0).expression.get());
    REQUIRE(bind != nullptr);
    REQUIRE(*bind == BindParameterNode(":id", {}));
}

// --- IN table-name ---

TEST_CASE("parser: IN table-name") {
    auto parseResult = parse("SELECT * FROM t WHERE x IN tbl");
    REQUIRE(parseResult);
    const auto& sel = require_node<SelectNode>(parseResult);
    auto* inNode = dynamic_cast<const InNode*>(sel.whereClause.get());
    REQUIRE(inNode != nullptr);
    REQUIRE(*inNode == InNode(make_node<ColumnRefNode>("x"), std::string("tbl"), false, {}));
}
