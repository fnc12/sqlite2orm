#pragma once

#include "codegen_context.h"

#include <vector>

namespace sqlite2orm {

    /**
     *  Marks every expression generated while it stands as one sqlite_orm writes into the DDL
     *  of a schema object rather than binding into a query, and starts the list of infinities
     *  met in it empty. A statement generated inside another — a trigger body — leaves both the
     *  enclosing mark and the infinities gathered under it as it found them.
     */
    class DdlSerializationScope {
      public:
        explicit DdlSerializationScope(CodeGeneratorContext& context);
        ~DdlSerializationScope();

        DdlSerializationScope(const DdlSerializationScope&) = delete;
        DdlSerializationScope& operator=(const DdlSerializationScope&) = delete;

      private:
        CodeGeneratorContext& context;
        bool enclosing;
        std::vector<DdlInfinityLiteral> enclosingLiterals;
    };

}  // namespace sqlite2orm
