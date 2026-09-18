#include <sqlite2orm/schema_report.h>

#include <sqlite2orm/json_emit.h>
#include <sqlite2orm/schema_header.h>

#include <sstream>

namespace sqlite2orm {

    SchemaReport reportSqliteSchema(const ProcessSqliteSchemaResult& schema, bool jsonOnly,
                                    const CodeGenPolicy* policy) {
        std::ostringstream out;
        std::ostringstream err;
        SchemaReport report;
        if(jsonOnly) {
            out << sqliteSchemaResultToJson(schema) << "\n";
        }
        for(const SchemaStatementResult& statement: schema.statements) {
            for(const CodegenWarning& warning: statement.pipeline.codegen.warnings) {
                err << "warning: " << warning.message << "\n";
            }
        }
        if(!jsonOnly) {
            const CodeGenResult header = generateSqliteSchemaHeader(schema, policy);
            for(const CodegenWarning& warning: header.warnings) {
                err << "warning: " << warning.message << "\n";
            }
            if(schema.allOk()) {
                out << header.code;
                if(!header.code.empty() && header.code.back() != '\n') {
                    out << "\n";
                }
            }
        }
        if(!schema.allOk()) {
            for(const SchemaStatementResult& statement: schema.statements) {
                if(statement.pipeline.ok()) {
                    continue;
                }
                const std::string where = "[" + statement.meta.type + " " + statement.meta.name + "]";
                for(const ParseError& error: statement.pipeline.parseResult.errors) {
                    err << "parse error " << where << ": " << error.message << " at " << error.location.line << ":"
                        << error.location.column << "\n";
                }
                for(const ValidationError& error: statement.pipeline.validationErrors) {
                    err << "validation " << where << ": " << error.message << " (" << error.nodeType << ")\n";
                }
                for(const std::string& error: statement.pipeline.codegen.errors) {
                    err << "codegen error " << where << ": " << error << "\n";
                }
            }
            report.exitCode = 1;
        }
        report.out = out.str();
        report.err = err.str();
        return report;
    }

}  // namespace sqlite2orm
