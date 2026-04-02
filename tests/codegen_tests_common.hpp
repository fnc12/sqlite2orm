#pragma once

#include <sqlite2orm/codegen.h>

#include <catch2/catch_all.hpp>

#include <string>
#include <string_view>
#include <vector>

using namespace sqlite2orm;

namespace codegen_test_helpers {

    std::string generate(std::string_view sql);
    CodeGenResult generateFull(std::string_view sql);
    CodeGenResult generateWithPolicy(std::string_view sql, const CodeGenPolicy& policy);
    /** Same as `generateWithPolicy`, but omits the `with_cte_style` decision point (matches alternative regeneration). */
    CodeGenResult generateWithPolicySuppressWithCteDp(std::string_view sql, const CodeGenPolicy& policy);
    std::string prefixFor(std::string_view sql);

    bool looksLikeMemberPointer(std::string_view code);

    DecisionPoint columnRefStyleDp(int id, std::string_view memberPointer);
    void appendColumnRefDps(std::vector<DecisionPoint>& out, int& nextId, std::string_view codeStr);

    DecisionPoint apiLevelStarSelectDp(int id, const std::string& structName, const std::string& trailingArgs);

    CodeGenResult expectedBinaryLeaf(std::string_view leftCode, std::string_view rightCode,
                                       std::string_view op, std::string_view funcName, int firstId = 1);

}  // namespace codegen_test_helpers

using namespace codegen_test_helpers;
