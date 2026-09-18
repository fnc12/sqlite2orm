#!/bin/sh
# The pre-commit hook from .githooks/ must reject a commit whose staged C++ sources clang-format
# 19 would reformat, and let a formatted one through. It runs against a throwaway repository so
# that it never touches this clone's own hook configuration.
set -e

clang_format="$1"
root="$2"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

git init -q "$dir/repo"
cd "$dir/repo"
git config user.email hook-test@example.com
git config user.name "Hook Test"

cp "$root/.clang-format" .
mkdir .githooks scripts
cp "$root/.githooks/pre-commit" .githooks/pre-commit
cp "$root/scripts/install-git-hooks.sh" scripts/install-git-hooks.sh

sh scripts/install-git-hooks.sh > "$dir/install.txt"
cat > "$dir/install.expected" <<'EOF'
Installed the repository git hooks: core.hooksPath is now .githooks
EOF
diff "$dir/install.expected" "$dir/install.txt"

CLANG_FORMAT="$clang_format"
export CLANG_FORMAT

# `if(cond)` is what this style spells `if (cond)`, so the hook must refuse this file.
cat > bad.cpp <<'EOF'
int main() {
    if(1) {
        return 0;
    }
    return 1;
}
EOF
git add .clang-format .githooks scripts bad.cpp

status=0
git commit -q -m "unformatted" > "$dir/out.txt" 2> "$dir/err.txt" || status=$?
test "$status" -eq 1
cat > "$dir/err.expected" <<'EOF'
pre-commit: bad.cpp is not formatted according to .clang-format.
pre-commit: run clang-format 19 with -i on those files and stage them again.
EOF
diff "$dir/err.expected" "$dir/err.txt"

# The commit was refused, so the repository still has no commits.
test "$(git rev-list --count --all)" -eq 0

# Only the staged content counts: formatting the file on disk without staging it changes nothing.
"$clang_format" --style=file -i bad.cpp
status=0
git commit -q -m "unformatted" > "$dir/out2.txt" 2> "$dir/err2.txt" || status=$?
test "$status" -eq 1
diff "$dir/err.expected" "$dir/err2.txt"

# Staging the formatted file lets the same commit through.
git add bad.cpp
git commit -q -m "formatted" > "$dir/out3.txt" 2> "$dir/err3.txt"
test ! -s "$dir/err3.txt"
test "$(git rev-list --count --all)" -eq 1

# Files the hook does not own are none of its business.
printf 'not   formatted C++\n' > notes.txt
git add notes.txt
git commit -q -m "notes" > "$dir/out4.txt" 2> "$dir/err4.txt"
test ! -s "$dir/err4.txt"
test "$(git rev-list --count --all)" -eq 2
