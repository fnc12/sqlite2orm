#!/bin/sh
# A failing formatting check has to name the files it is failing over. `ctest --output-on-failure`
# is all a contributor sees, so a check that only sets an exit status leaves them nothing to act on.
set -e

clang_format="$1"
root="$2"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

# clang_format_check.sh walks include/ src/ tests/ examples/, so a throwaway tree needs all four.
mkdir -p "$dir/tree/include" "$dir/tree/src" "$dir/tree/tests" "$dir/tree/examples"
cp "$root/.clang-format" "$dir/tree/.clang-format"
cat > "$dir/tree/src/bad.cpp" <<'EOF'
int main() {
    if(1) {
        return 0;
    }
    return 1;
}
EOF
cat > "$dir/tree/include/bad.h" <<'EOF'
inline int f() {
    if(1) {
        return 0;
    }
    return 1;
}
EOF

status=0
sh "$root/tests/clang_format_check.sh" "$clang_format" "$dir/tree" > "$dir/out.txt" 2>&1 || status=$?
test "$status" -ne 0

# Every offending file is named, and nothing else is.
sed -n 's/^> \(.*\):[0-9][0-9]*:[0-9][0-9]*: error: .*$/\1/p' "$dir/out.txt" | sort -u > "$dir/named.txt"
cat > "$dir/named.expected" <<'EOF'
include/bad.h
src/bad.cpp
EOF
diff "$dir/named.expected" "$dir/named.txt"

# Once they are formatted the same check is silent and succeeds.
"$clang_format" --style=file -i "$dir/tree/src/bad.cpp" "$dir/tree/include/bad.h"
sh "$root/tests/clang_format_check.sh" "$clang_format" "$dir/tree" > "$dir/out2.txt" 2>&1
test ! -s "$dir/out2.txt"
