#pragma once

#include <sqlite2orm/ast_base.h>
#include <sqlite2orm/codegen_result.h>

#include <string>
#include <string_view>
#include <vector>

namespace sqlite2orm {

    /**
     *  Generated code together with the map of which expression each stretch of it came from, the
     *  way `CodeGenResult::expressionSpans` reports it: offsets counted in characters from the start
     *  of this text. Code is assembled by concatenation, children before their parent, and where a
     *  child's text lands is only known once it is concatenated into its parent's; concatenating
     *  two of these moves the spans of the right one to where its text lands, so the map travels
     *  with the text and nothing has to parse the assembled code afterwards.
     *
     *  A plain string converts implicitly and carries no spans, so a piece of syntax the generator
     *  writes itself reads as it always has — `"c(" + operand + ")"`. Nothing converts back
     *  implicitly: a site that turns one of these into a plain string says so with `text()` and
     *  leaves its spans behind knowingly. What that costs is a gap in the map, never a span naming
     *  text that has moved from under it.
     */
    class SpannedCode {
      public:
        SpannedCode() = default;
        SpannedCode(std::string text);
        SpannedCode(std::string_view text);
        SpannedCode(const char* text);

        /** The code of `result` with the expression spans recorded for it, both taken out of it. */
        static SpannedCode takenFrom(CodeGenResult& result);

        /**
         *  Records that the whole of this text was generated from `node`, as the outermost span
         *  of the ones it holds. A node no parse recorded the text of — one a test builds by hand —
         *  and an empty text record nothing: there is no SQL to highlight, or no code.
         */
        void markGeneratedFrom(const AstNode& node);

        SpannedCode& operator+=(const SpannedCode& other);

        friend SpannedCode operator+(SpannedCode left, const SpannedCode& right) {
            left += right;
            return left;
        }

        [[nodiscard]] const std::string& text() const {
            return this->code;
        }
        [[nodiscard]] bool empty() const {
            return this->code.empty();
        }
        [[nodiscard]] const std::vector<GeneratedCodeSpan>& spans() const {
            return this->recordedSpans;
        }

        /** Moves the text and its spans into `result`, as its `code` and `expressionSpans`. */
        void placeInto(CodeGenResult& result) &&;

      private:
        std::string code;
        std::vector<GeneratedCodeSpan> recordedSpans;
        /** Characters of `code`, i.e. the offset the text appended next starts at. */
        size_t characters = 0;
    };

    /**
     *  A result whose code is `code`, its expression spans included. The aggregate initialization
     *  `CodeGenResult{text, …}` reads the same and silently leaves the spans behind, which is why
     *  a generator converted to `SpannedCode` builds its result here.
     */
    CodeGenResult spannedResult(SpannedCode code,
                                std::vector<DecisionPoint> decisionPoints = {},
                                std::vector<CodegenWarning> warnings = {},
                                std::vector<std::string> errors = {});

}  // namespace sqlite2orm
