#!/bin/sh
# Points this clone at the hooks versioned in .githooks/. Run it once per clone; the build
# never installs hooks on its own. Undo with `git config --unset core.hooksPath`.
set -e

cd "$(git rev-parse --show-toplevel)"
chmod +x .githooks/*
git config core.hooksPath .githooks
echo "Installed the repository git hooks: core.hooksPath is now .githooks"
