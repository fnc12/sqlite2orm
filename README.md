# sqlite2orm

Converts SQLite SQL into C++ code for [sqlite_orm](https://github.com/fnc12/sqlite_orm).

Paste any SQLite statement — `CREATE TABLE`, `SELECT`, `INSERT`, triggers, indexes, window functions — and get ready-to-compile `sqlite_orm` API calls with struct definitions and `make_storage()`.

## Try it online

No build required — paste SQL and get sqlite_orm code at [sqliteorm.com/playground](https://sqliteorm.com/playground).

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
| `SQLITE2ORM_SQLITE_ORM_REVISION` | `eb77998ef5e27350b25977b061e46e202742ecc8` | Revision of `fnc12/sqlite_orm` the runtime tests compile against |

### Pinned sqlite_orm revision

The runtime tests compile the generated code against sqlite_orm headers that `FetchContent`
pulls at the revision above, not at the tip of `dev`. `dev` moves, and a checkout populated a
few days earlier used to fail those tests in a way that looked like a bug in whatever was under
test. A bump spells the revision out in three files — `cmake/SqliteOrmPinnedRevision.cmake`, the
table row above and `tests/sqlite_orm_revision_tests.cpp` (`pinnedRevision` and two expected
literals) — and `sqlite2orm_tests` checks that all of them agree, and that the headers in
`build/_deps` are really the pinned ones.

A build tree keeps the headers it populated until it reconfigures, which is how a moving `dev`
stayed invisible to it; a bump edits `cmake/SqliteOrmPinnedRevision.cmake`, and a tree that never
named a revision of its own picks it up on its next configure. To try the tests against the tip of
`dev`:

```bash
cmake -S . -B build -DSQLITE2ORM_SQLITE_ORM_REVISION=dev
```

A branch is checked out as `origin/<branch>`, which the build works out by asking the remote: a
tree populated at the pinned hash holds no local branch of its own, and `git checkout dev` inside a
sqlite_orm checkout — which keeps a top-level `dev/` directory — is ambiguous between the two.
Tags and commit hashes are taken as they are written.

Naming the revision on the command line is what puts it in the cache, and a tree configured that
way keeps it — bumps included — until

```bash
cmake -U SQLITE2ORM_SQLITE_ORM_REVISION -S . -B build
```

puts it back on the pin. While a tree is aimed elsewhere the revision check only skips, and
configuring says so. A tree pointed at a checkout of its own with
`FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS` is not moved by `cmake` at all — check that
directory out at the pinned revision yourself.

Headers that are not a `git` checkout — a tree copied out of another build, a directory an
override points at — hold no revision to compare, and neither does any tree on a machine with no
`git` to ask, so the check can only skip over them. Configuring reports both cases, and says which
of the two it met, because headers of an unknown age are exactly how the failures this pin exists
to stop get blamed on the change under test. A populated dependency that is no longer a checkout
is put back by removing its `-subbuild` directory and configuring again with
`FETCHCONTENT_FULLY_DISCONNECTED` off — that is the step that clones it; removing the headers
themselves leaves the sub-build stamped as done and nothing repopulates.

## The corpus of live schemas

`tests/corpus/` holds real schemas — Chinook, Northwind and two taken from sqlite_orm's own test
suite — each next to the seed rows the tests run it with, and each naming where it came from and
under what licence. `tests/corpus_tests.cpp` runs the whole product path on every one of them:
build a database from the schema, generate the storage header for it, generate the code for a set
of queries, compile and link the lot against sqlite_orm, run it, and require that every row equals
both the literal written in the test and what SQLite itself answers on the same database. A query
the generator gets wrong today is pinned as `knownBad` with the card that tracks it, so the corpus
stays usable while the bug waits and goes red the day it is fixed.

Compiling and linking a program per schema costs seconds apiece, and the corpus grows with every
schema worth watching, so these cases are hidden from the default run and have a ctest name of
their own — `sqlite2orm_tests_corpus`, which CI selects along with the rest of the unit tests, so
they run on every pull request:

```bash
ctest --test-dir build -R sqlite2orm_tests_corpus
```

## Code style

The tree is formatted with **clang-format 19** using the `.clang-format` copied from
[sqlite_orm](https://github.com/fnc12/sqlite_orm). Other clang-format versions format this
tree differently, so use 19:

```bash
clang-format-19 -i $(find include src tests examples -name '*.h' -o -name '*.hpp' -o -name '*.cpp')
```

A pre-commit hook that checks the staged C++ files lives in `.githooks/`. It is **not**
installed automatically — enable it per clone with:

```bash
./scripts/install-git-hooks.sh
```

That sets `core.hooksPath` to `.githooks`; `git config --unset core.hooksPath` turns it off
again. Set `CLANG_FORMAT` to point the hook at a specific binary.

The whole tree is checked the same way by the `sqlite2orm_clang_format` test, which is
registered only when clang-format 19 is on `PATH`:

```bash
ctest --test-dir build -R "sqlite2orm_clang_format|sqlite2orm_git_hooks"
```

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

## Several worktrees over one clone

`git worktree` checkouts of one clone can sit behind paths only the process owning them can see — a
container mount, a network share, a disk that is not always attached. Every other process reads
those worktrees as `prunable`, and a single `git worktree prune` (including the one `git gc` runs)
deletes their administrative files. The owning checkout then answers every command with
`fatal: not a git repository: .../worktrees/<id>`, although no commit was lost.

`scripts/git-worktree-guard.sh` keeps that from happening, and repairs a clone where it already did:

| Command | Effect |
|---|---|
| `protect` | lock every registered worktree and record it in `.git/worktrees.manifest` |
| `check` | name every recorded worktree that lost its files or its lock, or has moved since |
| `restore` | rebuild the administrative files of every recorded worktree that lost them |
| `forget <id>` | drop one worktree from the manifest, for a checkout that is finished with |
| `unlock <id>` | drop one worktree's lock, so `git worktree remove` accepts it again |

Run `protect` from anywhere inside the repository, and run it again whenever a worktree's `HEAD`
moves — not only when one is added. What it records is a snapshot, so a worktree that has committed
since is recorded at the commit it left behind. It is cheap and idempotent, which is why it belongs
on every commit, checkout and branch switch a shared clone makes. Nothing enforces that rule, so
`check` compares each recorded `HEAD` with the one the worktree holds now and reports a record that
has fallen behind as `stale <id>`. A worktree on a branch does not go stale as the branch moves —
`ref: refs/heads/<branch>` stays true, and a restore from it loses nothing — but a detached one, or
one that switched branches, does.

`restore` rebuilds `HEAD`, `gitdir`, `commondir` and the index from the manifest, and names the
commit or branch it used (`restored <id> at <head>`). That is the recorded `HEAD`, which is not
necessarily the one the worktree had when it lost its files: **anything committed after the last
`protect` is not recovered**, and has to be found with `git fsck --lost-found`. The worktree itself
does not have to be visible from where `restore` runs. `protect` also writes
`refs/worktree-guard/<id>` for every worktree it records, so the commit in the manifest stays
reachable even once no worktree points at it, and the owner has something to compare a restored
checkout against. The first column of a manifest line is a directory name under `.git/worktrees`
and `restore` removes what it names, so an entry that is not a single path component — the kind of
slip a hand-edited path invites — is refused as `invalid id <id>` before anything is removed.

A locked worktree is one `git worktree prune` leaves alone — which is the point, and which also
means that once every worktree is protected, `git worktree prune` and `git gc` are safe to run in
this clone again. The price is that locked worktrees are held against removal as firmly as against
pruning: `git worktree remove` refuses one, and re-adding at the path of a locked missing worktree
fails with `fatal: '<path>' is a missing but locked worktree` and needs `add -f -f` where `add -f`
used to be enough. Run `unlock <id>` first in both cases. `forget <id>` then drops a worktree the
clone is really finished with from the manifest, and releases its ref; without it a retired
worktree stays `missing` in `check` for good and the next `restore` registers it again. Releasing
the ref is the half that matters beyond a green `check`: until `forget` runs, the recorded commit
and everything reachable from it are held against `git gc` for good. A worktree that is still
registered stays out of the manifest only until the next `protect`, and `check` reports it as
`unrecorded` in the meantime.

The manifest is keyed by the id git assigned the worktree, and git hands out the first free one by
basename (`work`, `work1`, …). Once a prune has emptied `.git/worktrees`, a worktree added at a
different path can therefore be given the id of a lost one, and the next `protect` overwrites that
entry — one more reason to `forget` a checkout the clone is finished with rather than leave it.

## License

MIT — see [LICENSE](LICENSE).
