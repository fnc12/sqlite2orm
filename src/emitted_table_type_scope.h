#pragma once

#include "codegen_context.h"

#include <set>
#include <string>
#include <utility>

namespace sqlite2orm {

    /**
     *  Keeps the recordsets the emitter records for one select apart from its parent's. On the
     *  way out the two are merged: sqlite_orm collects the tables of a nested select into the
     *  enclosing FROM as well — an explicit `from<...>()` fixes the level it stands on and no
     *  other — so whatever a subquery named its parent has to answer for too.
     *
     *  The mentions a level made itself are kept apart in `ownEmittedTableTypes` and in
     *  `ownVisibleEmittedTableTypes`, which are restored rather than merged: the parent answers
     *  for a subquery's width, but the forms that subquery wrote are not forms the parent has to
     *  name with its own FROM.
     */
    struct EmittedTableTypeScope {
        CodeGeneratorContext* ctx;
        std::set<std::string> enclosing;
        std::set<std::string> enclosingOwn;
        std::set<std::string> enclosingOwnVisible;

        explicit EmittedTableTypeScope(CodeGeneratorContext* context) :
            ctx(context), enclosing(std::move(context->emittedTableTypes)),
            enclosingOwn(std::move(context->ownEmittedTableTypes)),
            enclosingOwnVisible(std::move(context->ownVisibleEmittedTableTypes)) {
            ctx->emittedTableTypes.clear();
            ctx->ownEmittedTableTypes.clear();
            ctx->ownVisibleEmittedTableTypes.clear();
            ++ctx->selectNestingLevel;
        }

        ~EmittedTableTypeScope() {
            --ctx->selectNestingLevel;
            ctx->emittedTableTypes.insert(enclosing.begin(), enclosing.end());
            ctx->ownEmittedTableTypes = std::move(enclosingOwn);
            ctx->ownVisibleEmittedTableTypes = std::move(enclosingOwnVisible);
        }

        EmittedTableTypeScope(const EmittedTableTypeScope&) = delete;
        EmittedTableTypeScope& operator=(const EmittedTableTypeScope&) = delete;
    };

}  // namespace sqlite2orm
