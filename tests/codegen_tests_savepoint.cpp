#include "codegen_tests_common.hpp"

TEST_CASE("codegen: SAVEPOINT") {
    REQUIRE(generate("SAVEPOINT sp1;") == "storage.savepoint(\"sp1\");");
}

TEST_CASE("codegen: SAVEPOINT - RAII/functional comment attached") {
    auto result = generateFull("SAVEPOINT sp1;");
    REQUIRE(result.comments ==
        std::vector<std::string>{
            "sqlite_orm also offers a RAII variant (`auto guard = storage.savepoint_guard(name)`, rolls back and "
            "releases in its destructor unless `guard.release()` is called) and a functional one "
            "(`storage.savepoint(name, lambda)`); the direct calls here mirror the SQL statements 1:1."});
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
