#!/bin/bash
# ---------------------------------------------------------------------------
# fetch-submodules.sh -- populate every submodule worktree in a source tree.
#
#   tools/fetch-submodules.sh [TREE]        (default: ./obsoletium)
#
# Idempotent: safe to re-run; existing worktrees are left alone unless
# --force is passed, in which case they are re-checked-out from the recorded
# commit.
#
# Why this script exists instead of plain `git submodule update --init --recursive`:
#   * Some submodules (notably thirdparty/shadercompile/shared/*) declare
#     git@github.com: SSH URLs. Without an SSH key those clones fail. We inject
#     an https rewrite through GIT_CONFIG_* env vars, which propagate into
#     nested clones and touch no global config.
#   * `git submodule status` is NOT a sufficient check. An initialized submodule
#     whose worktree was never checked out still reports a clean status, and it
#     is a recursive `git submodule status | grep '^-'` that misses it entirely.
#     We therefore verify by counting real files in each worktree.
# ---------------------------------------------------------------------------
set -uo pipefail

# Only meaningful for a git clone of the original Obsoletium tree; the
# vendored obsoletium/ in this repo has no submodules.
if [ ! -d "$(dirname "$0")/../.git" ] &&
   [ ! -f "$(dirname "$0")/../.git" ]; then
  echo "no submodules in this repository." >&2
  echo "See docs/REPRODUCE.md." >&2
  exit 0
fi

export GIT_CONFIG_COUNT=1
export GIT_CONFIG_KEY_0='url.https://github.com/.insteadOf'
export GIT_CONFIG_VALUE_0='git@github.com:'

TREE="${1:-.}"
FORCE=""
[ "${2:-}" = "--force" ] && FORCE="--force"

cd "$TREE" || { echo "no such tree: $TREE" >&2; exit 1; }

echo "[$(date -Is)] populating submodules in $(pwd)"
git submodule sync --recursive >/dev/null 2>&1
git submodule update --init --recursive --jobs 8 $FORCE
rc=$?
echo "[$(date -Is)] submodule update rc=$rc"

# --- the verification that actually matters --------------------------------
# Walk every submodule at every depth and count regular files. A worktree with
# 0 or 1 file is empty (the 1 file being the .git pointer).
empty="$(git submodule foreach --recursive --quiet \
            'n=$(find . -type f 2>/dev/null | wc -l); [ "$n" -le 1 ] && echo "$displaypath ($n)"' \
            2>/dev/null)"
total="$(git submodule foreach --recursive --quiet 'echo x' 2>/dev/null | wc -l)"

echo "--- checked ${total} submodule(s) at all depths ---"
if [ -n "$empty" ]; then
  echo "FAIL: unpopulated submodule worktree(s):"
  printf '  EMPTY: %s\n' $empty
  exit 1
fi
echo "PASS: every submodule worktree is populated"
exit $rc
