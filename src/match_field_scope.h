#pragma once

#include "codegen_context.h"

namespace sqlite2orm {

    /**
     *  Marks the field operand of a MATCH while it is generated: everything named under it is
     *  invisible to the FROM sqlite_orm infers, nested selects included, because its
     *  `ast_iterator` never descends into that operand. The previous value is restored rather
     *  than cleared, so a MATCH standing inside another one's field stays marked as well.
     */
    struct MatchFieldScope {
        CodeGeneratorContext* ctx;
        bool enclosing;

        explicit MatchFieldScope(CodeGeneratorContext* context) : ctx(context), enclosing(context->emittingMatchField) {
            ctx->emittingMatchField = true;
        }

        ~MatchFieldScope() {
            ctx->emittingMatchField = enclosing;
        }

        MatchFieldScope(const MatchFieldScope&) = delete;
        MatchFieldScope& operator=(const MatchFieldScope&) = delete;
    };

}  // namespace sqlite2orm
