#include "parser_tests_common.hpp"

namespace sqlite2orm::parser_test_helpers {

    ParseResult parse(std::string_view sql) {
        Tokenizer tokenizer;
        auto tokens = tokenizer.tokenize(sql);
        Parser parser;
        return parser.parse(std::move(tokens));
    }

    ColumnDef columnWithDefault(std::string name, std::string typeName, std::shared_ptr<AstNode> defaultValue) {
        ColumnDef def;
        def.name = std::move(name);
        def.typeName = std::move(typeName);
        def.defaultValue = std::move(defaultValue);
        return def;
    }

    std::vector<FromClauseItem> fromOne(std::string_view tableName) {
        return {FromClauseItem{JoinKind::none,
                               FromTableClause{std::nullopt, std::string(tableName), std::nullopt},
                               nullptr,
                               {}}};
    }

    std::string repeated(size_t count, std::string_view separator, std::string_view item) {
        std::string text;
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) {
                text += separator;
            }
            text += item;
        }
        return text;
    }

}  // namespace sqlite2orm::parser_test_helpers
