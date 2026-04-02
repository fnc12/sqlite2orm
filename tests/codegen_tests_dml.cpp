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
                    {Alternative{
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
        if(w.find("FROM") != std::string::npos) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}
