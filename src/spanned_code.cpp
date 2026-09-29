#include "spanned_code.h"

#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    SpannedCode::SpannedCode(std::string text) : code(std::move(text)), characters(utf8CharacterCount(this->code)) {}

    SpannedCode::SpannedCode(std::string_view text) : SpannedCode(std::string(text)) {}

    SpannedCode::SpannedCode(const char* text) : SpannedCode(std::string(text)) {}

    SpannedCode SpannedCode::takenFrom(CodeGenResult& result) {
        SpannedCode taken(std::move(result.code));
        taken.recordedSpans = std::move(result.expressionSpans);
        result.code.clear();
        result.expressionSpans.clear();
        return taken;
    }

    void SpannedCode::markGeneratedFrom(const AstNode& node) {
        const SourceSpan& source = node.sourceSpan;
        if (source.text.empty() || this->code.empty()) {
            return;
        }
        this->recordedSpans.insert(
            this->recordedSpans.begin(),
            GeneratedCodeSpan{0, this->characters, 0, source.location, utf8CharacterCount(source.text)});
    }

    SpannedCode& SpannedCode::operator+=(const SpannedCode& other) {
        const size_t offset = this->characters;
        this->code += other.code;
        this->characters += other.characters;
        for (GeneratedCodeSpan span: other.recordedSpans) {
            span.codeOffset += offset;
            this->recordedSpans.push_back(span);
        }
        return *this;
    }

    void SpannedCode::placeInto(CodeGenResult& result) && {
        result.code = std::move(this->code);
        result.expressionSpans = std::move(this->recordedSpans);
    }

    CodeGenResult spannedResult(SpannedCode code,
                                std::vector<DecisionPoint> decisionPoints,
                                std::vector<CodegenWarning> warnings,
                                std::vector<std::string> errors) {
        CodeGenResult result{{}, std::move(decisionPoints), std::move(warnings), std::move(errors)};
        std::move(code).placeInto(result);
        return result;
    }

}  // namespace sqlite2orm
