# SQLite Syntax Coverage

Tracking parser coverage of SQLite syntax for the sqlite2orm tool.
Source: https://www.sqlite.org/syntaxdiagrams.html

Statuses:
- `[ ]` — not implemented
- `[~]` — partially implemented
- `[x]` — fully implemented
- `[!]` — not supported in sqlite_orm (validator error)

---

## Lexical (whitespace & BOM)

- [x] Space, tab, newline; form feed (`\f`) and vertical tab (`\v`) via `isspace`
- [x] Carriage return (`\r`) and CRLF (`\r\n`) as line breaks (line/column tracking)
- [x] Optional `\n\r` pairing (consume stray `\r` after `\n`)
- [x] Line comments `--` and block comments `/* */`
- [x] UTF-8 BOM (`EF BB BF`) skipped once at start of input (typical for editor/GUI paste)

**Public API:** `processSql(std::string_view)` in `sqlite2orm/process.h` — same pipeline for SQL from GUI/TUI, tests, or DDL extracted from a database file.

**Database introspection (phase 21.2):** `sqlite2orm/schema_reader.h` — `SqliteSchemaReader` (read-only open) exposes `sqlite_master` plus `PRAGMA table_xinfo`, `foreign_key_list`, `index_list`, `index_info`.

**Schema pipeline (phases 21.3–21.7):** `schema_process.h` (`processSqliteSchema`), `schema_header.h` (`generateSqliteSchemaHeader`), `codegen_policy.h` + `processSql(..., const CodeGenPolicy*)`, `json_emit.h` (`sqliteSchemaResultToJson`, via **nlohmann/json** single header downloaded at CMake configure). CLI: `sqlite2orm --db path.db` (header), `--db path.db --json`. Merged header uses `using namespace sqlite_orm` inside `make_sqlite_schema_storage`; `CREATE VIRTUAL TABLE` is warned and omitted from `make_storage`.

---

## expr (https://www.sqlite.org/lang_expr.html)

### Literals
- [x] numeric-literal (integer)
- [x] numeric-literal (real / float)
- [x] string-literal
- [x] blob-literal
- [x] NULL
- [x] TRUE
- [x] FALSE
- [x] CURRENT_TIME
- [x] CURRENT_DATE
- [x] CURRENT_TIMESTAMP

### References
- [x] column-name
- [x] table-name.column-name
- [x] schema-name.table-name.column-name

### Unary operators
- [x] `-` (unary minus)
- [!] `+` (unary plus — not in sqlite_orm, validator error)
- [x] `~` (bitwise NOT)
- [x] `NOT`

### Binary operators (arithmetic)
- [x] `+` (add)
- [x] `-` (subtract)
- [x] `*` (multiply)
- [x] `/` (divide)
- [x] `%` (modulo)

### Binary operators (bitwise)
- [x] `&` (bitwise AND)
- [x] `|` (bitwise OR)
- [x] `<<` (shift left)
- [x] `>>` (shift right)

### Binary operators (comparison)
- [x] `=`
- [x] `==`
- [x] `!=`
- [x] `<>`
- [x] `<`
- [x] `<=`
- [x] `>`
- [x] `>=`

### Binary operators (string / pattern)
- [x] `||` (concatenation)
- [x] `LIKE`
- [x] `LIKE ... ESCAPE`
- [x] `GLOB`
- [x] `NOT LIKE`
- [x] `NOT GLOB`

### Binary operators (logical)
- [x] `AND`
- [x] `OR`

### IS operators
- [x] `IS NULL`
- [x] `IS NOT NULL`
- [x] `ISNULL` (single keyword)
- [x] `NOTNULL` (single keyword)
- [x] `NOT NULL` (two keywords)
- [!] `IS expr` — sqlite_orm has no binary IS; validator error (codegen fallback: `==` with warning)
- [!] `IS NOT expr` — sqlite_orm has no binary IS NOT; validator error (codegen fallback: `!=` with warning)
- [!] `IS DISTINCT FROM expr` — not supported in sqlite_orm; validator error
- [!] `IS NOT DISTINCT FROM expr` — not supported in sqlite_orm; validator error

### Special operators
- [x] `BETWEEN expr AND expr`
- [x] `NOT BETWEEN expr AND expr`
- [x] `IN (expr-list)`
- [x] `IN (select-stmt)`
- [!] `IN table-name` (table-valued IN — not in sqlite_orm; validator error)
- [x] `NOT IN (expr-list)`
- [x] `NOT IN (select-stmt)` (via `InNode` + subquery)
- [x] `EXISTS (select-stmt)`
- [x] `NOT EXISTS (select-stmt)`

### CASE
- [x] `CASE WHEN cond THEN result ... ELSE ... END`
- [x] `CASE expr WHEN value THEN result ... ELSE ... END`
- [x] CASE without ELSE

### CAST
- [x] `CAST(expr AS type-name)`
- [x] Multi-word type names (`UNSIGNED BIG INT`)
- [x] Type with size spec (`VARCHAR(255)`, `DECIMAL(10, 2)`)

### Subquery expression
- [x] `(select-stmt)` as scalar subquery

### Parenthesized expression
- [x] `(expr)` — grouping

### Function call
- [x] `function-name(args)`
- [x] `function-name(DISTINCT arg)`
- [x] `function-name(*)`
- [x] `function-name()` — no args
- [x] `function-name(...) FILTER (WHERE expr)` → `.filter(where(...))` before `.over(...)` when present
- [x] `function-name() OVER window-name`
- [x] `function-name() OVER (window-defn)` — PARTITION BY, ORDER BY, ROWS|RANGE|GROUPS frame, EXCLUDE

### Collation
- [x] `expr COLLATE collation-name` (parsed as CollateNode; codegen passes through + warning)

### Trigger references
- [x] `NEW.column-name`
- [x] `OLD.column-name`

### RAISE
- [x] `RAISE(IGNORE)` / `RAISE(ABORT|FAIL|ROLLBACK, 'msg')` (trigger expressions; codegen `raise_*`)

### Bind parameters
- [!] `?` (parsed as BindParameterNode; validator error — use C++ variables)
- [!] `?NNN` (parsed as BindParameterNode; validator error)
- [!] `:name` (parsed as BindParameterNode; validator error)
- [!] `@name` (parsed as BindParameterNode; validator error)
- [!] `$name` (parsed as BindParameterNode; validator error)

---

## select-stmt (https://www.sqlite.org/lang_select.html)

### Core SELECT
- [x] SELECT result-columns → `storage.select(...)` / `storage.get_all<T>()`
- [x] SELECT DISTINCT → `distinct(...)`
- [x] SELECT ALL (parsed, no effect on codegen)
- [x] FROM table-or-subquery (single table)
- [x] FROM join-clause (INNER/LEFT/CROSS/NATURAL…, ON / USING)
- [x] WHERE expr → `where(condition)`
- [x] GROUP BY expr-list → `group_by(...)`
- [x] HAVING expr → `group_by(...).having(condition)`
- [x] WINDOW name AS (window-defn) → trailing `window("name", ...)` args to `select(...)` (sqlite_orm)
- [x] ORDER BY ordering-term → `order_by(&T::col).asc()` / `.desc()`
- [x] LIMIT expr → `limit(n)`
- [x] LIMIT expr OFFSET expr → `limit(n, offset(m))`

### Result columns
- [x] `*` → `get_all<T>()`
- [x] `table.*` → `asterisk<Struct>()`
- [x] `schema.table.*` → parsed; codegen `asterisk<Struct>()` + warning (schema qualifier not represented in sqlite_orm mapping)
- [x] expr → `select(expr)` / `select(columns(...))`
- [x] expr AS alias (parsed, alias stored in AST)

### Table or subquery
- [x] table-name
- [x] schema-name.table-name (FROM; codegen warning for schema)
- [x] table-name AS alias / table-name alias (alias map for `qual.col` codegen)
- [x] Comma-separated table-refs (parsed as implicit `CROSS JOIN`; codegen emits join chain)
- [!] (select-stmt) AS alias — subselect in FROM (not in sqlite_orm)
- [!] table-function-name(args) (parsed; validator error — not in sqlite_orm codegen)
- [x] (join-clause) — parenthesized join (parsed and flattened into plain join sequence)

### Join
- [x] JOIN ... ON expr
- [x] INNER JOIN
- [x] LEFT JOIN
- [x] LEFT OUTER JOIN
- [x] CROSS JOIN
- [x] NATURAL JOIN
- [x] NATURAL LEFT JOIN
- [x] NATURAL LEFT OUTER JOIN
- [x] NATURAL INNER JOIN
- [x] NATURAL CROSS JOIN
- [x] JOIN ... USING(column-list)

### Ordering term
- [x] expr
- [x] ASC
- [x] DESC
- [x] COLLATE collation-name (SELECT ORDER BY — extracted from CollateNode)
- [!] NULLS FIRST (parsed into `OrderByTerm::nulls`; validator error — not in sqlite_orm)
- [!] NULLS LAST (parsed into `OrderByTerm::nulls`; validator error — not in sqlite_orm)

### Compound SELECT
- [x] UNION
- [x] UNION ALL
- [x] INTERSECT
- [x] EXCEPT

### WITH (CTE)
- [x] WITH cte AS (select-stmt)
- [x] WITH RECURSIVE cte AS (select-stmt)
- [x] Multiple CTEs
- [x] CTE column names
- [x] AS MATERIALIZED / AS NOT MATERIALIZED → `.as<sqlite_orm::materialized()>()` / `.as<sqlite_orm::not_materialized()>()` in generated `with` (requires C++20 + `SQLITE_ORM_WITH_CPP20_ALIASES` in consuming project)
- [x] WITH … INSERT / REPLACE / UPDATE / DELETE → `storage.with(..., insert|replace|update_all|remove_all<...>(...))` when inner DML maps to sqlite_orm

### VALUES
- [x] VALUES(expr-list), (expr-list), ... (parsed as SelectNode; codegen generates `select(values(…))`)

---

## insert-stmt (https://www.sqlite.org/lang_insert.html)

- [x] INSERT INTO table (columns) VALUES (...)
- [x] INSERT INTO table (columns) SELECT ...
- [x] INSERT INTO table DEFAULT VALUES
- [x] INSERT OR ABORT
- [x] INSERT OR FAIL
- [x] INSERT OR IGNORE
- [x] INSERT OR REPLACE
- [x] INSERT OR ROLLBACK
- [x] REPLACE INTO (alias for INSERT OR REPLACE)
- [!] RETURNING clause (parsed into `InsertNode::returning`; validator error — not in sqlite_orm)

### UPSERT
- [x] ON CONFLICT DO NOTHING
- [x] ON CONFLICT (columns) DO NOTHING
- [x] ON CONFLICT (columns) DO UPDATE SET ...
- [x] ON CONFLICT (columns) WHERE expr DO UPDATE SET ... WHERE expr (conflict-target `WHERE` parsed; codegen warns — not in sqlite_orm `on_conflict()`)

---

## update-stmt (https://www.sqlite.org/lang_update.html)

- [x] UPDATE table SET col=expr, ... WHERE ...
- [x] UPDATE OR ABORT (parsed; codegen omits OR + warning)
- [x] UPDATE OR FAIL (parsed; codegen omits OR + warning)
- [x] UPDATE OR IGNORE (parsed; codegen omits OR + warning)
- [x] UPDATE OR REPLACE (parsed; codegen omits OR + warning)
- [x] UPDATE OR ROLLBACK (parsed; codegen omits OR + warning)
- [!] UPDATE ... FROM ... (SQLite 3.33+ — parsed; validator error; codegen warning)
- [!] RETURNING clause (parsed into `UpdateNode::returning`; validator error — not in sqlite_orm)

---

## delete-stmt (https://www.sqlite.org/lang_delete.html)

- [x] DELETE FROM table WHERE ...
- [x] DELETE FROM table (no WHERE)
- [!] RETURNING clause (parsed into `DeleteNode::returning`; validator error — not in sqlite_orm)

---

## create-table-stmt (https://www.sqlite.org/lang_createtable.html)

### Table
- [x] CREATE TABLE name (column-defs)
- [x] CREATE TABLE IF NOT EXISTS
- [x] Schema prefix (main.table_name)
- [x] Table constraints in column list (PK, UNIQUE, CHECK, FK)
- [x] WITHOUT ROWID → `make_table(...).without_rowid()`
- [!] STRICT (parsed; codegen warning — not in sqlite_orm)

### Column definition
- [x] column-name type-name
- [x] column-name (no type)
- [x] Multi-word type names (UNSIGNED BIG INT)
- [x] Type with size spec (VARCHAR(255))
- [x] Type with precision (DECIMAL(10, 2))

### Column constraints
- [x] PRIMARY KEY
- [x] PRIMARY KEY ASC (parsed, ASC/DESC skipped)
- [x] PRIMARY KEY DESC (parsed, ASC/DESC skipped)
- [x] PRIMARY KEY conflict-clause → `primary_key().on_conflict_XXX()`
- [x] PRIMARY KEY AUTOINCREMENT
- [x] NOT NULL
- [x] NOT NULL conflict-clause (parsed and skipped — `not_null_t` has no conflict clause support in sqlite_orm)
- [x] CONSTRAINT name prefix (parsed and skipped)
- [x] UNIQUE → `unique()`
- [x] UNIQUE conflict-clause (parsed; `unique_t` has no conflict clause support in sqlite_orm)
- [x] CHECK(expr) → `check(expr)` (column-level, full expression support)
- [x] DEFAULT signed-number → `default_value(val)`
- [x] DEFAULT literal-value → `default_value(val)`
- [x] DEFAULT (expr) → `default_value(expr)` (supports functions like `date('now')`)
- [x] DEFAULT CURRENT_TIME
- [x] DEFAULT CURRENT_DATE
- [x] DEFAULT CURRENT_TIMESTAMP
- [x] COLLATE NOCASE → `collate_nocase()`
- [x] COLLATE BINARY → `collate_binary()`
- [x] COLLATE RTRIM → `collate_rtrim()`
- [x] COLLATE custom (parsed; generates warning for non-built-in collations)
- [x] REFERENCES table(column) → `foreign_key().references()` (table-level constraint in sqlite_orm)
- [x] REFERENCES table (without column)
- [x] GENERATED ALWAYS AS (expr) → `generated_always_as(expr)`
- [x] GENERATED ALWAYS AS (expr) STORED → `.stored()`
- [x] GENERATED ALWAYS AS (expr) VIRTUAL → `.virtual_()`
- [x] AS (expr) — shorthand for generated column → `as(expr)`

### Table constraints
- [x] PRIMARY KEY (columns) → `primary_key(&T::a, &T::b)`
- [x] UNIQUE (columns) → `unique(&T::a, &T::b)`
- [x] CHECK(expr) → `check(expr)`
- [x] FOREIGN KEY (column) REFERENCES table(column) + ON DELETE/UPDATE actions
- [x] CONSTRAINT name prefix (parsed and skipped)

### Foreign key clause
- [x] REFERENCES table(columns) → `foreign_key().references()`
- [x] ON DELETE SET NULL → `.on_delete.set_null()`
- [x] ON DELETE SET DEFAULT → `.on_delete.set_default()`
- [x] ON DELETE CASCADE → `.on_delete.cascade()`
- [x] ON DELETE RESTRICT → `.on_delete.restrict_()`
- [x] ON DELETE NO ACTION → `.on_delete.no_action()`
- [x] ON UPDATE SET NULL → `.on_update.set_null()`
- [x] ON UPDATE SET DEFAULT → `.on_update.set_default()`
- [x] ON UPDATE CASCADE → `.on_update.cascade()`
- [x] ON UPDATE RESTRICT → `.on_update.restrict_()`
- [x] ON UPDATE NO ACTION → `.on_update.no_action()`
- [!] DEFERRABLE (parsed; codegen warning — not in sqlite_orm)
- [!] NOT DEFERRABLE (parsed; codegen warning — not in sqlite_orm)
- [!] INITIALLY DEFERRED (parsed; codegen warning)
- [!] INITIALLY IMMEDIATE (parsed; codegen warning)

### Conflict clause
- [x] ON CONFLICT ROLLBACK
- [x] ON CONFLICT ABORT
- [x] ON CONFLICT FAIL
- [x] ON CONFLICT IGNORE
- [x] ON CONFLICT REPLACE

---

## create-index-stmt (https://www.sqlite.org/lang_createindex.html)

- [x] CREATE INDEX name ON table(columns)
- [x] CREATE UNIQUE INDEX
- [x] CREATE INDEX IF NOT EXISTS
- [x] indexed-column: column-name
- [x] indexed-column: expr
- [x] indexed-column COLLATE collation-name
- [x] indexed-column ASC
- [x] indexed-column DESC
- [x] WHERE expr (partial index)
- [x] COLLATE / ASC|DESC in either order (SQLite-compatible loop)
- [!] SQL without `IF NOT EXISTS` vs sqlite_orm always emitting `IF NOT EXISTS` in serialized DDL (codegen warns)
- [!] schema-qualified index or `ON` table (parsed; codegen warns)

---

## create-virtual-table-stmt (https://www.sqlite.org/lang_createvtab.html)

- [x] CREATE VIRTUAL TABLE name USING module
- [x] CREATE VIRTUAL TABLE IF NOT EXISTS
- [x] CREATE TEMP / TEMPORARY VIRTUAL TABLE (parsed; codegen warns)
- [x] schema-qualified virtual table name (parsed; codegen warns)
- [x] module-arguments: comma-separated expressions
- [x] FTS5 — column list → `struct` + `make_virtual_table` + `using_fts5(make_column(...), ...)`
- [x] FTS5 — non-trivial arguments (codegen stub + warn)
- [x] RTREE / RTREE_I32 — valid simple column lists → `using_rtree` / `using_rtree_i32`
- [x] RTREE — wrong arity or non-column args (validator and/or codegen)
- [x] generate_series — empty args → `make_virtual_table<generate_series>(..., internal::using_generate_series())`
- [x] dbstat — `using_dbstat()` / `using_dbstat("schema")` from literal
- [x] unknown module — comment stub + warn
- [!] SQL without `IF NOT EXISTS` vs sqlite_orm serialized DDL (codegen warns)

---

## create-trigger-stmt (https://www.sqlite.org/lang_createtrigger.html)

- [x] CREATE TRIGGER name
- [x] CREATE TRIGGER IF NOT EXISTS (parsed; codegen warns — not in `make_trigger()`)
- [x] BEFORE
- [x] AFTER
- [x] INSTEAD OF
- [x] INSERT
- [x] DELETE
- [x] UPDATE
- [x] UPDATE OF column-list
- [x] ON table-name
- [x] FOR EACH ROW
- [x] WHEN expr
- [x] BEGIN ... END
- [x] trigger-body: UPDATE statement
- [x] trigger-body: INSERT statement
- [x] trigger-body: DELETE statement
- [x] trigger-body: SELECT statement
- [x] RAISE(IGNORE) / RAISE(ROLLBACK|ABORT|FAIL, 'msg') in expressions
- [x] TEMP/TEMPORARY TRIGGER (parsed; codegen warns)
- [x] schema-qualified trigger or ON table (parsed; codegen warns)

---

## create-view-stmt (https://www.sqlite.org/lang_createview.html)

- [!] CREATE VIEW (not in sqlite_orm)

---

## Window functions (https://www.sqlite.org/windowfunctions.html)

### Built-in window functions
- [x] row_number()
- [x] rank()
- [x] dense_rank()
- [x] percent_rank()
- [x] cume_dist()
- [x] ntile(N)
- [x] lag(expr, offset, default)
- [x] lead(expr, offset, default)
- [x] first_value(expr)
- [x] last_value(expr)
- [x] nth_value(expr, N)

### Window definition
- [x] PARTITION BY
- [x] ORDER BY (within window)
- [x] frame-spec (see below)

### Frame spec
- [x] ROWS BETWEEN ... AND ...
- [x] RANGE BETWEEN ... AND ...
- [x] GROUPS BETWEEN ... AND ...
- [x] UNBOUNDED PRECEDING
- [x] N PRECEDING
- [x] CURRENT ROW
- [x] N FOLLOWING
- [x] UNBOUNDED FOLLOWING
- [x] EXCLUDE CURRENT ROW
- [x] EXCLUDE GROUP
- [x] EXCLUDE TIES
- [x] EXCLUDE NO OTHERS (default; explicit form parsed and ignored)

### Named window
- [x] `OVER window-name` → `window_ref("...")`
- [x] `WINDOW name AS (...)` on SELECT → `window("name", ...)` in `storage.select(...)`

---

## Built-in functions (https://www.sqlite.org/lang_corefunc.html)

### Core scalar functions
- [x] abs(X)
- [x] changes()
- [x] char(X1,X2,...,XN)
- [x] coalesce(X,Y,...)
- [!] concat(X,...) — not in sqlite_orm (SQLite 3.44+)
- [!] concat_ws(SEP,X,...) — not in sqlite_orm (SQLite 3.44+)
- [!] format(FORMAT,...) — not in sqlite_orm (SQLite 3.38+)
- [x] glob(X,Y)
- [x] hex(X)
- [x] ifnull(X,Y)
- [x] iif(X,Y,Z)
- [x] instr(X,Y)
- [x] last_insert_rowid()
- [x] length(X)
- [x] like(X,Y)
- [x] like(X,Y,Z)
- [x] likelihood(X,P)
- [x] likely(X)
- [!] load_extension(X) — not in sqlite_orm
- [!] load_extension(X,Y) — not in sqlite_orm
- [x] lower(X)
- [x] ltrim(X)
- [x] ltrim(X,Y)
- [x] max(X,Y,...)
- [x] min(X,Y,...)
- [x] nullif(X,Y)
- [!] octet_length(X) — not in sqlite_orm (SQLite 3.43+)
- [x] printf(FORMAT,...)
- [x] quote(X)
- [x] random()
- [x] randomblob(N)
- [x] replace(X,Y,Z)
- [x] round(X)
- [x] round(X,Y)
- [x] rtrim(X)
- [x] rtrim(X,Y)
- [x] sign(X)
- [x] soundex(X)
- [!] sqlite_compileoption_get(N) — not in sqlite_orm
- [!] sqlite_compileoption_used(X) — not in sqlite_orm
- [!] sqlite_offset(X) — not in sqlite_orm
- [!] sqlite_source_id() — not in sqlite_orm
- [!] sqlite_version() — not in sqlite_orm
- [x] substr(X,Y)
- [x] substr(X,Y,Z)
- [x] substring(X,Y)
- [x] substring(X,Y,Z)
- [x] total_changes()
- [x] trim(X)
- [x] trim(X,Y)
- [x] typeof(X)
- [!] unhex(X) — not in sqlite_orm (SQLite 3.41+)
- [!] unhex(X,Y) — not in sqlite_orm (SQLite 3.41+)
- [x] unicode(X)
- [x] unlikely(X)
- [x] upper(X)
- [x] zeroblob(N)

### Aggregate functions
- [x] avg(X)
- [x] count(X)
- [x] count(*)
- [x] group_concat(X)
- [x] group_concat(X,Y)
- [x] max(X)
- [x] min(X)
- [x] sum(X)
- [x] total(X)
- [!] string_agg(X,Y) — not in sqlite_orm (SQLite 3.44+)

### Date/time functions
- [x] date(time-value, modifier, ...)
- [x] time(time-value, modifier, ...)
- [x] datetime(time-value, modifier, ...)
- [x] julianday(time-value, modifier, ...)
- [!] unixepoch(time-value, modifier, ...) — not in sqlite_orm (SQLite 3.38+)
- [x] strftime(format, time-value, modifier, ...)
- [!] timediff(time-value, time-value) — not in sqlite_orm (SQLite 3.43+)

### Math functions (SQLITE_ENABLE_MATH_FUNCTIONS)
- [x] acos(X)
- [x] acosh(X)
- [x] asin(X)
- [x] asinh(X)
- [x] atan(X)
- [x] atanh(X)
- [x] atan2(Y,X)
- [x] ceil(X) / ceiling(X)
- [x] cos(X)
- [x] cosh(X)
- [x] degrees(X)
- [x] exp(X)
- [x] floor(X)
- [x] ln(X)
- [x] log(X)
- [x] log(B,X)
- [x] log2(X)
- [x] log10(X)
- [x] mod(X,Y)
- [x] pi()
- [x] pow(X,Y) / power(X,Y)
- [x] radians(X)
- [x] sin(X)
- [x] sinh(X)
- [x] sqrt(X)
- [x] tan(X)
- [x] tanh(X)
- [x] trunc(X)

### JSON functions (SQLITE_ENABLE_JSON1 / built-in since 3.38)
- [x] json(json)
- [x] json_array(value1, ...)
- [x] json_array_length(json)
- [x] json_array_length(json, path)
- [x] json_extract(json, path, ...)
- [x] json_insert(json, path, value, ...)
- [x] json_object(label1, value1, ...)
- [x] json_patch(json1, json2)
- [x] json_remove(json, path, ...)
- [x] json_replace(json, path, value, ...)
- [x] json_set(json, path, value, ...)
- [x] json_type(json)
- [x] json_type(json, path)
- [x] json_valid(json)
- [x] json_quote(value)
- [x] json_group_array(value)
- [x] json_group_object(name, value)
- [x] json_each(json)
- [x] json_each(json, path)
- [x] json_tree(json)
- [x] json_tree(json, path)
- [x] `->` operator → `json_extract(lhs, rhs)` (codegen warning: return type may differ)
- [x] `->>` operator → `json_extract(lhs, rhs)` (codegen warning: return type may differ)

---

## Constructs not supported by sqlite_orm

Legend: **`[!]`** — no sqlite_orm mapping (validator error, or warning where
noted). **`[x]`** — sqlite2orm emits sqlite_orm API for this construct. The
parser recognizes everything listed; this section tracks **downstream** support.

- [!] CREATE VIEW
- [!] Subselect in FROM
- [!] NULLS FIRST / NULLS LAST
- [!] RETURNING clause
- [!] Unary plus (`+expr`)
- [x] DROP TABLE — `storage.drop_table("name")` / `storage.drop_table_if_exists("name")`
- [x] DROP INDEX — `storage.drop_index("name")` / `storage.drop_index_if_exists("name")`
- [x] DROP TRIGGER — `storage.drop_trigger("name")` / `storage.drop_trigger_if_exists("name")`
- [x] VACUUM — `storage.vacuum()`
- [!] ALTER TABLE ADD COLUMN (runtime — `sync_schema()` in sqlite_orm)
- [!] ALTER TABLE RENAME (parsed; validator warns about `sync_schema()`)
- [!] ATTACH DATABASE (parsed as `AttachDatabaseNode`; validator error)
- [!] DETACH DATABASE (parsed as `DetachDatabaseNode`; validator error)
- [!] ANALYZE (parsed as `AnalyzeNode`; validator error)
- [!] REINDEX (parsed as `ReindexNode`; validator error)
- [!] EXPLAIN (parsed as `ExplainNode`; validator error)
- [!] EXPLAIN QUERY PLAN (parsed as `ExplainNode`; validator error)
- [x] PRAGMA — parsed as `PragmaNode`; supported names map to `storage.pragma` in sqlite_orm (`journal_mode`, `locking_mode`, `user_version`, `synchronous`, `application_id`, `busy_timeout`, `auto_vacuum`, `max_page_count`, `recursive_triggers`, `module_list`, `quick_check`, `integrity_check`, `table_info`, `table_xinfo`); schema-qualified `PRAGMA main.xxx` is a validator error; other pragma names are validator errors
- [!] SAVEPOINT (parsed as `SavepointNode`; validator error)
- [!] RELEASE (parsed as `ReleaseNode`; validator error)
- [!] snippet() — FTS5 (not in sqlite_orm)
- [!] bm25() — FTS5 (not in sqlite_orm)
- [!] concat(), concat_ws(), format() — not in sqlite_orm (SQLite 3.38+/3.44+)
- [!] load_extension() — not in sqlite_orm
- [!] octet_length(), unhex() — not in sqlite_orm (SQLite 3.41+/3.43+)
- [!] sqlite_version(), sqlite_source_id(), sqlite_compileoption_*(), sqlite_offset() — not in sqlite_orm
- [!] string_agg() — not in sqlite_orm (SQLite 3.44+)
- [!] unixepoch(), timediff() — not in sqlite_orm (SQLite 3.38+/3.43+)
