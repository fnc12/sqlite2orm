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

}  // namespace sqlite2orm::parser_test_helpers
