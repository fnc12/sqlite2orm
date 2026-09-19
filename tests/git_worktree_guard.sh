#!/bin/sh
# `scripts/git-worktree-guard.sh` has to survive the case it exists for: a clone whose worktrees are
# registered under paths only their own process can see, where every `git worktree prune` -- and the
# one `git gc` runs -- would otherwise delete their administrative files.
set -e

if [ "$#" -eq 0 ]; then
    echo "usage: git_worktree_guard.sh <path-to-git-worktree-guard.sh> [<git-executable>]" >&2
    exit 2
fi
# The test runs from a temporary clone, so the script it drives has to be named from where the
# caller stood rather than from where the test ends up.
guard=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
# Both this test and the script under test have to meet the same git the build found.
if [ -n "${2:-}" ]; then
    PATH=$(cd "$(dirname "$2")" && pwd):$PATH
    export PATH
fi

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

# Every line the guard prints is `<word> <id>`. Ordering a file by that id lets a case whose id git
# chose for itself still be compared whole, without pinning where that id happens to sort.
by_id() {
    LC_ALL=C sort -k 2,2 -o "$1" "$1"
}

git init -q -b master "$dir/repo"
cd "$dir/repo"
git config user.email s2o@example.com
git config user.name sqlite2orm
echo one > tracked.txt
git add tracked.txt
git commit -q -m one

git worktree add -q "$dir/wtA" -b feat
git worktree add -q --detach "$dir/wtB"
# A detached worktree that has committed holds a commit no branch reaches: exactly what a prune
# followed by a garbage collection would otherwise take away with its reflog.
echo two > "$dir/wtB/tracked.txt"
git -C "$dir/wtB" commit -q -am two
detached=$(git -C "$dir/wtB" rev-parse HEAD)

# `protect` locks both and records them.
sh "$guard" protect > "$dir/protect.txt"
cat > "$dir/protect.expected" <<'EOF'
locked wtA
locked wtB
EOF
diff "$dir/protect.expected" "$dir/protect.txt"

# Each recorded worktree also gets a ref of its own, so the commit the manifest names stays
# reachable however long the worktree is gone, and the owner has something to compare against.
test "$(git rev-parse refs/worktree-guard/wtB)" = "$detached"
test "$(git rev-parse refs/worktree-guard/wtA)" = "$(git rev-parse refs/heads/feat)"

# The paths go out of sight, which is what a container boundary does to them. Pruning now -- with an
# expiry that spares nothing -- has to leave both administrative directories where they are.
mv "$dir/wtA" "$dir/wtA.away"
mv "$dir/wtB" "$dir/wtB.away"
git worktree prune --expire now --verbose > "$dir/prune.txt"
test ! -s "$dir/prune.txt"
test -d .git/worktrees/wtA
test -d .git/worktrees/wtB

sh "$guard" check > "$dir/check.txt"
cat > "$dir/check.expected" <<'EOF'
ok wtA
ok wtB
EOF
diff "$dir/check.expected" "$dir/check.txt"

mv "$dir/wtA.away" "$dir/wtA"
mv "$dir/wtB.away" "$dir/wtB"

# What a prune that ran before the guard did leaves behind: the worktrees are no longer repositories.
rm -rf .git/worktrees
status=0
git -C "$dir/wtA" rev-parse HEAD > /dev/null 2>&1 || status=$?
test "$status" -ne 0

status=0
sh "$guard" check > "$dir/check_lost.txt" || status=$?
test "$status" -eq 1
cat > "$dir/check_lost.expected" <<'EOF'
missing wtA
missing wtB
EOF
diff "$dir/check_lost.expected" "$dir/check_lost.txt"

# `protect` is what a person reaches for first on a clone that has already been pruned -- stop the
# bleeding, then restore -- and by then the manifest is the only record left of where those
# worktrees were. Running it while their administrative directories are gone has to carry every
# entry forward rather than record the empty truth it can see.
cp .git/worktrees.manifest "$dir/manifest_pruned.before"
sh "$guard" protect > "$dir/protect_pruned.txt"
test ! -s "$dir/protect_pruned.txt"
diff "$dir/manifest_pruned.before" .git/worktrees.manifest

# The recorded commit survives a garbage collection that has no other reason to keep it: the
# detached worktree that held it is not a repository any more, and its reflog went with it.
git -c gc.reflogExpire=now -c gc.reflogExpireUnreachable=now gc --prune=now -q
test "$(git rev-parse refs/worktree-guard/wtB)" = "$detached"
git cat-file -e "$detached^{commit}"

# `restore` names the HEAD it wrote back, which is the one the last `protect` recorded rather than
# necessarily the one the worktree had when it lost its files.
sh "$guard" restore > "$dir/restore.txt"
cat > "$dir/restore.expected" <<EOF
restored wtA at refs/heads/feat
restored wtB at $detached
EOF
diff "$dir/restore.expected" "$dir/restore.txt"

# Both are repositories again, on what they had checked out, with an index that matches the working
# tree rather than one that reports every tracked file as deleted.
test "$(git -C "$dir/wtA" rev-parse --abbrev-ref HEAD)" = feat
test "$(git -C "$dir/wtB" rev-parse HEAD)" = "$detached"
git -C "$dir/wtA" status --porcelain > "$dir/wtA_status.txt"
test ! -s "$dir/wtA_status.txt"

# Restoring is idempotent, and the worktrees come back locked.
sh "$guard" restore > "$dir/restore_again.txt"
diff "$dir/check.expected" "$dir/restore_again.txt"
sh "$guard" check > "$dir/check_restored.txt"
diff "$dir/check.expected" "$dir/check_restored.txt"

# A directory holding fewer files than a worktree needs is a restore that did not finish, and
# neither command may take it for a worktree that is already back.
rm .git/worktrees/wtB/commondir
status=0
sh "$guard" check > "$dir/check_incomplete.txt" || status=$?
test "$status" -eq 1
cat > "$dir/check_incomplete.expected" <<'EOF'
ok wtA
incomplete wtB
EOF
diff "$dir/check_incomplete.expected" "$dir/check_incomplete.txt"

# `protect` cannot read that directory either, and one of those is no more a reason to leave the
# other worktrees unrecorded than one lock git refuses: it names the bad one on stderr, exits
# non-zero, and leaves the entry the manifest already held for it alone.
cp .git/worktrees.manifest "$dir/manifest_incomplete.before"
status=0
sh "$guard" protect > "$dir/protect_incomplete.txt" 2> "$dir/protect_incomplete.err" || status=$?
test "$status" -eq 1
cat > "$dir/protect_incomplete.expected" <<'EOF'
already-locked wtA
EOF
diff "$dir/protect_incomplete.expected" "$dir/protect_incomplete.txt"
cat > "$dir/protect_incomplete.err.expected" <<'EOF'
incomplete wtB
EOF
diff "$dir/protect_incomplete.err.expected" "$dir/protect_incomplete.err"
diff "$dir/manifest_incomplete.before" .git/worktrees.manifest

sh "$guard" restore > "$dir/restore_incomplete.txt"
cat > "$dir/restore_incomplete.expected" <<EOF
ok wtA
restored wtB at $detached
EOF
diff "$dir/restore_incomplete.expected" "$dir/restore_incomplete.txt"
sh "$guard" check > "$dir/check_repaired.txt"
diff "$dir/check.expected" "$dir/check_repaired.txt"

# An unlocked worktree is a worktree the next prune takes, so `check` reports it.
sh "$guard" unlock wtA > "$dir/unlock.txt"
cat > "$dir/unlock.expected" <<'EOF'
unlocked wtA
EOF
diff "$dir/unlock.expected" "$dir/unlock.txt"

status=0
sh "$guard" check > "$dir/check_unlocked.txt" || status=$?
test "$status" -eq 1
cat > "$dir/check_unlocked.expected" <<'EOF'
unlocked wtA
ok wtB
EOF
diff "$dir/check_unlocked.expected" "$dir/check_unlocked.txt"

# An id `unlock` does not know is reported and the rest of the arguments are still unlocked, the
# way `forget` treats one.
status=0
sh "$guard" unlock wtZ wtA > "$dir/unlock_unknown.txt" 2> "$dir/unlock_unknown.err" || status=$?
test "$status" -eq 1
cat > "$dir/unlock_unknown.expected" <<'EOF'
unlocked wtA
EOF
diff "$dir/unlock_unknown.expected" "$dir/unlock_unknown.txt"
cat > "$dir/unlock_unknown.err.expected" <<'EOF'
no worktree wtZ
EOF
diff "$dir/unlock_unknown.err.expected" "$dir/unlock_unknown.err"

# A worktree added after the last `protect` is recorded nowhere, so a restore could not bring it
# back; `check` names it too.
git worktree add -q "$dir/wtC" -b later
status=0
sh "$guard" check > "$dir/check_new.txt" || status=$?
test "$status" -eq 1
cat > "$dir/check_new.expected" <<'EOF'
unlocked wtA
ok wtB
unrecorded wtC
EOF
diff "$dir/check_new.expected" "$dir/check_new.txt"

sh "$guard" protect > "$dir/protect_again.txt"
cat > "$dir/protect_again.expected" <<'EOF'
locked wtA
already-locked wtB
locked wtC
EOF
diff "$dir/protect_again.expected" "$dir/protect_again.txt"

# Two worktrees can share a directory name; their administrative directories then get different
# ids, and only the recorded path names either of them to `git worktree lock`.
mkdir "$dir/nested"
git worktree add -q "$dir/nested/wtA" -b nested
nested_id=$(for admin in .git/worktrees/*/; do
    case "$(cat "$admin/gitdir")" in
        */nested/wtA/.git) basename "$admin" ;;
    esac
done)
test -n "$nested_id"
test "$nested_id" != wtA
sh "$guard" protect > "$dir/protect_nested.txt"
cat > "$dir/protect_nested.expected" <<EOF
already-locked wtA
locked $nested_id
already-locked wtB
already-locked wtC
EOF
by_id "$dir/protect_nested.expected"
by_id "$dir/protect_nested.txt"
diff "$dir/protect_nested.expected" "$dir/protect_nested.txt"

# A recorded worktree whose branch is gone cannot be pointed back at anything, and saying so beats
# writing a HEAD no command can read.
printf 'ghost\t%s\tref: refs/heads/gone\n' "$dir/ghost/.git" >> .git/worktrees.manifest
status=0
sh "$guard" restore > "$dir/restore_ghost.txt" || status=$?
test "$status" -eq 1
cat > "$dir/restore_ghost.expected" <<EOF
ok wtA
ok $nested_id
ok wtB
ok wtC
unresolvable ghost
EOF
by_id "$dir/restore_ghost.expected"
by_id "$dir/restore_ghost.txt"
diff "$dir/restore_ghost.expected" "$dir/restore_ghost.txt"
test ! -e .git/worktrees/ghost

# `forget` is what retires a worktree the clone is finished with: without it the entry outlives the
# checkout, `check` reports it `missing` for good, and the next `restore` registers it again.
sh "$guard" forget ghost > "$dir/forget_ghost.txt"
cat > "$dir/forget_ghost.expected" <<'EOF'
forgot ghost
EOF
diff "$dir/forget_ghost.expected" "$dir/forget_ghost.txt"

sh "$guard" unlock wtC > /dev/null
git worktree remove "$dir/wtC"
status=0
sh "$guard" check > "$dir/check_removed.txt" || status=$?
test "$status" -eq 1
cat > "$dir/check_removed.expected" <<EOF
ok wtA
ok $nested_id
ok wtB
missing wtC
EOF
by_id "$dir/check_removed.expected"
by_id "$dir/check_removed.txt"
diff "$dir/check_removed.expected" "$dir/check_removed.txt"

sh "$guard" forget wtC > "$dir/forget.txt"
cat > "$dir/forget.expected" <<'EOF'
forgot wtC
EOF
diff "$dir/forget.expected" "$dir/forget.txt"

# The entry is gone, so `check` goes green again, `restore` no longer resurrects the checkout, and
# the ref that kept its commit reachable is released.
sh "$guard" check > "$dir/check_forgotten.txt"
cat > "$dir/check_forgotten.expected" <<EOF
ok wtA
ok $nested_id
ok wtB
EOF
by_id "$dir/check_forgotten.expected"
by_id "$dir/check_forgotten.txt"
diff "$dir/check_forgotten.expected" "$dir/check_forgotten.txt"

sh "$guard" restore > "$dir/restore_forgotten.txt"
by_id "$dir/restore_forgotten.txt"
diff "$dir/check_forgotten.expected" "$dir/restore_forgotten.txt"
test ! -e .git/worktrees/wtC
status=0
git rev-parse --verify --quiet refs/worktree-guard/wtC > /dev/null || status=$?
test "$status" -ne 0

# Forgetting something the manifest never held is an error rather than a silent success.
status=0
sh "$guard" forget wtZ > "$dir/forget_unknown.txt" 2> "$dir/forget_unknown.err" || status=$?
test "$status" -eq 1
test ! -s "$dir/forget_unknown.txt"
cat > "$dir/forget_unknown.expected" <<'EOF'
no recorded worktree wtZ
EOF
diff "$dir/forget_unknown.expected" "$dir/forget_unknown.err"

# Everything committed after the last `protect` is what `restore` cannot bring back. It has to name
# the commit it did write, and the commit it left behind has to still be findable the way the README
# says it is -- `git fsck --lost-found`, which is only possible because `protect` kept the recorded
# commit reachable and the orphan with it.
recorded=$(git rev-parse refs/worktree-guard/wtB)
echo three > "$dir/wtB/tracked.txt"
git -C "$dir/wtB" commit -q -am three
orphan=$(git -C "$dir/wtB" rev-parse HEAD)
test "$orphan" != "$recorded"

# Before any of that is lost, the manifest has already gone stale, and `check` is the only thing
# that can notice: the rule "run `protect` whenever a HEAD moves" is a rule nothing executes. A
# branch-backed worktree that commits is not stale -- `ref: refs/heads/feat` stays true as the
# branch moves, and a restore from it loses nothing.
echo four > "$dir/wtA/tracked.txt"
git -C "$dir/wtA" commit -q -am four
status=0
sh "$guard" check > "$dir/check_stale.txt" || status=$?
test "$status" -eq 1
cat > "$dir/check_stale.expected" <<EOF
ok wtA
ok $nested_id
stale wtB
EOF
by_id "$dir/check_stale.expected"
by_id "$dir/check_stale.txt"
diff "$dir/check_stale.expected" "$dir/check_stale.txt"

rm -rf .git/worktrees
sh "$guard" restore > "$dir/restore_rewound.txt"
cat > "$dir/restore_rewound.expected" <<EOF
restored wtA at refs/heads/feat
restored $nested_id at refs/heads/nested
restored wtB at $recorded
EOF
by_id "$dir/restore_rewound.expected"
by_id "$dir/restore_rewound.txt"
diff "$dir/restore_rewound.expected" "$dir/restore_rewound.txt"
test "$(git -C "$dir/wtB" rev-parse HEAD)" = "$recorded"

git fsck --lost-found > /dev/null 2>&1
test -f ".git/lost-found/commit/$orphan"

# The manifest is a plain-text file this tool's own recovery procedure invites a person to edit, and
# `restore` removes the directory its first column names. An id that is not a single path component
# names something outside `.git/worktrees` -- here the object database of the very clone the script
# exists to protect -- so it is refused by name before anything is removed.
printf '../objects\t%s\t%s\n' "$dir/ghost/.git" "$recorded" >> .git/worktrees.manifest
status=0
sh "$guard" restore > "$dir/restore_traversal.txt" 2> "$dir/restore_traversal.err" || status=$?
test "$status" -eq 1
cat > "$dir/restore_traversal.expected" <<EOF
ok wtA
ok $nested_id
ok wtB
EOF
by_id "$dir/restore_traversal.expected"
by_id "$dir/restore_traversal.txt"
diff "$dir/restore_traversal.expected" "$dir/restore_traversal.txt"
cat > "$dir/restore_traversal.err.expected" <<'EOF'
invalid id ../objects
EOF
diff "$dir/restore_traversal.err.expected" "$dir/restore_traversal.err"
test -d .git/objects
git cat-file -e "$recorded^{commit}"
