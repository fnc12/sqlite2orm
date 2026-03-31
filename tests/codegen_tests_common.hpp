#pragma once

#include <sqlite2orm/codegen.h>

#include <catch2/catch_all.hpp>

#include <string>
#include <string_view>
#include <vector>

using namespace sqlite2orm;

namespace codegen_test_helpers {

    std::string generate(std::string_view sql);
    CodeGenResult generate_full(std::string_view sql);
    CodeGenResult generate_with_policy(std::string_view sql, const CodeGenPolicy& policy);
    std::string prefix_for(std::string_view sql);

    bool looks_like_member_pointer(std::string_view code);

    DecisionPoint column_ref_style_dp(int id, std::string_view memberPointer);
    void append_column_ref_dps(std::vector<DecisionPoint>& out, int& nextId, std::string_view codeStr);

    DecisionPoint api_level_star_select_dp(int id, const std::string& structName,
                                           const std::string& trailingArgs);

    CodeGenResult expected_binary_leaf(std::string_view leftCode, std::string_view rightCode,
                                       std::string_view op, std::string_view funcName, int firstId = 1);

}  // namespace codegen_test_helpers

using namespace codegen_test_helpers;
