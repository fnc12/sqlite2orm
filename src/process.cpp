#include <sqlite2orm/process.h>
#include <sqlite2orm/tokenizer.h>
#include <sqlite2orm/validator.h>

#include "codegen_utils.h"

#include <optional>
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
            std::map<std::string, int> batchVariableUses;
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
                codeGenerator.context().batchVariableUses = std::move(batchVariableUses);
                one.codegen = codeGenerator.generate(*one.parseResult.astNodePointer);
                batchVariableUses = std::move(codeGenerator.context().batchVariableUses);
                results.push_back(std::move(one));
            }
        } catch(const TokenizeError& e) {
            ProcessSqlResult one;
            one.parseResult.errors.push_back(ParseError{std::string(e.what()), e.location});
            results.push_back(std::move(one));
        }
        return results;
    }

    namespace {

        /**
         *  Post-pass for the `guard` savepoint style: same-name savepoints get unique guard
         *  variables (`sp_savepoint`, `sp_savepoint_2`, …) and RELEASE / ROLLBACK TO resolve
         *  against the innermost open savepoint with that name, following SQLite stack
         *  semantics (both pop the more recent savepoints; ROLLBACK TO keeps the matched one,
         *  COMMIT/ROLLBACK clear the whole stack).
         */
        std::vector<std::string> resolveGuardSavepoints(std::vector<std::string> statements,
                                                        const std::vector<const AstNode*>& statementNodes) {
            struct OpenGuard {
                std::string name;
                std::string variableName;
            };
            std::vector<OpenGuard> stack;
            std::map<std::string, int> usesByBaseVariable;

            auto findInnermost = [&](const std::string& name) -> std::optional<size_t> {
                for(size_t i = stack.size(); i-- > 0;) {
                    if(stack[i].name == name) {
                        return i;
                    }
                }
                return std::nullopt;
            };

            for(size_t index = 0; index < statements.size(); ++index) {
                const AstNode* node = index < statementNodes.size() ? statementNodes[index] : nullptr;
                std::string& code = statements[index];
                if(const auto* savepointNode = dynamic_cast<const SavepointNode*>(node);
                   savepointNode && code.starts_with("auto ") &&
                   code.find("storage.savepoint_guard(") != std::string::npos) {
                    const std::string baseVariable = savepointGuardVariableName(savepointNode->name);
                    const int use = ++usesByBaseVariable[baseVariable];
                    std::string variableName = baseVariable;
                    if(use > 1) {
                        variableName += "_" + std::to_string(use);
                        code.replace(code.find(baseVariable), baseVariable.size(), variableName);
                    }
                    stack.push_back(OpenGuard{savepointNode->name, std::move(variableName)});
                    continue;
                }
                if(const auto* releaseNode = dynamic_cast<const ReleaseNode*>(node);
                   releaseNode && code.ends_with(".release();")) {
                    if(const auto matched = findInnermost(releaseNode->name)) {
                        code = stack[*matched].variableName + ".release();";
                        stack.resize(*matched);  // RELEASE pops the matched savepoint and everything above
                    }
                    continue;
                }
                if(const auto* transactionControl = dynamic_cast<const TransactionControlNode*>(node)) {
                    if(transactionControl->rollbackToSavepoint && code.ends_with(".rollback_to();")) {
                        if(const auto matched = findInnermost(*transactionControl->rollbackToSavepoint)) {
                            code = stack[*matched].variableName + ".rollback_to();";
                            stack.resize(*matched + 1);  // ROLLBACK TO keeps the matched savepoint open
                        }
                    } else if(!transactionControl->rollbackToSavepoint) {
                        stack.clear();  // COMMIT / plain ROLLBACK end the transaction and every savepoint
                    }
                    continue;
                }
            }
            return statements;
        }

        /**
         *  Post-pass for the `functional` savepoint style: statements between
         *  `storage.savepoint(name, [&] { … })` and the matching RELEASE move inside the lambda.
         *  A savepoint whose RELEASE never appears in the batch degrades to the manual call so the
         *  emitted code stays valid.
         */
        std::vector<std::string> foldFunctionalSavepoints(std::vector<std::string> statements,
                                                          const std::vector<const AstNode*>& statementNodes) {
            struct OpenWrap {
                std::string name;
                std::string header;
                std::vector<std::string> innerStatements;
            };
            std::vector<OpenWrap> stack;
            std::vector<std::string> out;

            auto appendStatement = [&](std::string statement) {
                if(!stack.empty()) {
                    stack.back().innerStatements.push_back(std::move(statement));
                } else {
                    out.push_back(std::move(statement));
                }
            };
            auto indentBlock = [](const std::string& block) {
                std::string indented = "    ";
                for(const char c : block) {
                    indented += c;
                    if(c == '\n') {
                        indented += "    ";
                    }
                }
                while(indented.ends_with(' ')) {
                    indented.pop_back();
                }
                return indented;
            };

            for(size_t index = 0; index < statements.size(); ++index) {
                const AstNode* node = index < statementNodes.size() ? statementNodes[index] : nullptr;
                std::string& code = statements[index];
                if(const auto* savepointNode = dynamic_cast<const SavepointNode*>(node);
                   savepointNode && code.find(", [&] {") != std::string::npos) {
                    std::string header = code.substr(0, code.find('\n'));
                    stack.push_back(OpenWrap{savepointNode->name, std::move(header), {}});
                    continue;
                }
                if(const auto* releaseNode = dynamic_cast<const ReleaseNode*>(node);
                   releaseNode && !stack.empty() && stack.back().name == releaseNode->name) {
                    OpenWrap wrap = std::move(stack.back());
                    stack.pop_back();
                    std::string block = wrap.header;
                    for(const std::string& inner : wrap.innerStatements) {
                        block += '\n';
                        block += indentBlock(inner);
                    }
                    block += "\n    return true;\n});";
                    appendStatement(std::move(block));
                    continue;
                }
                appendStatement(std::move(code));
            }

            // Unmatched SAVEPOINTs: unwind to plain manual calls followed by their inner statements.
            while(!stack.empty()) {
                OpenWrap wrap = std::move(stack.back());
                stack.pop_back();
                std::vector<std::string> unwound;
                const size_t nameStart = wrap.header.find('(');
                const size_t nameEnd = wrap.header.find(", [&] {");
                unwound.push_back("storage.savepoint" +
                                  wrap.header.substr(nameStart, nameEnd - nameStart) + ");");
                for(std::string& inner : wrap.innerStatements) {
                    unwound.push_back(std::move(inner));
                }
                for(std::string& statement : unwound) {
                    if(!stack.empty()) {
                        stack.back().innerStatements.push_back(std::move(statement));
                    } else {
                        out.push_back(std::move(statement));
                    }
                }
            }
            return out;
        }

    }  // namespace

    std::string joinGeneratedCode(const std::vector<ProcessSqlResult>& results) {
        // Standalone DDL codegen wraps each statement in its own `auto storage = ...`; a batch
        // describes ONE database, so merge every storage argument into a single make_storage().
        static constexpr std::string_view storageMarker = "\nauto storage = make_storage(\"\",\n    ";

        std::vector<std::string> structBlocks;
        std::vector<std::string> storageArguments;
        std::vector<std::string> otherStatements;
        std::vector<const AstNode*> otherStatementNodes;
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
            otherStatementNodes.push_back(result.parseResult.astNodePointer.get());
        }
        otherStatements = resolveGuardSavepoints(std::move(otherStatements), otherStatementNodes);
        otherStatements = foldFunctionalSavepoints(std::move(otherStatements), otherStatementNodes);

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
