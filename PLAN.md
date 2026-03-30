# sqlite2orm — SQLite SQL → sqlite_orm C++ Code Generator

## What is this

A CLI tool that reads an existing SQLite database and generates C++ code
(structs + `make_storage()`) for use with the
[sqlite_orm](https://github.com/fnc12/sqlite_orm) library.

Related issue: https://github.com/fnc12/sqlite_orm/issues/450

## Location

Today the project lives in the `sqlite2orm/` folder at the root of the
[sqlite_orm](https://github.com/fnc12/sqlite_orm) repository. Upstream
`sqlite_orm` library sources (`dev/`, `tests/`, `include/` in that repo) are not
edited for sqlite2orm work.

**Standalone repository:** `sqlite2orm/` is intended to move into its own Git
repository (same layout: library + CLI + examples + docs). After the move,
point CI and consumers at the new repo; keep a dependency on sqlite_orm
(headers / version) via CMake `FetchContent`, submodule, or installed package.

## Architecture

```
SQL string
    │
    ▼
┌──────────┐
│ Tokenizer│  recognizes all SQLite SQL tokens
└────┬─────┘
     │ vector<Token>
     ▼
┌──────────┐
│  Parser  │  recursive descent, builds a clean AST
└────┬─────┘
     │ AST
     ▼
┌──────────┐
│ Validator│  checks that every AST node maps to sqlite_orm
└────┬─────┘  (error: "subselect in FROM not supported", etc.)
     │ validated AST
     ▼
┌──────────┐
│ CodeGen  │  walks the AST, produces C++ code
└────┬─────┘  order identical to input SQL
     │        decision points with per-node alternatives
     ▼
C++ source code + decision point annotations
```

## Code conventions

- **string_view**: use `std::string_view` everywhere instead of `const std::string&`
- **optional**: return `std::optional<T>` instead of `bool tryGet(T& out)` pattern
- **No trailing underscores** on member fields; access via `this->field` (same style as sqlite_orm)
- **Avoid meaningless abbreviations**: `AstNodePointer` not `AstNodePtr`, `token` not `tok`, `location` not `loc`
- **Variable naming**: name variables as lowercase of their type — `AstNodePointer astNodePointer` not `AstNodePointer ast`; `Tokenizer tokenizer` not `Tokenizer t`
- **Prefer `std::vector::at()`** over `operator[]` to avoid UB on out-of-bounds access
- **Struct/class formatting**: separate fields, constructors, and operators with blank lines inside struct/class bodies
- **C++20** minimum (string_view, optional, variant, structured bindings, defaulted comparisons)
- **SQLite vs sqlite_orm**: parse SQLite constructs faithfully into the AST wherever we implement that part of the grammar; if sqlite_orm cannot express it, **codegen emits `CodeGenResult::warnings`** (or the validator reports an error when generation is impossible)—never silently ignore parsed SQL
- **catch2 v3** for tests (via FetchContent)
- **CMake FetchContent**: print messages before and after downloading dependencies
- **External dependencies** are fine when needed (e.g., fmt, nlohmann::json, TUI library, etc.)
- **Enum cases in camelCase**: `BinaryOperator::equals`, `TokenType::integerLiteral`, `UnaryOperator::logicalNot`. Use descriptive names when a plain camelCase form collides with a C++ keyword (`or`, `and`, `not` → `logicalOr`, `logicalAnd`, `logicalNot`)

## Decision point system (alternatives)

Many SQL constructs have multiple equivalent representations in sqlite_orm.
Instead of a flat list of N*M alternatives, we use **per-node decision points**.

Each decision point is a location in the output code where a choice exists.
The primary output uses defaults; each point is annotated with alternatives.

### Decision point categories

**1. Expression style** (affects all expressions globally)
- `operator_wrap_left`: `c(&User::id) > 5` ← default
- `operator_wrap_right`: `&User::id > c(5)`
- `functional`: `greater_than(&User::id, 5)`
- `functional_short`: `gt(&User::id, 5)`

**2. API level** (context-dependent)
- `SELECT * FROM t` → `get_all<T>()` / `select(object<T>())` / `select(asterisk<T>())`
- `SELECT cols FROM t` → `select(columns(...))`
- `SELECT * FROM t WHERE ...` → `get_all<T>(where(...))` / `select(object<T>(), where(...))`

**3. Column reference style** (depends on inheritance)
- `&User::id` ← default
- `column<User>(&User::id)` — needed when inheritance or ambiguity is present

### Output format

```cpp
/*[dp:1 api_level]*/ auto users = storage.get_all<User>(
    /*[dp:2 expr_style]*/ where(c(&User::id) > 5)
);
```

A JSON file is generated alongside with descriptions for each decision point:
```json
{
  "decisionPoints": [
    {
      "id": 1,
      "category": "api_level",
      "chosen": "get_all",
      "alternatives": [
        {"value": "select_object", "code": "storage.select(object<User>(), ...)"},
        {"value": "select_asterisk", "code": "storage.select(asterisk<User>(), ...)"}
      ]
    }
  ]
}
```

---

## Development phases

Each phase = minimal working increment + tests + commit.
Each phase adds exactly one new AST node or construct.

Statuses: `[ ]` not started, `[~]` in progress, `[x]` done

### Phase 0: Infrastructure
- [x] 0.1: Project structure: CMakeLists.txt, directories src/, tests/, include/
- [x] 0.2: Tokenizer — all SQLite tokens (keywords, operators, literals, identifiers)
- [x] 0.3: Base AST framework: AstNode base
- [x] 0.4: Parser skeleton (accepts tokens, returns AST)
- [x] 0.5: CodeGen skeleton with decision point infrastructure
- [x] 0.6: Validator skeleton
- [x] 0.7: Test harness with catch2 (19 test cases, 170 assertions)

### Phase 1: Literals (one node per sub-phase)
- [x] 1.1: `IntegerLiteral` — `42`, `0`, `0xFF`
- [x] 1.2: `RealLiteral` — `3.14`, `.5`, `1e10`
- [x] 1.3: `StringLiteral` — `'hello'`, `'it''s'`
- [x] 1.4: `NullLiteral` — `NULL`
- [x] 1.5: `BoolLiteral` — `TRUE`, `FALSE`
- [x] 1.6: `BlobLiteral` — `X'48656C6C6F'`

### Phase 2: Column references (one node per sub-phase)
- [x] 2.1: `ColumnRef` — `column_name` (unqualified)
- [x] 2.2: `QualifiedColumnRef` — `table_name.column_name`
- [x] 2.3: `NewRef` — `NEW.column_name` (in triggers)
- [x] 2.4: `OldRef` — `OLD.column_name` (in triggers)

### Phase 3: Binary comparison operators (one per sub-phase)
- [x] 3.1: `Equals` — `=`
- [x] 3.2: `NotEquals` — `!=`, `<>`
- [x] 3.3: `LessThan` — `<`
- [x] 3.4: `LessOrEqual` — `<=`
- [x] 3.5: `GreaterThan` — `>`
- [x] 3.6: `GreaterOrEqual` — `>=`
- [x] 3.7: Decision point: expression style variants (first dp!)

### Phase 4: Arithmetic, string, and bitwise operators
- [x] 4.1: `Add` — `+`
- [x] 4.2: `Subtract` — `-`
- [x] 4.3: `Multiply` — `*`
- [x] 4.4: `Divide` — `/`
- [x] 4.5: `Modulo` — `%`
- [x] 4.6: `Concatenate` — `||`
- [x] 4.7: `UnaryMinus` — `-expr`
- [x] 4.8: `UnaryPlus` — `+expr` (not in sqlite_orm — validator error)
- [x] 4.9: `BitwiseAnd` — `&`
- [x] 4.10: `BitwiseOr` — `|`
- [x] 4.11: `BitwiseNot` — `~`
- [x] 4.12: `BitwiseShiftLeft` — `<<`
- [x] 4.13: `BitwiseShiftRight` — `>>`

### Phase 5: Logical operators
- [x] 5.1: `And` — `AND`
- [x] 5.2: `Or` — `OR`
- [x] 5.3: `Not` — `NOT`
- [x] 5.4: Precedence tests: `a = 1 OR b = 2 AND c = 3`

### Phase 6: Special operators (one per sub-phase)
- [x] 6.1: `IsNull` — `IS NULL`, `ISNULL`
- [x] 6.2: `IsNotNull` — `IS NOT NULL`, `NOTNULL`, `NOT NULL`
- [x] 6.3: `Between` — `BETWEEN ... AND ...`, `NOT BETWEEN ... AND ...`
- [x] 6.4: `In` with value list — `IN (1, 2, 3)`
- [x] 6.5: `NotIn` with value list — `NOT IN (1, 2, 3)` — decision point: `not_in()` vs `not_(in())`
- [x] 6.6: `Like` — `LIKE pattern`, `NOT LIKE pattern`
- [x] 6.7: `Like` with ESCAPE — `LIKE pattern ESCAPE char`
- [x] 6.8: `Glob` — `GLOB pattern`, `NOT GLOB pattern`

### Phase 7: Functions
- [x] 7.1: `FunctionCall` — basic call: `name(args...)`, no-arg, multi-arg
- [x] 7.2: Known sqlite_orm function table (80+ functions in validator)
- [x] 7.3: Aggregate functions: `count`, `sum`, `avg`, `min`, `max`, `total`, `group_concat`
- [x] 7.4: `count(*)` — `count()` (no args in C++)
- [x] 7.5: `count(DISTINCT expr)` — `count(distinct(expr))`
- [x] 7.6: Date/time: `date()`, `time()`, `datetime()`, `julianday()`, `strftime()`
- [x] 7.7: JSON: `json()`, `json_extract()`, `json_array()`, etc.
- [x] 7.8: Math: `abs()`, `round()`, `acos()`, `sin()`, `sqrt()`, etc.
- [x] 7.9: String: `length()`, `lower()`, `upper()`, `substr()`, `trim()`, `replace()`, `instr()`, `quote()`, `hex()`
- [x] 7.10: Null handling: `coalesce()`, `ifnull()`, `nullif()`
- [x] 7.11: Other: `typeof()`, `random()`, `randomblob()`, `unicode()`, `zeroblob()`
- [x] 7.12: FTS5: `highlight()`, `match()`, `rank()`
- [x] 7.13: Validator: unknown function → error
- [x] 7.14: Parenthesized expressions `(expr)` — proper grouping
- [x] 7.15: Keyword-as-function: `replace(...)`, `like(...)`, `glob(...)`, `match(...)`

### Phase 8: CAST and CASE
- [x] 8.1: `Cast` — `CAST(expr AS type)` → `cast<CppType>(expr)` with SQLite affinity type mapping
- [x] 8.2: Searched CASE — `CASE WHEN cond THEN result ... ELSE default END` → `case_<>().when(cond, then(result)).else_(default).end()`
- [x] 8.3: Simple CASE — `CASE expr WHEN value THEN result ... END` → `case_<>(expr).when(value, then(result)).end()`

### Phase 9: CREATE TABLE
- [x] 9.1: `CreateTable` — base structure: name, columns, IF NOT EXISTS, schema prefix
- [x] 9.2: `ColumnDef` — name, type (multi-word types, size specs like VARCHAR(255), DECIMAL(10,2))
- [x] 9.3: Type affinity mapping: SQLite type → C++ type (reuses sqlite_type_to_cpp)
- [x] 9.4: `PrimaryKeyColumnConstraint` — column-level PRIMARY KEY
- [x] 9.5: `AutoincrementConstraint` — AUTOINCREMENT
- [x] 9.6: `NotNullConstraint` — NOT NULL → removes std::optional, non-optional field
- [x] 9.7: `DefaultConstraint` — DEFAULT value (literal, signed number, parenthesized expression incl. functions)
- [x] 9.8: `UniqueColumnConstraint` — UNIQUE with conflict clause (parsed; conflict clause not in sqlite_orm for unique)
- [x] 9.9: `CheckColumnConstraint` — column-level CHECK(expr) → `check(expr)` (reuses expression parser/codegen)
- [x] 9.10: `CollateConstraint` — COLLATE NOCASE/BINARY/RTRIM → `collate_nocase()`/`collate_binary()`/`collate_rtrim()` (custom collations generate warning)
- [x] 9.11: `ForeignKeyColumnConstraint` — column-level FK: REFERENCES table(col) + ON DELETE/UPDATE actions → `foreign_key().references()` (table-level in sqlite_orm)
- [x] 9.12: `GeneratedConstraint` — GENERATED ALWAYS AS (expr) STORED|VIRTUAL → `generated_always_as(expr).stored()` / `.virtual_()` / `as(expr)` shorthand
- [x] 9.13: `PrimaryKeyTableConstraint` — table-level PRIMARY KEY(cols) → `primary_key(&T::a, &T::b)`
- [x] 9.14: `UniqueTableConstraint` — table-level UNIQUE(cols) → `unique(&T::a, &T::b)`
- [x] 9.15: `CheckTableConstraint` — table-level CHECK(expr) → `check(expr)`
- [x] 9.16: `ForeignKeyTableConstraint` — table-level FOREIGN KEY(col) REFERENCES table(col) + actions
- [x] 9.17: `WithoutRowid` — WITHOUT ROWID → `make_table(...).without_rowid()`
- [x] 9.18: On conflict: ABORT/FAIL/IGNORE/REPLACE/ROLLBACK for PK/UNIQUE (PK conflict implemented; UNIQUE conflict parsed + warning)
- [x] 9.19: FK actions: ON UPDATE/DELETE CASCADE/SET NULL/SET DEFAULT/RESTRICT/NO ACTION
- [x] 9.20: C++ struct + `make_storage()` + `make_table()` + `make_column()` generation (struct name capitalized from table name)
- [x] 9.21: `std::optional<T>` generation for nullable columns (all columns nullable by default before constraints)
- [x] 9.22: Decision point: column ref style

### Phase 10: Basic SELECT
- [x] 10.1: `Select` — `SELECT columns FROM table` → `storage.select(columns(...))`
- [x] 10.2: `Asterisk` — `SELECT *` → `storage.get_all<T>()`
- [x] 10.3: `QualifiedAsterisk` — `SELECT table.*` → `asterisk<Struct>()`
- [x] 10.4: `Where` clause → `where(condition)`
- [x] 10.5: `OrderBy` — ORDER BY col ASC|DESC → `order_by(&T::col).asc()` / `.desc()`
- [x] 10.6: `Limit` + `Offset` → `limit(n)` / `limit(n, offset(m))`
- [x] 10.7: `GroupBy` → `group_by(&T::col)`
- [x] 10.8: `Having` → `group_by(...).having(condition)`
- [x] 10.9: `Distinct` — SELECT DISTINCT → `distinct(...)` / `distinct(columns(...))`
- [x] 10.10: Column aliases — `SELECT col AS alias` (parsed, alias stored in AST)
- [x] 10.11: Table aliases — `FROM table AS alias` / `FROM table alias`; `FROM schema.table`; comma-separated tables parsed + codegen warning for multi-table FROM
- [x] 10.12: Decision point: api level (get_all vs select)

### Phase 11: JOIN
- [x] 11.1: `InnerJoin` — INNER JOIN ... ON
- [x] 11.2: `LeftJoin` — LEFT JOIN ... ON
- [x] 11.3: `LeftOuterJoin` — LEFT OUTER JOIN ... ON
- [x] 11.4: `CrossJoin` — CROSS JOIN
- [x] 11.5: `NaturalJoin` — NATURAL JOIN / NATURAL INNER JOIN
- [x] 11.6: `Join` — plain JOIN ... ON
- [x] 11.7: `Using` clause — JOIN ... USING(col); multi-column USING → `on` with equality chain

### Phase 12: Subquery
- [x] 12.1: `In` with subselect — `IN (SELECT ...)` (parsed + codegen via `in(..., select(...))`)
- [x] 12.2: `NotIn` with subselect
- [x] 12.3: `Exists` — `EXISTS(SELECT ...)`
- [x] 12.4: `NotExists` — `NOT EXISTS(SELECT ...)`
- [x] 12.5: Scalar subquery as expression — `(SELECT ...)` (e.g. in comparison or as primary)
- [x] 12.6: Subselect in comparison — `WHERE col > (SELECT ...)`
- [x] 12.7: Subselect in FROM — parsed into `FromTableClause.derivedSelect`; validator error; codegen stub + warning

### Phase 13: UNION / INTERSECT / EXCEPT
- [x] 13.1: `Union` — UNION
- [x] 13.2: `UnionAll` — UNION ALL
- [x] 13.3: `Intersect` — INTERSECT
- [x] 13.4: `Except` — EXCEPT

### Phase 14: Window functions
- [x] 14.1: `Over` clause — `func() OVER (...)` / `OVER name`
- [x] 14.2: `PartitionBy` — PARTITION BY
- [x] 14.3: Window ORDER BY
- [x] 14.4: Frame spec: `Rows`, `Range`, `Groups`
- [x] 14.5: Frame bounds: `UnboundedPreceding`, `Preceding(n)`, `CurrentRow`, `Following(n)`, `UnboundedFollowing`
- [x] 14.6: Frame exclude: `ExcludeCurrentRow`, `ExcludeGroup`, `ExcludeTies`
- [x] 14.7: Window functions: `row_number`, `rank`, `dense_rank`, `percent_rank`, `cume_dist`, `ntile`
- [x] 14.8: Window functions: `lag`, `lead`, `first_value`, `last_value`, `nth_value`
- [x] 14.9: Named windows — `WINDOW name AS (...)` on SELECT → trailing `window("name", ...)` in `storage.select(...)`
- [x] 14.10: `FILTER (WHERE expr)` — `.filter(where(...))` before `.over(...)` when both present

### Phase 15: CTE
- [x] 15.1: `With` — WITH cte AS (...)
- [x] 15.2: `WithRecursive` — WITH RECURSIVE
- [x] 15.3: Multiple CTEs — WITH a AS (...), b AS (...)
- [x] 15.4: CTE column names — WITH cte(x, y) AS (...)
- [x] 15.5: `AS MATERIALIZED` / `AS NOT MATERIALIZED` → `.as<sqlite_orm::materialized()>()` / `not_materialized()` (+ C++20 / `SQLITE_ORM_WITH_CPP20_ALIASES` warning)
- [x] 15.6: `WITH` + INSERT / REPLACE / UPDATE / DELETE → `storage.with(..., insert|update_all|remove_all<...>(...))` (inner call without `storage.`)
- [x] 15.7: Single-source `FROM cte` — bare columns → `column<cte_N>("col")`

### Phase 16: DML
- [x] 16.1: `Insert` — INSERT INTO table (cols) VALUES (...)
- [x] 16.2: `InsertOrConflict` — INSERT OR IGNORE/REPLACE/ABORT/FAIL/ROLLBACK
- [x] 16.3: Insert from SELECT — INSERT INTO ... SELECT ...
- [x] 16.4: `DefaultValues` — INSERT INTO table DEFAULT VALUES
- [x] 16.5: `Update` — UPDATE table SET col = expr WHERE ...
- [x] 16.6: `UpdateOrConflict` — UPDATE OR IGNORE/REPLACE/... (parsed; codegen `update_all` + warning: OR not in sqlite_orm)
- [x] 16.7: `Delete` — DELETE FROM table WHERE ...

### Phase 17: UPSERT
- [x] 17.1: `OnConflict` + `DoNothing`
- [x] 17.2: `OnConflict` + `DoUpdate` + `Set`
- [x] 17.3: `Excluded` — excluded(col) in DO UPDATE
- [x] 17.4: ON CONFLICT with WHERE

### Phase 18: CREATE TRIGGER
- [x] 18.1: `CreateTrigger` — base structure
- [x] 18.2: Timing: BEFORE / AFTER / INSTEAD OF
- [x] 18.3: Event: INSERT / DELETE
- [x] 18.4: Event: UPDATE / UPDATE OF(cols)
- [x] 18.5: WHEN clause
- [x] 18.6: Trigger body — list of statements
- [x] 18.7: `Raise` — RAISE(IGNORE), RAISE(ABORT, msg), RAISE(FAIL, msg), RAISE(ROLLBACK, msg)
- [x] 18.8: FOR EACH ROW

### Phase 19: CREATE INDEX
- [x] 19.1: `CreateIndex` — CREATE INDEX name ON table(cols)
- [x] 19.2: `CreateUniqueIndex` — CREATE UNIQUE INDEX
- [x] 19.3: Indexed column with ASC/DESC
- [x] 19.4: Indexed column with COLLATE
- [x] 19.5: Partial index — WHERE clause
- [x] 19.6: Indexed column with expression (instead of column name)

### Phase 20: Virtual tables
- [x] 20.1: `CreateVirtualTable` — CREATE VIRTUAL TABLE ... USING module(args)
- [x] 20.2: FTS5 — USING fts5(...)
- [x] 20.3: RTree — USING rtree(...)
- [x] 20.4: Other modules — generate_series, dbstat

### Phase 21: Full integration
- [x] 21.0: `processSql(std::string_view)` — primary entry for **SQL text** (TUI/GUI/tests); reading `.sqlite3` (21.2–21.4) only feeds the same DDL strings into this pipeline
- [x] 21.1: CLI: raw SQL via `-e`, file path, or stdin; separate `sqlite2orm_lib` + `sqlite2orm_cli` (binary `sqlite2orm`); link GUI apps to `sqlite2orm::sqlite2orm`
- [x] 21.2: Read sqlite_master + PRAGMA table_xinfo + PRAGMA foreign_key_list + PRAGMA index_list/index_info
- [x] 21.3: Parse DDL from sqlite_master.sql
- [x] 21.4: Generate full .h file: structs + make_storage()
- [x] 21.5: Decision point config: expression style, api level, column ref
- [x] 21.6: JSON output with decision points
- [x] 21.7: Roundtrip tests: parse → codegen → compile → serialize → compare

### Phase 22: Full SQL grammar coverage
- [x] 22.1: NULLS FIRST / NULLS LAST — `OrderByTerm::nulls` field, stored in AST, validator error
- [x] 22.2: RETURNING clause — `ReturningColumn` struct in `InsertNode`, `UpdateNode`, `DeleteNode`; validator error
- [x] 22.3: SAVEPOINT name — `SavepointNode`; validator error
- [x] 22.4: RELEASE [SAVEPOINT] name — `ReleaseNode`; validator error
- [x] 22.5: ATTACH [DATABASE] expr AS schema — `AttachDatabaseNode`; validator error
- [x] 22.6: DETACH [DATABASE] schema — `DetachDatabaseNode`; validator error
- [x] 22.7: ANALYZE [schema.table] — `AnalyzeNode`; validator error
- [x] 22.8: REINDEX [schema.object] — `ReindexNode`; validator error
- [x] 22.9: PRAGMA [schema.]name [= value | (value)] — `PragmaNode`; names implemented on `sqlite_orm::storage::pragma` (`pragma_t`, see upstream `dev/pragma.h`) pass validation and codegen as `storage.pragma.*()`; schema-qualified `PRAGMA main.xxx` and other pragma names → validator error
- [x] 22.10: EXPLAIN [QUERY PLAN] statement — `ExplainNode` wraps inner statement; validator error

### Phase 23: Open-source release
- [x] 23.1: Replace `SQLITE2ORM_TEST_SQLITE_ORM_INCLUDE` with FetchContent sqlite_orm
- [x] 23.2: Add MIT LICENSE
- [x] 23.3: Add README.md
- [x] 23.4: Add .gitignore
- [x] 23.5: Add examples/ (6 examples)
- [x] 23.6: Add GitHub Actions CI

### Phase 24: Standalone repository (optional)

- [ ] Move `sqlite2orm/` into a dedicated Git repository (same CMake layout).
  Keep consuming **sqlite_orm** via `FetchContent`, submodule, or `find_package`
  as a declared dependency; update CI badges and install docs accordingly.

---

## Not supported in sqlite_orm (validator errors)

These constructs are parsed into the AST but **sqlite_orm has no usable mapping**
(or sqlite2orm does not emit it yet). The validator reports an error with node
type and source location (unless noted as warning-only):

- Subselect in FROM (`FromTableClause.derivedSelect`; codegen stub)
- NULLS FIRST / NULLS LAST (`OrderByTerm::nulls`)
- RETURNING (`InsertNode` / `UpdateNode` / `DeleteNode`)
- Unary plus (`+expr`)
- `snippet()`, `bm25()` (FTS5); some newer SQLite built-ins
- SAVEPOINT / RELEASE
- ATTACH DATABASE / DETACH DATABASE
- ANALYZE / REINDEX
- **PRAGMA** — only *unsupported* pragma names (and schema-qualified
  `PRAGMA schema.name`) are errors; pragmas wrapped by `sqlite_orm::pragma_t`
  are validated and codegen’d — see `pragma_sqlite_orm.cpp`, `codegen.cpp`, and
  [COVERAGE.md](COVERAGE.md)
- EXPLAIN / EXPLAIN QUERY PLAN

---

## SQLite syntax coverage

See [COVERAGE.md](COVERAGE.md) for full SQLite syntax coverage tracking.
Source: https://www.sqlite.org/syntaxdiagrams.html

---

## How to work with this plan in a new context

Phases 1–23 above are **complete** (`[x]`). Further work is driven by
[COVERAGE.md](COVERAGE.md), issues, and sqlite_orm API changes — not by the
numbered phase list.

For a new feature or grammar gap:
1. Read `sqlite2orm/PLAN.md` (conventions + architecture)
2. Read `sqlite2orm/COVERAGE.md` for the relevant construct
3. Implement: tokenizer (if needed) + parser + AST + validator + codegen + tests
4. Update COVERAGE.md (and this file if you add a new numbered phase)
5. Commit

All sqlite2orm code lives in `sqlite2orm/`. Do not modify sqlite_orm library
sources in the parent repo except when fixing upstream bugs unrelated to
sqlite2orm.
