#!/bin/sh
# `sqlite2orm --db <db> --json` must exit 1 and say why on stderr when a statement does not
# generate, so a script can tell a failed schema from a good one without reading the JSON.
set -e

cli="$1"
sqlite3="$2"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

"$sqlite3" "$dir/v.db" 'CREATE TABLE q (a INTEGER CHECK (a IS NOT 1));'

status=0
"$cli" --db "$dir/v.db" --json > "$dir/out.json" 2> "$dir/err.txt" || status=$?

test "$status" -eq 1
test -s "$dir/err.txt"
test -s "$dir/out.json"
