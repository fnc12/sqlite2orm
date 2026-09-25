#include "code_span_builder.h"

#include <sqlite2orm/utils.h>

#include <algorithm>

namespace sqlite2orm {

    namespace {

        constexpr std::string_view blockIndent = "    ";

    }  // namespace

    GeneratedFrom generatedFromStatement(size_t statementIndex, const AstNode& statement) {
        const SourceSpan& span = statement.sourceSpan;
        if (span.text.empty()) {
            return GeneratedFrom{statementIndex, statement.location, 0};
        }
        return GeneratedFrom{statementIndex, span.location, utf8CharacterCount(span.text)};
    }

    void CodeSpanBuilder::append(std::string_view text) {
        this->text += text;
        this->characters += utf8CharacterCount(text);
    }

    void CodeSpanBuilder::appendFragment(std::string_view text, const std::optional<GeneratedFrom>& origin) {
        if (!origin) {
            this->append(text);
            return;
        }
        if (text.empty()) {
            return;
        }
        const size_t offset = this->characters;
        this->append(text);
        this->recordedSpans.push_back(GeneratedCodeSpan{offset,
                                                        this->characters - offset,
                                                        origin->statementIndex,
                                                        origin->sqlLocation,
                                                        origin->sqlLength});
    }

    void CodeSpanBuilder::appendBuilt(const CodeSpanBuilder& other) {
        const size_t offset = this->characters;
        this->append(other.text);
        for (GeneratedCodeSpan span: other.recordedSpans) {
            span.codeOffset += offset;
            this->recordedSpans.push_back(span);
        }
    }

    void CodeSpanBuilder::prepend(std::string_view text) {
        this->text.insert(0, text);
        const size_t offset = utf8CharacterCount(text);
        this->characters += offset;
        for (GeneratedCodeSpan& span: this->recordedSpans) {
            span.codeOffset += offset;
        }
    }

    CodeSpanBuilder CodeSpanBuilder::indented() const {
        // Where every character of this text lands in the indented one, so that a span is moved by
        // the indents standing before it rather than by their number being counted a second time.
        std::vector<size_t> indentedOffsets;
        indentedOffsets.reserve(this->characters + 1);

        CodeSpanBuilder out;
        out.text = std::string(blockIndent);
        out.characters = utf8CharacterCount(blockIndent);
        for (const char character: this->text) {
            if (!isUtf8ContinuationByte(character)) {
                indentedOffsets.push_back(out.characters);
                ++out.characters;
            }
            out.text += character;
            if (character == '\n') {
                out.text += blockIndent;
                out.characters += utf8CharacterCount(blockIndent);
            }
        }
        indentedOffsets.push_back(out.characters);
        while (out.text.ends_with(' ')) {
            out.text.pop_back();
            --out.characters;
        }

        for (GeneratedCodeSpan span: this->recordedSpans) {
            // Measured from the last character the span covers rather than from the one past it:
            // a span ending on a newline would otherwise swallow the indent of the line after it.
            const size_t start = indentedOffsets[span.codeOffset];
            const size_t end = std::min(indentedOffsets[span.codeOffset + span.codeLength - 1] + 1, out.characters);
            if (end <= start) {
                continue;
            }
            span.codeOffset = start;
            span.codeLength = end - start;
            out.recordedSpans.push_back(span);
        }
        return out;
    }

}  // namespace sqlite2orm
