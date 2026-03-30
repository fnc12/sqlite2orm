#pragma once

#include <sqlite2orm/codegen.h>
#include <sqlite2orm/codegen_policy.h>
#include <sqlite2orm/schema_process.h>

namespace sqlite2orm {

    /**
     *  Single header: structs + `make_storage(db_path, make_table…, index…, trigger…)` (phase 21.4).
     *  Uses successful parses only. Tables are ordered by FK dependencies when possible.
     */
    CodeGenResult generateSqliteSchemaHeader(const ProcessSqliteSchemaResult& schema,
                                             const CodeGenPolicy* policy = nullptr);

}  // namespace sqlite2orm
