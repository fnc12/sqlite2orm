#pragma once

#include <sqlite2orm/codegen.h>
#include <sqlite2orm/codegen_policy.h>
#include <sqlite2orm/error.h>
#include <sqlite2orm/parser.h>

#include <string_view>
#include <vector>

namespace sqlite2orm {

    /**
     *  Result of running the sqlite2orm pipeline on a single SQL statement (string input).
     *  Use this from CLI tools, desktop/mobile GUI, TUI, tests, or after reading DDL from sqlite_master —
     *  the same entry point regardless of whether SQL came from a file or user input.
     */
    struct ProcessSqlResult {
        ParseResult parseResult;
        std::vector<ValidationError> validationErrors;
        CodeGenResult codegen;

        bool ok() const {
            return static_cast<bool>(parseResult) && validationErrors.empty() && codegen.errors.empty();
        }

        bool operator==(const ProcessSqlResult& other) const = default;
    };

    /**
     *  Tokenize → parse → validate (if parse succeeded) → codegen (if validation passed).
     *  Tokenizer errors are reported as `parseResult.errors` with a null AST.
     *  Processes only the **first** statement; use `processMultiSql` for multi-statement input.
     */
    ProcessSqlResult processSql(std::string_view sql);

    /** Same as `processSql(sql)` when `policy` is null; otherwise steers codegen decision points. */
    ProcessSqlResult processSql(std::string_view sql, const CodeGenPolicy* policy);

    /** Tokenize → parse all semicolon-separated statements → validate + codegen each one. */
    std::vector<ProcessSqlResult> processMultiSql(std::string_view sql, const CodeGenPolicy* policy = nullptr);

    /** Join `codegen.code` from successful results into a single string, separating DDL and DML groups with a blank line. */
    std::string joinGeneratedCode(const std::vector<ProcessSqlResult>& results);

    /** The joined code of a batch together with the map of which statement each stretch of it came from. */
    struct JoinedGeneratedCode {
        std::string code;
        /**
         *  Statement-level map of `code`, in the order the code is written in: a span per fragment
         *  a statement generated, naming it by its index in the `results` the code was joined from
         *  and by the SQL text it was parsed from. Every statement the batch generated code for is
         *  named — a CREATE TABLE twice, once for its struct and once for its `make_table(...)`
         *  argument — and a statement that generated nothing is named by no span at all.
         */
        std::vector<GeneratedCodeSpan> spans;

        bool operator==(const JoinedGeneratedCode&) const = default;
    };

    /**
     *  Same as `joinGeneratedCode(results)`, with the map of the joined text next to it: a
     *  consumer showing SQL and generated code side by side highlights the one from the other
     *  without parsing either. The joining slices and reorders what the statements generated, so
     *  this is the only place that can tell where a statement ended up.
     */
    JoinedGeneratedCode joinGeneratedCodeWithSpans(const std::vector<ProcessSqlResult>& results);

}  // namespace sqlite2orm
