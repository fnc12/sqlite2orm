#!/bin/sh
# The pin only does its job if bumping it reaches the trees that are already configured. A
# `CACHE STRING` default is written once and wins over the file from then on, so a bump would move
# CI and leave every existing tree on the old headers, with the revision guard skipping because the
# revision it is handed is no longer the pinned one. The module under test therefore defaults the
# revision as a plain variable and lets only an explicit -D create a cache entry.
set -e

module="$1"
cmake="$2"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

mkdir "$dir/src"
cp "$module" "$dir/src/SqliteOrmPinnedRevision.cmake"
cat > "$dir/src/CMakeLists.txt" <<'PROJECT'
cmake_minimum_required(VERSION 3.16)
project(pinned_revision_probe NONE)
include("${CMAKE_CURRENT_LIST_DIR}/SqliteOrmPinnedRevision.cmake")
message(STATUS "[probe] revision=${SQLITE2ORM_SQLITE_ORM_REVISION}")
PROJECT

pinned=$(sed -n 's/^set(SQLITE2ORM_SQLITE_ORM_PINNED_REVISION "\(.*\)")$/\1/p' \
    "$dir/src/SqliteOrmPinnedRevision.cmake")
test -n "$pinned"
bumped="0123456789012345678901234567890123456789"
test "$pinned" != "$bumped"

configure() {
    tree="$1"
    shift
    "$cmake" -S "$dir/src" -B "$dir/$tree" "$@" 2>&1
}

revision_of() {
    configure "$@" | sed -n 's/^-- \[probe\] revision=//p'
}

guard_report_of() {
    configure "$@" | sed -n 's/^-- \(\[sqlite2orm\] This tree is configured against .*\)$/\1/p'
}

off_here() {
    printf '%s' "[sqlite2orm] This tree is configured against sqlite_orm $1, not the pinned $2: the revision guard is off here. Run cmake -U SQLITE2ORM_SQLITE_ORM_REVISION to follow the pin again."
}

# A tree that says nothing gets the pin, and its guard is on: nothing to report.
test "$pinned" = "$(revision_of plain)"
test "" = "$(guard_report_of plain)"

# A tree that names a revision gets that one, and says its guard is off.
test "dev" = "$(revision_of opted_out -DSQLITE2ORM_SQLITE_ORM_REVISION=dev)"
test "$(off_here dev "$pinned")" = "$(guard_report_of opted_out)"

# Bump the pin the way a bump does: by editing the file.
sed -i.bak "s/$pinned/$bumped/" "$dir/src/SqliteOrmPinnedRevision.cmake"
test "$bumped" = "$(sed -n 's/^set(SQLITE2ORM_SQLITE_ORM_PINNED_REVISION "\(.*\)")$/\1/p' \
    "$dir/src/SqliteOrmPinnedRevision.cmake")"

# The tree that never chose follows the bump on its next configure; a cached default would not.
test "$bumped" = "$(revision_of plain)"
test "" = "$(guard_report_of plain)"

# The tree that chose keeps its choice, and its report now names the new pin.
test "dev" = "$(revision_of opted_out)"
test "$(off_here dev "$bumped")" = "$(guard_report_of opted_out)"

# Unsetting the cache entry is the way back to following the pin.
test "$bumped" = "$(revision_of opted_out -USQLITE2ORM_SQLITE_ORM_REVISION)"
test "" = "$(guard_report_of opted_out)"
