#pragma once

#include <sqlite2orm/ast_base.h>
#include <sqlite2orm/codegen_result.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sqlite2orm {

    /**
     *  The statement a fragment of generated code came from, as `GeneratedCodeSpan` reports it and
     *  without the place in the code the statement ends up at. It travels next to the fragment
     *  while the code is assembled: the fragments are reordered — an index is written before the
     *  table it is made for — and sliced before anything knows its offset, so the origin has to
     *  move with the text instead of being worked out from the assembled string afterwards.
     */
    struct GeneratedFrom {
        size_t statementIndex = 0;
        SourceLocation sqlLocation;
        size_t sqlLength = 0;

        bool operator==(const GeneratedFrom&) const = default;
    };

    /**
     *  The whole of `statement` as an origin, which is what a statement-level span covers. A node
     *  no parse recorded a span for — one a test builds by hand — is named by its own location and
     *  a length of 0: the code is still known to be that statement's, and there is no text to
     *  highlight it by.
     */
    GeneratedFrom generatedFromStatement(size_t statementIndex, const AstNode& statement);

    /**
     *  A fragment of generated code waiting to be placed, and the statement it came from — none
     *  when that is not known, in which case the fragment is placed without a span rather than
     *  with a made-up one.
     */
    struct PlacedFragment {
        std::string text;
        std::optional<GeneratedFrom> origin;
    };

    /**
     *  Generated code being assembled together with the map of which statement each stretch of it
     *  came from. Text is appended either plainly — the punctuation the assembly writes itself,
     *  which no statement is behind — or as a fragment, which records one span; spans therefore
     *  come out sorted, non-overlapping and in the order the code is written in, and nothing has
     *  to parse the assembled text to find out what belongs to whom.
     *
     *  Offsets are counted in characters, as `GeneratedCodeSpan` says, so a name written with
     *  non-ASCII reaching the code inside a string literal moves the following spans by what a
     *  consumer measuring characters sees, not by the bytes it takes.
     */
    class CodeSpanBuilder {
      public:
        /** Appends text no statement is behind: separators, blank lines, a wrapping call. */
        void append(std::string_view text);
        /**
         *  Appends a fragment `origin` generated, recording the span it takes in the code. With no
         *  origin the text is appended as it is and names no statement: a span that is missing is
         *  a gap a consumer sees, one pointing at the wrong statement is a lie it believes.
         */
        void appendFragment(std::string_view text, const std::optional<GeneratedFrom>& origin);
        /** Appends another piece of assembled code, its spans moved to where its text lands. */
        void appendBuilt(const CodeSpanBuilder& other);
        /** Puts text in front of everything, moving every span it pushes along. */
        void prepend(std::string_view text);
        /**
         *  A copy indented by four spaces at the start of every line, spans following the text
         *  they cover. Trailing spaces are dropped so that an indent after the last newline does
         *  not leave a line of blanks behind.
         */
        [[nodiscard]] CodeSpanBuilder indented() const;

        [[nodiscard]] const std::string& code() const {
            return this->text;
        }
        [[nodiscard]] std::string takeCode() {
            return std::move(this->text);
        }
        [[nodiscard]] std::vector<GeneratedCodeSpan> takeSpans() {
            return std::move(this->recordedSpans);
        }
        [[nodiscard]] bool empty() const {
            return this->text.empty();
        }

      private:
        std::string text;
        std::vector<GeneratedCodeSpan> recordedSpans;
        /** Characters of `text`, i.e. the offset the next appended fragment starts at. */
        size_t characters = 0;
    };

}  // namespace sqlite2orm
