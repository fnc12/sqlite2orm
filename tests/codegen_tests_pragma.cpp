#include "codegen_tests_common.hpp"
#include "temp_build_dir.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
                                          SourceLocation{1, 29},
                                          2}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 00") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 00;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 00: SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          2}},
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
                                          SourceLocation{1, 29},
                                          1}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 010 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 010;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 010: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          3}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 1.5 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 1.5;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 1.5: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          3}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x7FFFFFFF is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x7FFFFFFF;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x7FFFFFFF: SQLite reads this as true; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          10}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x80000000 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x80000000;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x80000000: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          10}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 2147483648 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 2147483648;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 2147483648: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          10}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = -1 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = -1;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = -1: SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          2}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = an unknown name is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = blah;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = blah: SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          4}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = '2abc' is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = '2abc';") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = '2abc': SQLite reads this as true; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          6}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 255 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 255;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 255: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          3}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 256 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 256;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 256: SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          3}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 257 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 257;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 257: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          3}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 511 is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 511;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 511: SQLite reads this as true; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          3}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x100 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x100;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x100: SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          5}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 65536 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 65536;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 65536: SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          5}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 2147483392 is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 2147483392;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 2147483392: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          10}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = '256' is false, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = '256';") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = '256': SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          5}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = '  1' warns about the value as written") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = '  1';") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = '  1': SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          5}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = '' warns about the value as written") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = '';") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = '': SQLite reads this as false; spell it 0/1, "
                                          "TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          2}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = a double-quoted 256 warns about the value as written") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = \"256\";") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = \"256\": SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          5}},
                          {}});
}

// SQLite takes a newline inside a quoted value the way it takes any other character — `PRAGMA
// user_version = 'a<newline>b'` sets 0 on 3.45.1 and on 3.51.0 — so the value spans two lines while
// the underline a consumer draws from the warning's location runs along one. It stops at the end of
// the line the value starts on rather than past it.
TEST_CASE("codegen: PRAGMA user_version = a string literal written across two lines underlines its first line") {
    REQUIRE(generateFull("PRAGMA user_version = 'a\nb';") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 'a\nb': SQLite reads a PRAGMA value as a 32-bit "
                                          "integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          2}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = a quoted name written across two lines underlines its first line") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = \"a\nb\";") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = \"a\nb\": SQLite reads this as false; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          2}},
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
                                          SourceLocation{1, 29},
                                          10}},
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
                                          SourceLocation{1, 29},
                                          11}},
                          {}});
}

TEST_CASE("codegen: PRAGMA recursive_triggers = 0x1FFFFFFF is true, like SQLite") {
    REQUIRE(generateFull("PRAGMA recursive_triggers = 0x1FFFFFFF;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(true);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x1FFFFFFF: SQLite reads this as true; spell it "
                                          "0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          10}},
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
                                          SourceLocation{1, 23},
                                          19}},
                          {}});
    REQUIRE(generateFull("PRAGMA max_page_count = 0x1_0000_0000_0000_0000;") ==
            CodeGenResult{"storage.pragma.max_page_count(0);",
                          {},
                          {CodegenWarning{"PRAGMA max_page_count = 0x10000000000000000: SQLite reads a PRAGMA "
                                          "value as a 32-bit integer and this hex literal does not fit one, so it "
                                          "sets 0",
                                          SourceLocation{1, 25},
                                          23}},
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
                                          SourceLocation{1, 29},
                                          19}},
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
                                          SourceLocation{1, 29},
                                          4}},
                          {}});
    REQUIRE(generateFull("PRAGMA recursive_triggers = DEFAULT;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = DEFAULT: SQLite reads this as false; spell "
                                          "it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{1, 29},
                                          7}},
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
                                          SourceLocation{1, 29},
                                          17}},
                          {}});
}

// DELETE is SQLite's default journal mode and EXCLUSIVE one of the two locking modes, and both
// spell a keyword — neither reached codegen before the value took a name.
// The enumerator is spelled `DELETE_`, sqlite_orm's alternate name for the same value: the Windows
// SDK defines `DELETE` as a macro, and a translation unit that included <windows.h> before the
// generated code cannot compile the plain name. See the Windows compile probe below.
TEST_CASE("codegen: PRAGMA journal_mode = DELETE") {
    REQUIRE(generateFull("PRAGMA journal_mode = DELETE;") ==
            CodeGenResult{"storage.pragma.journal_mode(sqlite_orm::journal_mode::DELETE_);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA journal_mode = 'delete'") {
    REQUIRE(generateFull("PRAGMA journal_mode = 'delete';") ==
            CodeGenResult{"storage.pragma.journal_mode(sqlite_orm::journal_mode::DELETE_);", {}, {}, {}});
}

// The remaining journal modes SQLite has, each an enumerator of its own in sqlite_orm. Checked
// against libsqlite3 3.45.1: `PRAGMA journal_mode = <mode>` reads back the mode it was given for
// every one of them.
TEST_CASE("codegen: PRAGMA journal_mode = TRUNCATE") {
    REQUIRE(generateFull("PRAGMA journal_mode = TRUNCATE;") ==
            CodeGenResult{"storage.pragma.journal_mode(sqlite_orm::journal_mode::TRUNCATE);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA journal_mode = PERSIST") {
    REQUIRE(generateFull("PRAGMA journal_mode = PERSIST;") ==
            CodeGenResult{"storage.pragma.journal_mode(sqlite_orm::journal_mode::PERSIST);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA journal_mode = MEMORY") {
    REQUIRE(generateFull("PRAGMA journal_mode = MEMORY;") ==
            CodeGenResult{"storage.pragma.journal_mode(sqlite_orm::journal_mode::MEMORY);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA journal_mode = OFF") {
    REQUIRE(generateFull("PRAGMA journal_mode = OFF;") ==
            CodeGenResult{"storage.pragma.journal_mode(sqlite_orm::journal_mode::OFF);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA locking_mode = EXCLUSIVE") {
    REQUIRE(generateFull("PRAGMA locking_mode = EXCLUSIVE;") ==
            CodeGenResult{"storage.pragma.locking_mode(sqlite_orm::locking_mode::EXCLUSIVE);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA locking_mode = NORMAL") {
    REQUIRE(generateFull("PRAGMA locking_mode = NORMAL;") ==
            CodeGenResult{"storage.pragma.locking_mode(sqlite_orm::locking_mode::NORMAL);", {}, {}, {}});
}

TEST_CASE("codegen: PRAGMA table_info of a table named after a keyword") {
    REQUIRE(generateFull("PRAGMA table_info(row);") ==
            CodeGenResult{R"(storage.pragma.table_info("row");)", {}, {}, {}});
    // ON, TRUE, FALSE and CURRENT_DATE are names here too — SQLite's `nmnum` rule takes them for
    // one, and `PRAGMA table_info(on)` describes a table called `on`, empty when there is none.
    REQUIRE(generateFull("PRAGMA table_info(on);") == CodeGenResult{R"(storage.pragma.table_info("on");)", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA table_xinfo(TRUE);") ==
            CodeGenResult{R"(storage.pragma.table_xinfo("TRUE");)", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA table_info(current_date);") ==
            CodeGenResult{R"(storage.pragma.table_info("current_date");)", {}, {}, {}});
}

// `PRAGMA integrity_check` reads its argument with `sqlite3GetInt32()` and takes for a table name
// whatever that refuses, and a keyword is a name to SQLite's `nmnum` rule like any other. Checked
// against sqlite3 3.51.0 and 3.45.1: `PRAGMA integrity_check(on)` and its `true`/`false` siblings
// all answer `no such table: <keyword>`, and `integrity_check(true)` turns into `ok` as soon as a
// table named `"true"` exists. So the argument has to go out as the name it is — the boolean the
// literal node stands for would pick sqlite_orm's `max_errors` overload and check the whole
// database instead.
TEST_CASE("codegen: PRAGMA integrity_check of a table named after a keyword") {
    REQUIRE(generateFull("PRAGMA integrity_check(on);") ==
            CodeGenResult{R"(storage.pragma.integrity_check("on");)", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA integrity_check(OFF);") ==
            CodeGenResult{R"(storage.pragma.integrity_check("OFF");)", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA integrity_check(true);") ==
            CodeGenResult{R"(storage.pragma.integrity_check("true");)", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA integrity_check(False);") ==
            CodeGenResult{R"(storage.pragma.integrity_check("False");)", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA integrity_check = TRUE;") ==
            CodeGenResult{R"(storage.pragma.integrity_check("TRUE");)", {}, {}, {}});
    REQUIRE(generateFull("PRAGMA integrity_check(current_date);") ==
            CodeGenResult{R"(storage.pragma.integrity_check("current_date");)", {}, {}, {}});
    // A number still reads as the error count it did.
    REQUIRE(generateFull("PRAGMA integrity_check(1);") ==
            CodeGenResult{"storage.pragma.integrity_check(1);", {}, {}, {}});
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
                                          SourceLocation{1, 23},
                                          10}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = 0xFFFFFFFF;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 0xFFFFFFFF: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          10}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = 0x100000000;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 0x100000000: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          11}},
                          {}});
}

// The decimal half of the same range: eleven digits, or ten digits above 2147483647.
TEST_CASE("codegen: PRAGMA user_version = a decimal literal past the int32 range sets 0, like SQLite") {
    REQUIRE(generateFull("PRAGMA user_version = 2147483648;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 2147483648: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          10}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = 4294967296;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 4294967296: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          10}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = -2147483649;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = -2147483649: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          11}},
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
                                          SourceLocation{1, 23},
                                          5}},
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
                                          SourceLocation{1, 23},
                                          3}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = '12';") ==
            CodeGenResult{"storage.pragma.user_version(12);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = '12': SQLite reads a PRAGMA value as a 32-bit "
                                          "integer, so it sets 12",
                                          SourceLocation{1, 23},
                                          4}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = abc;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = abc: SQLite reads a PRAGMA value as a 32-bit "
                                          "integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          3}},
                          {}});
}

// SQLite's grammar takes a number or a name after `PRAGMA name =`, and nothing else: `= NULL` is
// `near "NULL": syntax error`.
TEST_CASE("codegen: PRAGMA user_version = NULL is an error, not nullptr") {
    REQUIRE(generateFull("PRAGMA user_version = NULL;") ==
            CodeGenResult{"", {}, {}, {"PRAGMA user_version = …: expected a number, a string or a name"}, {}});
}

// `application_id` and `busy_timeout` reach `sqlite3Atoi()` the same way `user_version` does, so
// the fold is theirs too; `busy_timeout` then takes the int32 as milliseconds.
TEST_CASE("codegen: the other sqlite3Atoi PRAGMAs fold a value past the int32 range the same way") {
    REQUIRE(generateFull("PRAGMA application_id = 2147483648;") ==
            CodeGenResult{"storage.pragma.application_id(0);",
                          {},
                          {CodegenWarning{"PRAGMA application_id = 2147483648: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 25},
                                          10}},
                          {}});
    REQUIRE(generateFull("PRAGMA busy_timeout = 0x80000000;") ==
            CodeGenResult{"storage.pragma.busy_timeout(0);",
                          {},
                          {CodegenWarning{"PRAGMA busy_timeout = 0x80000000: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          10}},
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
                                          SourceLocation{1, 23},
                                          11}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = 2_147_483_648;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 2_147_483_648: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          13}},
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
                                          SourceLocation{1, 23},
                                          5}},
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
                                          SourceLocation{1, 23},
                                          20}},
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
                                          SourceLocation{1, 23},
                                          5}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = '+12';") ==
            CodeGenResult{"storage.pragma.user_version(12);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = '+12': SQLite reads a PRAGMA value as a 32-bit "
                                          "integer, so it sets 12",
                                          SourceLocation{1, 23},
                                          5}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = '0x10';") ==
            CodeGenResult{"storage.pragma.user_version(16);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = '0x10': SQLite reads a PRAGMA value as a 32-bit "
                                          "integer, so it sets 16",
                                          SourceLocation{1, 23},
                                          6}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = '';") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = '': SQLite reads a PRAGMA value as a 32-bit "
                                          "integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          2}},
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
                                          SourceLocation{1, 23},
                                          3}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = 0.9;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = 0.9: SQLite reads a PRAGMA value as a 32-bit "
                                          "integer, so it sets 0",
                                          SourceLocation{1, 23},
                                          3}},
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
                                          SourceLocation{1, 23},
                                          4}},
                          {}});
    // The minus sign belongs to the value the message quotes, so the underline starts at the sign
    // and covers the space between it and the digits.
    REQUIRE(generateFull("PRAGMA user_version = - 2147483649;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = -2147483649: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          12}},
                          {}});
    // The message strips the digit separators SQLite refuses; the underline keeps them, because it
    // stands under the characters the user wrote.
    REQUIRE(generateFull("PRAGMA max_page_count = 0x1_0000_0000_0000_0000;") ==
            CodeGenResult{"storage.pragma.max_page_count(0);",
                          {},
                          {CodegenWarning{"PRAGMA max_page_count = 0x10000000000000000: SQLite reads a PRAGMA "
                                          "value as a 32-bit integer and this hex literal does not fit one, so it "
                                          "sets 0",
                                          SourceLocation{1, 25},
                                          23}},
                          {}});
    // A value on a line of its own is anchored there, not at the statement.
    REQUIRE(generateFull("PRAGMA recursive_triggers =\n    0x80000000;") ==
            CodeGenResult{"storage.pragma.recursive_triggers(false);",
                          {},
                          {CodegenWarning{"PRAGMA recursive_triggers = 0x80000000: SQLite reads this as false; "
                                          "spell it 0/1, TRUE/FALSE or ON/OFF instead",
                                          SourceLocation{2, 5},
                                          10}},
                          {}});
    // An underline spans one line, so a sign the user left on the line above is not covered by it:
    // the literal alone carries the anchor.
    REQUIRE(generateFull("PRAGMA user_version = -\n    2147483649;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = -2147483649: SQLite reads a PRAGMA value as a "
                                          "32-bit integer and cannot read this one, so it sets 0",
                                          SourceLocation{2, 5},
                                          10}},
                          {}});
}

TEST_CASE("codegen: a keyword PRAGMA value is spelled back and underlined as written") {
    // `ON` is a bool literal to the parser, but a PRAGMA reads the keyword's own letters, so the
    // message quotes `ON` rather than the `true` the node stands for, and the underline is two
    // characters wide — one more would cover the `;` and run past the end of the line.
    REQUIRE(generateFull("PRAGMA user_version = ON;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = ON: SQLite reads a PRAGMA value as a 32-bit "
                                          "integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          2}},
                          {}});
    // The case the keyword came in is carried through untouched.
    REQUIRE(generateFull("PRAGMA busy_timeout = On;") ==
            CodeGenResult{"storage.pragma.busy_timeout(0);",
                          {},
                          {CodegenWarning{"PRAGMA busy_timeout = On: SQLite reads a PRAGMA value as a 32-bit "
                                          "integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          2}},
                          {}});
    // TRUE and FALSE are the same bool literal node, and neither is spelled back as the other.
    REQUIRE(generateFull("PRAGMA user_version = TRUE;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = TRUE: SQLite reads a PRAGMA value as a 32-bit "
                                          "integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          4}},
                          {}});
    REQUIRE(generateFull("PRAGMA user_version = FALSE;") ==
            CodeGenResult{"storage.pragma.user_version(0);",
                          {},
                          {CodegenWarning{"PRAGMA user_version = FALSE: SQLite reads a PRAGMA value as a 32-bit "
                                          "integer and cannot read this one, so it sets 0",
                                          SourceLocation{1, 23},
                                          5}},
                          {}});
}

namespace {

    /**
     *  Compiles the generated PRAGMA calls against sqlite_orm in a translation unit that looks like
     *  a Windows one: winnt.h defines `DELETE` as a macro and sets `_WINNT_`, and sqlite_orm's
     *  journal_mode header only hides that macro while the enum is being declared. A generated
     *  enumerator whose name the Windows SDK has taken reads fine and compiles nowhere — only
     *  building it says so.
     */
    void requireCompilesWithWindowsDeleteMacro(const std::vector<std::string>& statements) {
        std::ostringstream program;
        program << "// winnt.h, as far as sqlite_orm's journal_mode header is concerned\n"
                   "#define _WINNT_\n"
                   "#define DELETE (0x00010000L)\n"
                   "\n"
                   "#include <sqlite_orm/sqlite_orm.h>\n"
                   "\n"
                   "struct User {\n"
                   "    int id = 0;\n"
                   "};\n"
                   "\n"
                   "int main() {\n"
                   "    auto storage = sqlite_orm::make_storage(\n"
                   "        \"\", sqlite_orm::make_table(\"users\", sqlite_orm::make_column(\"id\", &User::id)));\n";
        for (const auto& statement: statements) {
            program << "    " << statement << '\n';
        }
        program << "    return 0;\n"
                   "}\n";

        const TempBuildDir dir;
        const std::filesystem::path cpppath = dir.write("check.cpp", program.str());

        std::ostringstream cmd;
        cmd << TempBuildDir::compilerCommand() << " -fsyntax-only " << cpppath.string() << " 2>&1";

        const int exitCode = TempBuildDir::run(cmd.str());
        if (exitCode != 0) {
            WARN("fsyntax-only failed (exit " << exitCode << "); ensure c++ and sqlite_orm headers are usable");
        }
        REQUIRE(exitCode == 0);
    }

    /**
     *  Builds a program around the generated PRAGMA calls over a database that holds `users` and,
     *  when `extraTableName` is given, a table of that name as well; links it against sqlite_orm,
     *  runs it and returns one line per call: the rows it answered, joined with `,`, or `error` for
     *  the one SQLite raised. A generated call that compiles can still run another statement than
     *  the SQL it came from — `integrity_check` picks one of two overloads by the type of its
     *  argument, and only one of them names a table — so only running it, against a database where
     *  that name is a table and one where it is not, says which statement a user gets.
     */
    std::vector<std::string> pragmaOutcomes(const std::vector<std::string>& statements,
                                            std::string_view extraTableName = {}) {
        std::ostringstream program;
        program << "#include <sqlite_orm/sqlite_orm.h>\n"
                   "#include <iostream>\n"
                   "#include <string>\n"
                   "#include <system_error>\n"
                   "\n"
                   "struct User {\n"
                   "    int id = 0;\n"
                   "};\n"
                   "\n"
                   "struct Other {\n"
                   "    int id = 0;\n"
                   "};\n"
                   "\n"
                   "int main() {\n"
                   "    auto storage = sqlite_orm::make_storage(\n"
                   "        \"\", sqlite_orm::make_table(\"users\", sqlite_orm::make_column(\"id\", &User::id))";
        if (!extraTableName.empty()) {
            program << ",\n        sqlite_orm::make_table(\"" << extraTableName
                    << "\", sqlite_orm::make_column(\"id\", &Other::id))";
        }
        program << ");\n"
                   "    storage.sync_schema();\n";
        for (const auto& statement: statements) {
            // The generated call ends in its own semicolon, so binding its result needs no more
            // than the assignment in front of it.
            program << "    try {\n"
                       "        const auto rows = "
                    << statement
                    << "\n"
                       "        std::string joined;\n"
                       "        for(const auto& row: rows) {\n"
                       "            joined += joined.empty() ? row : \",\" + row;\n"
                       "        }\n"
                       "        std::cout << joined << '\\n';\n"
                       "    } catch(const std::system_error&) {\n"
                       "        std::cout << \"error\" << '\\n';\n"
                       "    }\n";
        }
        program << "    return 0;\n"
                   "}\n";

        const TempBuildDir dir;
        const std::filesystem::path cpppath = dir.write("check.cpp", program.str());
        const std::filesystem::path binpath = dir.file("check");
        const std::filesystem::path outpath = dir.file("check.out");

        std::ostringstream cmd;
        cmd << TempBuildDir::compilerCommand();
        cmd << ' ' << cpppath.string();
        cmd << ' ' << TempBuildDir::sqlite3LinkFlags() << " -o " << binpath.string();
        cmd << " && " << binpath.string() << " > " << outpath.string();
        cmd << " 2>&1";

        const int exitCode = TempBuildDir::run(cmd.str());
        std::vector<std::string> outcomes;
        {
            std::ifstream out(outpath);
            for (std::string line; std::getline(out, line);) {
                outcomes.push_back(line);
            }
        }
        if (exitCode != 0) {
            WARN("building the generated PRAGMA calls failed (exit "
                 << exitCode << "); ensure c++, sqlite_orm headers and libsqlite3 are usable");
        }
        REQUIRE(exitCode == 0);
        return outcomes;
    }

}  // namespace

// Every journal mode a user can write, built the way a Windows user builds it. `DELETE` is the one
// the Windows SDK has taken, but a mode name is only ever an enumerator in someone else's
// translation unit, so the probe covers the whole set rather than that one name.
TEST_CASE("codegen: generated journal_mode calls compile with the Windows DELETE macro in scope") {
    // A mode codegen has no enumerator for becomes an error, and an errored result carries no code
    // at all — the probe would then compile a program with nothing in it and pass on an empty set.
    // Every statement handed to the compiler has to be a call codegen really produced.
    const auto generateCall = [](const std::string& sql) {
        const CodeGenResult result = generateFull(sql);
        REQUIRE(result.errors == std::vector<std::string>{});
        REQUIRE(result.code != std::string{});
        return result.code;
    };

    std::vector<std::string> statements;
    for (const std::string mode: {"DELETE", "TRUNCATE", "PERSIST", "MEMORY", "WAL", "OFF"}) {
        statements.push_back(generateCall("PRAGMA journal_mode = " + mode + ";"));
    }
    for (const std::string mode: {"NORMAL", "EXCLUSIVE"}) {
        statements.push_back(generateCall("PRAGMA locking_mode = " + mode + ";"));
    }
    requireCompilesWithWindowsDeleteMacro(statements);
}

// The two `integrity_check` overloads run different statements: one checks the whole database and
// caps the errors it reports, the other checks the one table it is named. `PRAGMA
// integrity_check(on)` is the second — sqlite3 3.51.0 and 3.45.1 both answer `no such table: on`
// for it, and `ok` once a table called `on` exists — so the generated call has to fail on the
// database where that name is free and pass on the one where it is taken. The `max_errors`
// overload the bool literal used to pick reports `ok` on both.
TEST_CASE("codegen: a generated integrity_check keyword argument names the table SQLite names") {
    const std::vector<std::string> statements = {
        generateFull("PRAGMA integrity_check(on);").code,
        generateFull("PRAGMA integrity_check(users);").code,
        generateFull("PRAGMA integrity_check(1);").code,
    };
    REQUIRE(pragmaOutcomes(statements) == std::vector<std::string>{"error", "ok", "ok"});
    REQUIRE(pragmaOutcomes(statements, "on") == std::vector<std::string>{"ok", "ok", "ok"});
}
