#!/bin/sh
# Every committed C++ source must already be what clang-format 19 produces: the lint that runs
# on CI checks the whole tree, so a single stale file turns every pull request red.
set -e

clang_format="$1"
root="$2"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

cd "$root"
find include src tests examples \
    \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \) -print > "$dir/files.txt"
test -s "$dir/files.txt"

xargs "$clang_format" --style=file --dry-run -Werror < "$dir/files.txt" > "$dir/out.txt" 2>&1

# A clean tree produces no diagnostics at all.
: > "$dir/expected.txt"
diff "$dir/expected.txt" "$dir/out.txt"
