#pragma once

#include <sqlite2orm/codegen.h>
#include <sqlite2orm/codegen_policy.h>
#include <sqlite2orm/schema_coverage.h>
#include <sqlite2orm/schema_process.h>

namespace sqlite2orm {

    /**
     *  Single header: structs + `make_storage(db_path, make_table…, index…, trigger…)` (phase 21.4).
     *  Uses successful parses only. Tables are ordered by FK dependencies when possible.
     */
    CodeGenResult generateSqliteSchemaHeader(const ProcessSqliteSchemaResult& schema,
                                             const CodeGenPolicy* policy = nullptr);

    /**
     *  Same header, also counting in `coverage` how many of the schema's rows it carries — the N of
     *  M `--db --json` reports and `--strict` gates the exit code on.
     */
    CodeGenResult generateSqliteSchemaHeader(const ProcessSqliteSchemaResult& schema,
                                             const CodeGenPolicy* policy,
                                             SchemaCoverage& coverage);

}  // namespace sqlite2orm
