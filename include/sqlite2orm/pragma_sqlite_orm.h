#pragma once

#include <sqlite2orm/ast.h>

#include <optional>
#include <string>

namespace sqlite2orm {

    /**
     *  PRAGMA names mapped to `storage.pragma` in sqlite_orm (see dev/pragma.h).
     *  Returns an error message if the pragma is not supported for codegen; std::nullopt if OK.
     *  Schema-qualified PRAGMA (main.xxx) is rejected — sqlite_orm pragma_t does not take a schema.
     */
    std::optional<std::string> validatePragmaForSqliteOrm(const PragmaNode& node);

}  // namespace sqlite2orm
