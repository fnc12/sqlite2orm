#pragma once

#include <sqlite2orm/codegen_policy.h>
#include <sqlite2orm/schema_process.h>

#include <string>

namespace sqlite2orm {

    /**
     *  What one `--db` run prints and returns. Keeping it here rather than in the CLI is what lets
     *  a test pin the exact text and the exact exit code of a schema that fails to generate.
     */
    struct SchemaReport {
        /** Generated header, or the JSON decision points in `--json` mode. */
        std::string out;
        /** Warnings and, for a schema that did not generate, the per-statement diagnostics. */
        std::string err;
        /** 0 when every statement generated, 1 otherwise — independent of `--json`. */
        int exitCode = 0;

        bool operator==(const SchemaReport&) const = default;
    };

    /**
     *  Render `schema` the way the CLI reports it. `jsonOnly` swaps the generated header for the
     *  JSON decision points on `out`; the diagnostics and the exit code are the same either way,
     *  so a script can tell a failed schema from a good one without reading the JSON.
     */
    SchemaReport reportSqliteSchema(const ProcessSqliteSchemaResult& schema, bool jsonOnly,
                                    const CodeGenPolicy* policy = nullptr);

}  // namespace sqlite2orm
