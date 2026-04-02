#include "codegen_tests_common.hpp"

#include <sqlite2orm/tokenizer.h>
#include <sqlite2orm/parser.h>

namespace codegen_test_helpers {

    void setSuppressWithCteStyleDecisionPointForTests(sqlite2orm::CodeGenerator& generator, bool suppress) {
        generator.suppressWithCteStyleDecisionPoint = suppress;
    }

    std::string generate(std::string_view sql) {
        Tokenizer tokenizer;
        auto tokens = tokenizer.tokenize(sql);
        Parser parser;
        auto parseResult = parser.parse(std::move(tokens));
        REQUIRE(parseResult);
        CodeGenerator codeGenerator;
        return codeGenerator.generate(*parseResult.astNodePointer).code;
    }

    CodeGenResult generate_full(std::string_view sql) {
        Tokenizer tokenizer;
        auto tokens = tokenizer.tokenize(sql);
        Parser parser;
        auto parseResult = parser.parse(std::move(tokens));
        REQUIRE(parseResult);
        CodeGenerator codeGenerator;
        return codeGenerator.generate(*parseResult.astNodePointer);
    }

    CodeGenResult generate_with_policy(std::string_view sql, const CodeGenPolicy& policy) {
        Tokenizer tokenizer;
        auto tokens = tokenizer.tokenize(sql);
        Parser parser;
        auto parseResult = parser.parse(std::move(tokens));
        REQUIRE(parseResult);
        CodeGenerator codeGenerator;
        codeGenerator.codeGenPolicy = &policy;
        return codeGenerator.generate(*parseResult.astNodePointer);
    }

    CodeGenResult generate_with_policy_suppress_with_cte_dp(std::string_view sql, const CodeGenPolicy& policy) {
        Tokenizer tokenizer;
        auto tokens = tokenizer.tokenize(sql);
        Parser parser;
        auto parseResult = parser.parse(std::move(tokens));
        REQUIRE(parseResult);
        CodeGenerator codeGenerator;
        codeGenerator.codeGenPolicy = &policy;
        setSuppressWithCteStyleDecisionPointForTests(codeGenerator, true);
        return codeGenerator.generate(*parseResult.astNodePointer);
    }

    std::string prefix_for(std::string_view sql) {
        Tokenizer tokenizer;
        auto tokens = tokenizer.tokenize(sql);
        Parser parser;
        auto parseResult = parser.parse(std::move(tokens));
        REQUIRE(parseResult);
        CodeGenerator codeGenerator;
        codeGenerator.generate(*parseResult.astNodePointer);
        return codeGenerator.generatePrefix();
    }

    bool looks_like_member_pointer(std::string_view code) {
        return code.size() >= 5 && code[0] == '&' && code.find("::") != std::string_view::npos;
    }

    DecisionPoint column_ref_style_dp(int id, std::string_view memberPointer) {
        std::string mp(memberPointer);
        auto colons = mp.find("::");
        std::string structName = mp.substr(1, colons - 1);
        std::string columnPointer = "column<" + structName + ">(" + mp + ")";
        return DecisionPoint{
            id,
            "column_ref_style",
            "member_pointer",
            mp,
            {Alternative{"column_pointer", columnPointer, "explicit mapped type (inheritance / ambiguity)"}}};
    }

    void append_column_ref_dps(std::vector<DecisionPoint>& out, int& nextId, std::string_view codeStr) {
        if(looks_like_member_pointer(codeStr)) {
            out.push_back(column_ref_style_dp(nextId++, codeStr));
        }
    }

    DecisionPoint api_level_star_select_dp(int id, const std::string& structName,
                                           const std::string& trailingArgs) {
        std::string code = "auto rows = storage.get_all<" + structName + ">(" + trailingArgs + ");";
        std::string tail = trailingArgs.empty() ? "" : (", " + trailingArgs);
        std::string codeSelectObject = "auto rows = storage.select(object<" + structName + ">()" + tail + ");";
        std::string codeSelectAsterisk = "auto rows = storage.select(asterisk<" + structName + ">()" + tail + ");";
        return DecisionPoint{
            id,
            "api_level",
            "get_all",
            code,
            {Alternative{"select_object",
                         codeSelectObject,
                         "select(object<T>(), ...) returns std::tuple of columns"},
             Alternative{"select_asterisk",
                         codeSelectAsterisk,
                         "select(asterisk<T>(), ...) returns full row objects"}}};
    }

    CodeGenResult expected_binary_leaf(std::string_view leftCode, std::string_view rightCode,
                                       std::string_view op, std::string_view funcName, int firstId) {
        std::string l(leftCode);
        std::string r(rightCode);
        std::string cl = "c(" + l + ")";
        std::string cr = "c(" + r + ")";
        std::string o(op);
        std::string wrapLeft = cl + o + r;
        std::string wrapRight = l + o + cr;
        std::string wrapBoth = cl + o + cr;
        std::string functional = std::string(funcName) + "(" + l + ", " + r + ")";
        int nextId = firstId;
        std::vector<DecisionPoint> dps;
        append_column_ref_dps(dps, nextId, leftCode);
        append_column_ref_dps(dps, nextId, rightCode);
        dps.push_back(DecisionPoint{nextId, "expr_style", "operator_wrap_left", wrapLeft,
                                    {
                                        Alternative{"operator_wrap_right", wrapRight, "wrap right operand"},
                                        Alternative{"functional", functional, "functional style"},
                                        Alternative{"operator_wrap_both", wrapBoth, "wrap both operands", true},
                                    }});
        return CodeGenResult{wrapLeft, std::move(dps)};
    }

}  // namespace codegen_test_helpers
