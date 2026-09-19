#!/bin/sh
# `git -C <dir> rev-parse HEAD` answers for the enclosing repository when <dir> is not a checkout of
# its own, and the sqlite_orm headers live under build/_deps, inside this repository. A guard built
# on the bare query would report a sqlite2orm commit as the sqlite_orm revision, so the function
# under test has to answer with nothing whenever the directory is not the top of a checkout itself.
set -e

module="$1"
git="$2"
cmake="$3"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

cat > "$dir/driver.cmake" <<DRIVER
include("$module")
sqlite2orm_git_revision_of("\${DIRECTORY}" "$git" revision)
message("[\${revision}]")
DRIVER

revision_of() {
    "$cmake" -DDIRECTORY="$1" -P "$dir/driver.cmake" 2>&1
}

commit_in() {
    "$git" -C "$1" -c user.name=test -c user.email=test@example.com \
        -c commit.gpgsign=false commit --quiet --allow-empty -m "$2"
}

# An enclosing checkout, standing in for this repository, with a build directory inside it.
"$git" init --quiet "$dir/outer"
commit_in "$dir/outer" "outer"
outer_head=$("$git" -C "$dir/outer" rev-parse HEAD)
mkdir -p "$dir/outer/build/_deps"

# A dependency tree that carries no .git of its own: the bare query would answer $outer_head here.
mkdir -p "$dir/outer/build/_deps/headers-nogit"
test "[]" = "$(revision_of "$dir/outer/build/_deps/headers-nogit")"

# A checkout of its own in the same place: this one really is its revision.
"$git" init --quiet "$dir/outer/build/_deps/headers-src"
commit_in "$dir/outer/build/_deps/headers-src" "headers"
headers_head=$("$git" -C "$dir/outer/build/_deps/headers-src" rev-parse HEAD)
test "[$headers_head]" = "$(revision_of "$dir/outer/build/_deps/headers-src")"
test "$headers_head" != "$outer_head"

# A subdirectory of a checkout is not that checkout: it gets no revision either.
mkdir -p "$dir/outer/build/_deps/headers-src/include"
test "[]" = "$(revision_of "$dir/outer/build/_deps/headers-src/include")"

# The same checkout reached through a symlink still matches: the comparison is over real paths.
ln -s "$dir/outer/build/_deps/headers-src" "$dir/headers-link"
test "[$headers_head]" = "$(revision_of "$dir/headers-link")"

# Outside any repository, and pointed at a directory that does not exist at all.
test "[]" = "$(revision_of "$dir")"
test "[]" = "$(revision_of "$dir/missing")"

# Without git there is nothing to ask.
cat > "$dir/nogit.cmake" <<DRIVER
include("$module")
sqlite2orm_git_revision_of("$dir/outer/build/_deps/headers-src" "" revision)
message("[\${revision}]")
DRIVER
test "[]" = "$("$cmake" -P "$dir/nogit.cmake" 2>&1)"
