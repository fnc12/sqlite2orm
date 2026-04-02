#include "parser_tests_common.hpp"

using namespace sqlite2orm::parser_test_helpers;

namespace {

    std::unique_ptr<SelectNode> selectLiteralInt(std::string_view value) {
        auto selectNode = std::make_unique<SelectNode>(SourceLocation{});
        selectNode->columns = {SelectColumn{makeSharedNode<IntegerLiteralNode>(value), ""}};
        return selectNode;
    }

}  // namespace

TEST_CASE("parser: UNION two SELECT literals") {
    auto parseResult = parse("SELECT 1 UNION SELECT 2");
    REQUIRE(parseResult);
    std::vector<AstNodePointer> arms;
    arms.push_back(selectLiteralInt("1"));
    arms.push_back(selectLiteralInt("2"));
    CompoundSelectNode expected(std::move(arms), std::vector<CompoundSelectOperator>{CompoundSelectOperator::unionDistinct},
                                SourceLocation{});
    REQUIRE(requireNode<CompoundSelectNode>(parseResult) == expected);
}

TEST_CASE("parser: UNION ALL") {
    auto parseResult = parse("SELECT 1 UNION ALL SELECT 2");
    REQUIRE(parseResult);
    std::vector<AstNodePointer> arms;
    arms.push_back(selectLiteralInt("1"));
    arms.push_back(selectLiteralInt("2"));
    CompoundSelectNode expected(std::move(arms), std::vector<CompoundSelectOperator>{CompoundSelectOperator::unionAll},
                                SourceLocation{});
    REQUIRE(requireNode<CompoundSelectNode>(parseResult) == expected);
}

TEST_CASE("parser: INTERSECT") {
    auto parseResult = parse("SELECT 1 INTERSECT SELECT 2");
    REQUIRE(parseResult);
    std::vector<AstNodePointer> arms;
    arms.push_back(selectLiteralInt("1"));
    arms.push_back(selectLiteralInt("2"));
    CompoundSelectNode expected(std::move(arms), std::vector<CompoundSelectOperator>{CompoundSelectOperator::intersect},
                                SourceLocation{});
    REQUIRE(requireNode<CompoundSelectNode>(parseResult) == expected);
}

TEST_CASE("parser: EXCEPT") {
    auto parseResult = parse("SELECT 1 EXCEPT SELECT 2");
    REQUIRE(parseResult);
    std::vector<AstNodePointer> arms;
    arms.push_back(selectLiteralInt("1"));
    arms.push_back(selectLiteralInt("2"));
    CompoundSelectNode expected(std::move(arms), std::vector<CompoundSelectOperator>{CompoundSelectOperator::except},
                                SourceLocation{});
    REQUIRE(requireNode<CompoundSelectNode>(parseResult) == expected);
}

TEST_CASE("parser: chained UNION and UNION ALL") {
    auto parseResult = parse("SELECT 1 UNION SELECT 2 UNION ALL SELECT 3");
    REQUIRE(parseResult);
    std::vector<AstNodePointer> arms;
    arms.push_back(selectLiteralInt("1"));
    arms.push_back(selectLiteralInt("2"));
    arms.push_back(selectLiteralInt("3"));
    CompoundSelectNode expected(std::move(arms),
                                std::vector<CompoundSelectOperator>{CompoundSelectOperator::unionDistinct,
                                                                    CompoundSelectOperator::unionAll},
                                SourceLocation{});
    REQUIRE(requireNode<CompoundSelectNode>(parseResult) == expected);
}
