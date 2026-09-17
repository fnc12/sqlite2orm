#include "codegen_tests_common.hpp"

TEST_CASE("codegen: PRAGMA user_version getter") {
    REQUIRE(generateFull("PRAGMA user_version;") == CodeGenResult{"storage.pragma.user_version();", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA user_version = 1") {
    REQUIRE(generateFull("PRAGMA user_version = 1;") == CodeGenResult{"storage.pragma.user_version(1);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA journal_mode") {
    REQUIRE(generateFull("PRAGMA journal_mode;") == CodeGenResult{"storage.pragma.journal_mode();", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA journal_mode = WAL") {
    REQUIRE(generateFull("PRAGMA journal_mode = WAL;") ==
            CodeGenResult{"storage.pragma.journal_mode(sqlite_orm::journal_mode::WAL);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA table_info") {
    REQUIRE(generateFull("PRAGMA table_info('users');") ==
            CodeGenResult{R"(storage.pragma.table_info("users");)", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = ON") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = ON;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 'yes'") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 'yes';") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 2 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 2;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 2: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 010 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 010;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 010: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 1.5 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 1.5;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 1.5: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x7FFFFFFF is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x7FFFFFFF;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x7FFFFFFF: SQLite reads this as true; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x80000000 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x80000000;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x80000000: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 2147483648 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 2147483648;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 2147483648: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = -1 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = -1;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = -1: SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = an unknown name is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = blah;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = blah: SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = '2abc' is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = '2abc';") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = '2abc': SQLite reads this as true; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = '  1' warns about the value as written") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = '  1';") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = '  1': SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = '' warns about the value as written") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = '';") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = '': SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = NULL is an error, not silence") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = NULL;") ==
            CodeGenResult{"",
                          {},
                          {},
                          {"PRAGMA recursive_triggers = …: expected a number or a name, as in 0/1, TRUE/FALSE or "
                           "ON/OFF"}});
}
