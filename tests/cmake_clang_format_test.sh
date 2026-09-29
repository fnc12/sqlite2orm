#!/bin/sh
# CI has no clang-format 19, and without one the formatting tests are never registered, so the
# module under test downloads the pinned one where it is asked to -- by default wherever `CI` is
# set. The download is played here against a fake wheel on disk: the module must take it only when
# no clang-format 19 is at hand, check its hash, and stop the configure rather than skip the tests
# when a download that was asked for fails.
set -e

module="$1"
cmake="$2"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

mkdir "$dir/src"
cp "$module" "$dir/src/SqliteOrmClangFormat.cmake"
cat > "$dir/src/CMakeLists.txt" <<'PROJECT'
cmake_minimum_required(VERSION 3.16)
project(clang_format_probe NONE)
include("${CMAKE_CURRENT_LIST_DIR}/SqliteOrmClangFormat.cmake")
message(STATUS "[probe] executable=${SQLITE2ORM_CLANG_FORMAT_EXECUTABLE}")
PROJECT

fake_clang_format() {
    mkdir -p "$(dirname "$1")"
    printf '#!/bin/sh\necho "clang-format version %s (fake)"\n' "$2" > "$1"
    chmod +x "$1"
}

fake_clang_format "$dir/old/clang-format" 18.1.3
fake_clang_format "$dir/installed/clang-format" 19.1.0

# The wheel is a zip with the binary where the real one keeps it, and without its executable bit.
mkdir -p "$dir/wheel/clang_format/data/bin"
printf '#!/bin/sh\necho "clang-format version 19.1.7 (fake)"\n' > "$dir/wheel/clang_format/data/bin/clang-format"
chmod -x "$dir/wheel/clang_format/data/bin/clang-format"
(cd "$dir/wheel" && "$cmake" -E tar cf "$dir/fake.whl" --format=zip clang_format)
sha=$("$cmake" -E sha256sum "$dir/fake.whl" | cut -d' ' -f1)
url="file://$dir/fake.whl"
wrong_sha=0000000000000000000000000000000000000000000000000000000000000000

# Every tree starts from an old clang-format, which is what the runners have when they have one.
configure() {
    tree="$1"
    shift
    env -u CI "$cmake" -S "$dir/src" -B "$dir/$tree" \
        "-DSQLITE2ORM_CLANG_FORMAT_EXECUTABLE=$dir/old/clang-format" "$@" > "$dir/$tree.txt" 2>&1
}

report_of() {
    sed -n 's/^-- \[sqlite2orm\] //p; s/^-- \[probe\] //p' "$dir/$1.txt"
}

# Off, which is the default outside CI: the old one is refused and the tests are skipped.
configure off
test "$(report_of off)" = "clang-format 19 not found, skipping the formatting tests.
executable="

# On: the wheel is downloaded, and its binary comes out runnable.
configure on -DSQLITE2ORM_FETCH_CLANG_FORMAT=ON \
    "-DSQLITE2ORM_CLANG_FORMAT_WHEEL_URL=$url" "-DSQLITE2ORM_CLANG_FORMAT_WHEEL_SHA256=$sha"
test "$(report_of on)" = "Using the downloaded clang-format 19: $dir/on/clang-format-19/clang-format
executable=$dir/on/clang-format-19/clang-format"
test "$("$dir/on/clang-format-19/clang-format" --version)" = "clang-format version 19.1.7 (fake)"

# A reconfigure keeps the wheel it has: with the wheel gone from its URL, it still succeeds.
mv "$dir/fake.whl" "$dir/moved.whl"
configure on
test "$(report_of on)" = "Using the downloaded clang-format 19: $dir/on/clang-format-19/clang-format
executable=$dir/on/clang-format-19/clang-format"
mv "$dir/moved.whl" "$dir/fake.whl"

# `CI` in the environment turns it on without being asked.
CI=true "$cmake" -S "$dir/src" -B "$dir/ci" "-DSQLITE2ORM_CLANG_FORMAT_EXECUTABLE=$dir/old/clang-format" \
    "-DSQLITE2ORM_CLANG_FORMAT_WHEEL_URL=$url" "-DSQLITE2ORM_CLANG_FORMAT_WHEEL_SHA256=$sha" > "$dir/ci.txt" 2>&1
test "$(report_of ci)" = "Using the downloaded clang-format 19: $dir/ci/clang-format-19/clang-format
executable=$dir/ci/clang-format-19/clang-format"

# An installed clang-format 19 wins, and nothing is downloaded: the URL here leads nowhere.
configure installed -DSQLITE2ORM_FETCH_CLANG_FORMAT=ON \
    "-DSQLITE2ORM_CLANG_FORMAT_EXECUTABLE=$dir/installed/clang-format" \
    "-DSQLITE2ORM_CLANG_FORMAT_WHEEL_URL=file://$dir/missing.whl" "-DSQLITE2ORM_CLANG_FORMAT_WHEEL_SHA256=$sha"
test "$(report_of installed)" = "executable=$dir/installed/clang-format"
test ! -e "$dir/installed/clang-format-19"

# A wheel that does not match its hash stops the configure instead of skipping the tests. The
# words are cmake's own and vary with its version, so only the outcome is checked.
status=0
configure wrong_hash -DSQLITE2ORM_FETCH_CLANG_FORMAT=ON \
    "-DSQLITE2ORM_CLANG_FORMAT_WHEEL_URL=$url" "-DSQLITE2ORM_CLANG_FORMAT_WHEEL_SHA256=$wrong_sha" || status=$?
test "$status" -ne 0
test ! -e "$dir/wrong_hash/clang-format-19/clang-format"

# cmake wraps a fatal message over indented lines; put it back on one.
error_of() {
    awk '/^CMake Error at .*\(message\):$/ { on = 1; next }
         on && /^Call Stack/ { exit }
         on { sub(/^  /, ""); printf "%s%s", sep, $0; sep = " " }' "$dir/$1.txt"
}

# A wheel with the right hash and the wrong contents stops it too.
mkdir -p "$dir/empty/clang_format"
: > "$dir/empty/clang_format/__init__.py"
(cd "$dir/empty" && "$cmake" -E tar cf "$dir/empty.whl" --format=zip clang_format)
empty_sha=$("$cmake" -E sha256sum "$dir/empty.whl" | cut -d' ' -f1)
status=0
configure empty -DSQLITE2ORM_FETCH_CLANG_FORMAT=ON \
    "-DSQLITE2ORM_CLANG_FORMAT_WHEEL_URL=file://$dir/empty.whl" "-DSQLITE2ORM_CLANG_FORMAT_WHEEL_SHA256=$empty_sha" \
    || status=$?
test "$status" -ne 0
test "$(error_of empty)" = "[sqlite2orm] file://$dir/empty.whl does not unpack to clang_format/data/bin/clang-format."

# So does one whose binary is some other version.
fake_clang_format "$dir/other/clang_format/data/bin/clang-format" 18.1.8
(cd "$dir/other" && "$cmake" -E tar cf "$dir/other.whl" --format=zip clang_format)
other_sha=$("$cmake" -E sha256sum "$dir/other.whl" | cut -d' ' -f1)
status=0
configure other -DSQLITE2ORM_FETCH_CLANG_FORMAT=ON \
    "-DSQLITE2ORM_CLANG_FORMAT_WHEEL_URL=file://$dir/other.whl" "-DSQLITE2ORM_CLANG_FORMAT_WHEEL_SHA256=$other_sha" \
    || status=$?
test "$status" -ne 0
test "$(error_of other)" = "[sqlite2orm] The clang-format downloaded from file://$dir/other.whl does not report version 19."
