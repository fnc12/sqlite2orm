#!/bin/sh
# `--std <14|17|20|26>` sets the target C++ standard of the generated code. Without the flag the CLI
# generates for the default standard exactly as it did before the flag existed; an unknown value is
# refused with exit 2 instead of quietly generating for the default one.
set -e

cli="$1"
sqlite3="$2"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

table='CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT NOT NULL);'

cat > "$dir/classic.expected" <<'EOT'
struct Users {
    std::optional<int64_t> id;
    std::string name;
};

auto storage = make_storage("",
    make_table("users",
        make_column("id", &Users::id, primary_key()),
        make_column("name", &Users::name)));
EOT

"$cli" -e "$table" > "$dir/default.txt"
diff "$dir/classic.expected" "$dir/default.txt"
for standard in 14 17 20; do
    "$cli" --std "$standard" -e "$table" > "$dir/std$standard.txt"
    diff "$dir/classic.expected" "$dir/std$standard.txt"
done

# C++26 is offered the reflected table, and chooses it.
cat > "$dir/reflection.expected" <<'EOT'
struct [[= "users"_orm_name]] Users {
    [[= primary_key()]] std::optional<int64_t> id;
    std::string name;
};

auto storage = make_storage("",
    make_table<Users>());
EOT
"$cli" --std 26 -e "$table" > "$dir/std26.txt"
diff "$dir/reflection.expected" "$dir/std26.txt"
# The flag may follow the other arguments too.
"$cli" -e "$table" --std 26 > "$dir/std26_after.txt"
diff "$dir/reflection.expected" "$dir/std26_after.txt"

# A view is mapped by reflection whatever the target; below C++26 it says the code will not compile.
view='CREATE VIEW v AS SELECT 1 AS one;'
cat > "$dir/view.expected" <<'EOT'
struct [[= "v"_orm_name]] V {
    int64_t one = 0;
};

auto storage = make_storage("",
    make_view<V>(select(1)));
EOT
cat > "$dir/view_err.expected" <<'EOT'
warning: CREATE VIEW v: sqlite_orm views use C++26 reflection (make_view + [[= "…"_orm_name]]); this code requires C++26 and will not compile under the selected C++ standard
EOT
"$cli" --std 17 -e "$view" > "$dir/view17.txt" 2> "$dir/view17_err.txt"
diff "$dir/view.expected" "$dir/view17.txt"
diff "$dir/view_err.expected" "$dir/view17_err.txt"
"$cli" --std 26 -e "$view" > "$dir/view26.txt" 2> "$dir/view26_err.txt"
diff "$dir/view.expected" "$dir/view26.txt"
test ! -s "$dir/view26_err.txt"

# Refused values: exit 2, nothing on stdout, the reason first on stderr.
for value in 11 23 abc c++20; do
    status=0
    "$cli" --std "$value" -e "$table" > "$dir/bad.txt" 2> "$dir/bad_err.txt" || status=$?
    test "$status" -eq 2
    test ! -s "$dir/bad.txt"
    echo "sqlite2orm: unsupported --std value '$value' (expected one of 14, 17, 20, 26)" > "$dir/bad_first.expected"
    head -n 1 "$dir/bad_err.txt" > "$dir/bad_first.txt"
    diff "$dir/bad_first.expected" "$dir/bad_first.txt"
done
status=0
"$cli" -e "$table" --std > "$dir/missing.txt" 2> "$dir/missing_err.txt" || status=$?
test "$status" -eq 2
test ! -s "$dir/missing.txt"
echo "sqlite2orm: --std requires a value (one of 14, 17, 20, 26)" > "$dir/missing_first.expected"
head -n 1 "$dir/missing_err.txt" > "$dir/missing_first.txt"
diff "$dir/missing_first.expected" "$dir/missing_first.txt"

# `--db --json` names the standard it generated for; C++26 is offered `table_mapping_style`.
"$sqlite3" "$dir/t.db" 'CREATE TABLE t (id INTEGER PRIMARY KEY);'

cat > "$dir/json_default.expected" <<'EOT'
{"coverage":{"generated":1,"total":1},"statements":[{"comments":[],"decisionPoints":[],"name":"t","ok":true,"tableName":"t","type":"table"}],"targetCppStandard":20}
EOT
"$cli" --db "$dir/t.db" --json > "$dir/json_default.txt"
diff "$dir/json_default.expected" "$dir/json_default.txt"

cat > "$dir/json17.expected" <<'EOT'
{"coverage":{"generated":1,"total":1},"statements":[{"comments":[],"decisionPoints":[],"name":"t","ok":true,"tableName":"t","type":"table"}],"targetCppStandard":17}
EOT
"$cli" --db "$dir/t.db" --json --std 17 > "$dir/json17.txt"
diff "$dir/json17.expected" "$dir/json17.txt"

cat > "$dir/json26.expected" <<'EOT'
{"coverage":{"generated":1,"total":1},"statements":[{"comments":["The table is mapped by sqlite_orm's reflection-based `make_table<T>()`: the columns and their constraints are read off the struct's members and `[[= …]]` annotations, and the `[[= \"…\"_orm_name]]` annotation supplies the table name. This requires a C++26 compiler with reflection (P2996/P3394); sqlite_orm detects support automatically (SQLITE_ORM_REFLECTION_SUPPORTED). The `make_table` alternative of the `table_mapping_style` decision point is the classical form and compiles from C++14 on."],"decisionPoints":[{"category":"table_mapping_style","chosenCode":"struct [[= \"t\"_orm_name]] T {\n    [[= primary_key()]] std::optional<int64_t> id;\n};\n\nmake_table<T>()","chosenValue":"reflection","id":1,"options":[{"code":"struct T {\n    std::optional<int64_t> id;\n};\n\nmake_table(\"t\",\n        make_column(\"id\", &T::id, primary_key()))","comments":[],"description":"make_table(\"name\", make_column(…)) over a plain struct (wider compiler support)","hidden":false,"minCppStandard":14,"value":"make_table"},{"code":"struct [[= \"t\"_orm_name]] T {\n    [[= primary_key()]] std::optional<int64_t> id;\n};\n\nmake_table<T>()","comments":["The table is mapped by sqlite_orm's reflection-based `make_table<T>()`: the columns and their constraints are read off the struct's members and `[[= …]]` annotations, and the `[[= \"…\"_orm_name]]` annotation supplies the table name. This requires a C++26 compiler with reflection (P2996/P3394); sqlite_orm detects support automatically (SQLITE_ORM_REFLECTION_SUPPORTED). The `make_table` alternative of the `table_mapping_style` decision point is the classical form and compiles from C++14 on."],"description":"C++26 reflection: annotated struct + make_table<T>()","hidden":false,"minCppStandard":26,"value":"reflection"}]}],"name":"t","ok":true,"tableName":"t","type":"table"}],"targetCppStandard":26}
EOT
"$cli" --std 26 --db "$dir/t.db" --json > "$dir/json26.txt" 2> "$dir/json26_err.txt"
diff "$dir/json26.expected" "$dir/json26.txt"
test ! -s "$dir/json26_err.txt"

cat > "$dir/header26.expected" <<'EOT'
#pragma once

#include <sqlite_orm/sqlite_orm.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace sqlite_orm;

struct [[= "t"_orm_name]] T {
    [[= primary_key()]] std::optional<int64_t> id;
};


inline auto make_sqlite_schema_storage(const std::string& db_path) {
    using namespace sqlite_orm;
    return make_storage(db_path,
        make_table<T>());
}
EOT
"$cli" --std 26 --db "$dir/t.db" > "$dir/header26.txt"
diff "$dir/header26.expected" "$dir/header26.txt"

# A column CHECK is checked by SQLite exactly as a table CHECK is, so the reflected form passes it
# to `make_table<T>(…)` rather than giving the table up; the classical form keeps it in make_column().
"$sqlite3" "$dir/c.db" 'CREATE TABLE c (id INTEGER PRIMARY KEY, qty INTEGER NOT NULL CHECK(qty > 0), CHECK(id < 100));'

cat > "$dir/check20.expected" <<'EOT'
#pragma once

#include <sqlite_orm/sqlite_orm.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct C {
    std::optional<int64_t> id;
    int64_t qty = 0;
};


inline auto make_sqlite_schema_storage(const std::string& db_path) {
    using namespace sqlite_orm;
    return make_storage(db_path,
        make_table("c",
        make_column("id", &C::id, primary_key()),
        make_column("qty", &C::qty, check(c(&C::qty) > 0)),
        check(c(&C::id) < 100)));
}
EOT
"$cli" --db "$dir/c.db" > "$dir/check20.txt"
diff "$dir/check20.expected" "$dir/check20.txt"

cat > "$dir/check26.expected" <<'EOT'
#pragma once

#include <sqlite_orm/sqlite_orm.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace sqlite_orm;

struct [[= "c"_orm_name]] C {
    [[= primary_key()]] std::optional<int64_t> id;
    int64_t qty = 0;
};


inline auto make_sqlite_schema_storage(const std::string& db_path) {
    using namespace sqlite_orm;
    return make_storage(db_path,
        make_table<C>(
        check(c(&C::qty) > 0),
        check(c(&C::id) < 100)));
}
EOT
"$cli" --std 26 --db "$dir/c.db" > "$dir/check26.txt"
diff "$dir/check26.expected" "$dir/check26.txt"

cat > "$dir/check26_json.expected" <<'EOT'
{"statements":[{"comments":["The table is mapped by sqlite_orm's reflection-based `make_table<T>()`: the columns and their constraints are read off the struct's members and `[[= …]]` annotations, and the `[[= \"…\"_orm_name]]` annotation supplies the table name. This requires a C++26 compiler with reflection (P2996/P3394); sqlite_orm detects support automatically (SQLITE_ORM_REFLECTION_SUPPORTED). The `make_table` alternative of the `table_mapping_style` decision point is the classical form and compiles from C++14 on."],"decisionPoints":[{"category":"table_mapping_style","chosenCode":"struct [[= \"c\"_orm_name]] C {\n    [[= primary_key()]] std::optional<int64_t> id;\n    int64_t qty = 0;\n};\n\nmake_table<C>(\n        check(c(&C::qty) > 0),\n        check(c(&C::id) < 100))","chosenValue":"reflection","id":5,"options":[{"code":"struct C {\n    std::optional<int64_t> id;\n    int64_t qty = 0;\n};\n\nmake_table(\"c\",\n        make_column(\"id\", &C::id, primary_key()),\n        make_column(\"qty\", &C::qty, check(c(&C::qty) > 0)),\n        check(c(&C::id) < 100))","comments":[],"description":"make_table(\"name\", make_column(…)) over a plain struct (wider compiler support)","hidden":false,"minCppStandard":14,"value":"make_table"},{"code":"struct [[= \"c\"_orm_name]] C {\n    [[= primary_key()]] std::optional<int64_t> id;\n    int64_t qty = 0;\n};\n\nmake_table<C>(\n        check(c(&C::qty) > 0),\n        check(c(&C::id) < 100))","comments":["The table is mapped by sqlite_orm's reflection-based `make_table<T>()`: the columns and their constraints are read off the struct's members and `[[= …]]` annotations, and the `[[= \"…\"_orm_name]]` annotation supplies the table name. This requires a C++26 compiler with reflection (P2996/P3394); sqlite_orm detects support automatically (SQLITE_ORM_REFLECTION_SUPPORTED). The `make_table` alternative of the `table_mapping_style` decision point is the classical form and compiles from C++14 on."],"description":"C++26 reflection: annotated struct + make_table<T>()","hidden":false,"minCppStandard":26,"value":"reflection"}]}],"name":"c","ok":true,"tableName":"c","type":"table"}],"targetCppStandard":26}
EOT
"$cli" --std 26 --db "$dir/c.db" --json > "$dir/check26_json.txt" 2> "$dir/check26_json_err.txt"
diff "$dir/check26_json.expected" "$dir/check26_json.txt"
test ! -s "$dir/check26_json_err.txt"
