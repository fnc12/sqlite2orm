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
                {DecisionPoint{1,
                               "replace_style",
                               "replace_call",
                               "storage.replace(into<Posts>(), columns(&Posts::user_id), values(std::make_tuple(5)));",
                               {Option{"replace_call",
                                       "storage.replace(into<Posts>(), columns(&Posts::user_id), "
                                       "values(std::make_tuple(5)));",
                                       "storage.replace(into<T>(), ...)"},
                                Option{"insert_or_replace",
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
    REQUIRE(generateFull("INSERT INTO users (id, score) VALUES (1, 2) ON CONFLICT (id) WHERE score > 0 DO NOTHING") ==
            CodeGenResult{
                "storage.insert(into<Users>(), columns(&Users::id, &Users::score), values(std::make_tuple(1, 2)), "
                "on_conflict(&Users::id).do_nothing());",
                {},
                {"ON CONFLICT target WHERE is not represented in sqlite_orm on_conflict(); generated code omits that "
                 "predicate"}});
}

TEST_CASE("codegen: INSERT DEFAULT VALUES") {
    REQUIRE(generate("INSERT INTO users DEFAULT VALUES") == "storage.insert(into<Users>(), default_values());");
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
    REQUIRE(generate("DELETE FROM users WHERE id = 2") == "storage.remove_all<Users>(where(c(&Users::id) == 2));");
}

TEST_CASE("codegen: UPDATE OR IGNORE warns") {
    REQUIRE(generateFull("UPDATE OR IGNORE users SET a = 1") ==
            CodeGenResult{"storage.update_all(set(c(&Users::a) = 1));",
                          {},
                          {"UPDATE OR modifier is not represented in sqlite_orm; generated code uses update_all(...) "
                           "without OR"}});
}

TEST_CASE("codegen: CREATE TRIGGER before delete OLD in WHERE") {
    REQUIRE(generate("CREATE TRIGGER tr BEFORE DELETE ON users BEGIN DELETE FROM users WHERE id = OLD.id; END") ==
            "make_trigger(\"tr\", before().delete_().on<Users>().begin(remove_all<Users>(where(c(&Users::id) == "
            "old(&Users::id)))));");
}

TEST_CASE("codegen: CREATE TRIGGER OLD binds to subject table, not DML target") {
    REQUIRE(generate("CREATE TRIGGER tx_delete AFTER DELETE ON transactions BEGIN "
                     "DELETE FROM tx_rtree WHERE id = old.id; END") ==
            "make_trigger(\"tx_delete\", after().delete_().on<Transactions>().begin(remove_all<TxRtree>("
            "where(c(&TxRtree::id) == old(&Transactions::id)))));");
}

TEST_CASE("codegen: CREATE TRIGGER NEW binds to subject table, not DML target") {
    REQUIRE(generate("CREATE TRIGGER tx_insert AFTER INSERT ON transactions BEGIN "
                     "INSERT INTO audit (tx_id) VALUES (new.id); END") ==
            "make_trigger(\"tx_insert\", after().insert().on<Transactions>().begin(insert(into<Audit>(), "
            "columns(&Audit::tx_id), values(std::make_tuple(new_(&Transactions::id))))));");
}

TEST_CASE("codegen: CREATE TRIGGER after insert") {
    REQUIRE(generate("CREATE TRIGGER t2 AFTER INSERT ON users BEGIN DELETE FROM users; END") ==
            "make_trigger(\"t2\", after().insert().on<Users>().begin(remove_all<Users>()));");
}

TEST_CASE("codegen: CREATE TRIGGER update_of when for_each_row") {
    REQUIRE(generate("CREATE TRIGGER tr BEFORE UPDATE OF score, rank ON users FOR EACH ROW WHEN 1 BEGIN UPDATE users "
                     "SET score = 0; END") ==
            "make_trigger(\"tr\", before().update_of(&Users::score, &Users::rank).on<Users>().for_each_row().when(1)."
            "begin(update_all(set(c(&Users::score) = 0))));");
}

TEST_CASE("codegen: CREATE TRIGGER temp and if not exists warn") {
    REQUIRE(
        generateFull("CREATE TEMP TRIGGER IF NOT EXISTS tx BEFORE INSERT ON users BEGIN DELETE FROM users; END") ==
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
        generateFull("CREATE TRIGGER main.trig AFTER INSERT ON main.users BEGIN DELETE FROM users; END") ==
        CodeGenResult{
            "make_trigger(\"trig\", after().insert().on<Users>().begin(remove_all<Users>()));",
            {},
            {"schema-qualified trigger name is not represented in sqlite_orm; generated code uses unqualified trigger "
             "name only",
             "schema-qualified ON table in TRIGGER is not represented in sqlite_orm mapping"}});
}

// sqlite_orm keeps a trigger's WHEN expression in an `optional_container`, whose field is
// default-constructed before the expression is assigned to it, so the WHEN clause only compiles
// while every sqlite_orm type in it has a default constructor. `negated_condition_t`, `is_null_t`,
// `binary_operator` and `builtin_function_t` declare a constructor and no default one, so
// `when(not c(new_(&T::x)))` fails with `use of deleted function
// optional_container<negated_condition_t<...>>::optional_container()`. SQLite takes every WHEN
// clause below (checked against sqlite3 3.51.0), so the statement still generates, with a warning
// naming the form that cannot be held.
TEST_CASE("codegen: CREATE TRIGGER - a WHEN clause sqlite_orm cannot default-construct warns") {
    SECTION("NOT") {
        const auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NOT NEW.x BEGIN DELETE FROM t; END");
        REQUIRE(result.code ==
                "make_trigger(\"tr\", after().insert().on<T>().when(not c(new_(&T::x))).begin(remove_all<T>()));");
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"CREATE TRIGGER tr uses NOT in its WHEN clause, a form sqlite_orm gives no default "
                     "constructor: make_trigger() keeps a trigger's WHEN expression in an optional_container, "
                     "which default-constructs the expression before assigning it, so the generated trigger does "
                     "not compile"}});
    }
    SECTION("IS NULL") {
        const auto result =
            generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x IS NULL BEGIN DELETE FROM t; END");
        REQUIRE(result.code ==
                "make_trigger(\"tr\", after().insert().on<T>().when(is_null(new_(&T::x))).begin(remove_all<T>()));");
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"CREATE TRIGGER tr uses IS NULL in its WHEN clause, a form sqlite_orm gives no default "
                     "constructor: make_trigger() keeps a trigger's WHEN expression in an optional_container, "
                     "which default-constructs the expression before assigning it, so the generated trigger does "
                     "not compile"}});
    }
    SECTION("a function and an operator are named one by one, innermost first") {
        const auto result =
            generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN length(NEW.y) + 1 > 0 BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(length(new_(&T::y)) + 1 > "
                               "0).begin(remove_all<T>()));");
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"CREATE TRIGGER tr uses length() in its WHEN clause, a form sqlite_orm gives no default "
                     "constructor: make_trigger() keeps a trigger's WHEN expression in an optional_container, "
                     "which default-constructs the expression before assigning it, so the generated trigger does "
                     "not compile"},
                    {"CREATE TRIGGER tr uses + in its WHEN clause, a form sqlite_orm gives no default "
                     "constructor: make_trigger() keeps a trigger's WHEN expression in an optional_container, "
                     "which default-constructs the expression before assigning it, so the generated trigger does "
                     "not compile"}});
    }
    SECTION("the ORDER BY of an OVER clause, the one part of a window definition that has a constructor") {
        const auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x = (SELECT count(*) "
                                         "OVER (ORDER BY y) FROM t) BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(c(new_(&T::x)) == "
                               "select(count<T>().over(order_by(&T::y)))).begin(remove_all<T>()));");
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"CREATE TRIGGER tr uses ORDER BY in its WHEN clause, a form sqlite_orm gives no default "
                     "constructor: make_trigger() keeps a trigger's WHEN expression in an optional_container, "
                     "which default-constructs the expression before assigning it, so the generated trigger does "
                     "not compile"}});
    }
    SECTION("a FILTER keeps recording what its expression puts out") {
        const auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x = (SELECT count(*) "
                                         "FILTER (WHERE y IS NULL) FROM t) BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(c(new_(&T::x)) == "
                               "select(count<T>().filter(where(is_null(&T::y))))).begin(remove_all<T>()));");
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"CREATE TRIGGER tr uses IS NULL in its WHEN clause, a form sqlite_orm gives no default "
                     "constructor: make_trigger() keeps a trigger's WHEN expression in an optional_container, "
                     "which default-constructs the expression before assigning it, so the generated trigger does "
                     "not compile"}});
    }
    SECTION("a concatenation, the half of the || token that is not an OR") {
        const auto result =
            generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.y || 'a' BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(c(new_(&T::y)) || "
                               "\"a\").begin(remove_all<T>()));");
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"CREATE TRIGGER tr uses || in its WHEN clause, a form sqlite_orm gives no default "
                     "constructor: make_trigger() keeps a trigger's WHEN expression in an optional_container, "
                     "which default-constructs the expression before assigning it, so the generated trigger does "
                     "not compile"}});
    }
    SECTION("an aggregate function, which is not one of the window forms") {
        const auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x = (SELECT count(y) "
                                         "FROM t) BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(c(new_(&T::x)) == "
                               "select(count(&T::y))).begin(remove_all<T>()));");
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"CREATE TRIGGER tr uses count() in its WHEN clause, a form sqlite_orm gives no default "
                     "constructor: make_trigger() keeps a trigger's WHEN expression in an optional_container, "
                     "which default-constructs the expression before assigning it, so the generated trigger does "
                     "not compile"}});
    }
    SECTION("the ORDER BY of a window function's OVER clause, the one part of it with a constructor") {
        const auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x = (SELECT row_number() "
                                         "OVER (ORDER BY y) FROM t) BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(c(new_(&T::x)) == "
                               "select(row_number().over(order_by(&T::y)))).begin(remove_all<T>()));");
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"CREATE TRIGGER tr uses ORDER BY in its WHEN clause, a form sqlite_orm gives no default "
                     "constructor: make_trigger() keeps a trigger's WHEN expression in an optional_container, "
                     "which default-constructs the expression before assigning it, so the generated trigger does "
                     "not compile"}});
    }
    SECTION("a subquery is read clause by clause") {
        const auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x = (SELECT y FROM t "
                                         "WHERE y = 'a') BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(c(new_(&T::x)) == "
                               "select(&T::y, where(c(&T::y) == \"a\"))).begin(remove_all<T>()));");
        REQUIRE(result.warnings ==
                std::vector<CodegenWarning>{
                    {"CREATE TRIGGER tr uses WHERE in its WHEN clause, a form sqlite_orm gives no default "
                     "constructor: make_trigger() keeps a trigger's WHEN expression in an optional_container, "
                     "which default-constructs the expression before assigning it, so the generated trigger does "
                     "not compile"}});
    }
}

// The comparisons, AND and OR all produce a `binary_condition`, which declares a default
// constructor, and a CAST, a CASE and a subquery over a bare FROM hold only what they are given —
// so these WHEN clauses carry no warning and do compile (see the compile test in
// schema_pipeline_tests.cpp). An OR only holds since it is spelled `or_(...)`: the `||` token it
// used to be generated with reads as a concatenation, whose `conc_t` has no default constructor.
TEST_CASE("codegen: CREATE TRIGGER - a WHEN clause sqlite_orm can default-construct is not warned about") {
    SECTION("a comparison") {
        const auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x = 0 BEGIN DELETE FROM t; END");
        REQUIRE(result.code ==
                "make_trigger(\"tr\", after().insert().on<T>().when(c(new_(&T::x)) == 0).begin(remove_all<T>()));");
        REQUIRE(result.warnings.empty());
    }
    SECTION("a CAST over a column") {
        const auto result = generateFull(
            "CREATE TRIGGER tr AFTER INSERT ON t WHEN CAST(NEW.x AS INTEGER) > 0 BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(cast<int64_t>(new_(&T::x)) > "
                               "0).begin(remove_all<T>()));");
        REQUIRE(result.warnings.empty());
    }
    SECTION("an OR, which is spelled or_() because the || token would read as a concatenation") {
        const auto result =
            generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x OR NEW.y BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(or_(new_(&T::x), "
                               "new_(&T::y))).begin(remove_all<T>()));");
        REQUIRE(result.warnings.empty());
    }
    SECTION("MATCH, whose match_t is an aggregate holding its two operands") {
        const auto result =
            generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.y MATCH 'x' BEGIN DELETE FROM t; END");
        REQUIRE(
            result.code ==
            "make_trigger(\"tr\", after().insert().on<T>().when(match(new_(&T::y), \"x\")).begin(remove_all<T>()));");
        REQUIRE(result.warnings.empty());
    }
    SECTION("count(*) with a FILTER, which keeps only the expression of its where") {
        const auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x = (SELECT count(*) "
                                         "FILTER (WHERE y = 'a') FROM t) BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(c(new_(&T::x)) == "
                               "select(count<T>().filter(where(c(&T::y) == \"a\")))).begin(remove_all<T>()));");
        REQUIRE(result.warnings.empty());
    }
    SECTION("count(*) with an OVER clause of a PARTITION BY and a frame, both aggregates") {
        const auto result =
            generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x = (SELECT count(*) OVER (PARTITION BY y "
                         "ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) FROM t) BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(c(new_(&T::x)) == "
                               "select(count<T>().over(partition_by(&T::y), sqlite_orm::rows(unbounded_preceding(), "
                               "current_row())))).begin(remove_all<T>()));");
        REQUIRE(result.warnings.empty());
    }
    SECTION("a window function, whose row_number_t holds nothing") {
        const auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x = (SELECT row_number() "
                                         "OVER () FROM t) BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(c(new_(&T::x)) == "
                               "select(row_number().over(), from<T>())).begin(remove_all<T>()));");
        REQUIRE(result.warnings.empty());
    }
    SECTION("a window function over an argument, whose lag_t keeps it in a tuple") {
        const auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x = (SELECT lag(x, 1, 0) "
                                         "OVER () FROM t) BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(c(new_(&T::x)) == "
                               "select(lag(&T::x, 1, 0).over())).begin(remove_all<T>()));");
        REQUIRE(result.warnings.empty());
    }
    SECTION("MATCH in its function spelling, the same match_t as the operator one") {
        const auto result =
            generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN match(NEW.y, 'x') BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(match(new_(&T::y), "
                               "\"x\")).begin(remove_all<T>()));");
        REQUIRE(result.warnings.empty());
    }
    SECTION("a subquery over a bare FROM") {
        const auto result =
            generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x = (SELECT y FROM t) BEGIN DELETE FROM t; END");
        REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(c(new_(&T::x)) == "
                               "select(&T::y)).begin(remove_all<T>()));");
        REQUIRE(result.warnings.empty());
    }
}

TEST_CASE("codegen: CREATE INDEX single column") {
    REQUIRE(generate("CREATE INDEX idx ON users (id)") == "make_index(\"idx\", indexed_column(&Users::id));");
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

// An index and a trigger are created from the text sqlite_orm serializes them into, exactly as a
// table is, so an infinity written in one of them cannot survive the trip either. The two differ in
// when SQLite says so: it compiles an index as it creates it, so the CREATE INDEX is refused, while
// a trigger is only stored, so it is created and the statement that fires it is refused. Checked
// against sqlite3 3.51.0 and the libsqlite3 3.45.1 the tests link.
TEST_CASE("codegen: CREATE INDEX - an infinity in a partial WHERE warns that the index does not sync") {
    const auto result = generateFull("CREATE INDEX ix ON t (x) WHERE x < 9e999");
    REQUIRE(result.code ==
            "make_index(\"ix\", indexed_column(&T::x), where(c(&T::x) < std::numeric_limits<double>::infinity()));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs "
                 "from serialized output"},
                {"index ix uses 9e999, an infinity: sqlite_orm writes an infinity into DDL as `inf`, which SQLite "
                 "reads as a column name rather than as a number, so sync_schema() throws instead of creating the "
                 "index (\"no such column: inf\")",
                 SourceLocation{1, 36},
                 5}});
}

TEST_CASE("codegen: CREATE TRIGGER - an infinity in a WHEN clause warns that firing it fails") {
    const auto result =
        generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN new.x > 9e999 BEGIN INSERT INTO t (x) VALUES (1); END");
    REQUIRE(result.code == "make_trigger(\"tr\", after().insert().on<T>().when(c(new_(&T::x)) > "
                           "std::numeric_limits<double>::infinity()).begin(insert(into<T>(), columns(&T::x), "
                           "values(std::make_tuple(1)))));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"trigger tr uses 9e999, an infinity: sqlite_orm writes an infinity into DDL as `inf`, which SQLite "
                 "reads as a column name rather than as a number, so sync_schema() creates a trigger that refuses "
                 "every statement firing it (\"no such column: inf\")",
                 SourceLocation{1, 50},
                 5}});
}

// `make_index` deduces the table an index is made for from its first argument, and an expression
// names no table, so an index that starts with one spells that table out. Without it the generated
// code did not compile at all: `no matching function for call to make_index(const char[8],
// indexed_column_t<...>)`. sqlite_orm serializes the expression itself — `CREATE INDEX "i_lower" ON
// "users" (LOWER("name"))` — so nothing else about the index changes.
TEST_CASE("codegen: CREATE INDEX expression column") {
    REQUIRE(generate("CREATE INDEX i_lower ON users (lower(name))") ==
            "make_index<Users>(\"i_lower\", indexed_column(lower(&Users::name)));");
}

TEST_CASE("codegen: CREATE INDEX over an arithmetic expression") {
    REQUIRE(generate("CREATE INDEX i ON t ((a + 1))") == "make_index<T>(\"i\", indexed_column(c(&T::a) + 1));");
}

// sqlite_orm serializes an indexed column's collation as a bare COLLATE after the expression, with
// no parentheses around it, and SQLite binds COLLATE tighter than every operator an expression is
// built of. `indexed_column(c(&T::a) + 1).collate("nocase")` therefore stores
// `CREATE INDEX "i" ON "t" ("a" + 1 COLLATE nocase)`, which SQLite reads as `"a" + (1 COLLATE
// nocase)`: the collation lands on the literal, and `pragma_index_xinfo` answers BINARY for the
// indexed expression where the schema the code was generated from answers NOCASE (checked against
// sqlite3 3.51.0). The collation is left out and warned about rather than generated onto part of
// the expression. The order the same indexed column carries is not affected and stays.
TEST_CASE("codegen: CREATE INDEX over an expression leaves out a COLLATE the serialized SQL rebinds") {
    const CodeGenResult expected{
        "make_index<T>(\"i\", indexed_column(c(&T::a) + 1).desc());",
        expectedBinaryLeaf("&T::a", "1", " + ", "add").decisionPoints,
        {"sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs from "
         "serialized output",
         CodegenWarning{"COLLATE NOCASE over an expression in index i is not generated: sqlite_orm serializes an "
                        "indexed column's collation as a bare COLLATE after the expression, and SQLite binds "
                        "COLLATE tighter than the operators in it, so the collation would apply to part of the "
                        "expression rather than to the indexed value",
                        SourceLocation{1, 22},
                        7}}};
    REQUIRE(generateFull("CREATE INDEX i ON t ((a + 1) COLLATE NOCASE DESC)") == expected);
}

// The same over a concatenation, the operator SQLite binds tightest of all and still looser than
// COLLATE, in an index that starts with a column — the form that compiles on its own.
TEST_CASE("codegen: CREATE INDEX over a concatenation leaves out its COLLATE") {
    const CodeGenResult codeGenResult = generateFull("CREATE INDEX i ON t (a, (b || 'x') COLLATE NOCASE)");
    REQUIRE(codeGenResult.code == "make_index(\"i\", indexed_column(&T::a), indexed_column(c(&T::b) || \"x\"));");
    REQUIRE(codeGenResult.warnings ==
            std::vector<CodegenWarning>{
                {"sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs "
                 "from serialized output"},
                {"COLLATE NOCASE over an expression in index i is not generated: sqlite_orm serializes an indexed "
                 "column's collation as a bare COLLATE after the expression, and SQLite binds COLLATE tighter than "
                 "the operators in it, so the collation would apply to part of the expression rather than to the "
                 "indexed value",
                 SourceLocation{1, 25},
                 10}});
}

// A negation is generated as the `0 - x` subtraction SQLite computes for it, and the parentheses
// that subtraction reads with elsewhere are the enclosing binary serializer's: as the argument of
// `indexed_column` it comes out bare, `0 - "a"`, which a trailing COLLATE splits like any other
// binary operator.
TEST_CASE("codegen: CREATE INDEX over a negation leaves out its COLLATE") {
    const CodeGenResult codeGenResult = generateFull("CREATE INDEX IF NOT EXISTS i ON t ((-a) COLLATE NOCASE)");
    REQUIRE(codeGenResult.code == "make_index<T>(\"i\", indexed_column((c(0) - c(&T::a))));");
    REQUIRE(codeGenResult.warnings ==
            std::vector<CodegenWarning>{
                {"COLLATE NOCASE over an expression in index i is not generated: sqlite_orm serializes an indexed "
                 "column's collation as a bare COLLATE after the expression, and SQLite binds COLLATE tighter than "
                 "the operators in it, so the collation would apply to part of the expression rather than to the "
                 "indexed value",
                 SourceLocation{1, 36},
                 4}});
}

// A collation that is not built in is left out the same way: what the expression does to a trailing
// COLLATE is decided before it is decided how the collation would be spelled, so this index does
// not warn about a literal collation name it no longer generates.
TEST_CASE("codegen: CREATE INDEX over an expression leaves out a collation that is not built in") {
    const CodeGenResult codeGenResult = generateFull("CREATE INDEX IF NOT EXISTS i ON t ((a IS NULL) COLLATE MYCOLL)");
    REQUIRE(codeGenResult.code == "make_index<T>(\"i\", indexed_column(is_null(&T::a)));");
    REQUIRE(codeGenResult.warnings ==
            std::vector<CodegenWarning>{
                {"COLLATE MYCOLL over an expression in index i is not generated: sqlite_orm serializes an indexed "
                 "column's collation as a bare COLLATE after the expression, and SQLite binds COLLATE tighter than "
                 "the operators in it, so the collation would apply to part of the expression rather than to the "
                 "indexed value",
                 SourceLocation{1, 36},
                 11}});
}

// An expression that ends in no operand of its own takes the trailing COLLATE whole, so its
// collation is generated: `LOWER("a") COLLATE nocase` indexes the call the SQL collates, and
// `pragma_index_xinfo` answers NOCASE for it (checked against sqlite3 3.51.0).
TEST_CASE("codegen: CREATE INDEX over a call keeps its COLLATE") {
    const CodeGenResult codeGenResult = generateFull("CREATE INDEX IF NOT EXISTS i ON t ((lower(a)) COLLATE NOCASE)");
    REQUIRE(codeGenResult.code == "make_index<T>(\"i\", indexed_column(lower(&T::a)).collate(\"nocase\"));");
    REQUIRE(codeGenResult.warnings.empty());
}

// A prefix operator is the one shape SQLite binds tighter than COLLATE, so `~"a" COLLATE nocase`
// collates the whole of it and the collation is generated.
TEST_CASE("codegen: CREATE INDEX over a bitwise NOT keeps its COLLATE") {
    const CodeGenResult codeGenResult = generateFull("CREATE INDEX IF NOT EXISTS i ON t ((~a) COLLATE NOCASE)");
    REQUIRE(codeGenResult.code == "make_index<T>(\"i\", indexed_column(~c(&T::a)).collate(\"nocase\"));");
    REQUIRE(codeGenResult.warnings.empty());
}

// An IN ends in its value list, which is no expression for a trailing COLLATE to attach to: SQLite
// reads `"a" IN (1, 2) COLLATE nocase` as the collation of the whole IN, so it is generated.
TEST_CASE("codegen: CREATE INDEX over an IN over a value list keeps its COLLATE") {
    const CodeGenResult codeGenResult =
        generateFull("CREATE INDEX IF NOT EXISTS i ON t ((a IN (1, 2)) COLLATE NOCASE)");
    REQUIRE(codeGenResult.code == "make_index<T>(\"i\", indexed_column(in(&T::a, {1, 2})).collate(\"nocase\"));");
    REQUIRE(codeGenResult.warnings.empty());
}

// A JSON arrow is the one binary operator generated as a call, `JSON_EXTRACT("j", '$.x')`, which
// ends in no operand of its own: SQLite collates the whole call, so the collation is generated and
// `pragma_index_xinfo` answers NOCASE for the indexed expression (checked against sqlite3 3.51.0).
TEST_CASE("codegen: CREATE INDEX over a JSON ->> keeps its COLLATE") {
    const CodeGenResult codeGenResult = generateFull("CREATE INDEX IF NOT EXISTS i ON t ((j ->> '$.x') COLLATE "
                                                     "NOCASE)");
    REQUIRE(codeGenResult.code ==
            "make_index<T>(\"i\", indexed_column(json_extract<std::string>(&T::j, \"$.x\")).collate(\"nocase\"));");
    REQUIRE(codeGenResult.warnings.empty());
}

// The text arrow is generated as the same call and keeps its collation the same way; the report it
// carries is the one about the value the call answers, not about the collation.
TEST_CASE("codegen: CREATE INDEX over a JSON -> keeps its COLLATE") {
    const CodeGenResult codeGenResult = generateFull("CREATE INDEX IF NOT EXISTS i ON t ((j -> '$.x') COLLATE "
                                                     "NOCASE)");
    REQUIRE(codeGenResult.code ==
            "make_index<T>(\"i\", indexed_column(json_extract<std::string>(&T::j, \"$.x\")).collate(\"nocase\"));");
    REQUIRE(codeGenResult.warnings ==
            std::vector<CodegenWarning>{
                {"`->` is generated as a JSON_EXTRACT call, which answers the SQL value at the path where `->` "
                 "answers the JSON text of that value: a string comes back unquoted, and `true`, `false` and "
                 "`null` come back as 1, 0 and NULL. sqlite_orm has no form for the operator itself",
                 SourceLocation{1, 39},
                 2}});
}

// `NOT` is the one prefix operator SQLite binds looser than COLLATE, so `NOT "a" COLLATE nocase`
// reads as `NOT ("a" COLLATE nocase)` and the collation is left out like a binary operator's.
TEST_CASE("codegen: CREATE INDEX over a logical NOT leaves out its COLLATE") {
    const CodeGenResult codeGenResult = generateFull("CREATE INDEX IF NOT EXISTS i ON t ((NOT a) COLLATE NOCASE)");
    REQUIRE(codeGenResult.code == "make_index<T>(\"i\", indexed_column(not column<T>(&T::a)));");
    REQUIRE(codeGenResult.warnings ==
            std::vector<CodegenWarning>{
                {"COLLATE NOCASE over an expression in index i is not generated: sqlite_orm serializes an indexed "
                 "column's collation as a bare COLLATE after the expression, and SQLite binds COLLATE tighter than "
                 "the operators in it, so the collation would apply to part of the expression rather than to the "
                 "indexed value",
                 SourceLocation{1, 36},
                 7}});
}

// A unary plus generates its operand and nothing else, so what a trailing COLLATE lands on is
// decided by the concatenation under it: the plus is gone from the generated code and the
// collation goes with it.
TEST_CASE("codegen: CREATE INDEX over a unary plus over a concatenation leaves out its COLLATE") {
    const CodeGenResult codeGenResult = generateFull("CREATE INDEX IF NOT EXISTS i ON t ((+(b || 'x')) COLLATE "
                                                     "NOCASE)");
    REQUIRE(codeGenResult.code == "make_index<T>(\"i\", indexed_column(c(&T::b) || \"x\"));");
    REQUIRE(codeGenResult.warnings ==
            std::vector<CodegenWarning>{
                {"COLLATE NOCASE over an expression in index i is not generated: sqlite_orm serializes an indexed "
                 "column's collation as a bare COLLATE after the expression, and SQLite binds COLLATE tighter than "
                 "the operators in it, so the collation would apply to part of the expression rather than to the "
                 "indexed value",
                 SourceLocation{1, 36},
                 13}});
}

// A negation folded into the constant it negates is a literal in the serialized SQL, a term a
// trailing COLLATE takes whole, so this one keeps its collation where `(-a)` above loses it.
TEST_CASE("codegen: CREATE INDEX over a folded negation keeps its COLLATE") {
    const CodeGenResult codeGenResult = generateFull("CREATE INDEX IF NOT EXISTS i ON t ((- -3) COLLATE NOCASE)");
    REQUIRE(codeGenResult.code == "make_index<T>(\"i\", indexed_column(-(-3)).collate(\"nocase\"));");
    REQUIRE(codeGenResult.warnings.empty());
}

TEST_CASE("codegen: CREATE INDEX over an expression keeps its partial WHERE") {
    REQUIRE(generate("CREATE INDEX i ON t (a + 1) WHERE a > 0") ==
            "make_index<T>(\"i\", indexed_column(c(&T::a) + 1), where(c(&T::a) > 0));");
}

// Only the first argument is deduced from, so an index that starts with a column keeps the short
// form however the columns after it are written.
TEST_CASE("codegen: CREATE INDEX starting with a column keeps the deduced form") {
    REQUIRE(generate("CREATE INDEX i ON t (a, b + 1)") ==
            "make_index(\"i\", indexed_column(&T::a), indexed_column(c(&T::b) + 1));");
}

TEST_CASE("codegen: CREATE UNIQUE INDEX starting with a column keeps the deduced form") {
    REQUIRE(generate("CREATE UNIQUE INDEX u ON t (a, b + 1)") ==
            "make_unique_index(\"u\", indexed_column(&T::a), indexed_column(c(&T::b) + 1));");
}

// `make_unique_index` takes the table as a template parameter defaulted behind its argument pack,
// which no explicit template argument list can reach: `make_unique_index<T>("u", ...)` binds `T` to
// the pack instead. A UNIQUE index that starts with an expression therefore has no form at all.
TEST_CASE("codegen: CREATE UNIQUE INDEX over an expression is not generated") {
    const CodeGenResult expected{
        "",
        expectedBinaryLeaf("&T::a", "1", " + ", "add").decisionPoints,
        {"sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs from "
         "serialized output",
         "UNIQUE index u starts with an expression: sqlite_orm deduces the table an index is made for from its "
         "first indexed column, and make_unique_index has no form that spells that table out, so the index is "
         "not generated"}};
    const CodeGenResult codeGenResult = generateFull("CREATE UNIQUE INDEX u ON t (a + 1)");
    REQUIRE(codeGenResult == expected);
}

// A UNIQUE index that is left out still reports what every slot of it decides and warns about: the
// clause of a partial index is generated before it is decided whether the index has a form at all.
// Reporting only the slots ahead of the bail-out made the same COLLATE silent in the WHERE while it
// was reported from an indexed column. SQLite takes this index (`PRAGMA index_info` answers -2 for
// its only column, an expression), checked against sqlite3 3.51.
TEST_CASE("codegen: CREATE UNIQUE INDEX over an expression still reports its partial WHERE") {
    std::vector<DecisionPoint> decisionPoints = expectedBinaryLeaf("&T::a", "1", " + ", "add").decisionPoints;
    decisionPoints.push_back(columnRefStyleDp(3, "&T::a"));
    const CodeGenResult expected{
        "",
        std::move(decisionPoints),
        {"sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs from "
         "serialized output",
         "COLLATE NOCASE on expressions is not directly supported in sqlite_orm codegen",
         "UNIQUE index u starts with an expression: sqlite_orm deduces the table an index is made for from its "
         "first indexed column, and make_unique_index has no form that spells that table out, so the index is "
         "not generated"}};
    const CodeGenResult codeGenResult = generateFull("CREATE UNIQUE INDEX u ON t (a + 1) WHERE a COLLATE NOCASE");
    REQUIRE(codeGenResult == expected);
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

// An index carries no bound parameter: both its indexed columns and the WHERE of a partial index
// reach SQLite as the text sqlite_orm serializes the index into, so a BLOB literal there is the
// same defect a table clause has (see `codegen: sqlite_orm writes a BLOB in a DDL clause as its
// bytes rather than as hex`). An index that lost the expression it is made for is no index, so the
// whole statement is left out. This case is the one that spells the warning out whole; the others
// build it from `kDdlBlobLiteralReason`.
TEST_CASE("codegen: CREATE INDEX over a BLOB literal is not generated") {
    const CodeGenResult expected{
        {},
        {},
        {"sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs from "
         "serialized output",
         CodegenWarning{"index i uses x'0102', a BLOB literal in a clause sqlite_orm writes into the schema as "
                        "text: it prints a blob as the bytes themselves inside x'…' instead of as their hex "
                        "digits, which SQLite reads as a different value where every byte of the blob is a hex "
                        "digit and refuses as an unrecognized token where one is not, so the index is not "
                        "generated"}}};
    REQUIRE(generateFull("CREATE INDEX i ON t ((x'0102'))") == expected);
}

TEST_CASE("codegen: CREATE INDEX with a BLOB literal in its partial WHERE is not generated") {
    const CodeGenResult result = generateFull("CREATE INDEX i ON t (a) WHERE a <> x'0102'");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs "
                 "from serialized output"},
                {"index i uses " + kDdlBlobLiteralReason("x'0102'") + ", so the index is not generated"}});
    REQUIRE(result.errors.empty());
}

// An empty blob prints as `x''` either way, so it is the one that keeps its index.
TEST_CASE("codegen: CREATE INDEX over an empty BLOB literal is generated") {
    const CodeGenResult expected{
        "make_index<T>(\"i\", indexed_column(std::vector<char>{}));",
        {},
        {"sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs from "
         "serialized output"}};
    REQUIRE(generateFull("CREATE INDEX i ON t ((x''))") == expected);
}

// Everything a trigger is made of reaches SQLite as the text of the CREATE TRIGGER — the WHEN
// clause as much as the statements of the body — so a BLOB literal anywhere in it takes the whole
// trigger with it.
TEST_CASE("codegen: CREATE TRIGGER - a BLOB literal leaves the trigger ungenerated") {
    auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t BEGIN UPDATE t SET x = x'0102'; END");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"CREATE TRIGGER tr uses " + kDdlBlobLiteralReason("x'0102'") + ", so the trigger is not generated"}});
    REQUIRE(result.errors.empty());
}

// A SELECT in a trigger body is generated as a select subexpression too, so a HAVING with no GROUP
// BY there leaves the step unmapped and the trigger with it, rather than a trigger running the
// query without the condition. SQLite stores and fires this trigger (checked on sqlite3 3.51.0).
TEST_CASE("codegen: CREATE TRIGGER - HAVING and no GROUP BY in the body leaves the trigger ungenerated") {
    auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT count(*) FROM t HAVING count(*) > 1; "
                               "END");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"HAVING without GROUP BY in subquery is not mapped to sqlite_orm select(...)"},
                {"a statement in the trigger body is not mapped to sqlite_orm codegen", SourceLocation{1, 43}, 42},
                {kStatementNotGenerated}});
    REQUIRE(result.errors.empty());
}

// The same literal in a statement of its own is bound rather than written into SQL, so it keeps
// every byte and stays generated.
TEST_CASE("codegen: UPDATE with a BLOB literal keeps it") {
    REQUIRE(generate("UPDATE t SET x = x'0102' WHERE x <> x'0102'") ==
            "storage.update_all(set(c(&T::x) = std::vector<char>{'\\x01', '\\x02'}), where(c(&T::x) != "
            "std::vector<char>{'\\x01', '\\x02'}));");
}

TEST_CASE("codegen: UPDATE FROM warning") {
    const auto result = generateFull("UPDATE t SET a = 1 FROM b WHERE t.id = b.id;");
    REQUIRE_FALSE(result.warnings.empty());
    bool found = false;
    for (const auto& w: result.warnings) {
        if (w.message.find("FROM") != std::string::npos) {
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
    auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t BEGIN UPDATE t SET x = 0x10000000000000000; END");
    REQUIRE(result.code.empty());
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"CREATE TRIGGER tr uses 0x10000000000000000, too big for a signed 64-bit integer: SQLite stores the "
                 "trigger but refuses every statement that fires it, and C++ has no literal for it, so the trigger is "
                 "not generated"}});
    REQUIRE(result.errors.empty());
}

// A trigger that is left out has no generated code to fail to compile, so the WHEN warnings are
// held back until the trigger is known to generate: otherwise the same trigger would be reported
// as both not generated and generated but not compiling. SQLite stores this one and refuses every
// INSERT on t (checked against sqlite3 3.51.0).
TEST_CASE("codegen: CREATE TRIGGER - a trigger left out carries no WHEN warning") {
    auto result = generateFull("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.x IS NULL BEGIN UPDATE t SET x = "
                               "0x10000000000000000; END");
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

// A third sign does not bring the value back: SQLite folds only the innermost one into the
// literal and negates what the rest stand on, so `sqlite3 :memory: "SELECT
// typeof(-(-(-9223372036854775808)))"` is real, and so is the value a unary plus stands on,
// which SQLite's parser drops altogether.
TEST_CASE("codegen: INSERT VALUES - a third sign leaves a negated INT64_MIN past the range") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (-(-(-9223372036854775808)));");
    REQUIRE(result.code ==
            "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(-(-(-9223372036854775808.0)))));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"INSERT into column 'x' of table 't' uses -9223372036854775808, past the signed 64-bit integer "
                 "range: SQLite types a value before it applies the column affinity and keeps such a one a REAL, "
                 "and the int64_t field cannot hold it, so the row is generated through columns()/values(), which "
                 "writes the value SQLite stores, rather than as a struct, which would write a different one",
                 SourceLocation{1, 50},
                 24}});
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
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"INSERT into column 'x' of table 't' uses 9223372036854775808, past the signed 64-bit integer "
                 "range: SQLite types a value before it applies the column affinity and keeps such a one a REAL, "
                 "and the int64_t field cannot hold it, so the row is generated through columns()/values(), which "
                 "writes the value SQLite stores, rather than as a struct, which would write a different one",
                 SourceLocation{1, 50},
                 19}});
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
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x, &T::y), "
                           "values(std::make_tuple(99999999999999999999.0, \"a\"), std::make_tuple(1, \"b\")));");
    REQUIRE(result.warnings ==
            std::vector<CodegenWarning>{
                {"INSERT into column 'x' of table 't' uses 99999999999999999999, past the signed 64-bit integer "
                 "range: SQLite types a value before it applies the column affinity and keeps such a one a REAL, "
                 "and the int64_t field cannot hold it, so the row is generated through columns()/values(), which "
                 "writes the value SQLite stores, rather than as a struct, which would write a different one",
                 SourceLocation{1, 58},
                 20}});
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

// A BOOLEAN column has NUMERIC affinity, which leaves an integer the way SQLite typed it: sqlite3
// 3.51 stores `integer|5` here, while the object form sends the 5 through the `bool` field and
// stores 1.
TEST_CASE("codegen: INSERT VALUES - a whole number no bool holds into a BOOLEAN column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (5);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(5)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// The int64 maximum is inside the range of the column, and of every field but this one; sqlite3
// 3.51 stores `integer|9223372036854775807`.
TEST_CASE("codegen: INSERT VALUES - the int64 maximum into a BOOLEAN column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (9223372036854775807);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(9223372036854775807)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A `bool` field holds no negative number either: sqlite3 3.51 stores `integer|-1`.
TEST_CASE("codegen: INSERT VALUES - a negative one into a BOOLEAN column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (-1);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(-1)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// The digits alone do not say which value a literal denotes: `1_0` is ten, and SQLite stores
// `integer|10`.
TEST_CASE("codegen: INSERT VALUES - a literal with digit separators into a BOOLEAN column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (1_0);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(1'0)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// SQLite reads a hex literal as a signed 64-bit integer and wraps it around, so the value of
// `0xFFFFFFFFFFFFFFFF` is -1 — `integer|-1` in sqlite3 3.51 — and no `bool` field carries it.
TEST_CASE("codegen: INSERT VALUES - a hex literal into a BOOLEAN column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (0xFFFFFFFFFFFFFFFF);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), "
                           "values(std::make_tuple(static_cast<int64_t>(0xFFFFFFFFFFFFFFFF))));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// The object form stays where the field carries the value: 0 and 1 are the whole range of a `bool`,
// spelled in any base.
TEST_CASE("codegen: INSERT VALUES - a bool the field holds keeps the object form") {
    REQUIRE(generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (1);").code ==
            "storage.insert(T{1});");
    REQUIRE(generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (0);").code ==
            "storage.insert(T{0});");
    REQUIRE(generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (0x1);").code ==
            "storage.insert(T{0x1});");
}

// SQLite reads TRUE and FALSE as the integers 1 and 0, which is exactly what the field holds.
TEST_CASE("codegen: INSERT VALUES - a boolean keyword into a BOOLEAN column keeps the object form") {
    auto result = generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES (TRUE);");
    REQUIRE(result.code == "storage.insert(T{true});");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A NOT NULL column has a bare `bool` field, which a braced initializer refuses the 2 for outright:
// "narrowing conversion of '2' from 'int' to 'bool'". The column list is the same answer.
TEST_CASE("codegen: INSERT VALUES - a whole number no bool holds into a NOT NULL BOOLEAN column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x BOOLEAN NOT NULL); INSERT INTO t VALUES (2);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(2)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// One value out of reach of its field spells out every column of the row, the neighbours included.
TEST_CASE("codegen: INSERT VALUES - a whole number no bool holds beside a string") {
    auto result = generateLastOfBatch("CREATE TABLE t(x BOOLEAN, y TEXT); INSERT INTO t VALUES (2, 'a');");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x, &T::y), values(std::make_tuple(2, \"a\")));");
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

// A column with no type has BLOB affinity and maps to a `std::vector<char>` field, which no number
// initializes: `storage.insert(Ch{1})` did not compile at all. SQLite stores whatever the value is
// in such a column — `CREATE TABLE ch(x); INSERT INTO ch VALUES (1)` is `integer|1` in sqlite3 3.51
// — so the column list is spelled out, where the value is bound as itself.
TEST_CASE("codegen: INSERT VALUES - a number into a column with no type") {
    auto result = generateLastOfBatch("CREATE TABLE ch(x); INSERT INTO ch VALUES (1);");
    REQUIRE(result.code == "storage.insert(into<Ch>(), columns(&Ch::x), values(std::make_tuple(1)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A blob literal is the one kind a `std::vector<char>` field does carry, so it keeps the object
// form. sqlite3 3.51 stores `blob|A` for this row.
TEST_CASE("codegen: INSERT VALUES - a blob literal into a column with no type keeps the object form") {
    auto result = generateLastOfBatch("CREATE TABLE ch(x); INSERT INTO ch VALUES (X'41');");
    REQUIRE(result.code == "storage.insert(Ch{std::vector<char>{'\\x41'}});");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// The same mismatch the other way round: a `std::string` field takes no number, and a numeric field
// takes no text. SQLite applies the column affinity to the value it typed, storing `text|1` and
// `text|a` for these two rows in sqlite3 3.51, which is what binding the value reproduces.
TEST_CASE("codegen: INSERT VALUES - a number into a TEXT column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x TEXT); INSERT INTO t VALUES (1);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(1)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

TEST_CASE("codegen: INSERT VALUES - a string into an INTEGER column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES ('a');");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(\"a\")));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A blob literal reaches neither of them. sqlite3 3.51 stores `blob|A` in a REAL column, a blob
// being the one storage class no affinity converts.
TEST_CASE("codegen: INSERT VALUES - a blob literal into a REAL column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x REAL); INSERT INTO t VALUES (X'41');");
    REQUIRE(result.code ==
            "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(std::vector<char>{'\\x41'})));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A `bool` field is the one that takes a string without a word from the compiler: the pointer
// turns into `true` and the row gets 1, where sqlite3 3.51 stores `text|a`.
TEST_CASE("codegen: INSERT VALUES - a string into a BOOLEAN column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x BOOLEAN); INSERT INTO t VALUES ('a');");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(\"a\")));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A string literal is what a `std::string` field carries, so a TEXT column keeps the object form.
TEST_CASE("codegen: INSERT VALUES - a string into a TEXT column keeps the object form") {
    auto result = generateLastOfBatch("CREATE TABLE t(x TEXT); INSERT INTO t VALUES ('a');");
    REQUIRE(result.code == "storage.insert(T{\"a\"});");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// Only SQLite knows what an expression comes out as, so no field carries it: the object form put
// the generated sqlite_orm expression into the struct, which does not compile for any field type.
// sqlite3 3.51 stores `integer|2` for this row, and so does the bound expression.
TEST_CASE("codegen: INSERT VALUES - an expression value") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (1+1);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(c(1) + 1)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A NOT NULL column has a bare field rather than an optional one, and `std::nullopt` initializes
// neither it nor the `int64_t` behind it. SQLite prepares the statement and refuses the row when it
// runs it ("NOT NULL constraint failed: t.x"), which the bound `nullptr` reproduces.
TEST_CASE("codegen: INSERT VALUES - NULL into a NOT NULL column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER NOT NULL); INSERT INTO t VALUES (NULL);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(nullptr)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A nullable column has the optional field `std::nullopt` initializes, so NULL keeps the object form.
TEST_CASE("codegen: INSERT VALUES - NULL into a nullable column keeps the object form") {
    auto result = generateLastOfBatch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES (NULL);");
    REQUIRE(result.code == "storage.insert(T{std::nullopt});");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// One value out of reach of its field spells the whole column list out, and the rest of the row
// goes along with it.
TEST_CASE("codegen: INSERT VALUES - a number beside a string in a two-column table") {
    auto result = generateLastOfBatch("CREATE TABLE t(x, y TEXT); INSERT INTO t VALUES (1, 'a'), (X'41', 'b');");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x, &T::y), "
                           "values(std::make_tuple(1, \"a\"), std::make_tuple(std::vector<char>{'\\x41'}, \"b\")));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A table this batch never declared says nothing about the field a value reaches, here either.
TEST_CASE("codegen: INSERT VALUES - a string into an unknown table") {
    auto result = generateFull("INSERT INTO t VALUES ('a');");
    REQUIRE(result.code == "storage.insert(T{\"a\"});");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A NOT NULL REAL column has a bare `double` field, and the object form brace-initializes it: a
// braced initializer refuses an integer constant no `double` holds exactly, so
// `storage.insert(T{9223372036854775807})` did not compile. The column list binds the integer and
// leaves the REAL affinity to SQLite, which stores `real|9.22337203685478e+18` in sqlite3 3.51 —
// the very value the field would have held.
TEST_CASE("codegen: INSERT VALUES - a whole number no double holds into a NOT NULL REAL column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x REAL NOT NULL); INSERT INTO t VALUES (9223372036854775807);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(9223372036854775807)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// The first whole number a `double` rounds is 2^53 + 1; 2^53 itself survives the round trip and
// keeps the object form.
TEST_CASE("codegen: INSERT VALUES - the first whole number a double rounds into a REAL column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x REAL NOT NULL); INSERT INTO t VALUES (9007199254740993);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(9007199254740993)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

TEST_CASE("codegen: INSERT VALUES - a whole number a double holds keeps the object form") {
    auto result = generateLastOfBatch("CREATE TABLE t(x REAL NOT NULL); INSERT INTO t VALUES (9007199254740992);");
    REQUIRE(result.code == "storage.insert(T{9007199254740992});");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// SQLite reads a hexadecimal literal as a signed 64-bit integer, so the sign bit decides which
// number a `double` is asked to hold: `0xFFFFFFFFFFFFFFFF` is -1 and fits, `0x7FFFFFFFFFFFFFFF` is
// the int64 maximum and does not.
TEST_CASE("codegen: INSERT VALUES - a hex literal into a NOT NULL REAL column") {
    auto wrapped = generateLastOfBatch("CREATE TABLE t(x REAL NOT NULL); INSERT INTO t VALUES (0xFFFFFFFFFFFFFFFF);");
    REQUIRE(wrapped.code == "storage.insert(T{static_cast<int64_t>(0xFFFFFFFFFFFFFFFF)});");
    REQUIRE(wrapped.warnings.empty());
    REQUIRE(wrapped.errors.empty());

    auto rounded = generateLastOfBatch("CREATE TABLE t(x REAL NOT NULL); INSERT INTO t VALUES (0x7FFFFFFFFFFFFFFF);");
    REQUIRE(rounded.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(0x7FFFFFFFFFFFFFFF)));");
    REQUIRE(rounded.warnings.empty());
    REQUIRE(rounded.errors.empty());
}

// A literal past the int64 range is generated as a REAL, which initializes a `double` field as
// itself, so it keeps the object form even though a whole number of that size never round-trips.
TEST_CASE("codegen: INSERT VALUES - a literal past the int64 range into a NOT NULL REAL column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x REAL NOT NULL); INSERT INTO t VALUES (99999999999999999999);");
    REQUIRE(result.code == "storage.insert(T{99999999999999999999.0});");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A nullable REAL column reaches the same field through `std::optional`, whose constructor converts
// rather than narrows, but the value it would store is the rounded one, not the integer SQLite
// binds, so the column list is spelled out there too.
TEST_CASE("codegen: INSERT VALUES - a whole number no double holds into a nullable REAL column") {
    auto result = generateLastOfBatch("CREATE TABLE t(x REAL); INSERT INTO t VALUES (9223372036854775807);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(9223372036854775807)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// A NUMERIC column maps to the same `double` field and takes the same route. What the generated
// storage then stores for it is a REAL rather than the `integer|9223372036854775807` sqlite3 3.51
// stores in a NUMERIC column, because the field declares the column REAL — a separate bug, and the
// only one left between the two once the value is bound rather than narrowed.
TEST_CASE("codegen: INSERT VALUES - a whole number no double holds into a NUMERIC column") {
    auto result =
        generateLastOfBatch("CREATE TABLE t(x NUMERIC NOT NULL); INSERT INTO t VALUES (9223372036854775807);");
    REQUIRE(result.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(9223372036854775807)));");
    REQUIRE(result.warnings.empty());
    REQUIRE(result.errors.empty());
}

// The past-range warning belongs to the field that holds whole numbers only, where the column list
// writes a different number than the struct would have. A TEXT or a BLOB column reaches the same
// literal through a field of another storage class, so the column list is spelled out for the
// storage class alone and the literal itself costs nothing: SQLite stores `text|1.0e+20` and
// `real|1.0e+20` for these two rows in sqlite3 3.51, which is what binding the value reproduces.
// Dropping the field-type guard on the warning makes both of these warn about an `std::string` or
// an `std::vector<char>` field that "cannot hold" the value, which is not what decided the form.
TEST_CASE("codegen: INSERT VALUES - a literal past the int64 range into a column of another storage class") {
    auto text = generateLastOfBatch("CREATE TABLE t(x TEXT); INSERT INTO t VALUES (99999999999999999999);");
    REQUIRE(text.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(99999999999999999999.0)));");
    REQUIRE(text.warnings.empty());
    REQUIRE(text.errors.empty());

    auto blob = generateLastOfBatch("CREATE TABLE t(x BLOB); INSERT INTO t VALUES (99999999999999999999);");
    REQUIRE(blob.code == "storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(99999999999999999999.0)));");
    REQUIRE(blob.warnings.empty());
    REQUIRE(blob.errors.empty());
}
