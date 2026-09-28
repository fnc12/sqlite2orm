#include "../src/code_span_builder.h"

#include <catch2/catch_all.hpp>

#include <optional>
#include <string>
#include <vector>

using namespace sqlite2orm;

// A fragment whose statement is not known is placed as plain text: the map misses it rather than
// naming a statement it did not come from, and the spans around it still land where their text is.
TEST_CASE("CodeSpanBuilder: a fragment with no origin is placed without a span") {
    CodeSpanBuilder builder;
    builder.appendFragment("first();", GeneratedFrom{0, SourceLocation{1, 1}, 7});
    builder.append("\n");
    builder.appendFragment("unknown();", std::nullopt);
    builder.append("\n");
    builder.appendFragment("second();", GeneratedFrom{1, SourceLocation{1, 9}, 8});
    REQUIRE(builder.code() == "first();\nunknown();\nsecond();");
    REQUIRE(builder.takeSpans() ==
            std::vector<GeneratedCodeSpan>{{0, 8, 0, SourceLocation{1, 1}, 7}, {20, 9, 1, SourceLocation{1, 9}, 8}});
    REQUIRE(builder.takeCode() == "first();\nunknown();\nsecond();");
}

// A fragment's expression spans count from the start of the fragment; placed, they move to where
// its text lands and take the index of the statement the fragment is named by. A span reaching past
// the fragment describes some other text and is not placed, and neither is any span of a fragment
// with no origin: there is no statement to name it by.
TEST_CASE("CodeSpanBuilder: a fragment's expression spans land where its text does") {
    CodeSpanBuilder builder;
    builder.append("// ");
    builder.appendFragment(
        "f(c(1) + 2);",
        GeneratedFrom{3, SourceLocation{2, 1}, 12},
        {{2, 8, 0, SourceLocation{2, 8}, 5}, {4, 1, 0, SourceLocation{2, 8}, 1}, {10, 5, 0, SourceLocation{2, 1}, 1}});
    builder.append("\n");
    builder.appendFragment("g(3);", std::nullopt, {{2, 1, 0, SourceLocation{3, 3}, 1}});
    REQUIRE(builder.code() == "// f(c(1) + 2);\ng(3);");
    REQUIRE(builder.takeSpans() == std::vector<GeneratedCodeSpan>{{3, 12, 3, SourceLocation{2, 1}, 12}});
    REQUIRE(builder.takeExpressionSpans() ==
            std::vector<GeneratedCodeSpan>{{5, 8, 3, SourceLocation{2, 8}, 5}, {7, 1, 3, SourceLocation{2, 8}, 1}});
}

// Indenting a block moves the expression spans with the text they cover, exactly as it moves the
// statement-level ones, and text put in front of everything moves both lists alike.
TEST_CASE("CodeSpanBuilder: indenting and prepending move the expression spans too") {
    CodeSpanBuilder statement;
    statement.appendFragment("a(\n1);",
                             GeneratedFrom{0, SourceLocation{1, 1}, 6},
                             {{3, 1, 0, SourceLocation{1, 5}, 1}});
    CodeSpanBuilder block;
    block.append("{\n");
    block.appendBuilt(statement.indented());
    block.prepend("ü");
    REQUIRE(block.code() == "ü{\n    a(\n    1);");
    REQUIRE(block.takeSpans() == std::vector<GeneratedCodeSpan>{{7, 10, 0, SourceLocation{1, 1}, 6}});
    REQUIRE(block.takeExpressionSpans() == std::vector<GeneratedCodeSpan>{{14, 1, 0, SourceLocation{1, 5}, 1}});
}
