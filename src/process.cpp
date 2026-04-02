#include <sqlite2orm/process.h>
#include <sqlite2orm/tokenizer.h>
#include <sqlite2orm/validator.h>

namespace sqlite2orm {

    ProcessSqlResult processSql(std::string_view sql) {
        return processSql(sql, nullptr);
    }

    ProcessSqlResult processSql(std::string_view sql, const CodeGenPolicy* policy) {
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

}  // namespace sqlite2orm
