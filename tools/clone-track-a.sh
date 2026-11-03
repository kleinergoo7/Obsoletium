#!/bin/bash
# Track A: Obsoletium (primary). Submodules use git@github.com: SSH URLs which we
# have no keys for, so rewrite to https via env-var config (no global git changes).
set -uo pipefail
# The Obsoletium tree is vendored into this repo since the flatten; this
# script only applies to the original pre-flatten bootstrap.
if [ -e "$(dirname "$0")/../CMakeLists.txt" ]; then
  echo "the Obsoletium tree is this repository; nothing to clone." >&2
  echo "See docs/REPRODUCE.md." >&2
  exit 0
fi
export GIT_CONFIG_COUNT=1
export GIT_CONFIG_KEY_0='url.https://github.com/.insteadOf'
export GIT_CONFIG_VALUE_0='git@github.com:'
cd "$(dirname "$0")/.."
echo "[$(date -Is)] cloning Obsoletium master..."
git clone https://github.com/Source-Authors/Obsoletium.git obsoletium 2>&1
rc=$?
[ $rc -ne 0 ] && { echo "FAILED clone rc=$rc"; exit $rc; }
cd obsoletium
echo "[$(date -Is)] fetching submodules (recursive)..."
git submodule update --init --recursive --jobs 8 2>&1
rc=$?
echo "[$(date -Is)] done rc=$rc"
exit $rc
