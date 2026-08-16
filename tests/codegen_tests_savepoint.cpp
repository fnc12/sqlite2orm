#include "codegen_tests_common.hpp"

TEST_CASE("codegen: SAVEPOINT") {
    REQUIRE(generate("SAVEPOINT sp1;") == "storage.savepoint(\"sp1\");");
}

TEST_CASE("codegen: SAVEPOINT - savepoint_style decision point with three alternatives") {
    auto result = generateFull("SAVEPOINT sp1;");
    REQUIRE(result.decisionPoints.size() == 1);
    const auto& dp = result.decisionPoints.front();
    REQUIRE(dp.category == "savepoint_style");
    REQUIRE(dp.chosenValue == "manual");
    REQUIRE(dp.chosenCode == "storage.savepoint(\"sp1\");");
    REQUIRE(dp.alternatives.size() == 3);
    REQUIRE(dp.alternatives[0].value == "manual");
    REQUIRE(dp.alternatives[1].value == "guard");
    REQUIRE(dp.alternatives[1].code == "auto sp1_savepoint = storage.savepoint_guard(\"sp1\");");
    REQUIRE(dp.alternatives[2].value == "functional");
    REQUIRE(dp.alternatives[2].code ==
        "storage.savepoint(\"sp1\", [&] {\n    return true;\n});");
}

TEST_CASE("codegen: savepoint_style=guard") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["savepoint_style"] = "guard";
    REQUIRE(generateWithPolicy("SAVEPOINT sp1;", policy).code ==
        "auto sp1_savepoint = storage.savepoint_guard(\"sp1\");");
    REQUIRE(generateWithPolicy("RELEASE SAVEPOINT sp1;", policy).code == "sp1_savepoint.release();");
    REQUIRE(generateWithPolicy("ROLLBACK TO SAVEPOINT sp1;", policy).code == "sp1_savepoint.rollback_to();");
}

TEST_CASE("codegen: savepoint_style=guard - name is sanitized for the variable") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["savepoint_style"] = "guard";
    REQUIRE(generateWithPolicy("SAVEPOINT \"sp one\";", policy).code ==
        "auto sp_one_savepoint = storage.savepoint_guard(\"sp one\");");
}

TEST_CASE("codegen: savepoint_style=functional - standalone statement") {
    CodeGenPolicy policy;
    policy.chosenAlternativeValueByCategory["savepoint_style"] = "functional";
    REQUIRE(generateWithPolicy("SAVEPOINT sp1;", policy).code ==
        "storage.savepoint(\"sp1\", [&] {\n    return true;\n});");
    // RELEASE / ROLLBACK TO keep the direct calls; joinGeneratedCode folds a
    // matching RELEASE into the lambda at batch level.
    REQUIRE(generateWithPolicy("RELEASE SAVEPOINT sp1;", policy).code ==
        "storage.release_savepoint(\"sp1\");");
    REQUIRE(generateWithPolicy("ROLLBACK TO SAVEPOINT sp1;", policy).code ==
        "storage.rollback_to_savepoint(\"sp1\");");
}

TEST_CASE("codegen: RELEASE SAVEPOINT") {
    REQUIRE(generate("RELEASE SAVEPOINT sp1;") == "storage.release_savepoint(\"sp1\");");
}

TEST_CASE("codegen: RELEASE without SAVEPOINT keyword") {
    REQUIRE(generate("RELEASE sp1;") == "storage.release_savepoint(\"sp1\");");
}

TEST_CASE("codegen: ROLLBACK TO SAVEPOINT") {
    REQUIRE(generate("ROLLBACK TO SAVEPOINT sp1;") == "storage.rollback_to_savepoint(\"sp1\");");
}

TEST_CASE("codegen: ROLLBACK TRANSACTION TO SAVEPOINT") {
    REQUIRE(generate("ROLLBACK TRANSACTION TO SAVEPOINT sp1;") == "storage.rollback_to_savepoint(\"sp1\");");
}

TEST_CASE("codegen: plain ROLLBACK still maps to storage.rollback") {
    REQUIRE(generate("ROLLBACK;") == "storage.rollback();");
}

TEST_CASE("codegen: savepoint name with quote is escaped") {
    REQUIRE(generate("SAVEPOINT \"sp one\";") == "storage.savepoint(\"sp one\");");
}
