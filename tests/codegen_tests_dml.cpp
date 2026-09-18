#include "codegen_tests_common.hpp"

TEST_CASE("codegen: INSERT columns VALUES") {
    REQUIRE(generate("INSERT INTO users (id, name) VALUES (1, 'a')") ==
            "storage.insert(into<Users>(), columns(&Users::id, &Users::name), values(std::make_tuple(1, \"a\")));");
}

TEST_CASE("codegen: INSERT OR IGNORE") {
    REQUIRE(generate("INSERT OR IGNORE INTO t (x) VALUES (2)") ==
            "storage.insert(or_ignore(), into<T>(), columns(&T::x), values(std::make_tuple(2)));");
}

TEST_CASE("codegen: REPLACE INTO") {
    REQUIRE(generateFull("REPLACE INTO posts (user_id) VALUES (5)") ==
            CodeGenResult{
                "storage.replace(into<Posts>(), columns(&Posts::user_id), values(std::make_tuple(5)));",
                {DecisionPoint{
                    1,
                    "replace_style",
                    "replace_call",
                    "storage.replace(into<Posts>(), columns(&Posts::user_id), values(std::make_tuple(5)));",
                    {Option{"replace_call",
                            "storage.replace(into<Posts>(), columns(&Posts::user_id), "
                            "values(std::make_tuple(5)));",
                            "storage.replace(into<T>(), ...)"},
                     Option{
                        "insert_or_replace",
                        "storage.insert(or_replace(), into<Posts>(), columns(&Posts::user_id), "
                        "values(std::make_tuple(5)));",
                        "same semantics via raw insert(or_replace(), into<T>(), ...)"}}}},
                {}});
}

TEST_CASE("codegen: INSERT ON CONFLICT DO NOTHING") {
    REQUIRE(generate("INSERT INTO users (id) VALUES (1) ON CONFLICT DO NOTHING") ==
            "storage.insert(into<Users>(), columns(&Users::id), values(std::make_tuple(1)), "
            "on_conflict().do_nothing());");
}

TEST_CASE("codegen: INSERT ON CONFLICT (id) DO NOTHING") {
    REQUIRE(generate("INSERT INTO users (id) VALUES (1) ON CONFLICT (id) DO NOTHING") ==
            "storage.insert(into<Users>(), columns(&Users::id), values(std::make_tuple(1)), "
            "on_conflict(&Users::id).do_nothing());");
}

TEST_CASE("codegen: INSERT ON CONFLICT (a,b) DO UPDATE excluded") {
    REQUIRE(generate("INSERT INTO t (a, b) VALUES (1, 2) ON CONFLICT (a, b) DO UPDATE SET b = excluded.b") ==
            "storage.insert(into<T>(), columns(&T::a, &T::b), values(std::make_tuple(1, 2)), "
            "on_conflict(columns(&T::a, &T::b)).do_update(set(c(&T::b) = excluded(&T::b))));");
}

TEST_CASE("codegen: INSERT ON CONFLICT DO UPDATE SET WHERE") {
    REQUIRE(generate("INSERT INTO users (id, score) VALUES (1, 5) ON CONFLICT (id) DO UPDATE SET score = score + 1 "
                     "WHERE score < 10") ==
            "storage.insert(into<Users>(), columns(&Users::id, &Users::score), values(std::make_tuple(1, 5)), "
            "on_conflict(&Users::id).do_update(set(c(&Users::score) = c(&Users::score) + 1), where(c(&Users::score) < "
            "10)));");
}

TEST_CASE("codegen: INSERT ON CONFLICT target WHERE warns") {
    REQUIRE(
        generateFull("INSERT INTO users (id, score) VALUES (1, 2) ON CONFLICT (id) WHERE score > 0 DO NOTHING") ==
        CodeGenResult{
            "storage.insert(into<Users>(), columns(&Users::id, &Users::score), values(std::make_tuple(1, 2)), "
            "on_conflict(&Users::id).do_nothing());",
            {},
            {"ON CONFLICT target WHERE is not represented in sqlite_orm on_conflict(); generated code omits that "
             "predicate"}});
}

TEST_CASE("codegen: INSERT DEFAULT VALUES") {
    REQUIRE(generate("INSERT INTO users DEFAULT VALUES") ==
            "storage.insert(into<Users>(), default_values());");
}

TEST_CASE("codegen: INSERT SELECT") {
    REQUIRE(generate("INSERT INTO archive (id) SELECT id FROM users WHERE active = 0") ==
            "storage.insert(into<Archive>(), columns(&Archive::id), "
            "select(&Users::id, where(c(&Users::active) == 0)));");
}

TEST_CASE("codegen: UPDATE SET WHERE") {
    REQUIRE(generate("UPDATE users SET name = 'y' WHERE id = 1") ==
            "storage.update_all(set(c(&Users::name) = \"y\"), where(c(&Users::id) == 1));");
}

TEST_CASE("codegen: UPDATE multi SET") {
    REQUIRE(generate("UPDATE users SET a = 1, b = 2") ==
            "storage.update_all(set(c(&Users::a) = 1, c(&Users::b) = 2));");
}

TEST_CASE("codegen: DELETE FROM") {
    REQUIRE(generate("DELETE FROM users") == "storage.remove_all<Users>();");
}

TEST_CASE("codegen: DELETE WHERE") {
    REQUIRE(generate("DELETE FROM users WHERE id = 2") ==
            "storage.remove_all<Users>(where(c(&Users::id) == 2));");
}

TEST_CASE("codegen: UPDATE OR IGNORE warns") {
    REQUIRE(generateFull("UPDATE OR IGNORE users SET a = 1") ==
            CodeGenResult{
                "storage.update_all(set(c(&Users::a) = 1));",
                {},
                {"UPDATE OR modifier is not represented in sqlite_orm; generated code uses update_all(...) "
                 "without OR"}});
}

TEST_CASE("codegen: CREATE TRIGGER before delete OLD in WHERE") {
    REQUIRE(
        generate(
            "CREATE TRIGGER tr BEFORE DELETE ON users BEGIN DELETE FROM users WHERE id = OLD.id; END") ==
        "make_trigger(\"tr\", before().delete_().on<Users>().begin(remove_all<Users>(where(c(&Users::id) == "
        "old(&Users::id)))));");
}

TEST_CASE("codegen: CREATE TRIGGER OLD binds to subject table, not DML target") {
    REQUIRE(
        generate(
            "CREATE TRIGGER tx_delete AFTER DELETE ON transactions BEGIN "
            "DELETE FROM tx_rtree WHERE id = old.id; END") ==
        "make_trigger(\"tx_delete\", after().delete_().on<Transactions>().begin(remove_all<TxRtree>("
        "where(c(&TxRtree::id) == old(&Transactions::id)))));");
}

TEST_CASE("codegen: CREATE TRIGGER NEW binds to subject table, not DML target") {
    REQUIRE(
        generate(
            "CREATE TRIGGER tx_insert AFTER INSERT ON transactions BEGIN "
            "INSERT INTO audit (tx_id) VALUES (new.id); END") ==
        "make_trigger(\"tx_insert\", after().insert().on<Transactions>().begin(insert(into<Audit>(), "
        "columns(&Audit::tx_id), values(std::make_tuple(new_(&Transactions::id))))));");
}

TEST_CASE("codegen: CREATE TRIGGER after insert") {
    REQUIRE(generate("CREATE TRIGGER t2 AFTER INSERT ON users BEGIN DELETE FROM users; END") ==
            "make_trigger(\"t2\", after().insert().on<Users>().begin(remove_all<Users>()));");
}

TEST_CASE("codegen: CREATE TRIGGER update_of when for_each_row") {
    REQUIRE(
        generate("CREATE TRIGGER tr BEFORE UPDATE OF score, rank ON users FOR EACH ROW WHEN 1 BEGIN UPDATE users "
                 "SET score = 0; END") ==
        "make_trigger(\"tr\", before().update_of(&Users::score, &Users::rank).on<Users>().for_each_row().when(1)."
        "begin(update_all(set(c(&Users::score) = 0))));");
}

TEST_CASE("codegen: CREATE TRIGGER temp and if not exists warn") {
    REQUIRE(
        generateFull(
            "CREATE TEMP TRIGGER IF NOT EXISTS tx BEFORE INSERT ON users BEGIN DELETE FROM users; END") ==
        CodeGenResult{
            "make_trigger(\"tx\", before().insert().on<Users>().begin(remove_all<Users>()));",
            {},
            {"CREATE TRIGGER IF NOT EXISTS is not represented in sqlite_orm make_trigger(); generated code omits IF "
             "NOT EXISTS",
             "TEMP/TEMPORARY TRIGGER is not represented in sqlite_orm make_trigger(); generated code does not mark "
             "the trigger as temporary"}});
}

TEST_CASE("codegen: CREATE TRIGGER schema-qualified names warn") {
    REQUIRE(
        generateFull(
            "CREATE TRIGGER main.trig AFTER INSERT ON main.users BEGIN DELETE FROM users; END") ==
        CodeGenResult{
            "make_trigger(\"trig\", after().insert().on<Users>().begin(remove_all<Users>()));",
            {},
            {"schema-qualified trigger name is not represented in sqlite_orm; generated code uses unqualified trigger "
             "name only",
             "schema-qualified ON table in TRIGGER is not represented in sqlite_orm mapping"}});
}

TEST_CASE("codegen: CREATE INDEX single column") {
    REQUIRE(generate("CREATE INDEX idx ON users (id)") ==
            "make_index(\"idx\", indexed_column(&Users::id));");
}

TEST_CASE("codegen: CREATE UNIQUE INDEX two columns") {
    REQUIRE(generate("CREATE UNIQUE INDEX u ON t (a, b)") ==
            "make_unique_index(\"u\", indexed_column(&T::a), indexed_column(&T::b));");
}

TEST_CASE("codegen: CREATE INDEX COLLATE asc") {
    REQUIRE(generate("CREATE INDEX i1 ON users (name COLLATE NOCASE ASC)") ==
            "make_index(\"i1\", indexed_column(&Users::name).collate(\"nocase\").asc());");
}

TEST_CASE("codegen: CREATE INDEX partial WHERE") {
    REQUIRE(generate("CREATE INDEX p ON posts (user_id) WHERE 1") ==
            "make_index(\"p\", indexed_column(&Posts::user_id), where(1));");
}

TEST_CASE("codegen: CREATE INDEX expression column") {
    REQUIRE(generate("CREATE INDEX i_lower ON users (lower(name))") ==
            "make_index(\"i_lower\", indexed_column(lower(&Users::name)));");
}

TEST_CASE("codegen: CREATE INDEX without IF NOT EXISTS warns") {
    const CodeGenResult expected{
        "make_index(\"j\", indexed_column(&Users::name));",
        {columnRefStyleDp(1, "&Users::name")},
        {"sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs from "
         "serialized output"}};
    const CodeGenResult codeGenResult = generateFull("CREATE INDEX j ON users (name)");
    REQUIRE(codeGenResult == expected);
}

TEST_CASE("codegen: CREATE INDEX IF NOT EXISTS no warning") {
    const CodeGenResult expected{"make_index(\"k\", indexed_column(&Users::id));",
                                 {columnRefStyleDp(1, "&Users::id")},
                                 {}};
    const CodeGenResult codeGenResult = generateFull("CREATE INDEX IF NOT EXISTS k ON users (id)");
    REQUIRE(codeGenResult == expected);
}

TEST_CASE("codegen: UPDATE FROM warning") {
    const auto result = generateFull("UPDATE t SET a = 1 FROM b WHERE t.id = b.id;");
    REQUIRE_FALSE(result.warnings.empty());
    bool found = false;
    for(const auto& w: result.warnings) {
        if(w.message.find("FROM") != std::string::npos) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

// A trigger body is stored and compiled only when the trigger fires, so SQLite accepts a hex
// literal in it that it refuses in a statement of its own, and every INSERT on the subject table
// then fails. C++ has no literal for the value, so the trigger cannot be generated — without
// failing the statement, so a schema holding it still generates. Checked against sqlite3 3.51.
TEST_CASE("codegen: CREATE TRIGGER - a hex literal too big leaves the trigger ungenerated") {
    auto result = generateFull(
        "CREATE TRIGGER tr AFTER INSERT ON t BEGIN UPDATE t SET x = 0x10000000000000000; END");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
        std::vector<CodegenWarning>{
            {"CREATE TRIGGER tr uses 0x10000000000000000, too big for a signed 64-bit integer: SQLite stores the "
             "trigger but refuses every statement that fires it, and C++ has no literal for it, so the trigger is "
             "not generated"}});
    REQUIRE(result.errors.empty());
}

// An INSERT compiles its values, so there SQLite refuses the same literal outright.
TEST_CASE("codegen: INSERT with a hex literal too big is refused") {
    REQUIRE(generateFull("INSERT INTO t VALUES (0x10000000000000000);") ==
            CodeGenResult{{}, {}, {}, {"hex literal too big: 0x10000000000000000"}, {}});
}

// SQLite types a value by itself and applies column affinity only afterwards, so a decimal literal
// an int64 cannot hold stays a REAL even in an INTEGER column:
// `CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (99999999999999999999);` stores `real|1.0e+20`
// in sqlite3 3.51. The object form of the insert would send the value through the `int64_t` field,
// where converting it is undefined and hands the row INT64_MIN, so the statement spells the column
// list out instead: there the value is bound as the double it is and SQLite applies the affinity
// itself, exactly as it does for the SQL.
TEST_CASE("codegen: INSERT VALUES - a literal past the int64 range into an INTEGER column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (99999999999999999999);");
    REQUIRE(result.code ==
            "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(99999999999999999999.0)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"INSERT into column 'x' of table 't' uses 99999999999999999999, past the signed 64-bit integer "
                 "range: SQLite types a value before it applies the column affinity and keeps such a one a REAL, "
                 "and the int64_t field cannot hold it, so the row is generated through columns()/values(), which "
                 "writes the value SQLite stores, rather than as a struct, which would write a different one",
                 SourceLocation{1, 50},
                 20}});
    REQUIRE(result.errors.empty());
}

// The sign belongs to the value SQLite types, so a negated literal is named with it.
TEST_CASE("codegen: INSERT VALUES - a negated literal past the int64 range into an INTEGER column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (-99999999999999999999);");
    REQUIRE(result.code ==
            "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(-99999999999999999999.0)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"INSERT into column 'x' of table 't' uses -99999999999999999999, past the signed 64-bit integer "
                 "range: SQLite types a value before it applies the column affinity and keeps such a one a REAL, "
                 "and the int64_t field cannot hold it, so the row is generated through columns()/values(), which "
                 "writes the value SQLite stores, rather than as a struct, which would write a different one",
                 SourceLocation{1, 50},
                 21}});
    REQUIRE(result.errors.empty());
}

// The underline covers the signs the message quotes along with the digits, whatever stands
// between them and the literal.
TEST_CASE("codegen: INSERT VALUES - the warning underlines the sign of a past-range literal") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (- 99999999999999999999);");
    REQUIRE(result.code ==
            "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(-99999999999999999999.0)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"INSERT into column 'x' of table 't' uses -99999999999999999999, past the signed 64-bit integer "
                 "range: SQLite types a value before it applies the column affinity and keeps such a one a REAL, "
                 "and the int64_t field cannot hold it, so the row is generated through columns()/values(), which "
                 "writes the value SQLite stores, rather than as a struct, which would write a different one",
                 SourceLocation{1, 50},
                 22}});
    REQUIRE(result.errors.empty());
}

// Two signs fold into a positive value, which is the value SQLite ends up with as well: negating
// INT64_MIN leaves the int64 range, and sqlite3 3.51 stores `real|9.22337203685477581e+18` for
// this row, as the generated code does.
TEST_CASE("codegen: INSERT VALUES - a doubly negated INT64_MIN leaves the int64 range") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (-(-9223372036854775808));");
    REQUIRE(result.code ==
            "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(-(-9223372036854775808.0))));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"INSERT into column 'x' of table 't' uses 9223372036854775808, past the signed 64-bit integer "
                 "range: SQLite types a value before it applies the column affinity and keeps such a one a REAL, "
                 "and the int64_t field cannot hold it, so the row is generated through columns()/values(), which "
                 "writes the value SQLite stores, rather than as a struct, which would write a different one",
                 SourceLocation{1, 50},
                 22}});
    REQUIRE(result.errors.empty());
}

// The limit moves by one for a negated literal, the way SQLite's own `codeInteger()` moves it:
// `INSERT INTO t VALUES (-9223372036854775808)` stores `integer|-9223372036854775808`, which the
// `int64_t` field holds, so the object form stays.
TEST_CASE("codegen: INSERT VALUES - INT64_MIN into an INTEGER column keeps the object form") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (-9223372036854775808);");
    REQUIRE(result.code == "storage.insert(T{-9223372036854775808.0});");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

TEST_CASE("codegen: INSERT VALUES - INT64_MAX into an INTEGER column keeps the object form") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (9223372036854775807);");
    REQUIRE(result.code == "storage.insert(T{9223372036854775807});");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// One past INT64_MAX is a REAL for SQLite (`real|9.22337203685478e+18`), and 2^63 is not a value an
// `int64_t` holds at all.
TEST_CASE("codegen: INSERT VALUES - one past INT64_MAX into an INTEGER column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (9223372036854775808);");
    REQUIRE(result.code ==
            "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(9223372036854775808.0)));");
    REQUIRE(result.warnings.size() == 1u);
    REQUIRE(result.errors.empty());
}

// A REAL literal is the same story without the range: SQLite gives an INTEGER column `real|1.5`,
// while the `int64_t` field would truncate it to 1. Binding the double leaves the affinity to
// SQLite, so `2.0` still lands as `integer|2`. Nothing about the range is warned about here.
TEST_CASE("codegen: INSERT VALUES - a fractional literal into an INTEGER column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (1.5);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(1.5)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A REAL column maps to a `double` field, which holds every value SQLite gives the literal, so the
// object form carries it.
TEST_CASE("codegen: INSERT VALUES - a literal past the int64 range into a REAL column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x REAL); INSERT INTO t VALUES (99999999999999999999);");
    REQUIRE(result.code == "storage.insert(T{99999999999999999999.0});");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// One value out of reach spells the whole column list out, values of every other row included.
TEST_CASE("codegen: INSERT VALUES - several rows, one value past the int64 range") {
    auto result = generateLastOfBatch(
        "CREATE TABLE t(x INTEGER, y TEXT); INSERT INTO t VALUES (99999999999999999999, 'a'), (1, 'b');");
    REQUIRE(result.code ==
            "storage.insert(into<T>(), columns(&T::x, &T::y), "
            "values(std::make_tuple(99999999999999999999.0, \"a\"), std::make_tuple(1, \"b\")));");
    REQUIRE(result.warnings.size() == 1u);
    REQUIRE(result.errors.empty());
}

// Ordinary values keep the object form, which is what a reader of a generated snippet expects.
TEST_CASE("codegen: INSERT VALUES - an ordinary value keeps the object form") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (1);");
    REQUIRE(result.code == "storage.insert(T{1});");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// SQLite computes a generated column, so a VALUES row never holds one and the spelled-out column
// list leaves it out. `CREATE TABLE t(x INTEGER, y AS (x+1)); INSERT INTO t VALUES
// (99999999999999999999);` stores `real|1.0e+20` in both columns in sqlite3 3.51, and so does the
// generated code. A bare `AS (...)`, the spelling SQLite documents as the default, carries no
// storage keyword, so what makes the column generated is its expression rather than the keyword.
TEST_CASE("codegen: INSERT VALUES - a literal past the int64 range beside a generated column") {
    const std::string generatedColumn = GENERATE("y AS (x+1)",
                                                 "y AS (x+1) VIRTUAL",
                                                 "y AS (x+1) STORED",
                                                 "y GENERATED ALWAYS AS (x+1)",
                                                 "y GENERATED ALWAYS AS (x+1) STORED");
    INFO(generatedColumn);
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER, " + generatedColumn +
                                      ");\nINSERT INTO t VALUES (99999999999999999999);");
    REQUIRE(result.code ==
            "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(99999999999999999999.0)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"INSERT into column 'x' of table 't' uses 99999999999999999999, past the signed 64-bit integer "
                 "range: SQLite types a value before it applies the column affinity and keeps such a one a REAL, "
                 "and the int64_t field cannot hold it, so the row is generated through columns()/values(), which "
                 "writes the value SQLite stores, rather than as a struct, which would write a different one",
                 SourceLocation{2, 23},
                 20}});
    REQUIRE(result.errors.empty());
}

// A BOOLEAN column maps to a `bool` field, which holds no whole number besides 0 and 1, so a value
// SQLite keeps a REAL is out of its reach the same way: sqlite3 3.51 stores `real|1.0e+20` here,
// while the object form would send the value through the field and store `integer|1`.
TEST_CASE("codegen: INSERT VALUES - a literal past the int64 range into a BOOLEAN column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (99999999999999999999);");
    REQUIRE(result.code ==
            "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(99999999999999999999.0)));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"INSERT into column 'x' of table 't' uses 99999999999999999999, past the signed 64-bit integer "
                 "range: SQLite types a value before it applies the column affinity and keeps such a one a REAL, "
                 "and the bool field cannot hold it, so the row is generated through columns()/values(), which "
                 "writes the value SQLite stores, rather than as a struct, which would write a different one",
                 SourceLocation{1, 50},
                 20}});
    REQUIRE(result.errors.empty());
}

// sqlite3 3.51 stores `real|1.5` in a BOOLEAN column, and `integer|2` for `2.0`; the `bool` field
// would make both of them 1.
TEST_CASE("codegen: INSERT VALUES - a fractional literal into a BOOLEAN column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (1.5);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(1.5)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// An integer a `bool` field cannot hold either is a separate story: the object form writes 1 where
// SQLite stores 5, and the fix for that belongs with the literal the field carries, not here.
TEST_CASE("codegen: INSERT VALUES - a whole number into a BOOLEAN column keeps the object form") {
    auto result = generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (5);");
    REQUIRE(result.code == "storage.insert(T{5});");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A table this batch never declared says nothing about the type of the field a value reaches, so
// the statement is generated the way it always was.
TEST_CASE("codegen: INSERT VALUES - a literal past the int64 range into an unknown table") {
    auto result = generateFull("INSERT INTO t VALUES (99999999999999999999);");
    REQUIRE(result.code == "storage.insert(T{99999999999999999999.0});");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}
