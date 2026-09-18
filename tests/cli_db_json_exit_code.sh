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
status=0
"$cli" --db "$dir/v.db" > "$dir/plain.txt" 2> "$dir/plain_err.txt" || status=$?

test "$status" -eq 1
diff "$dir/err.expected" "$dir/plain_err.txt"
test ! -s "$dir/plain.txt"

# A schema that generates keeps exit 0 and a quiet stderr in `--json` mode.
"$sqlite3" "$dir/ok.db" 'CREATE TABLE t (id INTEGER PRIMARY KEY);'
"$cli" --db "$dir/ok.db" --json > "$dir/ok.json" 2> "$dir/ok_err.txt"

cat > "$dir/ok.expected" <<'EOF'
{"statements":[{"comments":[],"decisionPoints":[],"name":"t","ok":true,"tableName":"t","type":"table"}]}
EOF
diff "$dir/ok.expected" "$dir/ok.json"
test ! -s "$dir/ok_err.txt"
