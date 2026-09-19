#!/bin/sh
# `scripts/git-worktree-guard.sh` has to survive the case it exists for: a clone whose worktrees are
# registered under paths only their own process can see, where every `git worktree prune` -- and the
# one `git gc` runs -- would otherwise delete their administrative files.
set -e

guard="$1"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

git init -q -b master "$dir/repo"
cd "$dir/repo"
git config user.email s2o@example.com
git config user.name sqlite2orm
echo one > tracked.txt
git add tracked.txt
git commit -q -m one

git worktree add -q "$dir/wtA" -b feat
git worktree add -q --detach "$dir/wtB"
detached=$(git -C "$dir/wtB" rev-parse HEAD)

# `protect` locks both and records them.
sh "$guard" protect > "$dir/protect.txt"
cat > "$dir/protect.expected" <<'EOF'
locked wtA
locked wtB
EOF
diff "$dir/protect.expected" "$dir/protect.txt"

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

sh "$guard" restore > "$dir/restore.txt"
cat > "$dir/restore.expected" <<'EOF'
restored wtA
restored wtB
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
test -d .git/worktrees/wtA1
sh "$guard" protect > "$dir/protect_nested.txt"
cat > "$dir/protect_nested.expected" <<'EOF'
already-locked wtA
locked wtA1
already-locked wtB
already-locked wtC
EOF
diff "$dir/protect_nested.expected" "$dir/protect_nested.txt"

# A recorded worktree whose branch is gone cannot be pointed back at anything, and saying so beats
# writing a HEAD no command can read.
printf 'ghost\t%s\tref: refs/heads/gone\n' "$dir/ghost/.git" >> .git/worktrees.manifest
status=0
sh "$guard" restore > "$dir/restore_ghost.txt" || status=$?
test "$status" -eq 1
cat > "$dir/restore_ghost.expected" <<'EOF'
ok wtA
ok wtA1
ok wtB
ok wtC
unresolvable ghost
EOF
diff "$dir/restore_ghost.expected" "$dir/restore_ghost.txt"
test ! -e .git/worktrees/ghost
