#include "codegen_tests_common.hpp"

#include <sqlite2orm/json_emit.h>

TEST_CASE("codegen: CREATE TABLE - basic") {
    auto result = generate("CREATE TABLE users (id INTEGER, name TEXT)");
    REQUIRE(result == "struct Users {\n"
                      "    std::optional<int64_t> id;\n"
                      "    std::optional<std::string> name;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"users\",\n"
                      "        make_column(\"id\", &Users::id),\n"
                      "        make_column(\"name\", &Users::name)));");
}

TEST_CASE("codegen: CREATE TABLE - type mapping") {
    auto result = generate("CREATE TABLE t (a INTEGER, b REAL, c TEXT, d BLOB, e BOOLEAN)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> a;\n"
                      "    std::optional<double> b;\n"
                      "    std::optional<std::string> c;\n"
                      "    std::optional<std::vector<char>> d;\n"
                      "    std::optional<bool> e;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"a\", &T::a),\n"
                      "        make_column(\"b\", &T::b),\n"
                      "        make_column(\"c\", &T::c),\n"
                      "        make_column(\"d\", &T::d),\n"
                      "        make_column(\"e\", &T::e)));");
}

TEST_CASE("codegen: CREATE TABLE - struct name capitalized") {
    auto result = generate("CREATE TABLE products (id INTEGER)");
    REQUIRE(result == "struct Products {\n"
                      "    std::optional<int64_t> id;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"products\",\n"
                      "        make_column(\"id\", &Products::id)));");
}

TEST_CASE("codegen: CREATE TABLE - no type defaults to BLOB") {
    auto result = generate("CREATE TABLE t (x)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<std::vector<char>> x;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"x\", &T::x)));");
}

TEST_CASE("codegen: CREATE TABLE - VARCHAR(255) maps to string") {
    auto result = generate("CREATE TABLE t (name VARCHAR(255))");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<std::string> name;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"name\", &T::name)));");
}

TEST_CASE("codegen: CREATE TABLE - PRIMARY KEY") {
    auto result = generate("CREATE TABLE t (id INTEGER PRIMARY KEY)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> id;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id, primary_key())));");
}

TEST_CASE("codegen: CREATE TABLE - PRIMARY KEY AUTOINCREMENT") {
    auto result = generate("CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> id;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id, primary_key().autoincrement())));");
}

TEST_CASE("codegen: CREATE TABLE - NOT NULL removes optional") {
    auto result = generate("CREATE TABLE t (name TEXT NOT NULL)");
    REQUIRE(result == "struct T {\n"
                      "    std::string name;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"name\", &T::name)));");
}

TEST_CASE("codegen: CREATE TABLE - NOT NULL int has initializer") {
    auto result = generate("CREATE TABLE t (count INTEGER NOT NULL)");
    REQUIRE(result == "struct T {\n"
                      "    int64_t count = 0;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"count\", &T::count)));");
}

TEST_CASE("codegen: CREATE TABLE - mixed constraints") {
    auto result = generate("CREATE TABLE users ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                           "name TEXT NOT NULL, "
                           "email TEXT)");
    REQUIRE(result == "struct Users {\n"
                      "    std::optional<int64_t> id;\n"
                      "    std::string name;\n"
                      "    std::optional<std::string> email;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"users\",\n"
                      "        make_column(\"id\", &Users::id, primary_key().autoincrement()),\n"
                      "        make_column(\"name\", &Users::name),\n"
                      "        make_column(\"email\", &Users::email)));");
}

TEST_CASE("codegen: CREATE TABLE - PRIMARY KEY ON CONFLICT") {
    auto result = generate("CREATE TABLE t (id INTEGER PRIMARY KEY ON CONFLICT REPLACE)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> id;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id, primary_key().on_conflict_replace())));");
}

TEST_CASE("codegen: CREATE TABLE - PRIMARY KEY ON CONFLICT + AUTOINCREMENT") {
    auto result = generate("CREATE TABLE t (id INTEGER PRIMARY KEY ON CONFLICT ABORT AUTOINCREMENT)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> id;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id, primary_key().on_conflict_abort().autoincrement())));");
}

TEST_CASE("codegen: CREATE TABLE - DEFAULT integer") {
    auto result = generate("CREATE TABLE t (x INTEGER DEFAULT 42)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> x;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"x\", &T::x, default_value(42))));");
}

TEST_CASE("codegen: CREATE TABLE - DEFAULT integer with leading zeros") {
    auto result = generate("CREATE TABLE t (x INTEGER DEFAULT 010)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> x;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"x\", &T::x, default_value(10))));");
}

// An int64 cannot hold this default, so SQLite keeps the column's default as a REAL: the
// schema `CREATE TABLE t (x INTEGER DEFAULT 99999999999999999999)` round-trips to
// `DEFAULT (1e+20)` and a defaulted row reads back as real 1.0e+20. Checked against sqlite3 3.51.
TEST_CASE("codegen: CREATE TABLE - DEFAULT integer beyond int64") {
    auto result = generate("CREATE TABLE t (x INTEGER DEFAULT 99999999999999999999)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> x;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"x\", &T::x, default_value(99999999999999999999.0))));");
}

// A DEFAULT reaches C++ as a literal like any other, so the value C++ has no literal for is
// spelled the same way here: SQLite keeps `DEFAULT 9e999` and hands out an Inf, and the generated
// code says so without a `-Woverflow` on the way. Checked against sqlite3 3.51.
TEST_CASE("codegen: CREATE TABLE - DEFAULT real past the double range") {
    auto result = generate("CREATE TABLE t (x REAL DEFAULT 9e999, y REAL CHECK (y < 1e400))");
    REQUIRE(result ==
            "struct T {\n"
            "    std::optional<double> x;\n"
            "    std::optional<double> y;\n"
            "};\n"
            "\n"
            "auto storage = make_storage(\"\",\n"
            "    make_table(\"t\",\n"
            "        make_column(\"x\", &T::x, default_value(std::numeric_limits<double>::infinity())),\n"
            "        make_column(\"y\", &T::y, check(c(&T::y) < std::numeric_limits<double>::infinity()))));");
}

// SQLite wraps a hex default around inside the int64, so this one is -1, not 18446744073709551615.
TEST_CASE("codegen: CREATE TABLE - DEFAULT hexadecimal past the int64 range") {
    auto result = generate("CREATE TABLE t (x INTEGER DEFAULT 0xFFFFFFFFFFFFFFFF)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> x;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"x\", &T::x, default_value(static_cast<int64_t>(0xFFFFFFFFFFFFFFFF)))));");
}

// SQLite raises `hex literal too big` in codeInteger(), when it compiles an expression, and a
// DEFAULT is stored without ever being compiled: `CREATE TABLE weird(x INTEGER DEFAULT
// 0x10000000000000000)` is accepted and kept in sqlite_master, and only `INSERT INTO weird
// DEFAULT VALUES` fails with it. C++ has no literal for the value either, so the column stays and
// the default goes. Checked against sqlite3 3.51.
TEST_CASE("codegen: CREATE TABLE - DEFAULT hex literal too big for an int64") {
    auto result = generateFull("CREATE TABLE weird (x INTEGER DEFAULT 0x10000000000000000)");
    REQUIRE(result.code == "struct Weird {\n"
                           "    std::optional<int64_t> x;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"weird\",\n"
                           "        make_column(\"x\", &Weird::x)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"DEFAULT 0x10000000000000000 on column 'x' is too big for a signed 64-bit integer: SQLite stores it "
                 "but refuses every use of the default, and C++ has no literal for it, so the generated column has no "
                 "default_value()"}});
    REQUIRE(result.errors.empty());
}

// The literal is just as stored inside a parenthesized DEFAULT expression, and the separators are
// taken out of the diagnostic the way SQLite takes them out of its own.
TEST_CASE("codegen: CREATE TABLE - DEFAULT expression holding a hex literal too big") {
    auto result = generateFull("CREATE TABLE t (x INTEGER DEFAULT (0x1_0000_0000_0000_0000 + 1))");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> x;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"x\", &T::x)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"DEFAULT 0x10000000000000000 on column 'x' is too big for a signed 64-bit integer: SQLite stores it "
                 "but refuses every use of the default, and C++ has no literal for it, so the generated column has no "
                 "default_value()"}});
    REQUIRE(result.errors.empty());
}

// A CHECK is stored the same way, and SQLite refuses it only once a row goes in.
TEST_CASE("codegen: CREATE TABLE - column CHECK holding a hex literal too big") {
    auto result = generateFull("CREATE TABLE t (x INTEGER CHECK (x <> 0x10000000000000000))");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> x;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"x\", &T::x)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"CHECK on column 'x' uses 0x10000000000000000, too big for a signed 64-bit integer: SQLite stores it "
                 "but refuses every use of the constraint, and C++ has no literal for it, so the generated column has "
                 "no check()"}});
    REQUIRE(result.errors.empty());
}

TEST_CASE("codegen: CREATE TABLE - table-level CHECK holding a hex literal too big") {
    auto result = generateFull("CREATE TABLE t (a INTEGER, CHECK (a <> 0x10000000000000000))");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"a\", &T::a)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"table CHECK uses 0x10000000000000000, too big for a signed 64-bit integer: SQLite stores it but "
                 "refuses every use of the constraint, and C++ has no literal for it, so the generated table has no "
                 "check()"}});
    REQUIRE(result.errors.empty());
}

// Only a STORED generated column is stored text. SQLite compiles it when a row is written, so
// `sqlite3 s.db "CREATE TABLE g(x INTEGER, y AS (x + 0x10000000000000000) STORED)"` succeeds and
// sqlite_master holds the table, while every INSERT into it is `hex literal too big`. C++ has no
// literal for the value, and a column that lost its as(...) would be an ordinary column rather
// than a generated one, so the table is left out whole instead of being reshaped — the statement
// is not an error and the rest of a schema holding it still generates. On its own it generates a
// placeholder comment rather than nothing, the way an unsupported CREATE VIEW does. Checked
// against sqlite3 3.51.
TEST_CASE("codegen: CREATE TABLE - STORED generated column holding a hex literal too big") {
    auto result = generateFull("CREATE TABLE g (x INTEGER, y AS (x + 0x10000000000000000) STORED)");
    REQUIRE(result.code == "/* CREATE TABLE g — not supported for sqlite_orm */");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"STORED generated column 'y' uses 0x10000000000000000, too big for a signed 64-bit integer: SQLite "
                 "stores the table but refuses every row written to it, and C++ has no literal for it, so the table "
                 "is not generated"}});
    REQUIRE(result.errors.empty());
}

// The GENERATED ALWAYS spelling of the same column is stored by SQLite just the same.
TEST_CASE("codegen: CREATE TABLE - GENERATED ALWAYS STORED column holding a hex literal too big") {
    auto result =
        generateFull("CREATE TABLE g (x INTEGER, y GENERATED ALWAYS AS (x + 0x1_0000_0000_0000_0000) STORED)");
    REQUIRE(result.code == "/* CREATE TABLE g — not supported for sqlite_orm */");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"STORED generated column 'y' uses 0x10000000000000000, too big for a signed 64-bit integer: SQLite "
                 "stores the table but refuses every row written to it, and C++ has no literal for it, so the table "
                 "is not generated"}});
    REQUIRE(result.errors.empty());
}

// A VIRTUAL generated column is the opposite: SQLite compiles it at CREATE TABLE time and refuses
// the statement outright — `Error: stepping, hex literal too big` — so the table never reaches
// sqlite_master and an error here is what matches. The bare `AS (...)` spelling is VIRTUAL too.
TEST_CASE("codegen: CREATE TABLE - VIRTUAL generated column holding a hex literal too big is an error") {
    auto result = generateFull("CREATE TABLE g (x INTEGER, y AS (x + 0x10000000000000000) VIRTUAL)");
    REQUIRE(result.errors == std::vector<std::string>{"hex literal too big: 0x10000000000000000"});
    REQUIRE(result.warnings.empty());

    auto bare = generateFull("CREATE TABLE g (x INTEGER, y AS (x + 0x10000000000000000))");
    REQUIRE(bare.errors == std::vector<std::string>{"hex literal too big: 0x10000000000000000"});
    REQUIRE(bare.warnings.empty());
}

// The boundary is unmoved: 0xFFFFFFFFFFFFFFFF is the largest hex literal SQLite reads, so a STORED
// generated column holding it is generated as it always was.
TEST_CASE("codegen: CREATE TABLE - STORED generated column at the top of the int64 range") {
    auto result = generateFull("CREATE TABLE g (x INTEGER, y AS (x + 0xFFFFFFFFFFFFFFFF) STORED)");
    REQUIRE(result.code ==
            "struct G {\n"
            "    std::optional<int64_t> x;\n"
            "    std::optional<std::vector<char>> y;\n"
            "};\n"
            "\n"
            "auto storage = make_storage(\"\",\n"
            "    make_table(\"g\",\n"
            "        make_column(\"x\", &G::x),\n"
            "        make_column(\"y\", &G::y, as(c(&G::x) + static_cast<int64_t>(0xFFFFFFFFFFFFFFFF)).stored())));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

TEST_CASE("codegen: CREATE TABLE - DEFAULT string") {
    auto result = generate("CREATE TABLE t (x TEXT DEFAULT 'hello')");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<std::string> x;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"x\", &T::x, default_value(\"hello\"))));");
}

TEST_CASE("codegen: CREATE TABLE - DEFAULT expression with function") {
    auto result = generate("CREATE TABLE t (x TEXT DEFAULT (date('now')))");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<std::string> x;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"x\", &T::x, default_value(date(\"now\")))));");
}

TEST_CASE("codegen: CREATE TABLE - DEFAULT with NOT NULL") {
    auto result = generate("CREATE TABLE t (x INTEGER NOT NULL DEFAULT 0)");
    REQUIRE(result == "struct T {\n"
                      "    int64_t x = 0;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"x\", &T::x, default_value(0))));");
}

TEST_CASE("codegen: CREATE TABLE - UNIQUE") {
    auto result = generate("CREATE TABLE t (email TEXT UNIQUE)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<std::string> email;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"email\", &T::email, unique())));");
}

TEST_CASE("codegen: CREATE TABLE - all constraints combined") {
    auto result = generate("CREATE TABLE users ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                           "name TEXT NOT NULL DEFAULT 'unnamed', "
                           "email TEXT UNIQUE)");
    REQUIRE(result == "struct Users {\n"
                      "    std::optional<int64_t> id;\n"
                      "    std::string name;\n"
                      "    std::optional<std::string> email;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"users\",\n"
                      "        make_column(\"id\", &Users::id, primary_key().autoincrement()),\n"
                      "        make_column(\"name\", &Users::name, default_value(\"unnamed\")),\n"
                      "        make_column(\"email\", &Users::email, unique())));");
}

TEST_CASE("codegen: CREATE TABLE - UNIQUE has no warnings") {
    auto result = generateFull("CREATE TABLE t (email TEXT UNIQUE)");
    REQUIRE(result.warnings.empty());
}

TEST_CASE("codegen: CREATE TABLE - UNIQUE ON CONFLICT generates warning") {
    auto result = generateFull("CREATE TABLE t (email TEXT UNIQUE ON CONFLICT IGNORE)");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                "UNIQUE ON CONFLICT clause on column 'email' is not supported by sqlite_orm::unique()"});
}

TEST_CASE("codegen: CREATE TABLE - CHECK constraint") {
    auto result = generate("CREATE TABLE t (age INTEGER CHECK(age > 0))");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> age;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"age\", &T::age, check(c(&T::age) > 0))));");
}

TEST_CASE("codegen: CREATE TABLE - CHECK with function") {
    auto result = generate("CREATE TABLE t (name TEXT CHECK(length(name) > 0))");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<std::string> name;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"name\", &T::name, check(length(&T::name) > 0))));");
}

TEST_CASE("codegen: CREATE TABLE - COLLATE NOCASE") {
    auto result = generate("CREATE TABLE t (name TEXT COLLATE NOCASE)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<std::string> name;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"name\", &T::name, collate_nocase())));");
}

TEST_CASE("codegen: CREATE TABLE - COLLATE RTRIM") {
    auto result = generate("CREATE TABLE t (name TEXT COLLATE RTRIM)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<std::string> name;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"name\", &T::name, collate_rtrim())));");
}

TEST_CASE("codegen: CREATE TABLE - custom COLLATE generates warning") {
    auto result = generateFull("CREATE TABLE t (name TEXT COLLATE UNICODE)");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{"COLLATE UNICODE on column 'name' is not a built-in collation in sqlite_orm"});
}

TEST_CASE("codegen: CREATE TABLE - all column constraints") {
    auto result = generate("CREATE TABLE users ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                           "name TEXT NOT NULL DEFAULT 'unnamed' CHECK(length(name) > 0) COLLATE NOCASE, "
                           "email TEXT UNIQUE)");
    REQUIRE(result == "struct Users {\n"
                      "    std::optional<int64_t> id;\n"
                      "    std::string name;\n"
                      "    std::optional<std::string> email;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"users\",\n"
                      "        make_column(\"id\", &Users::id, primary_key().autoincrement()),\n"
                      "        make_column(\"name\", &Users::name, default_value(\"unnamed\"), "
                      "check(length(&Users::name) > 0), collate_nocase()),\n"
                      "        make_column(\"email\", &Users::email, unique())));");
}

TEST_CASE("codegen: CREATE TABLE - REFERENCES simple") {
    auto result = generate("CREATE TABLE posts (user_id INTEGER REFERENCES users(id))");
    REQUIRE(result == "struct Posts {\n"
                      "    std::optional<int64_t> user_id;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"posts\",\n"
                      "        make_column(\"user_id\", &Posts::user_id),\n"
                      "        foreign_key(&Posts::user_id).references(&Users::id)));");
}

TEST_CASE("codegen: CREATE TABLE - REFERENCES ON DELETE CASCADE") {
    auto result = generate("CREATE TABLE posts (user_id INTEGER REFERENCES users(id) ON DELETE CASCADE)");
    REQUIRE(result == "struct Posts {\n"
                      "    std::optional<int64_t> user_id;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"posts\",\n"
                      "        make_column(\"user_id\", &Posts::user_id),\n"
                      "        foreign_key(&Posts::user_id).references(&Users::id).on_delete.cascade()));");
}

TEST_CASE("codegen: CREATE TABLE - REFERENCES both actions") {
    auto result =
        generate("CREATE TABLE posts (user_id INTEGER REFERENCES users(id) ON DELETE CASCADE ON UPDATE SET NULL)");
    REQUIRE(result ==
            "struct Posts {\n"
            "    std::optional<int64_t> user_id;\n"
            "};\n"
            "\n"
            "auto storage = make_storage(\"\",\n"
            "    make_table(\"posts\",\n"
            "        make_column(\"user_id\", &Posts::user_id),\n"
            "        foreign_key(&Posts::user_id).references(&Users::id).on_delete.cascade().on_update.set_null()));");
}

TEST_CASE("codegen: CREATE TABLE - REFERENCES with PK and other columns") {
    auto result = generate("CREATE TABLE posts ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                           "user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE, "
                           "title TEXT NOT NULL)");
    REQUIRE(result == "struct Posts {\n"
                      "    std::optional<int64_t> id;\n"
                      "    int64_t user_id = 0;\n"
                      "    std::string title;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"posts\",\n"
                      "        make_column(\"id\", &Posts::id, primary_key().autoincrement()),\n"
                      "        make_column(\"user_id\", &Posts::user_id),\n"
                      "        make_column(\"title\", &Posts::title),\n"
                      "        foreign_key(&Posts::user_id).references(&Users::id).on_delete.cascade()));");
}

TEST_CASE("codegen: CREATE TABLE - table-level FOREIGN KEY") {
    auto result = generate("CREATE TABLE posts ("
                           "id INTEGER PRIMARY KEY, "
                           "user_id INTEGER, "
                           "FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE)");
    REQUIRE(result == "struct Posts {\n"
                      "    std::optional<int64_t> id;\n"
                      "    std::optional<int64_t> user_id;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"posts\",\n"
                      "        make_column(\"id\", &Posts::id, primary_key()),\n"
                      "        make_column(\"user_id\", &Posts::user_id),\n"
                      "        foreign_key(&Posts::user_id).references(&Users::id).on_delete.cascade()));");
}

TEST_CASE("codegen: CREATE TABLE - GENERATED ALWAYS AS STORED") {
    auto result =
        generate("CREATE TABLE t (id INTEGER PRIMARY KEY, full_name TEXT GENERATED ALWAYS AS (id + 1) STORED)");
    REQUIRE(result ==
            "struct T {\n"
            "    std::optional<int64_t> id;\n"
            "    std::optional<std::string> full_name;\n"
            "};\n"
            "\n"
            "auto storage = make_storage(\"\",\n"
            "    make_table(\"t\",\n"
            "        make_column(\"id\", &T::id, primary_key()),\n"
            "        make_column(\"full_name\", &T::full_name, generated_always_as(c(&T::id) + 1).stored())));");
}

TEST_CASE("codegen: CREATE TABLE - AS VIRTUAL shorthand") {
    auto result = generate("CREATE TABLE t (id INTEGER, v INTEGER AS (id * 2) VIRTUAL)");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> id;\n"
                      "    std::optional<int64_t> v;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id),\n"
                      "        make_column(\"v\", &T::v, as(c(&T::id) * 2).virtual_())));");
}

TEST_CASE("codegen: CREATE TABLE - GENERATED ALWAYS AS no storage type") {
    auto result = generate("CREATE TABLE t (id INTEGER, v INTEGER GENERATED ALWAYS AS (id + 10))");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> id;\n"
                      "    std::optional<int64_t> v;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id),\n"
                      "        make_column(\"v\", &T::v, generated_always_as(c(&T::id) + 10))));");
}

TEST_CASE("codegen: CREATE TABLE - table-level PRIMARY KEY") {
    auto result = generate("CREATE TABLE t (a INTEGER, b TEXT, PRIMARY KEY (a, b))");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> a;\n"
                      "    std::optional<std::string> b;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"a\", &T::a),\n"
                      "        make_column(\"b\", &T::b),\n"
                      "        primary_key(&T::a, &T::b)));");
}

TEST_CASE("codegen: CREATE TABLE - table-level UNIQUE") {
    auto result = generate("CREATE TABLE t (a INTEGER, b TEXT, UNIQUE (a, b))");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> a;\n"
                      "    std::optional<std::string> b;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"a\", &T::a),\n"
                      "        make_column(\"b\", &T::b),\n"
                      "        sqlite_orm::unique(&T::a, &T::b)));");
}

TEST_CASE("codegen: CREATE TABLE - table-level UNIQUE over same-typed columns") {
    auto result = generate("CREATE TABLE t (a INTEGER, b INTEGER, UNIQUE (a, b))");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> a;\n"
                      "    std::optional<int64_t> b;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"a\", &T::a),\n"
                      "        make_column(\"b\", &T::b),\n"
                      "        sqlite_orm::unique(&T::a, &T::b)));");
}

TEST_CASE("codegen: CREATE TABLE - table-level UNIQUE over three columns") {
    auto result = generate("CREATE TABLE t (a INTEGER, b INTEGER, c INTEGER, UNIQUE (a, b, c))");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> a;\n"
                      "    std::optional<int64_t> b;\n"
                      "    std::optional<int64_t> c;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"a\", &T::a),\n"
                      "        make_column(\"b\", &T::b),\n"
                      "        make_column(\"c\", &T::c),\n"
                      "        sqlite_orm::unique(&T::a, &T::b, &T::c)));");
}

TEST_CASE("codegen: CREATE TABLE - table-level CHECK") {
    auto result = generate("CREATE TABLE t (a INTEGER, CHECK (a > 0))");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> a;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"a\", &T::a),\n"
                      "        check(c(&T::a) > 0)));");
}

TEST_CASE("codegen: CREATE TABLE - mixed table-level constraints") {
    auto result = generate("CREATE TABLE t (a INTEGER, b TEXT, c INTEGER, "
                           "PRIMARY KEY (a, b), UNIQUE (b, c), CHECK (a > 0), "
                           "FOREIGN KEY (c) REFERENCES other(id))");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> a;\n"
                      "    std::optional<std::string> b;\n"
                      "    std::optional<int64_t> c;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"a\", &T::a),\n"
                      "        make_column(\"b\", &T::b),\n"
                      "        make_column(\"c\", &T::c),\n"
                      "        foreign_key(&T::c).references(&Other::id),\n"
                      "        primary_key(&T::a, &T::b),\n"
                      "        sqlite_orm::unique(&T::b, &T::c),\n"
                      "        check(c(&T::a) > 0)));");
}

// SQLite matches a column name written in a table constraint against the declared columns ignoring
// case and quotes, so every constraint below is over the one column the table has, and SQLite takes
// all five statements (checked against sqlite3 3.51.0). The member the constraint has to be written
// as is named after the declaration, not after the constraint's own spelling: before it was resolved
// there, each of these generated a member pointer into a member the struct never declared — a header
// that does not compile, handed out at exit 0 and without a warning.
TEST_CASE("codegen: CREATE TABLE - a table PRIMARY KEY naming its column in another case") {
    auto result = generateFull("CREATE TABLE t (\"Id\" INTEGER, v TEXT, PRIMARY KEY(ID))");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> Id;\n"
                           "    std::optional<std::string> v;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"Id\", &T::Id),\n"
                           "        make_column(\"v\", &T::v),\n"
                           "        primary_key(&T::Id)));");
    REQUIRE(result.warnings.empty());
}

TEST_CASE("codegen: CREATE TABLE - a table UNIQUE naming its column in brackets") {
    auto result = generateFull("CREATE TABLE t (\"Id\" INTEGER, v TEXT, UNIQUE([id]))");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> Id;\n"
                           "    std::optional<std::string> v;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"Id\", &T::Id),\n"
                           "        make_column(\"v\", &T::v),\n"
                           "        sqlite_orm::unique(&T::Id)));");
    REQUIRE(result.warnings.empty());
}

// Both sides of the key are resolved: the column of this table against its declarations, the column
// of the referenced one against the declarations `o` was registered with.
TEST_CASE("codegen: CREATE TABLE - a table FOREIGN KEY naming both columns in another case") {
    auto result = generateLastOfBatch("CREATE TABLE o (\"k\" INTEGER PRIMARY KEY);"
                                      "CREATE TABLE t (\"Id\" INTEGER, v TEXT, FOREIGN KEY(ID) REFERENCES o(K));");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> Id;\n"
                           "    std::optional<std::string> v;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"Id\", &T::Id),\n"
                           "        make_column(\"v\", &T::v),\n"
                           "        foreign_key(&T::Id).references(&O::k)));");
    REQUIRE(result.warnings.empty());
}

TEST_CASE("codegen: CREATE TABLE - a table CHECK naming its column in another case") {
    auto result = generateFull("CREATE TABLE t (\"Id\" INTEGER, v TEXT, CHECK(ID > 0))");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> Id;\n"
                           "    std::optional<std::string> v;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"Id\", &T::Id),\n"
                           "        make_column(\"v\", &T::v),\n"
                           "        check(c(&T::Id) > 0)));");
    REQUIRE(result.warnings.empty());
}

// A CHECK may qualify the column with the table it is declared on, and a column constraint holds the
// same kind of expression as a table one — both reach the same resolver.
TEST_CASE("codegen: CREATE TABLE - a qualified CHECK naming its column in another case") {
    auto result = generateFull("CREATE TABLE t (\"Id\" INTEGER CHECK(t.ID > 0), g INTEGER AS (ID + 1))");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> Id;\n"
                           "    std::optional<int64_t> g;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"Id\", &T::Id, check(c(&T::Id) > 0)),\n"
                           "        make_column(\"g\", &T::g, as(c(&T::Id) + 1))));");
    REQUIRE(result.warnings.empty());
}

// The other end of the resolution: a name no declaration answers. SQLite refuses every one of these
// CREATE TABLE statements — "no such column: zz", and "unknown column \"zz\" in foreign key
// definition" for the key (checked against sqlite3 3.51.0) — so there is no member to point at and
// the constraint is left out with a warning rather than generated into a header that cannot be built.
TEST_CASE("codegen: CREATE TABLE - a table constraint over a column the table does not declare") {
    auto primaryKey = generateFull("CREATE TABLE t (a INTEGER, PRIMARY KEY(zz))");
    REQUIRE(primaryKey.code == "struct T {\n"
                               "    std::optional<int64_t> a;\n"
                               "};\n"
                               "\n"
                               "auto storage = make_storage(\"\",\n"
                               "    make_table(\"t\",\n"
                               "        make_column(\"a\", &T::a)));");
    REQUIRE(primaryKey.warnings ==
            std::vector<CodegenWarning>{{"the PRIMARY KEY of table t names column 'zz', which the table does not "
                                         "declare: SQLite refuses such a CREATE TABLE, so the generated table has "
                                         "no primary_key()"}});

    auto unique = generateFull("CREATE TABLE t (a INTEGER, UNIQUE(zz))");
    REQUIRE(unique.code == "struct T {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"a\", &T::a)));");
    REQUIRE(unique.warnings ==
            std::vector<CodegenWarning>{{"the UNIQUE constraint of table t names column 'zz', which the table does "
                                         "not declare: SQLite refuses such a CREATE TABLE, so the generated table "
                                         "has no unique()"}});

    auto foreignKey = generateFull("CREATE TABLE t (a INTEGER, FOREIGN KEY(zz) REFERENCES o(k))");
    REQUIRE(foreignKey.code == "struct T {\n"
                               "    std::optional<int64_t> a;\n"
                               "};\n"
                               "\n"
                               "auto storage = make_storage(\"\",\n"
                               "    make_table(\"t\",\n"
                               "        make_column(\"a\", &T::a)));");
    REQUIRE(foreignKey.warnings ==
            std::vector<CodegenWarning>{{"the FOREIGN KEY of table t names column 'zz', which the table does not "
                                         "declare: SQLite refuses such a CREATE TABLE, so the generated table has "
                                         "no foreign_key()"}});

    auto check = generateFull("CREATE TABLE t (a INTEGER, CHECK(zz > 0))");
    REQUIRE(check.code == "struct T {\n"
                          "    std::optional<int64_t> a;\n"
                          "};\n"
                          "\n"
                          "auto storage = make_storage(\"\",\n"
                          "    make_table(\"t\",\n"
                          "        make_column(\"a\", &T::a)));");
    REQUIRE(check.warnings ==
            std::vector<CodegenWarning>{{"the CHECK constraint of table t names column 'zz', which the table does "
                                         "not declare: SQLite refuses such a CREATE TABLE, so the generated table "
                                         "has no check()"}});
}

// A generated column that lost its `as(...)` would be an ordinary column, which is a table SQLite
// does not have, so the table is left out whole — the same answer a STORED generated column holding
// a hex literal past the int64 range gets.
TEST_CASE("codegen: CREATE TABLE - a column constraint over a column the table does not declare") {
    auto columnCheck = generateFull("CREATE TABLE t (a INTEGER CHECK(zz > 0))");
    REQUIRE(columnCheck.code == "struct T {\n"
                                "    std::optional<int64_t> a;\n"
                                "};\n"
                                "\n"
                                "auto storage = make_storage(\"\",\n"
                                "    make_table(\"t\",\n"
                                "        make_column(\"a\", &T::a)));");
    REQUIRE(columnCheck.warnings ==
            std::vector<CodegenWarning>{{"the CHECK on column 'a' of table t names column 'zz', which the table does "
                                         "not declare: SQLite refuses such a CREATE TABLE, so the generated column "
                                         "has no check()"}});

    auto generated = generateFull("CREATE TABLE t (a INTEGER, g INTEGER AS (zz + 1))");
    REQUIRE(generated.code == "/* CREATE TABLE t \xe2\x80\x94 not supported for sqlite_orm */");
    REQUIRE(generated.warnings ==
            std::vector<CodegenWarning>{{"generated column 'g' of table t names column 'zz', which the table does "
                                         "not declare: SQLite refuses such a CREATE TABLE, so the table is not "
                                         "generated"}});
}

// A key back into the table's own primary key names no column of the parent, so the member it
// references is the one that key stands over — and that key spells its column the way its own
// constraint was written. sqlite3 3.51.0 takes both statements and enforces the key with PRAGMA
// foreign_keys=ON. Written from the constraint's spelling, the two came out as `references(&T::ID)`
// beside `primary_key(&T::Id)`: one column of one table written as two different members.
TEST_CASE("codegen: CREATE TABLE - a FOREIGN KEY into a table key naming its column in another case") {
    const std::string expected = "struct T {\n"
                                 "    std::optional<int64_t> Id;\n"
                                 "    std::optional<std::string> v;\n"
                                 "};\n"
                                 "\n"
                                 "auto storage = make_storage(\"\",\n"
                                 "    make_table(\"t\",\n"
                                 "        make_column(\"Id\", &T::Id),\n"
                                 "        make_column(\"v\", &T::v),\n"
                                 "        foreign_key(&T::v).references(&T::Id),\n"
                                 "        primary_key(&T::Id)));";

    auto tableLevel = generateFull("CREATE TABLE t (\"Id\" INTEGER, v TEXT, PRIMARY KEY(ID), "
                                   "FOREIGN KEY(v) REFERENCES t)");
    REQUIRE(tableLevel.code == expected);
    REQUIRE(tableLevel.warnings.empty());

    auto columnLevel = generateFull("CREATE TABLE t (\"Id\" INTEGER, v TEXT REFERENCES t, PRIMARY KEY(ID))");
    REQUIRE(columnLevel.code == expected);
    REQUIRE(columnLevel.warnings.empty());
}

// `rowid`, `oid` and `_rowid_` are the implicit row id of a rowid table rather than a column it
// declares, and what SQLite does with a constraint over one depends on the constraint and on the
// table: sqlite3 3.51.0 takes `CHECK(rowid > 0)` on a rowid table, refuses it on a WITHOUT ROWID one
// ("no such column: rowid"), and refuses `PRIMARY KEY(rowid)` and a generated column over `rowid`
// either way. So the warning speaks for sqlite_orm, which maps no member onto a row id the table
// never declared, and says nothing about the statement SQLite was handed.
TEST_CASE("codegen: CREATE TABLE - a constraint over the implicit row id") {
    const std::string table = "struct T {\n"
                              "    std::optional<int64_t> a;\n"
                              "};\n"
                              "\n"
                              "auto storage = make_storage(\"\",\n"
                              "    make_table(\"t\",\n"
                              "        make_column(\"a\", &T::a)));";

    auto tableCheck = generateFull("CREATE TABLE t (a INTEGER, CHECK(rowid > 0))");
    REQUIRE(tableCheck.code == table);
    REQUIRE(tableCheck.warnings ==
            std::vector<CodegenWarning>{{"the CHECK constraint of table t names column 'rowid', which the table does "
                                         "not declare: sqlite_orm maps no member onto the implicit row id, so the "
                                         "generated table has no check()"}});

    auto columnCheck = generateFull("CREATE TABLE t (a INTEGER CHECK(_rowid_ > 0))");
    REQUIRE(columnCheck.code == table);
    REQUIRE(columnCheck.warnings ==
            std::vector<CodegenWarning>{{"the CHECK on column 'a' of table t names column '_rowid_', which the table "
                                         "does not declare: sqlite_orm maps no member onto the implicit row id, so "
                                         "the generated column has no check()"}});

    auto primaryKey = generateFull("CREATE TABLE t (a INTEGER, PRIMARY KEY(oid))");
    REQUIRE(primaryKey.code == table);
    REQUIRE(primaryKey.warnings ==
            std::vector<CodegenWarning>{{"the PRIMARY KEY of table t names column 'oid', which the table does not "
                                         "declare: sqlite_orm maps no member onto the implicit row id, so the "
                                         "generated table has no primary_key()"}});
}

// A DEFAULT written without parentheses is a literal value and not an expression, so an identifier
// standing there names no column: SQLite reads it as a string in every spelling, and stores it even
// beside a column of that very name. sqlite3 3.51.0 stores 'A' for the first column below, 'abc'
// for the second and 'a"b', 'x y' and 'q' for the rest; before the parser read the term as the
// string it is, each of them generated `default_value(&T::…)` — a member pointer where a string
// belongs, and for `DEFAULT A` beside a column `"a"` one that compiles.
TEST_CASE("codegen: CREATE TABLE - an identifier in a DEFAULT without parentheses is a string") {
    auto result = generateFull("CREATE TABLE t (\"a\" INT, b TEXT DEFAULT A, c TEXT DEFAULT abc, "
                               "d TEXT DEFAULT \"a\"\"b\", e TEXT DEFAULT [x y], f TEXT DEFAULT `q`)");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> a;\n"
                           "    std::optional<std::string> b;\n"
                           "    std::optional<std::string> c;\n"
                           "    std::optional<std::string> d;\n"
                           "    std::optional<std::string> e;\n"
                           "    std::optional<std::string> f;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"a\", &T::a),\n"
                           "        make_column(\"b\", &T::b, default_value(\"A\")),\n"
                           "        make_column(\"c\", &T::c, default_value(\"abc\")),\n"
                           "        make_column(\"d\", &T::d, default_value(\"a\\\"b\")),\n"
                           "        make_column(\"e\", &T::e, default_value(\"x y\")),\n"
                           "        make_column(\"f\", &T::f, default_value(\"q\"))));");
    REQUIRE(result.warnings.empty());
}

// The other side of the same fork: a parenthesized DEFAULT is an expression, and an expression
// DEFAULT has to be constant, so sqlite3 3.51.0 refuses every column reference written in one
// ("default value of column [b] is not constant") — whichever spelling it takes, and whether or not
// the table declares a column of that name. There is no member to write it from either way.
TEST_CASE("codegen: CREATE TABLE - a parenthesized DEFAULT over a column") {
    const std::string table = "struct T {\n"
                              "    std::optional<int64_t> a;\n"
                              "    std::optional<int64_t> b;\n"
                              "};\n"
                              "\n"
                              "auto storage = make_storage(\"\",\n"
                              "    make_table(\"t\",\n"
                              "        make_column(\"a\", &T::a),\n"
                              "        make_column(\"b\", &T::b)));";

    auto undeclared = generateFull("CREATE TABLE t (a INTEGER, b INTEGER DEFAULT (zz))");
    REQUIRE(undeclared.code == table);
    REQUIRE(undeclared.warnings ==
            std::vector<CodegenWarning>{{"the DEFAULT of column 'b' of table t names column 'zz': SQLite refuses a "
                                         "DEFAULT that is not constant (\"default value of column [b] is not "
                                         "constant\"), so the generated column has no default_value()"}});

    auto declared = generateFull("CREATE TABLE t (\"a\" INTEGER, b INTEGER DEFAULT (A))");
    REQUIRE(declared.code == table);
    REQUIRE(declared.warnings ==
            std::vector<CodegenWarning>{{"the DEFAULT of column 'b' of table t names column 'A': SQLite refuses a "
                                         "DEFAULT that is not constant (\"default value of column [b] is not "
                                         "constant\"), so the generated column has no default_value()"}});
}

// SQLite reads a double-quoted name that no column of the table answers as a string literal instead
// of refusing the statement — its "double-quoted string" misfeature — and takes every CHECK and
// generated column below, computing 'a' || 'zz' for the generated one (checked against sqlite3
// 3.51.0). Before the clause told the two apart, all three lost the constraint and the generated
// one lost the whole table, and the warning said SQLite had refused a statement it takes.
TEST_CASE("codegen: CREATE TABLE - a double-quoted string in a CHECK or a generated column") {
    auto tableCheck = generateFull("CREATE TABLE t (\"V\" TEXT, CHECK(\"V\" <> \"zz\"), CHECK(\"v\" <> \"a\"\"b\"))");
    REQUIRE(tableCheck.code == "struct T {\n"
                               "    std::optional<std::string> V;\n"
                               "};\n"
                               "\n"
                               "auto storage = make_storage(\"\",\n"
                               "    make_table(\"t\",\n"
                               "        make_column(\"V\", &T::V),\n"
                               "        check(c(&T::V) != \"zz\"),\n"
                               "        check(c(&T::V) != \"a\\\"b\")));");
    REQUIRE(tableCheck.warnings.empty());

    auto columnCheck = generateFull("CREATE TABLE t (v TEXT CHECK(v <> \"zz\"))");
    REQUIRE(columnCheck.code == "struct T {\n"
                                "    std::optional<std::string> v;\n"
                                "};\n"
                                "\n"
                                "auto storage = make_storage(\"\",\n"
                                "    make_table(\"t\",\n"
                                "        make_column(\"v\", &T::v, check(c(&T::v) != \"zz\"))));");
    REQUIRE(columnCheck.warnings.empty());

    auto generated = generateFull("CREATE TABLE t (v TEXT, g TEXT AS (v || \"zz\"))");
    REQUIRE(generated.code == "struct T {\n"
                              "    std::optional<std::string> v;\n"
                              "    std::optional<std::string> g;\n"
                              "};\n"
                              "\n"
                              "auto storage = make_storage(\"\",\n"
                              "    make_table(\"t\",\n"
                              "        make_column(\"v\", &T::v),\n"
                              "        make_column(\"g\", &T::g, as(c(&T::v) || \"zz\"))));");
    REQUIRE(generated.warnings.empty());
}

// Only the double quote carries that misfeature. sqlite3 3.51.0 refuses the same name written bare,
// in backticks or in brackets ("no such column: zz"), and refuses a qualified double-quoted one
// ("no such column: t.zz") — so those stay the constraint SQLite has no column for, and the warning
// that says as much stays true.
TEST_CASE("codegen: CREATE TABLE - a name no column answers written any other way") {
    const std::string table = "struct T {\n"
                              "    std::optional<std::string> v;\n"
                              "};\n"
                              "\n"
                              "auto storage = make_storage(\"\",\n"
                              "    make_table(\"t\",\n"
                              "        make_column(\"v\", &T::v)));";
    const std::vector<CodegenWarning> warnings{
        {"the CHECK constraint of table t names column 'zz', which the table does not declare: SQLite refuses such "
         "a CREATE TABLE, so the generated table has no check()"}};

    for (const std::string& expression:
         {std::string("v <> zz"), std::string("v <> `zz`"), std::string("v <> [zz]"), std::string("t.\"zz\" <> v")}) {
        auto result = generateFull("CREATE TABLE t (v TEXT, CHECK(" + expression + "))");
        REQUIRE(result.code == table);
        REQUIRE(result.warnings == warnings);
    }
}

// A WITHOUT ROWID table has no row id to fall back on, so one whose only PRIMARY KEY was left out
// has nothing left to identify a row by: the mapping would compile and the CREATE TABLE sync_schema
// runs from it is one sqlite3 3.51.0 refuses ("PRIMARY KEY missing on table t"). It is left out
// whole instead, the same answer a generated column that lost its `as(...)` gets.
TEST_CASE("codegen: CREATE TABLE - WITHOUT ROWID whose PRIMARY KEY was left out") {
    auto result = generateFull("CREATE TABLE t (a INTEGER, PRIMARY KEY(zz)) WITHOUT ROWID");
    REQUIRE(result.code == "/* CREATE TABLE t \xe2\x80\x94 not supported for sqlite_orm */");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{{"the PRIMARY KEY of table t names column 'zz', which the table does not "
                                         "declare: SQLite refuses such a CREATE TABLE, so the generated table has "
                                         "no primary_key()"},
                                        {"table t is WITHOUT ROWID and the PRIMARY KEY it declares was left out, so "
                                         "the table is not generated"}});
}

// A CHECK qualified with a table other than the one it is declared on is that other table's column,
// and is written as that table declares it rather than as this one does — the declarations of the
// table being generated answer for its own name only. sqlite3 3.51.0 refuses such a CREATE TABLE
// ("no such column: o.k"), so there is no valid statement behind this shape; the member written
// stays the one `o` declares rather than a member of `O` picked from `t`'s columns.
TEST_CASE("codegen: CREATE TABLE - a CHECK qualified with another table") {
    auto result = generateLastOfBatch("CREATE TABLE o (\"K\" INTEGER);"
                                      "CREATE TABLE t (a INTEGER, CHECK(o.k > 0));");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"a\", &T::a),\n"
                           "        check(c(&O::K) > 0)));");
    REQUIRE(result.warnings.empty());
}

TEST_CASE("codegen: CREATE TABLE - WITHOUT ROWID") {
    auto result = generate("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT NOT NULL) WITHOUT ROWID");
    REQUIRE(result == "struct T {\n"
                      "    int64_t id = 0;\n"
                      "    std::string name;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id, primary_key()),\n"
                      "        make_column(\"name\", &T::name)).without_rowid());");
}

// SQLite leaves a PRIMARY KEY column of a rowid table nullable — `PRAGMA table_info` reports
// `notnull = 0` for it, and an INTEGER PRIMARY KEY turns an inserted NULL into the next rowid — so
// the member it is mapped to is an optional like any other column the schema does not declare NOT
// NULL. A member that promised more would make sqlite_orm emit `NOT NULL` and `sync_schema()` drop
// the user's table to rebuild it.
TEST_CASE("codegen: CREATE TABLE - a table-level PRIMARY KEY leaves its columns nullable") {
    const auto result = generate("CREATE TABLE t (a INTEGER, b INTEGER, PRIMARY KEY(a, b));");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> a;\n"
                      "    std::optional<int64_t> b;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"a\", &T::a),\n"
                      "        make_column(\"b\", &T::b),\n"
                      "        primary_key(&T::a, &T::b)));");
}

// A WITHOUT ROWID table is where SQLite documents the other answer: its PRIMARY KEY columns are
// implicitly NOT NULL, the pragma reports `notnull = 1` for them, and the members follow.
TEST_CASE("codegen: CREATE TABLE - a table-level PRIMARY KEY of a WITHOUT ROWID table is NOT NULL") {
    const auto result = generate("CREATE TABLE t (a INTEGER, b INTEGER, PRIMARY KEY(a, b)) WITHOUT ROWID;");
    REQUIRE(result == "struct T {\n"
                      "    int64_t a = 0;\n"
                      "    int64_t b = 0;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"a\", &T::a),\n"
                      "        make_column(\"b\", &T::b),\n"
                      "        primary_key(&T::a, &T::b)).without_rowid());");
}

// A STRICT table is the other place SQLite makes a key implicitly NOT NULL — the pragma reports
// `notnull = 1` for every PRIMARY KEY column of one. Checked against sqlite3 3.51.0 and 3.45.1.
TEST_CASE("codegen: CREATE TABLE - a PRIMARY KEY of a STRICT table is NOT NULL") {
    const auto result = generate("CREATE TABLE t (id TEXT PRIMARY KEY, v TEXT) STRICT;");
    REQUIRE(result == "struct T {\n"
                      "    std::string id;\n"
                      "    std::optional<std::string> v;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id, primary_key()),\n"
                      "        make_column(\"v\", &T::v)));");
}

TEST_CASE("codegen: CREATE TABLE - a table-level PRIMARY KEY of a STRICT table is NOT NULL") {
    const auto result = generate("CREATE TABLE t (a TEXT, b INT, PRIMARY KEY(a, b)) STRICT;");
    REQUIRE(result == "struct T {\n"
                      "    std::string a;\n"
                      "    int64_t b = 0;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"a\", &T::a),\n"
                      "        make_column(\"b\", &T::b),\n"
                      "        primary_key(&T::a, &T::b)));");
}

// The rowid alias is the one PRIMARY KEY a STRICT table leaves nullable: the column is the rowid
// itself, so SQLite goes on turning an inserted NULL into the next rowid and reports
// `notnull = 0`.
TEST_CASE("codegen: CREATE TABLE - the rowid alias of a STRICT table stays nullable") {
    const auto result = generate("CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT) STRICT;");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> id;\n"
                      "    std::optional<std::string> v;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id, primary_key()),\n"
                      "        make_column(\"v\", &T::v)));");
}

// The alias is a matter of spelling, and these are the spellings that miss it: an ASC keeps the
// alias, a DESC on the column takes it away, and the declared type has to be INTEGER exactly —
// `INT PRIMARY KEY` is an ordinary column with a rowid of its own behind it.
TEST_CASE("codegen: CREATE TABLE - a DESC PRIMARY KEY of a STRICT table is no alias") {
    const auto result = generate("CREATE TABLE t (id INTEGER PRIMARY KEY DESC, v TEXT) STRICT;");
    REQUIRE(result == "struct T {\n"
                      "    int64_t id = 0;\n"
                      "    std::optional<std::string> v;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id, primary_key()),\n"
                      "        make_column(\"v\", &T::v)));");
}

TEST_CASE("codegen: CREATE TABLE - an ASC PRIMARY KEY of a STRICT table is still the alias") {
    const auto result = generate("CREATE TABLE t (id INTEGER PRIMARY KEY ASC, v TEXT) STRICT;");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> id;\n"
                      "    std::optional<std::string> v;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id, primary_key()),\n"
                      "        make_column(\"v\", &T::v)));");
}

TEST_CASE("codegen: CREATE TABLE - an INT PRIMARY KEY of a STRICT table is no alias") {
    const auto result = generate("CREATE TABLE t (id INT PRIMARY KEY, v TEXT) STRICT;");
    REQUIRE(result == "struct T {\n"
                      "    int64_t id = 0;\n"
                      "    std::optional<std::string> v;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id, primary_key()),\n"
                      "        make_column(\"v\", &T::v)));");
}

// Spelled on the table, a single INTEGER column is the alias just the same.
TEST_CASE("codegen: CREATE TABLE - a table-level key over one INTEGER column is the alias") {
    const auto result = generate("CREATE TABLE t (id INTEGER, v TEXT, PRIMARY KEY(id)) STRICT;");
    REQUIRE(result == "struct T {\n"
                      "    std::optional<int64_t> id;\n"
                      "    std::optional<std::string> v;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id),\n"
                      "        make_column(\"v\", &T::v),\n"
                      "        primary_key(&T::id)));");
}

// WITHOUT ROWID leaves no rowid to alias, so the exception does not apply and the key is NOT NULL.
TEST_CASE("codegen: CREATE TABLE - a STRICT WITHOUT ROWID table has no alias to spare") {
    const auto result = generate("CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT) STRICT, WITHOUT ROWID;");
    REQUIRE(result == "struct T {\n"
                      "    int64_t id = 0;\n"
                      "    std::optional<std::string> v;\n"
                      "};\n"
                      "\n"
                      "auto storage = make_storage(\"\",\n"
                      "    make_table(\"t\",\n"
                      "        make_column(\"id\", &T::id, primary_key()),\n"
                      "        make_column(\"v\", &T::v)).without_rowid());");
}

TEST_CASE("codegen: STRICT table warning") {
    const auto result = generateFull("CREATE TABLE t (a TEXT) STRICT;");
    REQUIRE_FALSE(result.warnings.empty());
    bool found = false;
    for (const auto& w: result.warnings) {
        if (w.message.find("STRICT") != std::string::npos) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("codegen: FK DEFERRABLE parsed") {
    auto result = generateFull("CREATE TABLE t (id INT REFERENCES p(id) DEFERRABLE INITIALLY DEFERRED);");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> id;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"id\", &T::id),\n"
                           "        foreign_key(&T::id).references(&P::id)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{"DEFERRABLE INITIALLY DEFERRED on foreign key for column 'id' "
                                        "is not supported in sqlite_orm — ignored in codegen"});
}

// sqlite_orm maps a table by C++26 reflection when the target allows it (upstream #1492): the
// columns and their constraints come off the struct's members and their annotations, so
// `make_table<T>()` takes no column at all. The classical form stays the alternative of the
// `table_mapping_style` decision point, and it is what a target below C++26 gets — the option is
// not even offered there, so nothing about that output changes.
namespace {
    const std::string kTableReflectionComment =
        "The table is mapped by sqlite_orm's reflection-based `make_table<T>()`: the columns and "
        "their constraints are read off the struct's members and `[[= …]]` annotations, and the "
        "`[[= \"…\"_orm_name]]` annotation supplies the table name. This requires a C++26 compiler "
        "with reflection (P2996/P3394); sqlite_orm detects support automatically "
        "(SQLITE_ORM_REFLECTION_SUPPORTED). The `make_table` alternative of the `table_mapping_style` "
        "decision point is the classical form and compiles from C++14 on.";

    const std::string kClassicalOptionDescription =
        "make_table(\"name\", make_column(…)) over a plain struct (wider compiler support)";
    const std::string kReflectionOptionDescription = "C++26 reflection: annotated struct + make_table<T>()";

    /** The one option a table that has no reflected form is left with, carrying `reason`. */
    Option classicalOnlyOption(std::string code, const std::string& reason) {
        Option option{"make_table", std::move(code), kClassicalOptionDescription};
        option.comments.push_back("the C++26 reflection alternative is not offered for this table: " + reason);
        return option;
    }

    CodeGenResult generateTargetingCpp26(std::string_view sql) {
        CodeGenPolicy policy;
        policy.targetCppStandard = 26;
        return generateWithPolicy(sql, policy);
    }
}  // namespace

TEST_CASE("codegen: CREATE TABLE - targeting C++20 offers no table_mapping_style decision point") {
    CodeGenPolicy policy;
    policy.targetCppStandard = 20;
    const auto result = generateWithPolicy("CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT "
                                           "NULL, score INTEGER DEFAULT 0, handle TEXT COLLATE NOCASE);",
                                           policy);
    REQUIRE(result == CodeGenResult{"struct Users {\n"
                                    "    std::optional<int64_t> id;\n"
                                    "    std::string name;\n"
                                    "    std::optional<int64_t> score;\n"
                                    "    std::optional<std::string> handle;\n"
                                    "};\n"
                                    "\n"
                                    "auto storage = make_storage(\"\",\n"
                                    "    make_table(\"users\",\n"
                                    "        make_column(\"id\", &Users::id, primary_key().autoincrement()),\n"
                                    "        make_column(\"name\", &Users::name),\n"
                                    "        make_column(\"score\", &Users::score, default_value(0)),\n"
                                    "        make_column(\"handle\", &Users::handle, collate_nocase())));"});
}

TEST_CASE("codegen: CREATE TABLE - targeting C++26 chooses the reflected mapping") {
    const auto result = generateTargetingCpp26("CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT "
                                               "NOT NULL, score INTEGER DEFAULT 0, handle TEXT COLLATE NOCASE);");
    const std::string classicalCode = "struct Users {\n"
                                      "    std::optional<int64_t> id;\n"
                                      "    std::string name;\n"
                                      "    std::optional<int64_t> score;\n"
                                      "    std::optional<std::string> handle;\n"
                                      "};\n"
                                      "\n"
                                      "make_table(\"users\",\n"
                                      "        make_column(\"id\", &Users::id, primary_key().autoincrement()),\n"
                                      "        make_column(\"name\", &Users::name),\n"
                                      "        make_column(\"score\", &Users::score, default_value(0)),\n"
                                      "        make_column(\"handle\", &Users::handle, collate_nocase()))";
    const std::string reflectedCode = "struct [[= \"users\"_orm_name]] Users {\n"
                                      "    [[= primary_key().autoincrement()]] std::optional<int64_t> id;\n"
                                      "    std::string name;\n"
                                      "    [[= default_value(0)]] std::optional<int64_t> score;\n"
                                      "    [[= collate_nocase()]] std::optional<std::string> handle;\n"
                                      "};\n"
                                      "\n"
                                      "make_table<Users>()";
    Option reflectedOption{"reflection", reflectedCode, kReflectionOptionDescription};
    reflectedOption.comments.push_back(kTableReflectionComment);
    reflectedOption.minCppStandard = 26;
    REQUIRE(result == CodeGenResult{"struct [[= \"users\"_orm_name]] Users {\n"
                                    "    [[= primary_key().autoincrement()]] std::optional<int64_t> id;\n"
                                    "    std::string name;\n"
                                    "    [[= default_value(0)]] std::optional<int64_t> score;\n"
                                    "    [[= collate_nocase()]] std::optional<std::string> handle;\n"
                                    "};\n"
                                    "\n"
                                    "auto storage = make_storage(\"\",\n"
                                    "    make_table<Users>());",
                                    {DecisionPoint{1,
                                                   "table_mapping_style",
                                                   "reflection",
                                                   reflectedCode,
                                                   {Option{"make_table", classicalCode, kClassicalOptionDescription},
                                                    reflectedOption}}},
                                    {},
                                    {},
                                    {kTableReflectionComment}});
}

// No released compiler implements P2996 yet, so a consumer targeting C++26 has to be able to ask
// for the classical mapping back: the `table_mapping_style` category does exactly that, and both
// variants stay on offer either way.
TEST_CASE("codegen: CREATE TABLE - an explicit make_table policy keeps the classical mapping under C++26") {
    CodeGenPolicy policy;
    policy.targetCppStandard = 26;
    policy.chosenAlternativeValueByCategory["table_mapping_style"] = "make_table";
    const auto result = generateWithPolicy("CREATE TABLE t (a INTEGER PRIMARY KEY);", policy);
    const std::string classicalCode = "struct T {\n"
                                      "    std::optional<int64_t> a;\n"
                                      "};\n"
                                      "\n"
                                      "make_table(\"t\",\n"
                                      "        make_column(\"a\", &T::a, primary_key()))";
    const std::string reflectedCode = "struct [[= \"t\"_orm_name]] T {\n"
                                      "    [[= primary_key()]] std::optional<int64_t> a;\n"
                                      "};\n"
                                      "\n"
                                      "make_table<T>()";
    Option reflectedOption{"reflection", reflectedCode, kReflectionOptionDescription};
    reflectedOption.comments.push_back(kTableReflectionComment);
    reflectedOption.minCppStandard = 26;
    REQUIRE(result == CodeGenResult{"struct T {\n"
                                    "    std::optional<int64_t> a;\n"
                                    "};\n"
                                    "\n"
                                    "auto storage = make_storage(\"\",\n"
                                    "    make_table(\"t\",\n"
                                    "        make_column(\"a\", &T::a, primary_key())));",
                                    {DecisionPoint{1,
                                                   "table_mapping_style",
                                                   "make_table",
                                                   classicalCode,
                                                   {Option{"make_table", classicalCode, kClassicalOptionDescription},
                                                    reflectedOption}}}});
}

// The other way round the standard wins: below C++26 the reflected form does not compile, so
// asking for it changes nothing — there is no decision point to answer in the first place.
TEST_CASE("codegen: CREATE TABLE - an explicit reflection policy is overridden by targetCppStandard 20") {
    CodeGenPolicy policy;
    policy.targetCppStandard = 20;
    policy.chosenAlternativeValueByCategory["table_mapping_style"] = "reflection";
    const auto result = generateWithPolicy("CREATE TABLE t (a INTEGER PRIMARY KEY);", policy);
    REQUIRE(result == CodeGenResult{"struct T {\n"
                                    "    std::optional<int64_t> a;\n"
                                    "};\n"
                                    "\n"
                                    "auto storage = make_storage(\"\",\n"
                                    "    make_table(\"t\",\n"
                                    "        make_column(\"a\", &T::a, primary_key())));"});
}

// A table that has no reflected form has nothing to switch to either: the policy is answered by
// the one option there is.
TEST_CASE("codegen: CREATE TABLE - an explicit reflection policy cannot revive a blocked table") {
    CodeGenPolicy policy;
    policy.targetCppStandard = 26;
    policy.chosenAlternativeValueByCategory["table_mapping_style"] = "reflection";
    const auto result = generateWithPolicy("CREATE TABLE t (a INTEGER CHECK(a > 0));", policy);
    const std::string classicalCode = "struct T {\n"
                                      "    std::optional<int64_t> a;\n"
                                      "};\n"
                                      "\n"
                                      "make_table(\"t\",\n"
                                      "        make_column(\"a\", &T::a, check(c(&T::a) > 0)))";
    REQUIRE(result.decisionPoints ==
            std::vector<DecisionPoint>{
                DecisionPoint{3,
                              "table_mapping_style",
                              "make_table",
                              classicalCode,
                              {classicalOnlyOption(classicalCode,
                                                   "CHECK on column `a` names members of the struct being declared, "
                                                   "which an annotation cannot")}}});
}

// A table-level constraint is no annotation: it stays a call argument, of `make_table<T>(…)` this
// time, and `.without_rowid()` still follows the call.
TEST_CASE("codegen: CREATE TABLE - table-level constraints stay arguments of the reflected make_table") {
    const auto result = generateTargetingCpp26("CREATE TABLE t (a INTEGER, b INTEGER, PRIMARY KEY(a, b)) WITHOUT "
                                               "ROWID;");
    REQUIRE(result.code == "struct [[= \"t\"_orm_name]] T {\n"
                           "    int64_t a = 0;\n"
                           "    int64_t b = 0;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table<T>(\n"
                           "        primary_key(&T::a, &T::b)).without_rowid());");
    REQUIRE(result.decisionPoints.size() == 1);
    REQUIRE(result.decisionPoints.at(0).chosenValue == "reflection");
    REQUIRE(result.decisionPoints.at(0).options.at(0).code == "struct T {\n"
                                                              "    int64_t a = 0;\n"
                                                              "    int64_t b = 0;\n"
                                                              "};\n"
                                                              "\n"
                                                              "make_table(\"t\",\n"
                                                              "        make_column(\"a\", &T::a),\n"
                                                              "        make_column(\"b\", &T::b),\n"
                                                              "        primary_key(&T::a, &T::b)).without_rowid()");
}

TEST_CASE("codegen: CREATE TABLE - a foreign key stays an argument of the reflected make_table") {
    const auto result = generateTargetingCpp26("CREATE TABLE t (id INT REFERENCES p(id) ON DELETE CASCADE, u TEXT);");
    REQUIRE(result.code == "struct [[= \"t\"_orm_name]] T {\n"
                           "    std::optional<int64_t> id;\n"
                           "    std::optional<std::string> u;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table<T>(\n"
                           "        foreign_key(&T::id).references(&P::id).on_delete.cascade()));");
}

TEST_CASE("codegen: CREATE TABLE - a table CHECK stays an argument of the reflected make_table") {
    const auto result = generateTargetingCpp26("CREATE TABLE t (a INTEGER, CHECK(a > 0));");
    REQUIRE(result.code == "struct [[= \"t\"_orm_name]] T {\n"
                           "    std::optional<int64_t> a;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table<T>(\n"
                           "        check(c(&T::a) > 0)));");
}

// A reflected column is named after the member it reflects — sqlite_orm reads
// `std::meta::identifier_of`, and there is no renaming — so a column name C++ cannot spell leaves
// the table on the classical mapping even under C++26, with the reason on the one option offered.
TEST_CASE("codegen: CREATE TABLE - a column name that is no C++ identifier keeps the classical mapping") {
    const auto result = generateTargetingCpp26("CREATE TABLE t (\"first name\" TEXT, id INTEGER PRIMARY KEY);");
    const std::string classicalCode = "struct T {\n"
                                      "    std::optional<std::string> first_name;\n"
                                      "    std::optional<int64_t> id;\n"
                                      "};\n"
                                      "\n"
                                      "make_table(\"t\",\n"
                                      "        make_column(\"first name\", &T::first_name),\n"
                                      "        make_column(\"id\", &T::id, primary_key()))";
    REQUIRE(result == CodeGenResult{"struct T {\n"
                                    "    std::optional<std::string> first_name;\n"
                                    "    std::optional<int64_t> id;\n"
                                    "};\n"
                                    "\n"
                                    "auto storage = make_storage(\"\",\n"
                                    "    make_table(\"t\",\n"
                                    "        make_column(\"first name\", &T::first_name),\n"
                                    "        make_column(\"id\", &T::id, primary_key())));",
                                    {DecisionPoint{1,
                                                   "table_mapping_style",
                                                   "make_table",
                                                   classicalCode,
                                                   {classicalOnlyOption(classicalCode,
                                                                        "column `first name` is not a C++ identifier, "
                                                                        "and a reflected column is named after the "
                                                                        "member it reflects")}}},
                                    {CodegenWarning{"table t: column `first name` is not a C++ identifier; the member "
                                                    "holding it is named `first_name`",
                                                    SourceLocation{1, 17},
                                                    12}}});
}

// An annotation is a constant expression, and a text DEFAULT is generated as a pointer to a string
// literal, which is not one.
TEST_CASE("codegen: CREATE TABLE - a text DEFAULT keeps the classical mapping under C++26") {
    const auto result = generateTargetingCpp26("CREATE TABLE t (a INTEGER, b TEXT DEFAULT 'x');");
    const std::string classicalCode = "struct T {\n"
                                      "    std::optional<int64_t> a;\n"
                                      "    std::optional<std::string> b;\n"
                                      "};\n"
                                      "\n"
                                      "make_table(\"t\",\n"
                                      "        make_column(\"a\", &T::a),\n"
                                      "        make_column(\"b\", &T::b, default_value(\"x\")))";
    REQUIRE(result.decisionPoints ==
            std::vector<DecisionPoint>{
                DecisionPoint{1,
                              "table_mapping_style",
                              "make_table",
                              classicalCode,
                              {classicalOnlyOption(classicalCode,
                                                   "the DEFAULT of column `b` is generated as `\"x\"`, which is not a "
                                                   "constant expression an annotation can carry")}}});
}

// `unique_t` is one of sqlite_orm's column constraints (`is_column_constraint`), and a member
// annotation is handed straight to `make_column()`, whose only gate is that very list — so a
// column UNIQUE annotates the member like the primary key and the collation do.
TEST_CASE("codegen: CREATE TABLE - a column UNIQUE annotates the member of the reflected struct") {
    const auto result = generateTargetingCpp26("CREATE TABLE t (a INTEGER UNIQUE);");
    REQUIRE(result.code == "struct [[= \"t\"_orm_name]] T {\n"
                           "    [[= unique()]] std::optional<int64_t> a;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table<T>());");
    REQUIRE(result.decisionPoints.size() == 1);
    REQUIRE(result.decisionPoints.at(0).chosenValue == "reflection");
    REQUIRE(result.decisionPoints.at(0).options.at(0).code == "struct T {\n"
                                                              "    std::optional<int64_t> a;\n"
                                                              "};\n"
                                                              "\n"
                                                              "make_table(\"t\",\n"
                                                              "        make_column(\"a\", &T::a, unique()))");
}

// A column CHECK and a generated column both name the struct's own members, and a member annotation
// is parsed inside the class, before the members it names are all declared — upstream sqlite_orm
// states such annotations are not expressible in C++26.
TEST_CASE("codegen: CREATE TABLE - a column CHECK keeps the classical mapping under C++26") {
    const auto result = generateTargetingCpp26("CREATE TABLE t (a INTEGER CHECK(a > 0));");
    const std::string classicalCode = "struct T {\n"
                                      "    std::optional<int64_t> a;\n"
                                      "};\n"
                                      "\n"
                                      "make_table(\"t\",\n"
                                      "        make_column(\"a\", &T::a, check(c(&T::a) > 0)))";
    REQUIRE(result.decisionPoints ==
            std::vector<DecisionPoint>{
                DecisionPoint{3,
                              "table_mapping_style",
                              "make_table",
                              classicalCode,
                              {classicalOnlyOption(classicalCode,
                                                   "CHECK on column `a` names members of the struct being declared, "
                                                   "which an annotation cannot")}}});
}

TEST_CASE("codegen: CREATE TABLE - a generated column keeps the classical mapping under C++26") {
    const auto result = generateTargetingCpp26("CREATE TABLE t (a INTEGER, b INTEGER AS (a + 1));");
    const std::string classicalCode = "struct T {\n"
                                      "    std::optional<int64_t> a;\n"
                                      "    std::optional<int64_t> b;\n"
                                      "};\n"
                                      "\n"
                                      "make_table(\"t\",\n"
                                      "        make_column(\"a\", &T::a),\n"
                                      "        make_column(\"b\", &T::b, as(c(&T::a) + 1)))";
    REQUIRE(result.decisionPoints ==
            std::vector<DecisionPoint>{
                DecisionPoint{3,
                              "table_mapping_style",
                              "make_table",
                              classicalCode,
                              {classicalOnlyOption(classicalCode,
                                                   "generated column `b` names members of the struct being declared, "
                                                   "which an annotation cannot")}}});
}

// What a consumer reading `--json` gets: the decision point carries both variants, each with the
// C++ standard it needs, so a playground can offer the classical mapping to a user whose compiler
// has no reflection.
TEST_CASE("codegen: CREATE TABLE - the table_mapping_style decision point reaches --json") {
    const auto result = generateTargetingCpp26("CREATE TABLE t (a INTEGER);");
    REQUIRE(
        decisionPointsToJson(result.decisionPoints) ==
        R"JSON([{"category":"table_mapping_style","chosenCode":"struct [[= \"t\"_orm_name]] T {\n    std::optional<int64_t> a;\n};\n\nmake_table<T>()","chosenValue":"reflection","id":1,"options":[{"code":"struct T {\n    std::optional<int64_t> a;\n};\n\nmake_table(\"t\",\n        make_column(\"a\", &T::a))","comments":[],"description":"make_table(\"name\", make_column(…)) over a plain struct (wider compiler support)","hidden":false,"minCppStandard":14,"value":"make_table"},{"code":"struct [[= \"t\"_orm_name]] T {\n    std::optional<int64_t> a;\n};\n\nmake_table<T>()","comments":["The table is mapped by sqlite_orm's reflection-based `make_table<T>()`: the columns and their constraints are read off the struct's members and `[[= …]]` annotations, and the `[[= \"…\"_orm_name]]` annotation supplies the table name. This requires a C++26 compiler with reflection (P2996/P3394); sqlite_orm detects support automatically (SQLITE_ORM_REFLECTION_SUPPORTED). The `make_table` alternative of the `table_mapping_style` decision point is the classical form and compiles from C++14 on."],"description":"C++26 reflection: annotated struct + make_table<T>()","hidden":false,"minCppStandard":26,"value":"reflection"}]}])JSON");
}

// An SQL name reaches the struct as a C++ identifier, and it is rewritten character by character:
// `üü` and `ää` are two characters each — not the four bytes each takes — so they stay two
// distinct members instead of collapsing into the same run of underscores, which is a struct that
// declares one member twice and does not compile. SQLite takes non-ASCII in a bare identifier as
// readily as in a quoted one, so this is not an exotic schema.
TEST_CASE("codegen: CREATE TABLE - non-ASCII column names become members of their own") {
    const auto result = generateFull("CREATE TABLE t (üü INTEGER, ää INTEGER)");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> u00FCu00FC;\n"
                           "    std::optional<int64_t> u00E4u00E4;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"üü\", &T::u00FCu00FC),\n"
                           "        make_column(\"ää\", &T::u00E4u00E4)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"table t: column `üü` is not a C++ identifier; the member holding it is named `u00FCu00FC`",
                 SourceLocation{1, 17},
                 2},
                {"table t: column `ää` is not a C++ identifier; the member holding it is named `u00E4u00E4`",
                 SourceLocation{1, 29},
                 2}});
}

// A character above the basic multilingual plane is written the way C++ writes one, with the eight
// hex digits of its code point; a byte that is no character at all — SQLite takes those in an
// identifier too — is written as that byte.
TEST_CASE("codegen: CREATE TABLE - a column name outside the basic multilingual plane") {
    const auto result = generateFull("CREATE TABLE t (🙂 INTEGER)");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> U0001F642;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"🙂\", &T::U0001F642)));");
}

TEST_CASE("codegen: CREATE TABLE - a column name that is not valid UTF-8 at all") {
    const std::string sql = "CREATE TABLE t (" + std::string(2, static_cast<char>(0x80)) + " INTEGER)";
    const auto result = generateFull(sql);
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> x80x80;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"" +
                               std::string(2, static_cast<char>(0x80)) + "\", &T::x80x80)));");
}

// Two names C++ has one spelling for is a member declared twice, which no amount of rewriting can
// avoid — it happens to plain ASCII names as readily as to any other — so it is reported, anchored
// at the name that lands on the member second, quotes and all.
TEST_CASE("codegen: CREATE TABLE - two column names mapped to one member are reported") {
    const auto result = generateFull("CREATE TABLE t (\"a-b\" INTEGER, \"a b\" INTEGER)");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> a_b;\n"
                           "    std::optional<int64_t> a_b;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"a-b\", &T::a_b),\n"
                           "        make_column(\"a b\", &T::a_b)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"table t: column `a-b` is not a C++ identifier; the member holding it is named `a_b`",
                 SourceLocation{1, 17},
                 5},
                {"table t: columns `a-b` and `a b` are both named `a_b` in C++; the generated struct declares that "
                 "member twice and does not compile",
                 SourceLocation{1, 32},
                 5}});
}

// SQLite takes a name of no characters at all: `""` is a column called nothing, and it reads and
// writes like any other. C++ has no identifier of no characters, so the member is named with the
// one character that carries no letters — without it the struct declared a member with no name at
// all, which no compiler takes.
TEST_CASE("codegen: CREATE TABLE - an empty column name still names a member") {
    const auto result = generateFull("CREATE TABLE t (\"\" INTEGER)");
    REQUIRE(result.code == "struct T {\n"
                           "    std::optional<int64_t> _;\n"
                           "};\n"
                           "\n"
                           "auto storage = make_storage(\"\",\n"
                           "    make_table(\"t\",\n"
                           "        make_column(\"\", &T::_)));");
    REQUIRE(result.warnings == std::vector<CodegenWarning>{
                                   {"table t: column `` is not a C++ identifier; the member holding it is named `_`",
                                    SourceLocation{1, 17},
                                    2}});
}
