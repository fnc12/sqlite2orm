#include "../src/spanned_code.h"

#include <sqlite2orm/ast.h>

#include <catch2/catch_all.hpp>

#include <string>
#include <vector>

using namespace sqlite2orm;

namespace {

    IntegerLiteralNode literalWrittenAt(std::string_view text, SourceLocation location) {
        IntegerLiteralNode node(text, location);
        node.sourceSpan = SourceSpan{location, std::string(text)};
        return node;
    }

}  // namespace

// Concatenation is where the offsets are worked out: text written in front of a piece moves the
// spans it carries by the characters it takes, and a plain string carries none of its own.
TEST_CASE("SpannedCode: concatenating moves the spans of the right-hand piece") {
    SpannedCode one = "1";
    one.markGeneratedFrom(literalWrittenAt("1", SourceLocation{1, 8}));
    SpannedCode two = "2";
    two.markGeneratedFrom(literalWrittenAt("2", SourceLocation{1, 12}));
    const SpannedCode sum = "c(" + one + ") + " + two;
    REQUIRE(sum.text() == "c(1) + 2");
    REQUIRE(sum.spans() ==
            std::vector<GeneratedCodeSpan>{{2, 1, 0, SourceLocation{1, 8}, 1}, {7, 1, 0, SourceLocation{1, 12}, 1}});
}

// The node a whole text was generated from is the outermost span, so it is put in front of the
// ones its operands recorded: the list stays in the order the code is written in, outer first.
// Offsets are characters, so the `ü` counts once.
TEST_CASE("SpannedCode: the node of the whole text is its outermost span") {
    SpannedCode operand = "\"ü\"";
    operand.markGeneratedFrom(literalWrittenAt("'ü'", SourceLocation{1, 3}));
    SpannedCode whole = "c(" + operand + ")";
    whole.markGeneratedFrom(literalWrittenAt("('ü')", SourceLocation{1, 2}));
    REQUIRE(whole.text() == "c(\"ü\")");
    REQUIRE(whole.spans() ==
            std::vector<GeneratedCodeSpan>{{0, 6, 0, SourceLocation{1, 2}, 5}, {2, 3, 0, SourceLocation{1, 3}, 3}});
}

// No span is made up: a node with no text of SQL behind it — one built by hand — has nothing to
// highlight, and an empty text has no code to.
TEST_CASE("SpannedCode: a node without SQL text or an empty text records nothing") {
    SpannedCode code = "x";
    code.markGeneratedFrom(IntegerLiteralNode("1", SourceLocation{1, 1}));
    SpannedCode empty;
    empty.markGeneratedFrom(literalWrittenAt("1", SourceLocation{1, 1}));
    REQUIRE(code.spans() == std::vector<GeneratedCodeSpan>{});
    REQUIRE(empty.spans() == std::vector<GeneratedCodeSpan>{});
}

// A result hands its code and spans over whole and gets them back whole, so a generator can build
// on an operand's result and answer with its own.
TEST_CASE("SpannedCode: taken from a result and placed into one") {
    CodeGenResult operand{"1", {}};
    operand.expressionSpans = {{0, 1, 0, SourceLocation{1, 5}, 1}};
    const CodeGenResult result = spannedResult("-" + SpannedCode::takenFrom(operand), {}, {"w"});
    REQUIRE(operand.code == "");
    REQUIRE(operand.expressionSpans == std::vector<GeneratedCodeSpan>{});
    CodeGenResult expected{"-1", {}, {"w"}};
    expected.expressionSpans = {{1, 1, 0, SourceLocation{1, 5}, 1}};
    REQUIRE(result == expected);
}
