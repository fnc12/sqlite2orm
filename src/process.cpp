#include <sqlite2orm/process.h>
#include <sqlite2orm/tokenizer.h>
#include <sqlite2orm/validator.h>

#include "codegen_utils.h"
#include "process_internal.h"

namespace sqlite2orm {

    namespace {

        /** Column registry from every CREATE TABLE in the batch, so views can infer field types. */
        std::map<std::string, std::vector<SourceTableColumn>>
        collectSourceTables(const std::vector<ParseResult>& parseResults) {
            std::map<std::string, std::vector<SourceTableColumn>> sourceTables;
            for(const ParseResult& parseResult : parseResults) {
                const auto* createTable =
                    dynamic_cast<const CreateTableNode*>(parseResult.astNodePointer.get());
                if(!createTable) {
                    continue;
                }
                sourceTables[normalizeSqlIdentifier(stripIdentifierQuotes(createTable->tableName))] =
                    sourceTableColumnsFromCreateTable(*createTable);
            }
            return sourceTables;
        }

    }  // namespace

    ProcessSqlResult processSql(std::string_view sql) {
        return processSql(sql, nullptr);
    }

    ProcessSqlResult processSql(std::string_view sql, const CodeGenPolicy* policy) {
        return processSqlWithSourceTables(sql, policy, {});
    }

    ProcessSqlResult processSqlWithSourceTables(
        std::string_view sql,
        const CodeGenPolicy* policy,
        const std::map<std::string, std::vector<SourceTableColumn>>& sourceTables) {
        ProcessSqlResult out;
        try {
            Tokenizer tokenizer;
            auto tokens = tokenizer.tokenize(sql);
            Parser parser;
            out.parseResult = parser.parse(std::move(tokens));
        } catch(const TokenizeError& e) {
            out.parseResult.errors.push_back(ParseError{std::string(e.what()), e.location});
            return out;
        }

        if(!out.parseResult.astNodePointer) {
            return out;
        }

        Validator validator;
        out.validationErrors = validator.validate(*out.parseResult.astNodePointer);
        if(!out.validationErrors.empty()) {
            return out;
        }

        CodeGenerator codeGenerator;
        codeGenerator.codeGenPolicy = policy;
        codeGenerator.context().sourceTableColumnsByNormalizedName = sourceTables;
        out.codegen = codeGenerator.generate(*out.parseResult.astNodePointer);
        return out;
    }

    std::vector<ProcessSqlResult> processMultiSql(std::string_view sql, const CodeGenPolicy* policy) {
        std::vector<ProcessSqlResult> results;
        try {
            Tokenizer tokenizer;
            auto tokens = tokenizer.tokenize(sql);
            Parser parser;
            auto parseResults = parser.parseAll(std::move(tokens));
            const auto sourceTables = collectSourceTables(parseResults);
            for(auto& pr : parseResults) {
                ProcessSqlResult one;
                one.parseResult = std::move(pr);
                if(!one.parseResult.astNodePointer) {
                    results.push_back(std::move(one));
                    continue;
                }
                Validator validator;
                one.validationErrors = validator.validate(*one.parseResult.astNodePointer);
                if(!one.validationErrors.empty()) {
                    results.push_back(std::move(one));
                    continue;
                }
                CodeGenerator codeGenerator;
                codeGenerator.codeGenPolicy = policy;
                codeGenerator.context().sourceTableColumnsByNormalizedName = sourceTables;
                one.codegen = codeGenerator.generate(*one.parseResult.astNodePointer);
                results.push_back(std::move(one));
            }
        } catch(const TokenizeError& e) {
            ProcessSqlResult one;
            one.parseResult.errors.push_back(ParseError{std::string(e.what()), e.location});
            results.push_back(std::move(one));
        }
        return results;
    }

    std::string joinGeneratedCode(const std::vector<ProcessSqlResult>& results) {
        // Standalone DDL codegen wraps each statement in its own `auto storage = ...`; a batch
        // describes ONE database, so merge every storage argument into a single make_storage().
        static constexpr std::string_view storageMarker = "\nauto storage = make_storage(\"\",\n    ";

        std::vector<std::string> structBlocks;
        std::vector<std::string> storageArguments;
        std::vector<std::string> otherStatements;
        for(const auto& result : results) {
            const std::string& code = result.codegen.code;
            if(code.empty()) {
                continue;
            }
            const size_t markerPosition = code.find(storageMarker);
            if(markerPosition != std::string::npos && code.ends_with(");")) {
                std::string structPart = code.substr(0, markerPosition);
                while(!structPart.empty() && structPart.back() == '\n') {
                    structPart.pop_back();
                }
                if(!structPart.empty()) {
                    structBlocks.push_back(std::move(structPart));
                }
                storageArguments.push_back(
                    code.substr(markerPosition + storageMarker.size(),
                                code.size() - markerPosition - storageMarker.size() - 2));
                continue;
            }
            if(code.starts_with("make_index(") || code.starts_with("make_unique_index(") ||
               code.starts_with("make_trigger(")) {
                std::string argument = code;
                while(!argument.empty() &&
                      (argument.back() == '\n' || argument.back() == ';' || argument.back() == ' ')) {
                    argument.pop_back();
                }
                storageArguments.push_back(std::move(argument));
                continue;
            }
            otherStatements.push_back(code);
        }

        std::string out;
        for(const std::string& structBlock : structBlocks) {
            out += structBlock;
            out += "\n\n";
        }
        if(!storageArguments.empty()) {
            out += "auto storage = make_storage(\"\"";
            for(const std::string& storageArgument : storageArguments) {
                out += ",\n    ";
                out += storageArgument;
            }
            out += ");\n";
        }
        bool separatorAdded = out.empty();
        for(const std::string& statement : otherStatements) {
            if(!separatorAdded) {
                out += '\n';
                separatorAdded = true;
            }
            out += statement;
            if(out.back() != '\n') {
                out += '\n';
            }
        }
        return out;
    }

}  // namespace sqlite2orm
