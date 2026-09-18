#include "codegen_tests_common.hpp"

TEST_CASE("codegen: CREATE TABLE - basic") {
    auto result = generate("CREATE TABLE users (id INTEGER, name TEXT)");
    REQUIRE(result ==
        "struct Users {\n"
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
    REQUIRE(result ==
        "struct T {\n"
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
    REQUIRE(result ==
        "struct Products {\n"
        "    std::optional<int64_t> id;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"products\",\n"
        "        make_column(\"id\", &Products::id)));");
}

TEST_CASE("codegen: CREATE TABLE - no type defaults to BLOB") {
    auto result = generate("CREATE TABLE t (x)");
    REQUIRE(result ==
        "struct T {\n"
        "    std::optional<std::vector<char>> x;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"x\", &T::x)));");
}

TEST_CASE("codegen: CREATE TABLE - VARCHAR(255) maps to string") {
    auto result = generate("CREATE TABLE t (name VARCHAR(255))");
    REQUIRE(result ==
        "struct T {\n"
        "    std::optional<std::string> name;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"name\", &T::name)));");
}

TEST_CASE("codegen: CREATE TABLE - PRIMARY KEY") {
    auto result = generate("CREATE TABLE t (id INTEGER PRIMARY KEY)");
    REQUIRE(result ==
        "struct T {\n"
        "    int64_t id = 0;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"id\", &T::id, primary_key())));");
}

TEST_CASE("codegen: CREATE TABLE - PRIMARY KEY AUTOINCREMENT") {
    auto result = generate("CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT)");
    REQUIRE(result ==
        "struct T {\n"
        "    int64_t id = 0;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"id\", &T::id, primary_key().autoincrement())));");
}

TEST_CASE("codegen: CREATE TABLE - NOT NULL removes optional") {
    auto result = generate("CREATE TABLE t (name TEXT NOT NULL)");
    REQUIRE(result ==
        "struct T {\n"
        "    std::string name;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"name\", &T::name)));");
}

TEST_CASE("codegen: CREATE TABLE - NOT NULL int has initializer") {
    auto result = generate("CREATE TABLE t (count INTEGER NOT NULL)");
    REQUIRE(result ==
        "struct T {\n"
        "    int64_t count = 0;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"count\", &T::count)));");
}

TEST_CASE("codegen: CREATE TABLE - mixed constraints") {
    auto result = generate(
        "CREATE TABLE users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT NOT NULL, "
        "email TEXT)");
    REQUIRE(result ==
        "struct Users {\n"
        "    int64_t id = 0;\n"
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
    REQUIRE(result ==
        "struct T {\n"
        "    int64_t id = 0;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"id\", &T::id, primary_key().on_conflict_replace())));");
}

TEST_CASE("codegen: CREATE TABLE - PRIMARY KEY ON CONFLICT + AUTOINCREMENT") {
    auto result = generate("CREATE TABLE t (id INTEGER PRIMARY KEY ON CONFLICT ABORT AUTOINCREMENT)");
    REQUIRE(result ==
        "struct T {\n"
        "    int64_t id = 0;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"id\", &T::id, primary_key().on_conflict_abort().autoincrement())));");
}

TEST_CASE("codegen: CREATE TABLE - DEFAULT integer") {
    auto result = generate("CREATE TABLE t (x INTEGER DEFAULT 42)");
    REQUIRE(result ==
        "struct T {\n"
        "    std::optional<int64_t> x;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"x\", &T::x, default_value(42))));");
}

TEST_CASE("codegen: CREATE TABLE - DEFAULT integer with leading zeros") {
    auto result = generate("CREATE TABLE t (x INTEGER DEFAULT 010)");
    REQUIRE(result ==
        "struct T {\n"
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
    REQUIRE(result ==
        "struct T {\n"
        "    std::optional<int64_t> x;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"x\", &T::x, default_value(99999999999999999999.0))));");
}

// SQLite wraps a hex default around inside the int64, so this one is -1, not 18446744073709551615.
TEST_CASE("codegen: CREATE TABLE - DEFAULT hexadecimal past the int64 range") {
    auto result = generate("CREATE TABLE t (x INTEGER DEFAULT 0xFFFFFFFFFFFFFFFF)");
    REQUIRE(result ==
        "struct T {\n"
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
    REQUIRE(result.code ==
        "struct Weird {\n"
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
    REQUIRE(result.code ==
        "struct T {\n"
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
    REQUIRE(result.code ==
        "struct T {\n"
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
    REQUIRE(result.code ==
        "struct T {\n"
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
// is not an error and the rest of a schema holding it still generates. Checked against sqlite3 3.51.
TEST_CASE("codegen: CREATE TABLE - STORED generated column holding a hex literal too big") {
    auto result = generateFull("CREATE TABLE g (x INTEGER, y AS (x + 0x10000000000000000) STORED)");
    REQUIRE(result.code.empty());
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
    REQUIRE(result.code.empty());
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
    REQUIRE(result ==
        "struct T {\n"
        "    std::optional<std::string> x;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"x\", &T::x, default_value(\"hello\"))));");
}

TEST_CASE("codegen: CREATE TABLE - DEFAULT expression with function") {
    auto result = generate("CREATE TABLE t (x TEXT DEFAULT (date('now')))");
    REQUIRE(result ==
        "struct T {\n"
        "    std::optional<std::string> x;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"x\", &T::x, default_value(date(\"now\")))));");
}

TEST_CASE("codegen: CREATE TABLE - DEFAULT with NOT NULL") {
    auto result = generate("CREATE TABLE t (x INTEGER NOT NULL DEFAULT 0)");
    REQUIRE(result ==
        "struct T {\n"
        "    int64_t x = 0;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"x\", &T::x, default_value(0))));");
}

TEST_CASE("codegen: CREATE TABLE - UNIQUE") {
    auto result = generate("CREATE TABLE t (email TEXT UNIQUE)");
    REQUIRE(result ==
        "struct T {\n"
        "    std::optional<std::string> email;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"email\", &T::email, unique())));");
}

TEST_CASE("codegen: CREATE TABLE - all constraints combined") {
    auto result = generate(
        "CREATE TABLE users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT NOT NULL DEFAULT 'unnamed', "
        "email TEXT UNIQUE)");
    REQUIRE(result ==
        "struct Users {\n"
        "    int64_t id = 0;\n"
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
    REQUIRE(result.warnings == std::vector<CodegenWarning>{
        "UNIQUE ON CONFLICT clause on column 'email' is not supported by sqlite_orm::unique()"
    });
}

TEST_CASE("codegen: CREATE TABLE - CHECK constraint") {
    auto result = generate("CREATE TABLE t (age INTEGER CHECK(age > 0))");
    REQUIRE(result ==
        "struct T {\n"
        "    std::optional<int64_t> age;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"age\", &T::age, check(c(&T::age) > 0))));");
}

TEST_CASE("codegen: CREATE TABLE - CHECK with function") {
    auto result = generate("CREATE TABLE t (name TEXT CHECK(length(name) > 0))");
    REQUIRE(result ==
        "struct T {\n"
        "    std::optional<std::string> name;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"name\", &T::name, check(length(&T::name) > 0))));");
}

TEST_CASE("codegen: CREATE TABLE - COLLATE NOCASE") {
    auto result = generate("CREATE TABLE t (name TEXT COLLATE NOCASE)");
    REQUIRE(result ==
        "struct T {\n"
        "    std::optional<std::string> name;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"name\", &T::name, collate_nocase())));");
}

TEST_CASE("codegen: CREATE TABLE - COLLATE RTRIM") {
    auto result = generate("CREATE TABLE t (name TEXT COLLATE RTRIM)");
    REQUIRE(result ==
        "struct T {\n"
        "    std::optional<std::string> name;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"name\", &T::name, collate_rtrim())));");
}

TEST_CASE("codegen: CREATE TABLE - custom COLLATE generates warning") {
    auto result = generateFull("CREATE TABLE t (name TEXT COLLATE UNICODE)");
    REQUIRE(result.warnings == std::vector<CodegenWarning>{
        "COLLATE UNICODE on column 'name' is not a built-in collation in sqlite_orm"
    });
}

TEST_CASE("codegen: CREATE TABLE - all column constraints") {
    auto result = generate(
        "CREATE TABLE users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT NOT NULL DEFAULT 'unnamed' CHECK(length(name) > 0) COLLATE NOCASE, "
        "email TEXT UNIQUE)");
    REQUIRE(result ==
        "struct Users {\n"
        "    int64_t id = 0;\n"
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
    REQUIRE(result ==
        "struct Posts {\n"
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
    REQUIRE(result ==
        "struct Posts {\n"
        "    std::optional<int64_t> user_id;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"posts\",\n"
        "        make_column(\"user_id\", &Posts::user_id),\n"
        "        foreign_key(&Posts::user_id).references(&Users::id).on_delete.cascade()));");
}

TEST_CASE("codegen: CREATE TABLE - REFERENCES both actions") {
    auto result = generate(
        "CREATE TABLE posts (user_id INTEGER REFERENCES users(id) ON DELETE CASCADE ON UPDATE SET NULL)");
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
    auto result = generate(
        "CREATE TABLE posts ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE, "
        "title TEXT NOT NULL)");
    REQUIRE(result ==
        "struct Posts {\n"
        "    int64_t id = 0;\n"
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
    auto result = generate(
        "CREATE TABLE posts ("
        "id INTEGER PRIMARY KEY, "
        "user_id INTEGER, "
        "FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE)");
    REQUIRE(result ==
        "struct Posts {\n"
        "    int64_t id = 0;\n"
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
    auto result = generate(
        "CREATE TABLE t (id INTEGER PRIMARY KEY, full_name TEXT GENERATED ALWAYS AS (id + 1) STORED)");
    REQUIRE(result ==
        "struct T {\n"
        "    int64_t id = 0;\n"
        "    std::optional<std::string> full_name;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"id\", &T::id, primary_key()),\n"
        "        make_column(\"full_name\", &T::full_name, generated_always_as(c(&T::id) + 1).stored())));");
}

TEST_CASE("codegen: CREATE TABLE - AS VIRTUAL shorthand") {
    auto result = generate(
        "CREATE TABLE t (id INTEGER, v INTEGER AS (id * 2) VIRTUAL)");
    REQUIRE(result ==
        "struct T {\n"
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
    auto result = generate(
        "CREATE TABLE t (id INTEGER, v INTEGER GENERATED ALWAYS AS (id + 10))");
    REQUIRE(result ==
        "struct T {\n"
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
    auto result = generate(
        "CREATE TABLE t (a INTEGER, b TEXT, PRIMARY KEY (a, b))");
    REQUIRE(result ==
        "struct T {\n"
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
    auto result = generate(
        "CREATE TABLE t (a INTEGER, b TEXT, UNIQUE (a, b))");
    REQUIRE(result ==
        "struct T {\n"
        "    std::optional<int64_t> a;\n"
        "    std::optional<std::string> b;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"a\", &T::a),\n"
        "        make_column(\"b\", &T::b),\n"
        "        unique(&T::a, &T::b)));");
}

TEST_CASE("codegen: CREATE TABLE - table-level CHECK") {
    auto result = generate(
        "CREATE TABLE t (a INTEGER, CHECK (a > 0))");
    REQUIRE(result ==
        "struct T {\n"
        "    std::optional<int64_t> a;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"a\", &T::a),\n"
        "        check(c(&T::a) > 0)));");
}

TEST_CASE("codegen: CREATE TABLE - mixed table-level constraints") {
    auto result = generate(
        "CREATE TABLE t (a INTEGER, b TEXT, c INTEGER, "
        "PRIMARY KEY (a, b), UNIQUE (b, c), CHECK (a > 0), "
        "FOREIGN KEY (c) REFERENCES other(id))");
    REQUIRE(result ==
        "struct T {\n"
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
        "        unique(&T::b, &T::c),\n"
        "        check(c(&T::a) > 0)));");
}

TEST_CASE("codegen: CREATE TABLE - WITHOUT ROWID") {
    auto result = generate(
        "CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT NOT NULL) WITHOUT ROWID");
    REQUIRE(result ==
        "struct T {\n"
        "    int64_t id = 0;\n"
        "    std::string name;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"id\", &T::id, primary_key()),\n"
        "        make_column(\"name\", &T::name)).without_rowid());");
}

TEST_CASE("codegen: STRICT table warning") {
    const auto result = generateFull("CREATE TABLE t (a TEXT) STRICT;");
    REQUIRE_FALSE(result.warnings.empty());
    bool found = false;
    for(const auto& w: result.warnings) {
        if(w.message.find("STRICT") != std::string::npos) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("codegen: FK DEFERRABLE parsed") {
    auto result = generateFull(
        "CREATE TABLE t (id INT REFERENCES p(id) DEFERRABLE INITIALLY DEFERRED);");
    REQUIRE(result.code ==
        "struct T {\n"
        "    std::optional<int64_t> id;\n"
        "};\n"
        "\n"
        "auto storage = make_storage(\"\",\n"
        "    make_table(\"t\",\n"
        "        make_column(\"id\", &T::id),\n"
        "        foreign_key(&T::id).references(&P::id)));");
    REQUIRE(result.warnings ==
        std::vector<CodegenWarning>{
            "DEFERRABLE INITIALLY DEFERRED on foreign key for column 'id' "
            "is not supported in sqlite_orm — ignored in codegen"});
}
