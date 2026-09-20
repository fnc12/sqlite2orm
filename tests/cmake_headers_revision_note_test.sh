#!/bin/sh
# The revision guard can only compare revisions it was handed, so headers that are not a git
# checkout leave it skipping -- silently, in the middle of an 85-second run. That is the shape this
# card is about: headers of some other age, two runtime asserts red, and nothing naming the reason.
# The note under test is the one place that case is reported, so its text is pinned here, and so is
# its silence in the cases that are somebody else's to report.
set -e

module="$1"
cmake="$2"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

cat > "$dir/driver.cmake" <<DRIVER
include("$module")
sqlite2orm_headers_revision_note(
    "\${DIRECTORY}" "\${OVERRIDE}" "\${REVISION}" "\${HEAD}" note)
message("note>\${note}")
DRIVER

# The marker keeps an unrelated note on cmake's stderr out of the comparison; a driver that fails
# before reaching the message prints no marked line, and the empty answer never matches a case that
# expects text.
note_of() {
    "$cmake" -DDIRECTORY="$1" -DOVERRIDE="$2" -DREVISION="$3" -DHEAD="$4" \
        -P "$dir/driver.cmake" 2>&1 | sed -n 's/^note>//p'
}

pinned="eb77998ef5e27350b25977b061e46e202742ecc8"
populated="/work/build/_deps/sqlite_orm_headers-src"
override="/elsewhere/sqlite_orm"

# A populated dependency with no .git: the case a copied header tree lands in.
test "The sqlite_orm headers under test are in $populated, which is not a git checkout: nothing \
here can tell whether it holds $pinned, so the revision guard is off. Remove that directory and \
re-run cmake to populate it again." = "$(note_of "$populated" "" "$pinned" "")"

# The same, under a source override: removing that directory is the wrong advice for it, and the
# reader has to be sent to the directory cmake will never move.
test "The sqlite_orm headers under test come from $override, where \
FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS points, and that directory is not a git checkout: \
nothing here can tell whether it holds $pinned, so the revision guard is off. Check it out at \
$pinned, or configure without the override." = "$(note_of "$populated" "$override" "$pinned" "")"

# A revision that is known says nothing here, whether it is the pinned one or not: the guard in
# tests/sqlite_orm_revision_tests.cpp compares those two and fails on the mismatch by name.
test "" = "$(note_of "$populated" "" "$pinned" "$pinned")"
test "" = "$(note_of "$populated" "" "$pinned" "881bf2cf1ee6e8b0d0e0b0f4a5b9b0e5d7f5e1a2")"
test "" = "$(note_of "$populated" "$override" "$pinned" "$pinned")"

# A tree aimed off the pin gets the note about its own revision, not about the pinned one.
test "The sqlite_orm headers under test are in $populated, which is not a git checkout: nothing \
here can tell whether it holds dev, so the revision guard is off. Remove that directory and re-run \
cmake to populate it again." = "$(note_of "$populated" "" "dev" "")"
