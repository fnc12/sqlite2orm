#include "codegen_tests_common.hpp"

TEST_CASE("codegen: PRAGMA user_version getter") {
    REQUIRE(generate_full("PRAGMA user_version;") == CodeGenResult{"storage.pragma.user_version();", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA user_version = 1") {
    REQUIRE(generate_full("PRAGMA user_version = 1;") == CodeGenResult{"storage.pragma.user_version(1);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA journal_mode") {
    REQUIRE(generate_full("PRAGMA journal_mode;") == CodeGenResult{"storage.pragma.journal_mode();", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA journal_mode = WAL") {
    REQUIRE(generate_full("PRAGMA journal_mode = WAL;") ==
            CodeGenResult{"storage.pragma.journal_mode(sqlite_orm::journal_mode::WAL);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA table_info") {
    REQUIRE(generate_full("PRAGMA table_info('users');") ==
            CodeGenResult{R"(storage.pragma.table_info("users");)", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = ON") {
    REQUIRE(generate_full("PRAGMA recursive_triggers = ON;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);", {}, {}, {}});
}
