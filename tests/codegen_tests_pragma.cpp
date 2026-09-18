#include "codegen_tests_common.hpp"

TEST_CASE("codegen: PRAGMA user_version getter") {
    REQUIRE(generateFull("PRAGMA user_version;") == CodeGenResult{"storage.pragma.user_version();", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA user_version = 1") {
    REQUIRE(generateFull("PRAGMA user_version = 1;") == CodeGenResult{"storage.pragma.user_version(1);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA user_version = 010") {
    REQUIRE(generateFull("PRAGMA user_version = 010;") ==
            CodeGenResult{"storage.pragma.user_version(10);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA integrity_check = 010") {
    REQUIRE(generateFull("PRAGMA integrity_check = 010;") ==
            CodeGenResult{"storage.pragma.integrity_check(10);", {}, {}, {}});
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

TEST_CASE("codegen: PRAGMA recursive_triggers = 01") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 01;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 01: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 00") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 00;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 00: SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead"}},
                          {}});
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

TEST_CASE("codegen: PRAGMA recursive_triggers = 255 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 255;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 255: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 256 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 256;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 256: SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 257 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 257;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 257: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 511 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 511;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 511: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x100 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x100;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x100: SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 65536 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 65536;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 65536: SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 2147483392 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 2147483392;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 2147483392: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = '256' is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = '256';") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = '256': SQLite reads this as false; spell it "
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

TEST_CASE("codegen: PRAGMA recursive_triggers = a double-quoted 256 warns about the value as written") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = \"256\";") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = \"256\": SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead"}},
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

TEST_CASE("codegen: PRAGMA recursive_triggers = 2147483649 is false, like SQLite") {
    // Past int32 the value reads as 0 before the low byte is taken, so the low byte of the
    // number itself (1) must not leak through.
    REQUIRE(generateFull("PRAGMA recursive_triggers = 2147483649;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 2147483649: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x1FFFFFFFF is false, like SQLite") {
    // A ninth hexadecimal digit makes the whole value read as 0, where the first eight digits
    // alone (0x1FFFFFFF) would have been true.
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x1FFFFFFFF;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x1FFFFFFFF: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x1FFFFFFF is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x1FFFFFFF;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x1FFFFFFF: SQLite reads this as true; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead"}},
                          {}});
}

// A PRAGMA value is not an expression: SQLite never compiles it, so `PRAGMA user_version =
// 0x10000000000000000` is accepted where `SELECT 0x10000000000000000` is refused, and read with
// sqlite3GetInt32(), which answers 0 for a value it cannot fit in an int32 — the schema then
// reports `PRAGMA user_version` as 0. Checked against sqlite3 3.51.
TEST_CASE("codegen: PRAGMA user_version = a hex literal too big for an int64") {
    REQUIRE(generateFull("PRAGMA user_version = 0x10000000000000000;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 0x10000000000000000: SQLite reads a PRAGMA value "
                                          "as a 32-bit integer and this hex literal does not fit one, so it sets "
                                          "0"}},
                          {}});
    REQUIRE(generateFull("PRAGMA max_page_count = 0x1_0000_0000_0000_0000;") ==
            CodeGenResult{"storage.pragma.max_page_count(0);",
                          {},
                          {CodegenWarning{"PRAGMA max_page_count = 0x10000000000000000: SQLite reads a PRAGMA "
                                          "value as a 32-bit integer and this hex literal does not fit one, so it "
                                          "sets 0"}},
                          {}});
}

// `PRAGMA integrity_check` is the one that takes a value SQLite falls back to reading as a table
// name, so a hex literal it cannot fit in an int32 is `Error: in prepare, no such table`.
TEST_CASE("codegen: PRAGMA integrity_check = a hex literal too big for an int64") {
    REQUIRE(generateFull("PRAGMA integrity_check = 0x10000000000000000;") ==
            CodeGenResult{{},
                          {},
                          {},
                          {"PRAGMA integrity_check = 0x10000000000000000: SQLite cannot read this hex literal as a "
                           "32-bit integer and refuses it as a table name"},
                          {}});
}

// The boolean PRAGMAs read the same int32, so the value is false and the spelling warning stands.
TEST_CASE("codegen: PRAGMA recursive_triggers = a hex literal too big for an int64") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x10000000000000000;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x10000000000000000: SQLite reads this as "
                                          "false; spell it 0/1, TRUE/FALSE or ON/OFF instead"}},
                          {}});
}
