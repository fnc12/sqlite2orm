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
