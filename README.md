# sqlite2orm

Converts SQLite SQL into C++ code for [sqlite_orm](https://github.com/fnc12/sqlite_orm).

Paste any SQLite statement — `CREATE TABLE`, `SELECT`, `INSERT`, triggers, indexes, window functions — and get ready-to-compile `sqlite_orm` API calls with struct definitions and `make_storage()`.

## Features

- Full SQLite grammar coverage (lexer + recursive-descent parser)
- Validator reports constructs unsupported by sqlite_orm
- **PRAGMA** — names exposed as `sqlite_orm::storage::pragma` are validated and codegen’d; others are rejected with a clear message (see `COVERAGE.md`)
- Code generator with **decision points** — alternative `sqlite_orm` representations for the same SQL
- Database file import — reads `sqlite_master` and generates a complete `.h` header
- JSON export of decision points for GUI/web integration
- Configurable code generation via `CodeGenPolicy`
- Build pulls in [{fmt}](https://github.com/fmtlib/fmt) via CMake `FetchContent` (CLI + examples); the library target does not require fmt unless you link it yourself

## Quick start

### Build

```bash
cmake -S . -B build -DSQLITE2ORM_BUILD_CLI=ON
cmake --build build -j$(nproc)
```

### CLI usage

```bash
# Single statement
./build/sqlite2orm -e "SELECT id, name FROM users WHERE age > 18;"

# SQL file
./build/sqlite2orm schema.sql

# Import from .sqlite database
./build/sqlite2orm --db app.sqlite

# JSON output with decision points
./build/sqlite2orm --db app.sqlite --json

# stdin
echo "CREATE TABLE t (id INTEGER PRIMARY KEY);" | ./build/sqlite2orm
```

### Library usage

Examples and the CLI use [{fmt}](https://github.com/fmtlib/fmt) for output. In C++20 you can instead use [`std::format`](https://en.cppreference.com/w/cpp/utility/format/format) / [`std::print`](https://en.cppreference.com/w/cpp/io/print) for the same role.

```cpp
#include <sqlite2orm/process.h>
#include <fmt/format.h>

int main() {
    auto result = sqlite2orm::processSql(
        "SELECT id, name FROM users WHERE age > 18 ORDER BY name;");

    if (!result.ok()) {
        for (const auto& e : result.validationErrors)
            fmt::print(stderr, "{}\n", e.message);
        return 1;
    }

    fmt::print("{}\n", result.codegen.code);
}
```

See [examples/](examples/) for more: programmatic API, custom policies, database import, JSON output.

## Build options

| Option | Default | Description |
|---|---|---|
| `SQLITE2ORM_BUILD_CLI` | `ON` | Build the `sqlite2orm` CLI tool |
| `SQLITE2ORM_BUILD_TESTS` | `ON` | Build unit tests (fetches Catch2 and sqlite_orm headers) |
| `SQLITE2ORM_BUILD_EXAMPLES` | `OFF` | Build example programs |

## Architecture

```
SQL string
    │
    ▼
┌──────────┐
│ Tokenizer│  all SQLite tokens
└────┬─────┘
     │ vector<Token>
     ▼
┌──────────┐
│  Parser  │  recursive descent → AST
└────┬─────┘
     │ AST
     ▼
┌──────────┐
│ Validator│  checks sqlite_orm compatibility
└────┬─────┘
     │ validated AST
     ▼
┌──────────┐
│ CodeGen  │  AST → C++ sqlite_orm code
└──────────┘  + decision points
```

Pipeline entry point: `processSql()` in `sqlite2orm/process.h`.

Database import: `SqliteSchemaReader` → `processSqliteSchema()` → `generateSqliteSchemaHeader()`.

## Decision points

Many SQL constructs map to multiple valid `sqlite_orm` representations. The code generator annotates these as **decision points** with alternatives:

- **Expression style**: `c(&User::id) > 5` vs `greater_than(&User::id, 5)`
- **API level**: `get_all<T>(where(...))` vs `select(asterisk<T>(), where(...))`
- **Column reference**: `&User::id` vs `column<User>(&User::id)`

## SQL coverage

See [COVERAGE.md](COVERAGE.md) for detailed tracking of every SQLite grammar construct.

## Repository layout

The upstream [sqlite_orm](https://github.com/fnc12/sqlite_orm) checkout may
still contain this tree under `sqlite2orm/`; the same sources are intended to
work as the root of a **standalone** sqlite2orm repository after a future move.

## License

MIT — see [LICENSE](LICENSE).
