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

**Schema pipeline (phases 21.3–21.7):** `schema_process.h` (`processSqliteSchema`), `schema_header.h` (`generateSqliteSchemaHeader`), `codegen_policy.h` + `processSql(..., const CodeGenPolicy*)`, `json_emit.h` (`sqliteSchemaResultToJson`, via **nlohmann/json** single header downloaded at CMake configure). CLI: `sqlite2orm --db path.db` (header), `--db path.db --json`; both render through `schema_report.h` (`reportSqliteSchema`), so a schema with a statement that did not generate prints its diagnostics on stderr and exits 1 in either mode. That statement is left out of `make_storage()` with a warning rather than taking the header with it — SQLite only compiles a view or trigger body when the object is used, so it stores bodies sqlite2orm refuses — and the name it created joins the ungeneratable set so nothing resting on it is emitted with a dangling reference. Merged header uses `using namespace sqlite_orm` inside `make_sqlite_schema_storage`; `CREATE VIRTUAL TABLE` is warned and omitted from `make_storage`.

---

## expr (https://www.sqlite.org/lang_expr.html)

### Literals
- [x] numeric-literal (integer) — leading zeros (and the separators between them) are dropped so the C++ literal stays decimal like SQLite (`010` → `10`, `0009` → `9`, `0_9` → `9`); `0x0FF` and reals keep their spelling
- [x] numeric-literal (integer) beyond int64 — SQLite reads it as a REAL, so the C++ literal gets a fractional part (`9223372036854775808` → `9223372036854775808.0`); a hex literal wraps around instead (`0xFFFFFFFFFFFFFFFF` → `static_cast<int64_t>(0xFFFFFFFFFFFFFFFF)`, i.e. -1); a negated `0x8000000000000000` leaves the int64 range and is `hex literal too big`, as in SQLite (validator error; a DDL clause SQLite stores without compiling reaches codegen instead, where the sign is kept out of the C++ constant that could not hold it and the clause carries a warning). The sign is folded into the literal before its range is read, the way SQLite's own `codeInteger()` folds it, so `-9223372036854775808` is an INTEGER while `9223372036854775808` is a REAL. In an `INSERT ... VALUES` over a table this batch declared such a literal also decides the form of the generated statement (see insert-stmt), because the object form would send it through a struct field that cannot hold it
- [x] hex literal past int64 (more than 16 significant digits) — refused as `hex literal too big` wherever the statement compiles the expression (SELECT / DML / `CREATE INDEX`), which is where SQLite raises it too (`codeInteger()`); the clauses SQLite only stores the text of keep it and warn instead, leaving that clause out: a column `DEFAULT`, a column or table `CHECK`, a view body, a trigger body, and a `STORED` generated column — the last one warns and drops the whole table, because a column that lost its `as(...)` would be an ordinary column instead of a generated one, and everything resting on that table — a foreign key into it, an index, a trigger or a view naming it, however indirectly (a subquery in any expression, a trigger `WHEN` clause, a CTE) — is left out with it, because sqlite_orm cannot reference a type it does not map — in the header `--db` generates and in the snippet a `.sql` file, stdin or `-e` generates alike, where a statement may even name a table declared after it; a view that is left out is a name without a type in the same way, whether it is dropped for a hex literal in its own body, for resting on such a table, or for a `SELECT` sqlite_orm cannot spell, so a trigger `INSTEAD OF` it and a view selecting from it go with it; on its own such a `CREATE TABLE` generates `/* CREATE TABLE g — not supported for sqlite_orm */`, as an ungeneratable `CREATE VIEW` does. A `VIRTUAL` generated column (the spelling `AS (...)` defaults to) is compiled at `CREATE TABLE` time and refused, so it stays an error. A `PRAGMA` value is read with `sqlite3GetInt32()`, which answers 0 for it, so `PRAGMA user_version = 0x10000000000000000` generates `user_version(0)` with a warning, and `PRAGMA integrity_check` is an error because SQLite falls back to reading the value as a table name
- [x] numeric-literal (real / float)
- [x] numeric-literal `_` digit separators (SQLite 3.46+) — `1_000_000` → `1'000'000`, `0x1_ffff` → `0x1'ffff`; a misplaced separator (`100_`, `1__0`, `0x_1f`) is an unrecognized token, as in SQLite
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
- [~] `-` (unary minus — folded into the numeric literal it precedes, as SQLite's own parser does; over any other operand generated as the `0 - expr` subtraction SQLite computes identically, because sqlite_orm's own unary minus reads back as 0. Over a predicate (`IN` / `BETWEEN` / `LIKE` / `GLOB` / `MATCH` / `IS [NOT] NULL` / `NOT`) neither form works — the generated code does not compile: codegen warning)
- [~] `+` (unary plus — an identity SQLite applies to the value, so the operand is generated and the plus is dropped, including for what the surrounding generation asks of that operand: `NOT +a` is generated exactly as `NOT a` is. The sign of a minus above one is read through it the way SQLite's parser reads it — `-+1` is the constant `-1` and `-+0x8000000000000000` the same `hex literal too big` a single minus over that literal is — while a COLLATE in between stops the fold, as it does in SQLite. Partial because the plus also takes the column's affinity out of the comparison it stands in, which sqlite_orm has no form for: over `t(a TEXT)` holding '1', SQLite answers `a = 1` with 1 and `+a = 1` with 0, and the generated comparison is the former either way. That loss is reported for the six comparison operators (`=`, `<>`, `<`, `<=`, `>`, `>=`) over a column reference, qualified or not: codegen warning. The report is that narrow and no narrower — `BETWEEN`, `IN` and the base expression of a `CASE` read the affinity the same way (`+a BETWEEN 1 AND 2`, `+a IN (1, 2)` and `CASE +a WHEN 1` all answer 0 / 'n' where the plus-less form answers 1 / 'y') and are silent, those branches dropping every codegen warning today, which card 1867000382920590460 owns; and the declared affinity of the column is not read, so the report also fires where the plus costs nothing, over an INTEGER column or one with no declared type, which card 1867077683549045974 owns. `LIKE`, `GLOB` and `MATCH` are correctly outside it: they do not apply a column's affinity at all, and `+a LIKE 1` answers what `a LIKE 1` does)
- [x] `~` (bitwise NOT)
- [~] `NOT` — `operator!` is the one sqlite_orm operator that keeps the `c(...)` its operand carries instead of unwrapping it, and the walk that collects a statement's tables and binds its values stops at such a wrapper. A column under a NOT is therefore generated as `column<T>(&T::x)` — the same SQL, but a form the walk reads, where `not c(&T::x)` came out with no FROM clause and threw `SQL logic error`; a column that names a SELECT alias stays `c(get<Alias>())`, because the aliased result column names the table already — and a value as `(c(0) + value)`, because an unbound literal made `NOT 0` answer NULL and `0 + x` is the numeric coercion SQLite applies in a boolean context anyway. A NOT over a NOT (or over the `!predicate` spelling of a negated `BETWEEN` / `LIKE` / `GLOB` / `MATCH`) is generated as `not cast<int64_t>(…)`: sqlite_orm's `negated_condition_t` is neither negatable nor an operator argument, so a second NOT over it does not compile. A NOT over a concatenation takes a CAST as well, but to REAL rather than to INTEGER: a `conc_t` is neither negatable nor an operator argument either, and a concatenation answers TEXT or NULL, whose truth SQLite reads through its real value — `NOT ('0' || '.5')` is 0 where `NOT CAST('0' || '.5' AS INTEGER)` is 1, and `NOT CAST(… AS REAL)` answers what `NOT …` does for every text value. A NOT over an OR needs nothing, because an OR is generated as the `or_condition_t` sqlite_orm negates. Partial because a NOT over a scalar subquery (`NOT (SELECT 1)`) is still generated as `not (select(1))`, which does not compile: sqlite_orm's `select_t` is neither negatable nor an operator argument either. Prefix `NOT` is also weaker than every binary operator SQLite has bar `AND` and `OR`, so its operand runs down to the `=` level: `NOT a + 1` is `NOT (a + 1)` and `NOT a IN (1, 2)` is `NOT (a IN (1, 2))`

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
- [~] `||` (concatenation) — generated as `conc(left, right)` when an operand is a sqlite_orm condition, as `left || right` otherwise. C++ spells `or` and the concatenation with the same token, and sqlite_orm's two `operator||` overloads pick between `or_condition_t` and `conc_t` by the operands: `(a = 1) || 'x'` spelled `c(&T::a) == 1 || "x"` came out as `(a = 1) OR 'x'`. A predicate operand is delimited with a `cast<int64_t>(…)` already — the grouping the serialized SQL needs — and a `cast_t` is not a condition, so it keeps the operator spelling. Partial because a `conc_t` is `binary_operator<L, R, conc_string>` and nothing else — not negatable, not an `arithmetic_t`, not an operator argument at all — so a concatenation that really is one is an operand nothing can be built on: `((a = 1) || 'x') = 1`, `((a = 1) || 'x') AND 1` and `(EXISTS(SELECT 1) || 'x') = 1` all stop at the compiler where the accidental `or_condition_t` they used to build compiled and answered the wrong value. A NOT over one is generated as `not cast<double>(…)` — the CAST makes it an operator argument and keeps the value, because SQLite reads the truth of the text a concatenation answers through its real value; the other slots are silent so far
- [x] `LIKE`
- [x] `LIKE ... ESCAPE`
- [x] `GLOB`
- [x] `NOT LIKE`
- [x] `NOT GLOB`

### Binary operators (logical)
- [x] `AND`
- [x] `OR` — generated as `or_(left, right)` when neither operand is a sqlite_orm condition, as `left or right` otherwise. `or` is the C++ token `||`, which sqlite_orm reads as its concatenation unless one of the operands is a condition: `SELECT 1 OR 0` spelled `c(1) or 0` ran as `SELECT 1 || 0` and answered '10'. An operand spelled as a call takes the rest of the chain with it, so one OR reads one way throughout. `MATCH` is not merely absent from the conditions: `match_t` derives from nothing at all, so it is not an operand `or_()` accepts either (`or_() arguments must be bindable values or sqlite_orm-recognized operands`), and the operator spelling has no `operator||` to pick for it. The call spelling quotes such an argument instead — `a MATCH 'x' OR b` is generated as `or_(c(match(&T::a, "x")), &T::b)`, which compiles and serializes to the `("t"."a" MATCH 'x' OR "t"."b")` that was written, `or_()` unwrapping the `quoted_expression_t` right back to the expression it holds. That quote belongs to the call spelling alone: `c(match(…)) || c(match(…))` picks the *concatenation* `operator||`, serializes to `("t"."a" MATCH 'x' || "t"."b" MATCH 'y')` and fails on the first step with `unable to use function MATCH in the requested context`, which is why an OR over two operands that are no conditions is offered as the call and nothing else. A MATCH beside a condition needs no quote at all — `a MATCH 'x' OR b = 1` is generated as `match(&T::a, "x") or c(&T::b) == 1` — the comparison being the recognized operand `operator||` asks for. That the wrap is needed at all is an upstream gap, reported at https://github.com/fnc12/sqlite_orm/issues/1543. The one operand that still does not compile under an OR is a negated `MATCH`, and it compiles nowhere else either — `operator!` rejects `match_t` (https://github.com/fnc12/sqlite_orm/issues/1501) — so that is a gap of the negation rather than of the OR

### IS operators
- [x] `IS NULL` — `IS` is a binary operator on the `=` level and `NULL` an ordinary right operand,
  so the operand keeps parsing: `a IS NULL - 1` is `a IS (NULL - 1)`, a binary IS, and gets the
  validator error below. `is_null(a)` is generated for a right operand that is exactly `NULL`
- [x] `IS NOT NULL` — the same rule: `a IS NOT NULL - 1` is `a IS NOT (NULL - 1)`
- [x] `ISNULL` (single keyword)
- [x] `NOTNULL` (single keyword)
- [x] `NOT NULL` (two keywords)
- [!] `IS expr` — sqlite_orm has no binary IS; validator error (codegen fallback: `==` with warning)
- [!] `IS NOT expr` — sqlite_orm has no binary IS NOT; validator error (codegen fallback: `!=` with warning)
- [!] `IS DISTINCT FROM expr` — not supported in sqlite_orm; validator error
- [!] `IS NOT DISTINCT FROM expr` — not supported in sqlite_orm; validator error

### Special operators
- [~] `BETWEEN expr AND expr` — generated as `between(operand, low, high)`, and sqlite_orm declares it `between(A, T, T)`: both bounds have to reach C++ as one type, so `x BETWEEN 1 AND 3000000000` (an `int` next to a `long`) does not compile, and neither does an integer bound next to a real, a string, a column or a nested expression. Independent of the field the prefix infers for the column, which widens to the wider bound
- [~] `NOT BETWEEN expr AND expr` — the same bounds rule
- [~] `IN (expr-list)` — generated as `in(operand, {...})`, one braced-init-list, so every value has to reach C++ as one type: `x IN (1, 3000000000)` does not compile, nor does a list mixing an integer with a real, a string, a column or a nested expression. Independent of the field the prefix infers, which widens to the widest value
- [x] `IN (select-stmt)`
- [!] `IN table-name` (table-valued IN — not in sqlite_orm; validator error)
- [~] `NOT IN (expr-list)` — the same list rule
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
- [~] `(expr)` — grouping. The parentheses do not survive into the AST; the generated code spells the
  grouping out in C++ terms instead, because C++ ranks the emitted operators differently (SQL binds
  `||` tightest of the binary operators and C++ binds it loosest, and SQL gives `<<` `>>` `&` `|` one
  level below `+` and `-`) and because every one of them is left-associative there too, so a nested
  right operand would regroup even at equal precedence. An operand C++ would regroup is parenthesized,
  which is what tells `1 - (2 - 3)` from `1 - 2 - 3`; the functional spelling passes its operands as
  arguments and needs none. The C++ grouping is only half of it: sqlite_orm serializes the statement
  back into SQL, and there it parenthesizes an operand only when that operand is a binary operator or
  condition of its own, so a predicate (`IN`, `BETWEEN`, `LIKE`, `GLOB`, `MATCH`, `IS [NOT] NULL`,
  `NOT`) under an operator SQLite binds tighter is generated as `cast<int64_t>(predicate)` — the CAST
  delimits it in the serialized SQL and keeps what it stands for, which is `0`, `1` or `NULL` either
  way. The predicates take the same CAST the other way round: a predicate serializer parenthesizes
  none of its arguments, and SQLite binds `AND` and `OR` looser than every predicate, so an `AND` or
  an `OR` in the argument slot of `IN`, `BETWEEN`, `LIKE`, `GLOB`, `MATCH` or `IS [NOT] NULL` is
  generated as `cast<int64_t>(…)` as well — `is_null(or_(1, 0))` would come out `1 OR 0 IS NULL`,
  which SQLite reads as `1 OR (0 IS NULL)`, and `between(1, or_(1, 0), 3)` would come out
  `1 BETWEEN 1 OR 0 AND 3`, which SQLite does not parse at all. The values of an `IN` list are
  delimited by their commas and stay bare. Partial because a grouping that makes a comparison or a
  concatenation the operand of an arithmetic or a bitwise operator has no sqlite_orm form: that code
  does not compile; and because a predicate or a `NOT` standing in one of those same argument slots
  still comes out bare, which is the other half of the rank order (`a LIKE (b IS NULL)`,
  `(NOT a) IS NULL`) and is on master in the same shape

### Function call
- [x] `function-name(args)`
- [x] `function-name(DISTINCT arg)`
- [x] `function-name(*)`
- [x] `function-name()` — no args
- [~] `function-name(...) FILTER (WHERE expr)` → `.filter(where(...))` before `.over(...)` when present; sqlite_orm gives a `filter()` to `count(*)` and the aggregate function calls only, so a FILTER over a window function generates a call that does not exist — SQLite refuses the same thing at prepare (`FILTER clause may only be used with aggregate window functions`) while storing a trigger or a view that holds it, so the code is generated with a codegen warning
- [x] `function-name() OVER window-name`
- [x] `function-name() OVER (window-defn)` — PARTITION BY, ORDER BY, ROWS|RANGE|GROUPS frame, EXCLUDE

### Collation
- [x] `expr COLLATE collation-name` (parsed as CollateNode; codegen passes through + warning) — the dropped node leaves the operand under it exactly as it comes out bare: the `c(...)` wrap, the grouping, the `as_optional` a result column is widened with, the field a compared, BETWEEN'd, IN'd, LIKE'd, GLOB'd or MATCH'd column gets, and the name and type of an argument handed to a user-defined function. The one thing it does change is SQLite's own folding of a sign into a literal, which goes through parentheses but not through a COLLATE, so a minus over one keeps the `0 - x` subtraction form

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
- [x] LIMIT expr `,` expr → `limit(count, offset(off))` (SQLite's comma form puts the offset first)

### Result columns
- [x] `*` → `get_all<T>()`
- [x] `table.*` → `asterisk<Struct>()`
- [x] `schema.table.*` → parsed; codegen `asterisk<Struct>()` + warning (schema qualifier not represented in sqlite_orm mapping)
- [x] expr → `select(expr)` / `select(columns(...))`
- [x] expr AS alias (parsed, alias stored in AST)
- [x] expr whose value can be NULL → `select(as_optional(expr))`, so the row reads back as a `std::optional`. sqlite_orm types an operator expression from the operator alone — `double` for the arithmetic ones, `std::string` for `||`, `bool` for a comparison —, a BETWEEN, an IN, a LIKE and a GLOB (negated or not) `bool`, a CAST the type the CAST asks for, and a call of a built-in function the return type that function declares, and a NULL row would come back as 0 / "" / false. Widened only where SQLite can answer NULL: a literal cannot, nor can a predicate or a CAST over operands the SQL spells out, and `hex`, `quote`, `typeof`, `char`, `pi`, `randomblob`, `zeroblob`, `count`, `total`, `changes`, `random`, `last_insert_rowid`, `total_changes`, `json_quote`, `json_object`, `json_group_array` and `json_group_object` answer a value for a NULL argument at all. A call is ruled out only for the built-ins that answer NULL for no reason other than a NULL argument — `abs`, `coalesce`, `glob`, `ifnull`, `iif`, `instr`, `json`, `json_array`, `json_patch`, `json_valid`, `length`, `like`, `likelihood`, `likely`, `lower`, `ltrim`, `replace`, `round`, `rtrim`, `soundex`, `trim`, `unlikely` and `upper`, the list SQLite answered a value for over every non-NULL argument of a 11.7M expression corpus — `iif` for its three-argument form alone, the `iif(X, Y)` SQLite 3.48 added being `iif(X, Y, NULL)` and so NULL whenever X is false. Every other known function is widened whatever its arguments hold, because it answers NULL over arguments the SQL spells out — `nullif(1, 1)`, `date('bogus')`, `unicode('')`, `sign('abc')`, `substr(x'', 1)`, `printf('')`, `json_extract('{}', '$.a')`, `sqrt(-1)`, `ln(0)`, `sin('a')` — or, being an aggregate, over an empty rowset: `avg`, `group_concat`, `max`, `min` and `sum`. The corpus runs against libsqlite3 3.45.1, the version this project links, which unlike the `sqlite3` CLI in the image carries the math functions; the two directions do not cost the same, a widening for nothing being an `std::optional` that is always engaged and a missing one a silent 0. Left alone where sqlite_orm reports a nullable type already: a column carries its field's type, `abs`, `max`, `min` and `sum` are a `std::unique_ptr`, `coalesce`, `ifnull`, `nullif`, `iif`, `likely`, `unlikely` and `likelihood` carry the result of an argument — unless the call spells its own result type, which replaces what sqlite_orm deduces and holds no NULL, and then the widening is back on — NULL itself is a `std::nullptr_t`, a window function is either its argument's type or a rank SQLite always answers, and a user-defined function comes back as the type the generated `operator()` declares. A `/` or `%` counts whatever its operands are, because SQLite answers a division by zero with NULL. The SQL is unchanged — `as_optional` serializes to its argument — and a WHERE / ORDER BY / GROUP BY expression, which is not read back, is left alone
- [x] a bitwise result column → `select(cast<int64_t>(expr))`, so the whole int64 reaches the caller. sqlite_orm types `&`, `|`, `<<`, `>>` and `~` as `int`, which truncated every result past the int32 range: `SELECT a & -1` over `a = 9223372036854775807` read back as -1. SQLite answers a bitwise operator with an INTEGER or a NULL whatever its operands hold — checked over 1463 operand pairs against sqlite3 3.51, no REAL or TEXT among them — so the CAST keeps the value, `typeof` included, and only widens the C++ type. A random cross-check of 250 expressions against sqlite3 3.51 went from 18 wrong out of 172 to 1, the one left being the REAL `-9223372036854775808` generates. The CAST is a result column's alone: a WHERE / ORDER BY / GROUP BY expression is not read back and is left as it was, and only the top-level operator of a result column decides, so `(a & -1) + 1` is read back as the `+` is
- [ ] a `+`, `-`, `*`, `/` or `%` result column is read back through sqlite_orm's `double`, so an INTEGER result past 2^53 comes back rounded: `SELECT a + 0` over `a = 9223372036854775807` hands the caller 9223372036854775808. Nothing in sqlite_orm reads such a column as an int64 without a CAST, and a CAST to INTEGER cannot be added the way the bitwise one can: these operators answer a REAL as soon as an operand is one (checked over 1710 operand pairs against sqlite3 3.51) and the CAST would truncate it, and an overflowing `*` answers a REAL as well. So the column is generated as it was and carries a warning naming the operator, anchored at its token. The warning is left out where the SQL already bounds the result inside the range a double holds exactly. That bound is an upper bound on the magnitude of an INTEGER answer and is computed in the int64 domain SQLite computes in — carried in a `double` it would round itself down past 2^53 and go quiet on `9007199254740993 + 0`, the very first value that loses anything — and every step saturates rather than grows, an INTEGER SQLite cannot hold being a REAL the caller reads back exactly. A REAL or a NULL operand takes the whole expression out of the warning: `+`, `-`, `*`, `/` and `%` answer a REAL or a NULL whenever an operand is one, so there is no INTEGER left to lose and the `as_optional` already generated carries the NULL. Otherwise both operands have to be spelled out for `+`, `-` and `*`, except that a factor of zero bounds a `*` on its own; a dividend bounds `/`; and either operand bounds `%`. `~` is not one of them: it casts its operand to an INTEGER first, so `1 + ~9007199254740994.0` is reported. Closing it needs a type-only integer wrapper in sqlite_orm, the way `as_optional` is one for nullability

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
- [~] UNION
- [~] UNION ALL
- [~] INTERSECT
- [~] EXCEPT

A result column of a compound SELECT is widened to `as_optional` the way a plain SELECT's is, and
widened in every branch at once: sqlite_orm reads a compound back through `std::common_type` of the
types its branches come out as, so `SELECT a + 1 FROM users UNION SELECT a * 2 FROM users` now reads
a NULL row back as an empty optional rather than as 0.

What is still `[~]`: the widening needs one common type across the branches, so branches whose types
differ — `SELECT a & 1 FROM users UNION SELECT a + 1 FROM users`, where sqlite_orm types `&` as
`int` and `+` as `double` — are left as written and still read a NULL row back as 0. Widening one of
them alone is what has no common type at all (`std::optional<int>` beside an
`std::optional<double>`), and the generated code would stop compiling. A branch whose type only the
schema knows — a column reference, a call, a literal — is left alone for the same reason. The
`cast<int64_t>` a plain SELECT puts on a bitwise result column is not placed in a branch either, so
a compound of bitwise branches is still read back through `int`.

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
- [x] INSERT INTO table VALUES (...) — the object form `storage.insert(T{...})`, except when a value cannot pass through the field it would reach in a table this batch declared, in which case the statement spells its column list out instead — `storage.insert(into<T>(), columns(&T::x), values(std::make_tuple(1)))` — which binds the value and leaves the affinity to SQLite, exactly as the SQL does. The object form sends every value through a struct field, and a field holds one storage class, while SQLite types a value by itself and applies the column affinity only afterwards, so the column list takes over for: a value of a storage class the field was not declared for (a number or a string into the `std::vector<char>` of a column with no type, a number or a blob into a `std::string`, a string or a blob into an `int64_t`, `double` or `bool`); `NULL` where the column is `NOT NULL`, so the field is not optional and `std::nullopt` does not initialize it; an expression (`1+1`), whose value only SQLite knows and which initializes no field at all; a decimal integer literal past the int64 range (`99999999999999999999`) or any REAL literal (`1.5`, `2.0`) reaching a field that holds whole numbers only (`int64_t` and `int`, and `bool` for a BOOLEAN column); a whole number no `double` holds exactly (`9223372036854775807`, `9007199254740993`) reaching a `double` field, which a braced initializer refuses outright; and any whole number besides 0 and 1 (`5`, `-1`, `1_0`, `0xFFFFFFFFFFFFFFFF`, `9223372036854775807`) reaching the `bool` field of a BOOLEAN column, whose NUMERIC affinity stores the number as SQLite typed it while the field turns it into 1 — or, over a `NOT NULL` column, does not compile at all. `TRUE` and `FALSE` are the 1 and 0 SQLite stores for them, so they keep the object form. A value the field does carry keeps the object form: a blob literal into a column with no type stays `T{std::vector<char>{'\x41'}}`, a string into a TEXT column stays `T{"a"}`, `NULL` into a nullable column stays `T{std::nullopt}` and `9007199254740992` into a REAL one stays `T{9007199254740992}`. A generated column is left out of that column list, as SQLite computes it, whichever of `AS (...)`, `... VIRTUAL`, `... STORED` and `GENERATED ALWAYS AS (...)` the DDL spells. Only the past-range literal carries a warning, naming the column and the form; the other switches are silent, because the bound statement stores exactly what SQLite stores (`1` into a column with no type is `integer|1`, `1.5` stays `real|1.5`, `2.0` becomes `integer|2`) and only the shape of the generated call changes. Left alone: a table this batch never declared (nothing is known about the field), `INSERT ... SELECT`, the explicit-column form above and a `DEFAULT` clause, which all bind the value already.
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
- [~] indexed-column COLLATE collation-name — generated as `.collate("…")` over a column, and over an expression that takes a trailing COLLATE whole: a call, `CAST`, `CASE`, a literal, `~x`, a JSON arrow (generated as a `JSON_EXTRACT` call), an `IN` over a value list. Over an expression that ends in an operand of its own — every other binary operator, `NOT`, `BETWEEN`, `LIKE`, `GLOB`, `IS [NOT] NULL`, and the `0 - x` subtraction a negation is generated as — the collation is left out and codegen warns: sqlite_orm serializes an indexed column's collation as a bare COLLATE after the expression, with no parentheses around it, and SQLite binds COLLATE tighter than those operators, so `indexed_column(c(&T::b) || "x").collate("nocase")` stores `CREATE INDEX "i" ON "t" ("b" || 'x' COLLATE nocase)`, which SQLite reads as `"b" || ('x' COLLATE nocase)` and indexes a BINARY key where the schema it was generated from asks for a NOCASE one (`pragma_index_xinfo`, checked against sqlite3 3.51.0)
- [x] indexed-column ASC
- [x] indexed-column DESC
- [x] WHERE expr (partial index)
- [x] COLLATE / ASC|DESC in either order (SQLite-compatible loop)
- [x] an index whose first indexed column is an expression names the table it is made for — `make_index<T>(...)` — because `make_index` deduces that table from its first argument alone and only a column reference carries one
- [!] SQL without `IF NOT EXISTS` vs sqlite_orm always emitting `IF NOT EXISTS` in serialized DDL (codegen warns)
- [!] schema-qualified index or `ON` table (parsed; codegen warns)
- [!] a UNIQUE index whose first indexed column is an expression: `make_unique_index` takes that table as a template parameter defaulted behind its argument pack, which no explicit template argument list can reach, so the index has no form in sqlite_orm and is left out of the storage (codegen warns); its indexed columns and its partial `WHERE` are still generated, so everything they decide and warn about is reported. The missing overload is reported upstream: https://github.com/fnc12/sqlite_orm/issues/1543

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
- [~] WHEN expr — `trigger_base_t::when()` keeps the expression in an `optional_container`, whose field is default-constructed before the expression is assigned to it, so only a WHEN clause whose every sqlite_orm type has a default constructor compiles. The comparisons, `AND` and `OR` produce a `binary_condition`, which declares one — an `OR` only because it is spelled `or_(...)`, since the `||` token it shares with a concatenation reads as the `binary_operator` behind `conc_t` — and a `CAST`, a `CASE`, a `COLLATE`, `MATCH` in either spelling, `CURRENT_*`, `RAISE()`, `count(*)` — including one wrapped in a `FILTER` or an `OVER`, since `count_asterisk_t::filter()` keeps only the expression of its `where` and `over_t` is an aggregate — the window functions (`row_number`, `rank`, `dense_rank`, `percent_rank`, `cume_dist`, `ntile`, `lag`, `lead`, `first_value`, `last_value`, `nth_value`), each of which generates an aggregate of its own rather than the `builtin_function_t` behind every other function call, and a subquery over a bare `FROM` (with `LIMIT`) hold only what they are given — those generate as before. The predicates (`NOT`, `IS [NOT] NULL`, `IN`, `BETWEEN`, `LIKE`, `GLOB`, `EXISTS` — but not `MATCH`, whose `match_t` is an aggregate), every arithmetic, bit and concatenation operator, the JSON arrows, every function call other than those, and a subquery carrying a `WHERE`, an `ORDER BY` — the one of a window definition included, the rest of which (`PARTITION BY`, the frame) is made of aggregates — a `DISTINCT`, a join constraint or a compound arm do not: SQLite stores such a trigger and the generated code does not compile (`use of deleted function optional_container<…>::optional_container()`), so it is generated with a codegen warning naming each form
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

- [x] CREATE VIEW → annotated struct (`[[= "name"_orm_name]]`) + `make_view<T>(select(...))`;
  field types inferred from CREATE TABLE statements in the same batch / database (fallback:
  name heuristics + warning). Requires a C++26 compiler with reflection (P2996) — sqlite_orm
  auto-detects it (`SQLITE_ORM_REFLECTION_SUPPORTED` → `SQLITE_ORM_WITH_VIEW`); codegen
  attaches an explanatory comment.
- [x] Explicit view column list — `CREATE VIEW v(a, b) AS ...` names the struct fields
- [x] `SELECT *` / `t.*` in view body — expanded to the source table's columns
- [~] Views whose SELECT is not supported as a subexpression (e.g. GROUP BY) — warning,
  view omitted from `make_storage()`
- [~] schema-qualified view name (parsed; codegen warns, uses unqualified name)

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
- [~] coalesce(X,Y,...) — sqlite_orm types the call as the common C++ type of its arguments (`common_argument_type<>`), and arguments with no common type — a text next to a number, a BLOB next to anything else, two `std::optional`s of different types, a NULL next to a number — took the `storage.select(...)` around the call down with them (`no type named 'type' in 'column_result_t<…>'`). Where the generated types say there is none, the call spells the type it is read back through instead. Where every argument that carries a value carries a NUMBER they all fit in, that type is the number they reduce to — a NULL carries no value of its own, SQLite answering such a call with one of the other arguments, and an `std::optional` is a wrapper around a value — so `coalesce(NULL, 1)` is `coalesce<int>(nullptr, 1)` and `coalesce(NULL, 1, 2.5)` is `coalesce<double>(…)`, an `int` being exact in a `double`, and nothing is lost to report. The numbers form a lattice and not a line: `int64_t` and `double` sit beside each other with nothing above them, a `double` losing every integer past 2^53 (9007199254740993 comes back as 9007199254740992) and an `int64_t` the fractional part of a REAL, so that pair falls through to the text fallback below rather than rounding silently — the same rule `CASE` reduces its branches by. Otherwise it is `std::vector<char>` when a BLOB takes part, which carries every byte of one, and `std::string` otherwise, which carries every storage class as its text; what that costs is reported at the column the value is read back into — a result column of a SELECT, a field of a view's struct — since a number comes back as its digits: codegen warning. A call standing in the column list of a scalar subquery spells its type but carries no report: the whole result-column report family stops at the outer column, `json_extract` included, on master as here; carded separately. The argument types are read in the scope of the SELECT the call belongs to, which for a call inside a scalar subquery or a view body is that select's own FROM clause and not the one the emitter stands in. Partial because only a constant and a column of a CREATE TABLE of the batch are answered for, and only where the generated code writes that column as a member of the table's struct; an argument sqlite_orm types out of what it is built over — a nested call, an operator, a CAST, a bind parameter, a column of a CTE or of a view, a column no schema names — is left as it is generated, so `coalesce(abs(a), 'x')` still does not compile
- [!] concat(X,...) — not in sqlite_orm (SQLite 3.44+)
- [!] concat_ws(SEP,X,...) — not in sqlite_orm (SQLite 3.44+)
- [!] format(FORMAT,...) — not in sqlite_orm (SQLite 3.38+)
- [x] glob(X,Y)
- [x] hex(X)
- [~] ifnull(X,Y) — the arguments are reduced to one C++ type the way `coalesce(X,Y,...)` reduces them (`common_argument_type<0, 1>`), with the same spelled result type, the same report and the same partiality
- [~] iif(X,Y,Z) — the two branches are reduced to one C++ type the way `coalesce(X,Y,...)` reduces its arguments (`common_argument_type<1, 2>`, the condition taking no part), with the same spelled result type, the same report and the same partiality
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
- [~] nullif(X,Y) — the arguments are reduced to one C++ type the way `coalesce(X,Y,...)` reduces them (`common_argument_type<0, 1>`), with the same spelled result type, the same report and the same partiality. The spelled type replaces the `std::optional` sqlite_orm wraps the deduced one in, so the result column is widened with `as_optional` instead — NULLIF answers NULL whenever its two arguments are equal
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
- [x] json_extract(json, path, ...) → `json_extract<std::string>(…)`: sqlite_orm declares the call with a result type parameter that has no default, so a call generated without one does not compile. JSON_EXTRACT over one path answers the value at it, of whatever storage class the JSON holds, and `std::string` reads every one of them back as its text — reported on a result column; over two paths or more it answers the JSON array of what it found, which is text anyway. The missing default result type is reported upstream: https://github.com/fnc12/sqlite_orm/issues/1543
- [x] json_insert(json, path, value, ...)
- [x] json_object(label1, value1, ...)
- [x] json_patch(json1, json2)
- [x] json_remove(json, path, ...)
- [x] json_replace(json, path, value, ...)
- [x] json_set(json, path, value, ...)
- [x] json_type(json)
- [x] json_type(json, path)
- [x] json_valid(json)
- [x] json_quote(value) → `json_quote<std::string>(…)`: the same result type parameter with no default, and text is what JSON_QUOTE always answers, reported upstream with `json_extract`: https://github.com/fnc12/sqlite_orm/issues/1543
- [x] json_group_array(value)
- [x] json_group_object(name, value)
- [x] json_each(json)
- [x] json_each(json, path)
- [x] json_tree(json)
- [x] json_tree(json, path)
- [x] `->` operator → `json_extract<std::string>(lhs, path)` (codegen warning: JSON_EXTRACT answers the SQL value at the path where `->` answers the JSON text of it — `'{"a":"s"}' -> '$.a'` is `"s"` in SQLite and `s` here; sqlite_orm has no form for the operator)
- [x] `->>` operator → `json_extract<std::string>(lhs, path)`, which SQLite answers value for value and storage class for storage class over the same path; the result type report a single-path JSON_EXTRACT carries on a result column is its, and not `->`'s: `->` answers the JSON text of the value, which is a text whatever the JSON holds, so reading it back through a `std::string` costs it nothing and only the divergence above is reported
- [x] both arrows take an abbreviated path the JSON_EXTRACT call they are generated as does not, so the path the operand spells out is expanded the way SQLite expands it: `'x'` → `$.x`, `'a.b'` → `$."a.b"`, `'[1]'` → `$[1]`, `1` → `$[1]`, `-1` → `$[#-1]`, and a `$` path as written. A path operand SQLite only expands while it runs — a column, a bind parameter, an expression, or a literal it reads as a REAL or a BLOB — is left as written and reported (codegen warning: the call takes the operand as written, and SQLite refuses a path that does not start with `$`)

---

## Constructs not supported by sqlite_orm

Legend: **`[!]`** — no sqlite_orm mapping (validator error, or warning where
noted). **`[x]`** — sqlite2orm emits sqlite_orm API for this construct. The
parser recognizes everything listed; this section tracks **downstream** support.

- [!] Subselect in FROM
- [!] NULLS FIRST / NULLS LAST
- [!] RETURNING clause
- [!] Unary minus over a predicate (`-(a BETWEEN 1 AND 9)`, `-(a IN (…))`, `-(a IS NULL)`, `- NOT a`, …) — sqlite_orm has no unary minus that reads back correctly, and the `0 - expr` spelling the other operands use would regroup a predicate SQLite binds looser than `-`; the generated negation does not compile (codegen warning)
- [!] A trigger `WHEN` clause over anything but a comparison, an `AND`/`OR`, a `CAST`, a `CASE`, a `COLLATE`, `MATCH`, `CURRENT_*`, `RAISE()`, `count(*)` (with a `FILTER` or an `OVER` that has no `ORDER BY`) or a window function (with an `OVER` that has no `ORDER BY`; a `FILTER` over one has no sqlite_orm form at all) or a subquery over a bare `FROM` — sqlite_orm holds the WHEN expression in an `optional_container`, which default-constructs it, and none of the predicate, operator or function types it would hold has a default constructor; the generated trigger does not compile (codegen warning naming each form, see create-trigger-stmt)
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
- [x] PRAGMA — parsed as `PragmaNode`; supported names map to `storage.pragma` in sqlite_orm (`journal_mode`, `locking_mode`, `user_version`, `synchronous`, `application_id`, `busy_timeout`, `auto_vacuum`, `max_page_count`, `recursive_triggers`, `module_list`, `quick_check`, `integrity_check`, `table_info`, `table_xinfo`); schema-qualified `PRAGMA main.xxx` is a validator error; other pragma names are validator errors. The value is a name to SQLite (`nmnum`), not an expression, so every keyword its parser falls back to an identifier stands as one, `ON`, `DELETE` and `DEFAULT` included — `PRAGMA journal_mode = DELETE`, `PRAGMA locking_mode = EXCLUSIVE` and `PRAGMA recursive_triggers = no` are statements, while the 55 reserved words are a syntax error there. Each PRAGMA reads that text with a reader of its own, so a name reaches the generated call as the number its reader answers: `getSafetyLevel()` reads `full` as 2 and `extra` as 3 for `synchronous`, `getAutoVacuum()` reads `none`/`full`/`incremental` as 0/1/2, and `sqlite3DecOrHexToI64()` refuses a name for `max_page_count`, which leaves the limit alone and only reports it — `PRAGMA synchronous = full` generates `synchronous(2)` with a warning, not the `&User::full` a name handed to expression codegen used to become. sqlite_orm declares each of those setters as taking an `int`, so a limit past that range — `max_page_count` clamps at 0xfffffffe — generates no call at all, the warning naming the limit SQLite sets and saying that `max_page_count(int)` cannot pass it on. A table name reaches `integrity_check` unquoted — `pragma_t::integrity_check(T)` streams the argument into the pragma text raw where `table_info` and `table_xinfo` run it through `streaming_identifier` — so the `storage.pragma.integrity_check("my table")` generated for a name that needs quoting prepares as `PRAGMA integrity_check(my table)`, which SQLite answers with `near "table": syntax error` where the quoted form answers `ok`. None of that text reaches the caller on the pinned revision: the pragma runs through `sqlite3_exec` and only its return code is translated, so the generated call throws a `std::system_error` reading `SQL logic error`, which names neither the pragma nor the name that broke it; reported upstream: https://github.com/fnc12/sqlite_orm/issues/1543
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
