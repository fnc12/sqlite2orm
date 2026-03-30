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

}  // namespace sqlite2orm
