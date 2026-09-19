#!/bin/sh
# Keep the administrative files of this repository's worktrees alive.
#
# Several checkouts of one clone can live behind mount points that only the process owning them can
# see: the path recorded in `.git/worktrees/<id>/gitdir` exists for that process and nowhere else.
# Every other process then reads those worktrees as `prunable`, and any `git worktree prune` -- the
# one a person types as much as the one `git gc` runs -- deletes their administrative files. The
# owning process is left with `fatal: not a git repository: .../worktrees/<id>` even though no
# commit was lost.
#
# `protect` locks every registered worktree, which is what `git worktree lock` is documented for,
# and records it in a manifest kept next to `.git/worktrees` so that `restore` can rebuild what an
# earlier prune already removed. Run it from anywhere inside the repository.
#
# What the manifest holds is a snapshot: the HEAD a worktree had when `protect` last ran. A worktree
# that has committed since is recorded at the commit it left behind, so `protect` belongs on every
# HEAD change and not only on every `git worktree add`, and `restore` names the HEAD it used rather
# than claiming the checkout came back where it was.
set -e

lock_reason='registered path is not visible from every process sharing this clone'
guard_refs='refs/worktree-guard'

usage() {
    cat <<'EOF'
usage: git-worktree-guard.sh <command> [<id>...]

  protect       lock every registered worktree and record it in the manifest
  check         name every recorded worktree that lost its administrative files or its lock
  restore       rebuild the administrative files of every recorded worktree that lost them
  forget <id>   drop one worktree from the manifest, for a checkout that is finished with
  unlock <id>   drop one worktree's lock, so `git worktree remove` accepts it again
EOF
}

# Ids of the worktrees the repository currently knows about, main worktree excluded: it has no
# administrative directory of its own and nothing can prune it.
registered_ids() {
    [ -d "$common/worktrees" ] || return 0
    for dir in "$common"/worktrees/*/; do
        [ -d "$dir" ] || continue
        basename "$dir"
    done | LC_ALL=C sort
}

# `git worktree lock` and `git worktree unlock` name a worktree by its path, not by the id of its
# administrative directory, and the two differ as soon as two worktrees share a directory name. The
# path is the one the `gitdir` file points into; it does not have to exist here.
worktree_path() {
    printf '%s\n' "${1%/.git}"
}

manifest_ids() {
    [ -f "$manifest" ] || return 0
    cut -f 1 "$manifest"
}

# A recorded HEAD is either `ref: refs/heads/<branch>` or a raw object name. This prints the commit
# it stands for, and fails when there is no longer one.
resolved_commit() {
    case "$1" in
        'ref: '*) git rev-parse --verify --quiet "${1#ref: }^{commit}" ;;
        *) git rev-parse --verify --quiet "$1^{commit}" ;;
    esac
}

# The same HEAD as something to print: a ref name stays a ref name, an object name stays itself.
display_head() {
    printf '%s\n' "${1#ref: }"
}

# The three files an administrative directory needs before its worktree is a repository again. A
# directory holding fewer than that is a restore that did not finish, not a worktree to leave alone.
admin_complete() {
    [ -f "$1/HEAD" ] && [ -f "$1/gitdir" ] && [ -f "$1/commondir" ]
}

cmd_protect() {
    tmp=$(mktemp "$manifest.XXXXXX")
    ids=$(mktemp "$manifest.XXXXXX")
    trap 'rm -f "$tmp" "$ids"' EXIT INT TERM
    registered_ids > "$ids"
    status=0
    while read -r id; do
        [ -n "$id" ] || continue
        dir="$common/worktrees/$id"
        gitdir=$(cat "$dir/gitdir")
        head=$(cat "$dir/HEAD")
        printf '%s\t%s\t%s\n' "$id" "$gitdir" "$head" >> "$tmp"
        # A ref of our own keeps the recorded commit reachable, so a later `git gc` cannot turn a
        # detached worktree's HEAD into something `restore` has to call `unresolvable`, and leaves
        # the owner something to compare a restored checkout against.
        if commit=$(resolved_commit "$head") && ! git update-ref "$guard_refs/$id" "$commit"; then
            echo "unrecordable $id" >&2
            status=1
        fi
        if [ -f "$dir/locked" ]; then
            echo "already-locked $id"
        elif git worktree lock --reason "$lock_reason" "$(worktree_path "$gitdir")"; then
            echo "locked $id"
        else
            # One worktree git refuses to lock is not a reason to drop the recording of the others.
            echo "unlockable $id" >&2
            status=1
        fi
    done < "$ids"
    # A worktree whose administrative files are already gone stays in the manifest: it is the only
    # record left of where it was and what it had checked out.
    if [ -f "$manifest" ]; then
        while IFS=$(printf '\t') read -r id gitdir head; do
            [ -n "$id" ] || continue
            [ -d "$common/worktrees/$id" ] || printf '%s\t%s\t%s\n' "$id" "$gitdir" "$head"
        done < "$manifest" >> "$tmp"
    fi
    LC_ALL=C sort -o "$tmp" "$tmp"
    mv "$tmp" "$manifest"
    return $status
}

cmd_check() {
    status=0
    if [ -f "$manifest" ]; then
        while IFS=$(printf '\t') read -r id gitdir head; do
            [ -n "$id" ] || continue
            dir="$common/worktrees/$id"
            if [ ! -d "$dir" ]; then
                echo "missing $id"
                status=1
            elif ! admin_complete "$dir"; then
                echo "incomplete $id"
                status=1
            elif [ ! -f "$dir/locked" ]; then
                echo "unlocked $id"
                status=1
            else
                echo "ok $id"
            fi
        done < "$manifest"
    fi
    # A worktree the manifest never saw cannot be restored from it, so it is worth a line too.
    unrecorded=$(registered_ids | while read -r id; do
        manifest_ids | grep -qxF "$id" || echo "unrecorded $id"
    done)
    if [ -n "$unrecorded" ]; then
        printf '%s\n' "$unrecorded"
        status=1
    fi
    return $status
}

cmd_restore() {
    if [ ! -f "$manifest" ]; then
        echo "no manifest at $manifest" >&2
        return 1
    fi
    status=0
    while IFS=$(printf '\t') read -r id gitdir head; do
        [ -n "$id" ] || continue
        dir="$common/worktrees/$id"
        if [ -d "$dir" ] && admin_complete "$dir"; then
            echo "ok $id"
            continue
        fi
        if ! resolved_commit "$head" > /dev/null; then
            echo "unresolvable $id"
            status=1
            continue
        fi
        rm -rf "$dir"
        mkdir -p "$dir"
        printf '%s\n' "$head" > "$dir/HEAD"
        printf '%s\n' "$gitdir" > "$dir/gitdir"
        printf '../..\n' > "$dir/commondir"
        printf '%s\n' "$lock_reason" > "$dir/locked"
        # The index went with the administrative directory. Rebuilding it from HEAD leaves the
        # owning checkout clean instead of reporting every tracked file as deleted; the working
        # tree itself is never touched, so it does not have to be visible from here.
        if ! GIT_DIR="$dir" GIT_INDEX_FILE="$dir/index" git read-tree HEAD; then
            # Half an administrative directory is worse than none: `check` would call it a worktree.
            rm -rf "$dir"
            echo "failed $id" >&2
            status=1
            continue
        fi
        # The HEAD written back is the one the last `protect` recorded, which is not necessarily the
        # one the worktree had when it lost its files. Saying which commit it came back on is the
        # difference between a repair the owner can check and one that silently rewinds them.
        echo "restored $id at $(display_head "$head")"
    done < "$manifest"
    return $status
}

cmd_forget() {
    if [ "$#" -eq 0 ]; then
        echo "forget needs at least one worktree id" >&2
        return 1
    fi
    if [ ! -f "$manifest" ]; then
        echo "no manifest at $manifest" >&2
        return 1
    fi
    status=0
    for id in "$@"; do
        if ! manifest_ids | grep -qxF "$id"; then
            echo "no recorded worktree $id" >&2
            status=1
            continue
        fi
        tmp=$(mktemp "$manifest.XXXXXX")
        trap 'rm -f "$tmp"' EXIT INT TERM
        while IFS=$(printf '\t') read -r recorded gitdir head; do
            [ -n "$recorded" ] || continue
            [ "$recorded" = "$id" ] || printf '%s\t%s\t%s\n' "$recorded" "$gitdir" "$head"
        done < "$manifest" > "$tmp"
        mv "$tmp" "$manifest"
        # The ref exists to keep a recorded commit reachable for a restore that can still happen.
        git update-ref -d "$guard_refs/$id"
        echo "forgot $id"
    done
    return $status
}

cmd_unlock() {
    if [ "$#" -eq 0 ]; then
        echo "unlock needs at least one worktree id" >&2
        return 1
    fi
    for id in "$@"; do
        if [ ! -d "$common/worktrees/$id" ]; then
            echo "no worktree $id" >&2
            return 1
        fi
        if [ -f "$common/worktrees/$id/locked" ]; then
            git worktree unlock "$(worktree_path "$(cat "$common/worktrees/$id/gitdir")")"
        fi
        echo "unlocked $id"
    done
}

command=${1:-}
if [ "$#" -gt 0 ]; then
    shift
fi

common=$(git rev-parse --git-common-dir)
common=$(cd "$common" && pwd)
manifest="$common/worktrees.manifest"

case "$command" in
    protect) cmd_protect ;;
    check) cmd_check ;;
    restore) cmd_restore ;;
    forget) cmd_forget "$@" ;;
    unlock) cmd_unlock "$@" ;;
    -h|--help|help) usage ;;
    *) usage >&2; exit 2 ;;
esac
