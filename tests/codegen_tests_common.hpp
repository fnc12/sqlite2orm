#pragma once

#include <sqlite2orm/codegen.h>

#include <catch2/catch_all.hpp>

#include <string>
#include <string_view>
#include <vector>

using namespace sqlite2orm;

namespace codegen_test_helpers {

    /**
     *  What a statement whose code would hold a placeholder standing in an expression slot warns
     *  instead of handing out a header that cannot be built.
     */
    inline const std::string kStatementNotGenerated =
        "a construct in this statement is not mapped to sqlite_orm, so the statement is not generated";

    /**
     *  Why a BLOB literal is left out of a clause sqlite_orm writes into the schema as text, as the
     *  middle of the warning: a site names the clause before it and what the clause becomes without
     *  the literal after it. The whole sentence is spelled out once in
     *  `codegen: CREATE INDEX over a BLOB literal is not generated`.
     */
    inline std::string kDdlBlobLiteralReason(std::string_view literal) {
        return std::string(literal) +
               ", a BLOB literal in a clause sqlite_orm writes into the schema as text: it prints a blob as the "
               "bytes themselves inside x'…' instead of as their hex digits, which SQLite reads as a different "
               "value where every byte of the blob is a hex digit and refuses as an unrecognized token where "
               "one is not";
    }

    std::string generate(std::string_view sql);
    CodeGenResult generateFull(std::string_view sql);
    /**
     *  Codegen of the last statement of a multi-statement batch, so that a DML statement is
     *  generated with the CREATE TABLE statements before it already known.
     */
    CodeGenResult generateLastOfBatch(std::string_view sql);
    CodeGenResult generateWithPolicy(std::string_view sql, const CodeGenPolicy& policy);
    /** Same as `generateWithPolicy`, but omits the `with_cte_style` decision point (matches alternative regeneration). */
    CodeGenResult generateWithPolicySuppressWithCteDp(std::string_view sql, const CodeGenPolicy& policy);
    std::string prefixFor(std::string_view sql);

    bool looksLikeMemberPointer(std::string_view code);

    DecisionPoint columnRefStyleDp(int id, std::string_view memberPointer);
    void appendColumnRefDps(std::vector<DecisionPoint>& out, int& nextId, std::string_view codeStr);

    DecisionPoint apiLevelStarSelectDp(int id, const std::string& structName, const std::string& trailingArgs);

    CodeGenResult expectedBinaryLeaf(std::string_view leftCode,
                                     std::string_view rightCode,
                                     std::string_view op,
                                     std::string_view funcName,
                                     int firstId = 1);

}  // namespace codegen_test_helpers

using namespace codegen_test_helpers;
