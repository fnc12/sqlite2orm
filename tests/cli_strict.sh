#!/bin/sh
# `--db --strict` exits 1 unless every schema statement is in the generated header, so a script can
# gate on a partial translation without reading stderr. Without the flag the exit code is what it
# always was; the JSON reports the same `coverage` either way.
set -e

cli="$1"
sqlite3="$2"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

cat > "$dir/help.expected" <<'EOT'
sqlite2orm — sqlite2orm codegen (single SQL statement or .sqlite3 schema)

Usage:
  sqlite2orm -e <sql>           Codegen from a SQL string
  sqlite2orm --db <file.sqlite3> [--json] [--strict]
                                Full header from DB schema (phase 21)
  sqlite2orm <file.sql>         Read one statement from file
  sqlite2orm                    Read one statement from stdin

Options:
  --json                       With --db: print JSON decision points (stderr: diagnostics)
  --strict                     With --db: exit 1 unless every schema statement is in the
                               generated header (the coverage --json reports)
  --std <14|17|20|26>          Target C++ standard of the generated code (default: 20)
  -h, --help                   Show this help
EOT
"$cli" --help > "$dir/help.txt"
diff "$dir/help.expected" "$dir/help.txt"

# The whole schema in the header: exit 0 and a quiet stderr, with or without `--json`.
"$sqlite3" "$dir/ok.db" 'CREATE TABLE t (id INTEGER PRIMARY KEY, a INTEGER); CREATE INDEX ta ON t(a);'

"$cli" --db "$dir/ok.db" --json --strict > "$dir/ok.json" 2> "$dir/ok_err.txt"
cat > "$dir/ok.expected" <<'EOT'
{"coverage":{"generated":2,"total":2},"statements":[{"comments":[],"decisionPoints":[],"name":"t","ok":true,"tableName":"t","type":"table"},{"comments":[],"decisionPoints":[{"category":"column_ref_style","chosenCode":"&T::a","chosenValue":"member_pointer","id":1,"options":[{"code":"&T::a","comments":[],"description":"direct member pointer","hidden":false,"minCppStandard":14,"value":"member_pointer"},{"code":"column<T>(&T::a)","comments":[],"description":"explicit mapped type (inheritance / ambiguity)","hidden":false,"minCppStandard":14,"value":"column_pointer"}]}],"name":"ta","ok":true,"tableName":"t","type":"index"}],"targetCppStandard":20}
EOT
diff "$dir/ok.expected" "$dir/ok.json"
cat > "$dir/ok_err.expected" <<'EOT'
warning: sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs from serialized output
EOT
diff "$dir/ok_err.expected" "$dir/ok_err.txt"

"$cli" --db "$dir/ok.db" --strict > "$dir/ok.txt" 2> "$dir/ok_plain_err.txt"
"$cli" --db "$dir/ok.db" > "$dir/ok_lenient.txt" 2> "$dir/ok_lenient_err.txt"
diff "$dir/ok_lenient.txt" "$dir/ok.txt"
diff "$dir/ok_lenient_err.txt" "$dir/ok_plain_err.txt"

# A statement that does not generate, and an index resting on it: the header carries one row of
# three. That already exits 1; `--strict` adds the count to stderr.
"$sqlite3" "$dir/partial.db" 'CREATE TABLE t (id INTEGER PRIMARY KEY);
CREATE TABLE q (a INTEGER);
ALTER TABLE q ADD COLUMN b INTEGER AS (0x10000000000000000);
CREATE INDEX qa ON q(a);'

status=0
"$cli" --db "$dir/partial.db" --json --strict > "$dir/partial.json" 2> "$dir/partial_err.txt" || status=$?
test "$status" -eq 1
cat > "$dir/partial.expected" <<'EOT'
{"coverage":{"generated":1,"total":3},"statements":[{"comments":[],"decisionPoints":[],"name":"q","ok":false,"tableName":"q","type":"table"},{"comments":[],"decisionPoints":[],"name":"t","ok":true,"tableName":"t","type":"table"},{"comments":[],"decisionPoints":[{"category":"column_ref_style","chosenCode":"&Q::a","chosenValue":"member_pointer","id":1,"options":[{"code":"&Q::a","comments":[],"description":"direct member pointer","hidden":false,"minCppStandard":14,"value":"member_pointer"},{"code":"column<Q>(&Q::a)","comments":[],"description":"explicit mapped type (inheritance / ambiguity)","hidden":false,"minCppStandard":14,"value":"column_pointer"}]}],"name":"qa","ok":true,"tableName":"q","type":"index"}],"targetCppStandard":20}
EOT
diff "$dir/partial.expected" "$dir/partial.json"
cat > "$dir/partial_err.expected" <<'EOT'
warning: sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs from serialized output
codegen error [table q]: hex literal too big: 0x10000000000000000
strict: 1 of 3 schema statements generated
EOT
diff "$dir/partial_err.expected" "$dir/partial_err.txt"

# The same coverage is reported without `--strict`; only the count line is gone from stderr.
status=0
"$cli" --db "$dir/partial.db" --json > "$dir/partial_lenient.json" 2> /dev/null || status=$?
test "$status" -eq 1
diff "$dir/partial.expected" "$dir/partial_lenient.json"

# Every statement generates on its own, but a virtual table is never merged into make_storage() and
# the view over it goes with it. Without `--strict` that exits 0, as it always has; `--strict` is the
# only thing that fails it. Needs a sqlite3 shell built with FTS5 to store the schema at all.
if "$sqlite3" "$dir/fts.db" 'CREATE VIRTUAL TABLE ft USING fts5(a); CREATE VIEW vv AS SELECT a FROM ft;' 2> /dev/null; then
    "$cli" --db "$dir/fts.db" > /dev/null 2>&1

    status=0
    "$cli" --db "$dir/fts.db" --strict > /dev/null 2> "$dir/fts_err.txt" || status=$?
    test "$status" -eq 1
    cat > "$dir/fts_err.expected" <<'EOT'
warning: sqlite_orm serializes virtual tables as CREATE VIRTUAL TABLE IF NOT EXISTS; SQL without IF NOT EXISTS differs from serialized output
warning: view vv: type of column `a` could not be inferred; defaulting to int
warning: CREATE VIEW vv: sqlite_orm views use C++26 reflection (make_view + [[= "…"_orm_name]]); this code requires C++26 and will not compile under the selected C++ standard
warning: CREATE TABLE `ft_config` is an internal FTS5 table of virtual table `ft` and is not merged into make_storage()
warning: CREATE TABLE `ft_content` is an internal FTS5 table of virtual table `ft` and is not merged into make_storage()
warning: CREATE TABLE `ft_data` is an internal FTS5 table of virtual table `ft` and is not merged into make_storage()
warning: CREATE TABLE `ft_docsize` is an internal FTS5 table of virtual table `ft` and is not merged into make_storage()
warning: CREATE TABLE `ft_idx` is an internal FTS5 table of virtual table `ft` and is not merged into make_storage()
warning: CREATE VIRTUAL TABLE `ft` is not merged into make_storage(); run sqlite2orm on its SQL separately
warning: `vv` rests on a table that is not generated and is not merged into make_storage()
strict: 0 of 2 schema statements generated
EOT
    diff "$dir/fts_err.expected" "$dir/fts_err.txt"
fi
