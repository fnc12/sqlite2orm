#!/bin/sh
# The revision guard can only compare revisions it was handed, so headers that are not a git
# checkout -- and a machine with no git to ask -- leave it skipping, silently, in the middle of an
# 85-second run. That is the shape this card is about: headers of some other age, two runtime
# asserts red, and nothing naming the reason. The note under test is the one place those cases are
# reported, so its text is pinned here, and so is its silence in the cases that are somebody else's
# to report.
set -e

module="$1"
cmake="$2"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

cat > "$dir/driver.cmake" <<DRIVER
include("$module")
sqlite2orm_headers_revision_note(
    "\${DIRECTORY}" "\${SUBBUILD}" "\${OVERRIDE}" "\${GIT}" "\${REVISION}" "\${HEAD}" note)
message("note>\${note}")
DRIVER

# The marker keeps an unrelated note on cmake's stderr out of the comparison; a driver that fails
# before reaching the message prints no marked line, and the empty answer never matches a case that
# expects text.
note_of() {
    "$cmake" -DDIRECTORY="$1" -DSUBBUILD="$2" -DOVERRIDE="$3" -DGIT="$4" -DREVISION="$5" \
        -DHEAD="$6" -P "$dir/driver.cmake" 2>&1 | sed -n 's/^note>//p'
}

pinned="eb77998ef5e27350b25977b061e46e202742ecc8"
# The note only ever asks whether a git was found, never runs one, so a stand-in says more here
# than a path that would read like the answer depends on which git this machine has.
git="a-git-was-found"
# A directory that is there but holds no .git is the case a copied header tree lands in; one that
# is not there at all is what removing a populated dependency leaves behind, and the note tells
# them apart rather than reporting the first cause that fits.
populated="$dir/_deps/sqlite_orm_headers-src"
subbuild="$dir/_deps/sqlite_orm_headers-subbuild"
override="$dir/elsewhere/sqlite_orm"
gone="$dir/_deps/removed"
mkdir -p "$populated" "$override"

# No git at all: the directory may well be a checkout, and saying it is not would send its reader
# after a problem they do not have.
test "No git executable was found, so nothing here can check whether the sqlite_orm headers under \
test, in $populated, hold $pinned: the revision guard is off. Put git on PATH and re-run cmake." \
    = "$(note_of "$populated" "$subbuild" "" "" "$pinned" "")"
test "No git executable was found, so nothing here can check whether the sqlite_orm headers under \
test, in $populated, hold $pinned: the revision guard is off. Put git on PATH and re-run cmake." \
    = "$(note_of "$populated" "$subbuild" "$override" "" "$pinned" "")"

# A populated dependency with no .git. Removing it alone does not bring it back -- the sub-build
# has the clone step stamped as done -- and the one setting that lets this state reach a configure
# at all is the one that skips the clone step, so both halves are in the advice.
test "The sqlite_orm headers under test are in $populated, which is not a git checkout: nothing \
here can tell whether it holds $pinned, so the revision guard is off. Remove $subbuild and \
configure again with FETCHCONTENT_FULLY_DISCONNECTED off, which clones the headers afresh; \
removing the headers themselves does not, and leaves the tests aimed at a directory that is not \
there." = "$(note_of "$populated" "$subbuild" "" "$git" "$pinned" "")"

# The same dependency once somebody has removed it: same advice, different cause, and no claim
# about the contents of a directory that is not there.
test "The sqlite_orm headers under test are in $gone, which is not there: nothing here can tell \
whether it holds $pinned, so the revision guard is off. Remove $subbuild and configure again with \
FETCHCONTENT_FULLY_DISCONNECTED off, which clones the headers afresh; removing the headers \
themselves does not, and leaves the tests aimed at a directory that is not there." \
    = "$(note_of "$gone" "$subbuild" "" "$git" "$pinned" "")"

# Under a source override: removing anything is the wrong advice for it, because cmake never moves
# an override, so the reader is sent to the checkout itself.
test "The sqlite_orm headers under test come from $override, where \
FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS points, and that directory is not a git checkout: \
nothing here can tell whether it holds $pinned, so the revision guard is off. Check it out at \
$pinned, or configure without the override." \
    = "$(note_of "$override" "$subbuild" "$override" "$git" "$pinned" "")"
test "The sqlite_orm headers under test come from $gone, where \
FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS points, and that directory is not there: nothing here \
can tell whether it holds $pinned, so the revision guard is off. Check it out at $pinned, or \
configure without the override." = "$(note_of "$gone" "$subbuild" "$gone" "$git" "$pinned" "")"

# A revision that is known says nothing here, whether it is the pinned one or not: the guard in
# tests/sqlite_orm_revision_tests.cpp compares those two and fails on the mismatch by name.
test "" = "$(note_of "$populated" "$subbuild" "" "$git" "$pinned" "$pinned")"
test "" = "$(note_of "$populated" "$subbuild" "" "$git" "$pinned" "881bf2cf1ee6e8b0d0e0b0f4a5b9b0e5d7f5e1a2")"
test "" = "$(note_of "$override" "$subbuild" "$override" "$git" "$pinned" "$pinned")"

# A tree aimed off the pin gets the note about its own revision, not about the pinned one.
test "The sqlite_orm headers under test are in $populated, which is not a git checkout: nothing \
here can tell whether it holds dev, so the revision guard is off. Remove $subbuild and configure \
again with FETCHCONTENT_FULLY_DISCONNECTED off, which clones the headers afresh; removing the \
headers themselves does not, and leaves the tests aimed at a directory that is not there." \
    = "$(note_of "$populated" "$subbuild" "" "$git" "dev" "")"
