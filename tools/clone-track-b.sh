#!/bin/bash
# Track B: nillerusr/source-engine (oracle + fallback). Same SSH->https rewrite.
set -uo pipefail
export GIT_CONFIG_COUNT=1
export GIT_CONFIG_KEY_0='url.https://github.com/.insteadOf'
export GIT_CONFIG_VALUE_0='git@github.com:'
cd "$(dirname "$0")/.."
echo "[$(date -Is)] cloning nillerusr/source-engine master..."
mkdir -p research
git clone https://github.com/nillerusr/source-engine.git research/nillerusr-source-engine 2>&1
rc=$?
[ $rc -ne 0 ] && { echo "FAILED clone rc=$rc"; exit $rc; }
cd research/nillerusr-source-engine
echo "[$(date -Is)] fetching submodules (recursive)..."
git submodule update --init --recursive --jobs 8 2>&1
rc=$?
echo "[$(date -Is)] done rc=$rc"
exit $rc
