#pragma once

#include "codegen_context.h"
#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_result.h>

#include <string>
#include <string_view>

namespace sqlite2orm {

    /**
     *  The helpers shared by the DDL generators of `codegen_ddl.cpp`, `codegen_ddl_table.cpp` and
     *  `codegen_ddl_view.cpp`. Each is documented at its definition.
     */

    std::string ddlBlobLiteralReason(std::string_view literal);
    CodegenWarning
    ddlInfinityWarning(const DdlInfinityLiteral& literal, std::string_view subject, std::string_view consequence);
    std::string viewDisplayName(const CreateViewNode& node);

}  // namespace sqlite2orm
