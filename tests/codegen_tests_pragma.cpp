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
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 2}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 00") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 00;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 00: SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 2}},
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
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 1}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 010 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 010;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 010: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 3}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 1.5 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 1.5;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 1.5: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 3}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x7FFFFFFF is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x7FFFFFFF;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x7FFFFFFF: SQLite reads this as true; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 10}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x80000000 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x80000000;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x80000000: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 10}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 2147483648 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 2147483648;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 2147483648: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 10}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = -1 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = -1;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = -1: SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 2}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = an unknown name is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = blah;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = blah: SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 4}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = '2abc' is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = '2abc';") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = '2abc': SQLite reads this as true; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 6}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 255 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 255;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 255: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 3}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 256 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 256;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 256: SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 3}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 257 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 257;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 257: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 3}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 511 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 511;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 511: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 3}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x100 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x100;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x100: SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 5}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 65536 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 65536;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 65536: SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 5}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 2147483392 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 2147483392;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 2147483392: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 10}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = '256' is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = '256';") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = '256': SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 5}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = '  1' warns about the value as written") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = '  1';") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = '  1': SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 5}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = '' warns about the value as written") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = '';") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = '': SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 2}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = a double-quoted 256 warns about the value as written") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = \"256\";") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = \"256\": SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 5}},
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
                                          "it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 10}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x1FFFFFFFF is false, like SQLite") {
    // A ninth hexadecimal digit makes the whole value read as 0, where the first eight digits
    // alone (0x1FFFFFFF) would have been true.
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x1FFFFFFFF;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x1FFFFFFFF: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 11}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x1FFFFFFF is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x1FFFFFFF;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x1FFFFFFF: SQLite reads this as true; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 10}},
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
                                          "as a 32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 19}},
                          {}});
    REQUIRE(generateFull("PRAGMA max_page_count = 0x1_0000_0000_0000_0000;") ==
            CodeGenResult{"storage.pragma.max_page_count(0);",
                          {},
                          {CodegenWarning{"PRAGMA max_page_count = 0x10000000000000000: SQLite reads a PRAGMA "
                                          "value as a 32-bit integer and this hex literal does not fit one, so it "
                                          "sets 0",
                                          SourceLocation{1, 25}, 23}},
                          {}});
}

// `PRAGMA integrity_check` is the one that takes a value SQLite falls back to reading as a table
// name, so a hex literal it cannot fit in an int32 is `Error: in prepare, no such table`.
TEST_CASE("codegen: PRAGMA integrity_check = a hex literal too big for an int64") {
    REQUIRE(generateFull("PRAGMA integrity_check = 0x10000000000000000;") ==
            CodeGenResult{{},
                          {},
                          {},
                          {"PRAGMA integrity_check = 0x10000000000000000: SQLite cannot read this literal as a "
                           "32-bit integer and refuses it as a table name"},
                          {}});
}

// The boolean PRAGMAs read the same int32, so the value is false and the spelling warning stands.
TEST_CASE("codegen: PRAGMA recursive_triggers = a hex literal too big for an int64") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x10000000000000000;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x10000000000000000: SQLite reads this as "
                                          "false; spell it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 19}},
                          {}});
}

// A PRAGMA value is a name to SQLite, so the keywords its parser falls back to an identifier are
// values like any other. `getSafetyLevel()` reads `no` as 0 and `full` as 3, and then drops the 3
// because `recursive_triggers` asks it to omit the levels above 1 — both are false. Checked
// against sqlite3 3.51: `PRAGMA recursive_triggers = no; PRAGMA recursive_triggers;` answers 0.
TEST_CASE("codegen: PRAGMA recursive_triggers = a keyword") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = no;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA recursive_triggers = full;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = full: SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 4}},
                          {}});
    REQUIRE(generateFull("PRAGMA recursive_triggers = DEFAULT;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = DEFAULT: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 7}},
                          {}});
}

// `CURRENT_DATE` and its siblings are names here too, not the datetime they stand for in an
// expression, so the value is what those letters read as: 0, because they start with no digit.
TEST_CASE("codegen: PRAGMA recursive_triggers = CURRENT_TIMESTAMP") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = CURRENT_TIMESTAMP;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = current_timestamp: SQLite reads this as "
                                          "false; spell it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29}, 17}},
                          {}});
}

// DELETE is SQLite's default journal mode and EXCLUSIVE one of the two locking modes, and both
// spell a keyword — neither reached codegen before the value took a name.
TEST_CASE("codegen: PRAGMA journal_mode = DELETE") {
    REQUIRE(generateFull("PRAGMA journal_mode = DELETE;") ==
            CodeGenResult{"storage.pragma.journal_mode(sqlite_orm::journal_mode::DELETE);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA locking_mode = EXCLUSIVE") {
    REQUIRE(generateFull("PRAGMA locking_mode = EXCLUSIVE;") ==
            CodeGenResult{"storage.pragma.locking_mode(sqlite_orm::locking_mode::EXCLUSIVE);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA table_info of a table named after a keyword") {
    REQUIRE(generateFull("PRAGMA table_info(row);") ==
            CodeGenResult{R"(storage.pragma.table_info("row");)", {}, {}, {}});
}

// SQLite reads a PRAGMA value with `sqlite3GetInt32()`, which refuses every hexadecimal value with
// the sign bit set, so `PRAGMA user_version = 0x80000000` assigns 0 — checked against sqlite3
// 3.51.0 and 3.45.1: `user_version = 42` followed by `user_version = 0x80000000` reads back 0.
TEST_CASE("codegen: PRAGMA user_version = a hex literal past the int32 range sets 0, like SQLite") {
    REQUIRE(generateFull("PRAGMA user_version = 0x80000000;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 0x80000000: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 10}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = 0xFFFFFFFF;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 0xFFFFFFFF: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 10}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = 0x100000000;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 0x100000000: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 11}},
                          {}});
}

// The decimal half of the same range: eleven digits, or ten digits above 2147483647.
TEST_CASE("codegen: PRAGMA user_version = a decimal literal past the int32 range sets 0, like SQLite") {
    REQUIRE(generateFull("PRAGMA user_version = 2147483648;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 2147483648: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 10}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = 4294967296;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 4294967296: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 10}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = -2147483649;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = -2147483649: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 11}},
                          {}});
}

// The edges themselves are generated as they are written, warning and all left out: SQLite reads
// `2147483647` and `-2147483648` back unchanged, and `0x7FFFFFFF` is the last hex literal it takes.
TEST_CASE("codegen: PRAGMA user_version at the int32 edges is generated as written") {
    REQUIRE(generateFull("PRAGMA user_version = 2147483647;") ==
            CodeGenResult{"storage.pragma.user_version(2147483647);", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA user_version = -2147483648;") ==
            CodeGenResult{"storage.pragma.user_version(-2147483648);", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA user_version = 0x7FFFFFFF;") ==
            CodeGenResult{"storage.pragma.user_version(0x7FFFFFFF);", {}, {}, {}});
}

// `sqlite3GetInt32()` takes the sign off before it looks for a `0x` prefix, so a minus sign sends a
// hexadecimal value down the decimal branch, which stops at the `x`: SQLite sets 0, not -16.
TEST_CASE("codegen: PRAGMA user_version = a negated hex literal sets 0, like SQLite") {
    REQUIRE(generateFull("PRAGMA user_version = -0x10;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = -0x10: SQLite reads a PRAGMA value as a 32-bit "
                                          "integer, so it sets 0",
                                          SourceLocation{1, 23}, 5}},
                          {}});
}

// A PRAGMA value is text, never an expression, so SQLite reads a string, a name and a REAL with the
// same `sqlite3GetInt32()`: `'12'` is 12, `1.5` is 1 — the digits stop at the dot — and a name is 0.
TEST_CASE("codegen: PRAGMA user_version = a value that is not an integer literal") {
    REQUIRE(generateFull("PRAGMA user_version = 1.5;") ==
            CodeGenResult{"storage.pragma.user_version(1);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 1.5: SQLite reads a PRAGMA value as a 32-bit "
                                          "integer, so it sets 1",
                                          SourceLocation{1, 23}, 3}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = '12';") ==
            CodeGenResult{"storage.pragma.user_version(12);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = '12': SQLite reads a PRAGMA value as a 32-bit "
                                          "integer, so it sets 12",
                                          SourceLocation{1, 23}, 4}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = abc;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = abc: SQLite reads a PRAGMA value as a 32-bit "
                                          "integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 3}},
                          {}});
}

// SQLite's grammar takes a number or a name after `PRAGMA name =`, and nothing else: `= NULL` is
// `near "NULL": syntax error`.
TEST_CASE("codegen: PRAGMA user_version = NULL is an error, not nullptr") {
    REQUIRE(generateFull("PRAGMA user_version = NULL;") ==
            CodeGenResult{"",
                          {},
                          {},
                          {"PRAGMA user_version = …: expected a number, a string or a name"},
                          {}});
}

// `application_id` and `busy_timeout` reach `sqlite3Atoi()` the same way `user_version` does, so
// the fold is theirs too; `busy_timeout` then takes the int32 as milliseconds.
TEST_CASE("codegen: the other sqlite3Atoi PRAGMAs fold a value past the int32 range the same way") {
    REQUIRE(generateFull("PRAGMA application_id = 2147483648;") ==
            CodeGenResult{"storage.pragma.application_id(0);",
                          {},
                          {CodegenWarning{"PRAGMA application_id = 2147483648: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 25}, 10}},
                          {}});
    REQUIRE(generateFull("PRAGMA busy_timeout = 0x80000000;") ==
            CodeGenResult{"storage.pragma.busy_timeout(0);",
                          {},
                          {CodegenWarning{"PRAGMA busy_timeout = 0x80000000: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 10}},
                          {}});
}

// The three PRAGMAs that do not reach `sqlite3Atoi()` keep their value: `getSafetyLevel()` answers
// its default 1 for a value that does not start with a digit, so `synchronous = -0x10` is 1 and not
// 0; `getAutoVacuum()` reads the name `incremental` as 2; and `max_page_count` reads the whole text
// as an int64, so `= 0x80000000` really does set 2147483648. Checked against sqlite3 3.51.0.
TEST_CASE("codegen: the PRAGMAs with a reader of their own keep a value past the int32 range") {
    REQUIRE(generateFull("PRAGMA max_page_count = 0x80000000;") ==
            CodeGenResult{"storage.pragma.max_page_count(static_cast<int64_t>(0x80000000));", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA max_page_count = 2147483648;") ==
            CodeGenResult{"storage.pragma.max_page_count(2147483648);", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA synchronous = -0x10;") ==
            CodeGenResult{"storage.pragma.synchronous(-0x10);", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA auto_vacuum = 4294967296;") ==
            CodeGenResult{"storage.pragma.auto_vacuum(4294967296);", {}, {}, {}});
}

// The value `PRAGMA integrity_check` cannot read as an int32 is the one it falls back to reading as
// a table name, so the whole int32 range behaves like the too-big hex literal already did.
TEST_CASE("codegen: PRAGMA integrity_check = a literal past the int32 range is an error") {
    REQUIRE(generateFull("PRAGMA integrity_check = 2147483648;") ==
            CodeGenResult{{},
                          {},
                          {},
                          {"PRAGMA integrity_check = 2147483648: SQLite cannot read this literal as a 32-bit "
                           "integer and refuses it as a table name"},
                          {}});
    REQUIRE(generateFull("PRAGMA integrity_check = 0x80000000;") ==
            CodeGenResult{{},
                          {},
                          {},
                          {"PRAGMA integrity_check = 0x80000000: SQLite cannot read this literal as a 32-bit "
                           "integer and refuses it as a table name"},
                          {}});
    REQUIRE(generateFull("PRAGMA integrity_check = 0x7FFFFFFF;") ==
            CodeGenResult{"storage.pragma.integrity_check(0x7FFFFFFF);", {}, {}, {}});
}

// A `_` digit separator inside a PRAGMA value is a syntax error to SQLite, which lexes such a value
// by its own rules — a standing difference of its own — but it must not make the int32 rule read a
// different literal than the generated C++ does: `0x8_0000000` is `0x80000000` to both.
TEST_CASE("codegen: PRAGMA user_version = a separated hex literal past the int32 range sets 0") {
    REQUIRE(generateFull("PRAGMA user_version = 0x8_0000000;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 0x8_0000000: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 11}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = 2_147_483_648;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 2_147_483_648: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 13}},
                          {}});
    REQUIRE(generateFull("PRAGMA integrity_check = 0x8_0000000;") ==
            CodeGenResult{{},
                          {},
                          {},
                          {"PRAGMA integrity_check = 0x80000000: SQLite cannot read this literal as a 32-bit "
                           "integer and refuses it as a table name"},
                          {}});
}

// A string is read with its separators: `sqlite3Atoi('1_2')` stops at the `_` and answers 1.
TEST_CASE("codegen: PRAGMA user_version = a string whose digits stop at an underscore") {
    REQUIRE(generateFull("PRAGMA user_version = '1_2';") ==
            CodeGenResult{"storage.pragma.user_version(1);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = '1_2': SQLite reads a PRAGMA value as a 32-bit "
                                          "integer, so it sets 1",
                                          SourceLocation{1, 23}, 5}},
                          {}});
}

// QA: `sqlite3GetInt32()` skips any number of leading zeros and only then reads eight hexadecimal
// digits, so the zeros never count towards that window: `0x0000000007FFFFFFF` is 2147483647 and is
// generated as written, while `0x000000000080000000` still has the sign bit set and sets 0.
// Checked against the sqlite3 3.51.0 and 3.45.1 CLIs.
TEST_CASE("codegen: PRAGMA user_version = a hex literal whose leading zeros precede eight digits") {
    REQUIRE(generateFull("PRAGMA user_version = 0x0000000007FFFFFFF;") ==
            CodeGenResult{"storage.pragma.user_version(0x0000000007FFFFFFF);", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA user_version = 0x000000000080000000;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 0x000000000080000000: SQLite reads a PRAGMA value "
                                          "as a 32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 20}},
                          {}});
}

// QA: a string is read by the very same rules and nothing is trimmed off it first, so a leading
// space refuses the whole value where a leading `+` does not, and a string spelling a hexadecimal
// value takes the hexadecimal branch because no sign shuts it off. Checked against sqlite3 3.51.0.
TEST_CASE("codegen: PRAGMA user_version = a string SQLite reads by its own rules") {
    REQUIRE(generateFull("PRAGMA user_version = ' 12';") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = ' 12': SQLite reads a PRAGMA value as a 32-bit "
                                          "integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 5}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = '+12';") ==
            CodeGenResult{"storage.pragma.user_version(12);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = '+12': SQLite reads a PRAGMA value as a 32-bit "
                                          "integer, so it sets 12",
                                          SourceLocation{1, 23}, 5}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = '0x10';") ==
            CodeGenResult{"storage.pragma.user_version(16);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = '0x10': SQLite reads a PRAGMA value as a 32-bit "
                                          "integer, so it sets 16",
                                          SourceLocation{1, 23}, 6}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = '';") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = '': SQLite reads a PRAGMA value as a 32-bit "
                                          "integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 2}},
                          {}});
}

// QA: the digits of a REAL stop at the first character that is not one, exponent included, so
// `1e3` is 1 rather than 1000 and `0.9` is 0 rather than a rounded 1. Checked against sqlite3 3.51.0.
TEST_CASE("codegen: PRAGMA user_version = a REAL is read by its leading digits only") {
    REQUIRE(generateFull("PRAGMA user_version = 1e3;") ==
            CodeGenResult{"storage.pragma.user_version(1);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 1e3: SQLite reads a PRAGMA value as a 32-bit "
                                          "integer, so it sets 1",
                                          SourceLocation{1, 23}, 3}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = 0.9;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 0.9: SQLite reads a PRAGMA value as a 32-bit "
                                          "integer, so it sets 0",
                                          SourceLocation{1, 23}, 3}},
                          {}});
}

// A warning about a PRAGMA value carries the span of that value, so that a consumer underlines the
// text the message is about rather than the whole statement. The span is the value as written —
// quotes, digit separators and the minus sign included — even where the message spells it back
// differently, and it follows the value onto whatever line it is written on.
TEST_CASE("codegen: a PRAGMA value warning is anchored at the value as written") {
    // `'12'` sits at line 1, column 23 of the SQL and is 4 characters long, quotes and all.
    REQUIRE(generateFull("PRAGMA user_version = '12';") ==
            CodeGenResult{"storage.pragma.user_version(12);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = '12': SQLite reads a PRAGMA value as a 32-bit "
                                          "integer, so it sets 12",
                                          SourceLocation{1, 23}, 4}},
                          {}});
    // The minus sign belongs to the value the message quotes, so the underline starts at the sign
    // and covers the space between it and the digits.
    REQUIRE(generateFull("PRAGMA user_version = - 2147483649;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = -2147483649: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23}, 12}},
                          {}});
    // The message strips the digit separators SQLite refuses; the underline keeps them, because it
    // stands under the characters the user wrote.
    REQUIRE(generateFull("PRAGMA max_page_count = 0x1_0000_0000_0000_0000;") ==
            CodeGenResult{"storage.pragma.max_page_count(0);",
                          {},
                          {CodegenWarning{"PRAGMA max_page_count = 0x10000000000000000: SQLite reads a PRAGMA "
                                          "value as a 32-bit integer and this hex literal does not fit one, so it "
                                          "sets 0",
                                          SourceLocation{1, 25}, 23}},
                          {}});
    // A value on a line of its own is anchored there, not at the statement.
    REQUIRE(generateFull("PRAGMA recursive_triggers =\n    0x80000000;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x80000000: SQLite reads this as false; "
                                          "spell it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{2, 5}, 10}},
                          {}});
    // An underline spans one line, so a sign the user left on the line above is not covered by it:
    // the literal alone carries the anchor.
    REQUIRE(generateFull("PRAGMA user_version = -\n    2147483649;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = -2147483649: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{2, 5}, 10}},
                          {}});
}
