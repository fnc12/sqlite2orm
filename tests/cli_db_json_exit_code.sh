#!/bin/sh
# `sqlite2orm --db <db> --json` must exit 1 and say why on stderr when a statement does not
# generate, so a script can tell a failed schema from a good one without reading the JSON.
# `IS NOT <expr>` is what makes the statement fail codegen; it is stored as written by every
# sqlite3 shell this runs against (checked down to 3.38.5), so the fixture does not need a recent one.
set -e

cli="$1"
sqlite3="$2"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

"$sqlite3" "$dir/v.db" 'CREATE TABLE q (a INTEGER CHECK (a IS NOT 1));'

status=0
"$cli" --db "$dir/v.db" --json > "$dir/out.json" 2> "$dir/err.txt" || status=$?

test "$status" -eq 1

cat > "$dir/err.expected" <<'EOF'
codegen error [table q]: binary IS / IS NOT / IS [NOT] DISTINCT FROM is not supported in sqlite_orm
EOF
diff "$dir/err.expected" "$dir/err.txt"

cat > "$dir/out.expected" <<'EOF'
{"statements":[{"comments":[],"decisionPoints":[],"name":"q","ok":false,"tableName":"q","type":"table"}]}
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
codegen error [table q]: binary IS / IS NOT / IS [NOT] DISTINCT FROM is not supported in sqlite_orm
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
{"statements":[{"comments":[],"decisionPoints":[],"name":"t","ok":true,"tableName":"t","type":"table"}]}
EOF
diff "$dir/ok.expected" "$dir/ok.json"
test ! -s "$dir/ok_err.txt"
