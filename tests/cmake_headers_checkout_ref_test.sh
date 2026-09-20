#!/bin/sh
# README names exactly one thing to do with SQLITE2ORM_SQLITE_ORM_REVISION other than follow the
# pin -- aim a tree at the tip of `dev` -- and on a tree that had already been configured, the pin
# turned that into a configure error out of FetchContent's update step. A clone populated at a
# commit hash holds no local branches, so the bare name resolves only under refs/remotes/, the
# update step leaves it bare, and `git checkout dev` is ambiguous with the top-level dev/ directory
# sqlite_orm keeps. The module under test settles the name before FetchContent sees it.
#
# Half of this is that module's answers, which are cheap; the other half is the transition itself,
# because the answers were right in a tree where nothing had been populated yet and the failure
# only existed in one where something had. That half runs a real FetchContent_Populate against a
# local repository shaped like sqlite_orm -- a `dev` branch, a top-level dev/ directory -- so it
# needs no network.
set -e

# The probe project includes the module from a build tree of its own, so the path has to be one
# that means the same thing from anywhere.
module=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
git_executable="$2"
cmake="$3"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

remote="$dir/sqlite_orm"
mkdir -p "$remote/dev" "$remote/include"
in_remote() {
    "$git_executable" -C "$remote" -c user.name=probe -c user.email=probe@example.invalid "$@"
}
in_remote init -q
echo "the directory that makes a bare branch name ambiguous" > "$remote/dev/readme.md"
echo "#pragma once" > "$remote/include/sqlite_orm.h"
in_remote add -A
in_remote commit -qm "first"
pinned=$(in_remote rev-parse HEAD)
in_remote checkout -q -b dev
echo "// moved on" >> "$remote/include/sqlite_orm.h"
in_remote add -A
in_remote commit -qm "second"
dev_tip=$(in_remote rev-parse HEAD)
in_remote tag v1.0
in_remote checkout -q "$pinned"
test "$pinned" != "$dev_tip"

# The module's own answers. A repository that is not there stands in for a remote that cannot be
# reached: the name is then left as written, which is what every tree did before this module.
cat > "$dir/driver.cmake" <<DRIVER
include("$module")
sqlite2orm_headers_checkout_ref("\${REVISION}" "\${REPOSITORY}" "\${GIT}" ref)
message("ref>\${ref}")
DRIVER

ref_of() {
    "$cmake" -DREVISION="$1" -DREPOSITORY="$2" -DGIT="$3" -P "$dir/driver.cmake" 2>&1 |
        sed -n 's/^ref>//p'
}

there="file://$remote"
unreachable="file://$dir/no-such-repository"

test "origin/dev" = "$(ref_of dev "$there" "$git_executable")"
test "v1.0" = "$(ref_of v1.0 "$there" "$git_executable")"
test "$pinned" = "$(ref_of "$pinned" "$there" "$git_executable")"
test "no-such-ref" = "$(ref_of no-such-ref "$there" "$git_executable")"
# Already spelled the way the answer would be: refs/heads/origin/dev is nothing, so it is kept.
test "origin/dev" = "$(ref_of origin/dev "$there" "$git_executable")"
test "dev" = "$(ref_of dev "$there" "")"
test "dev" = "$(ref_of dev "$unreachable" "$git_executable")"

# A hash is the shape the pin has, and it is not asked about at all: every configure that follows
# the pin would otherwise reach the remote before FetchContent decides whether it needs to. Both
# halves of that are the same answer, so what separates them is whether git ran -- recorded here,
# together with the one command this module is allowed to run.
cat > "$dir/recording-git" <<RECORDER
#!/bin/sh
echo "\$*" >> "$dir/git-calls"
exec "$git_executable" "\$@"
RECORDER
chmod +x "$dir/recording-git"

: > "$dir/git-calls"
test "$pinned" = "$(ref_of "$pinned" "$there" "$dir/recording-git")"
test "" = "$(cat "$dir/git-calls")"

: > "$dir/git-calls"
test "origin/dev" = "$(ref_of dev "$there" "$dir/recording-git")"
test "ls-remote --heads $there refs/heads/dev" = "$(cat "$dir/git-calls")"

# The transition, against a real FetchContent_Populate.
cat > "$dir/CMakeLists.txt" <<'PROJECT'
cmake_minimum_required(VERSION 3.16)
project(headers_checkout_ref_probe NONE)
include(FetchContent)
include("${MODULE}")
sqlite2orm_headers_checkout_ref("${REVISION}" "${REPOSITORY}" "${GIT}" ref)
FetchContent_Declare(headers GIT_REPOSITORY ${REPOSITORY} GIT_TAG ${ref})
FetchContent_Populate(headers)
PROJECT

configure() {
    tree="$1"
    shift
    "$cmake" -S "$dir" -B "$dir/$tree" -DMODULE="$module" -DREPOSITORY="$there" \
        -DGIT="$git_executable" "$@" > "$dir/$tree.log" 2>&1
}

head_of() {
    "$git_executable" -C "$dir/$1/_deps/headers-src" rev-parse HEAD
}

# A tree on the pin, the state every checkout of this repository starts in.
configure pinned -DREVISION="$pinned"
test "$pinned" = "$(head_of pinned)"

# What README tells its reader to do with that tree.
configure pinned -DREVISION=dev
test "$dev_tip" = "$(head_of pinned)"

# And back, which is the other half of trying a branch out.
configure pinned -DREVISION="$pinned"
test "$pinned" = "$(head_of pinned)"

# A tag from the same tree: passed through as written, and it still moves the headers.
configure pinned -DREVISION=v1.0
test "$dev_tip" = "$(head_of pinned)"

# A tree that names the branch from the start never had the local-branch problem; keep it honest
# that the rewritten name works there too.
configure fresh -DREVISION=dev
test "$dev_tip" = "$(head_of fresh)"
