#!/bin/sh
# `sqlite2orm --db <db> --json` must exit 1 and say why on stderr when a statement does not
# generate, so a script can tell a failed schema from a good one without reading the JSON.
# A generated column over a hex literal too big for 64 bits is what makes the statement fail codegen:
# SQLite refuses it inside CREATE TABLE, but stores it as written when ALTER TABLE adds it, and only
# refuses the statements that read it. Every sqlite3 shell this runs against does so (checked on
# 3.38.5 and 3.51.0), so the fixture does not need a recent one.
set -e

cli="$1"
sqlite3="$2"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

"$sqlite3" "$dir/v.db" 'CREATE TABLE q (a INTEGER); ALTER TABLE q ADD COLUMN b INTEGER AS (0x10000000000000000);'

status=0
"$cli" --db "$dir/v.db" --json > "$dir/out.json" 2> "$dir/err.txt" || status=$?

test "$status" -eq 1

cat > "$dir/err.expected" <<'EOF'
codegen error [table q]: hex literal too big: 0x10000000000000000
EOF
diff "$dir/err.expected" "$dir/err.txt"

cat > "$dir/out.expected" <<'EOF'
{"coverage":{"generated":0,"total":1},"statements":[{"comments":[],"decisionPoints":[],"name":"q","ok":false,"tableName":"q","type":"table"}],"targetCppStandard":20}
EOF
diff "$dir/out.expected" "$dir/out.json"

# The same schema without `--json`: the diagnostics and the exit code do not depend on the mode.
# The statement that does not generate is left out of `make_storage()` with a warning rather than
# taking the header with it, so the header is printed even though the exit code says 1.
status=0
"$cli" --db "$dir/v.db" > "$dir/plain.txt" 2> "$dir/plain_err.txt" || status=$?

test "$status" -eq 1

cat > "$dir/plain_err.expected" <<'EOF'
warning: CREATE TABLE `q` did not generate and is not merged into make_storage()
codegen error [table q]: hex literal too big: 0x10000000000000000
EOF
diff "$dir/plain_err.expected" "$dir/plain_err.txt"

cat > "$dir/plain.expected" <<'EOF'
#pragma once

#include <sqlite_orm/sqlite_orm.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>


inline auto make_sqlite_schema_storage(const std::string& db_path) {
    using namespace sqlite_orm;
    return make_storage(db_path);
}
EOF
diff "$dir/plain.expected" "$dir/plain.txt"

# A table that SQLite only stores the body of — a view SQLite accepts and refuses only when it is
# used — leaves every other table generated instead of emptying the whole header.
"$sqlite3" "$dir/view.db" 'CREATE TABLE t (id INTEGER PRIMARY KEY);'
"$sqlite3" "$dir/view.db" 'CREATE VIEW v AS SELECT -0x8000000000000000;'

status=0
"$cli" --db "$dir/view.db" > "$dir/view.txt" 2> "$dir/view_err.txt" || status=$?

test "$status" -eq 1

cat > "$dir/view_err.expected" <<'EOF'
warning: CREATE VIEW `v` did not generate and is not merged into make_storage()
validation [view v]: hex literal too big: -0x8000000000000000 (UnaryOperatorNode)
EOF
diff "$dir/view_err.expected" "$dir/view_err.txt"

cat > "$dir/view.expected" <<'EOF'
#pragma once

#include <sqlite_orm/sqlite_orm.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct T {
    std::optional<int64_t> id;
};


inline auto make_sqlite_schema_storage(const std::string& db_path) {
    using namespace sqlite_orm;
    return make_storage(db_path,
        make_table("t",
        make_column("id", &T::id, primary_key())));
}
EOF
diff "$dir/view.expected" "$dir/view.txt"

# A schema that generates keeps exit 0 and a quiet stderr in `--json` mode.
"$sqlite3" "$dir/ok.db" 'CREATE TABLE t (id INTEGER PRIMARY KEY);'
"$cli" --db "$dir/ok.db" --json > "$dir/ok.json" 2> "$dir/ok_err.txt"

cat > "$dir/ok.expected" <<'EOF'
{"coverage":{"generated":1,"total":1},"statements":[{"comments":[],"decisionPoints":[],"name":"t","ok":true,"tableName":"t","type":"table"}],"targetCppStandard":20}
EOF
diff "$dir/ok.expected" "$dir/ok.json"
test ! -s "$dir/ok_err.txt"

# A foreign key into a table the database does not hold: SQLite stores the schema and only says
# `no such table: main.users` once a row is written with enforcement on. The header leaves the key
# out rather than naming a `Users` it never declares, and says so on stderr; nothing failed to
# generate, so the exit code stays 0.
"$sqlite3" "$dir/fk.db" 'CREATE TABLE posts (id INTEGER PRIMARY KEY, author INTEGER REFERENCES users(id));'

"$cli" --db "$dir/fk.db" > "$dir/fk.txt" 2> "$dir/fk_err.txt"

cat > "$dir/fk_err.expected" <<'EOF'
warning: foreign key on column 'author' references users, which this schema does not create, so the generated table has no foreign_key()
EOF
diff "$dir/fk_err.expected" "$dir/fk_err.txt"

cat > "$dir/fk.expected" <<'EOF'
#pragma once

#include <sqlite_orm/sqlite_orm.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct Posts {
    std::optional<int64_t> id;
    std::optional<int64_t> author;
};


inline auto make_sqlite_schema_storage(const std::string& db_path) {
    using namespace sqlite_orm;
    return make_storage(db_path,
        make_table("posts",
        make_column("id", &Posts::id, primary_key()),
        make_column("author", &Posts::author)));
}
EOF
diff "$dir/fk.expected" "$dir/fk.txt"
