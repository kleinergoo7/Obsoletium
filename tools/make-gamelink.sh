#!/bin/bash
# ---------------------------------------------------------------------------
# make-gamelink.sh -- build a clean game root out of the read-only Steam assets.
#
# Why this is not just `ln -s "css-steam-assets" game`:
#   The Steam directory contains Valve's *prebuilt* engine binaries in
#   bin/ and cstrike/bin/. The Source engine searches <game>/bin BEFORE
#   <root>/bin, so symlinking cstrike/ wholesale would silently load Steam's
#   engine.so / materialsystem.so instead of ours -- and the test would appear
#   to pass while measuring nothing. We therefore link per entry, skipping
#   anything that would shadow our build output.
#
# Nothing under the Steam directory is ever written to.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STEAM_DIR="${ROOT_DIR}/css-steam-assets"
GAME_DIR="${1:-${ROOT_DIR}/game-run}"

if [ ! -d "$STEAM_DIR" ]; then
  echo "ERROR: Steam content not found at '$STEAM_DIR'" >&2
  echo "       Expected the directory fetched from Steam (appid 240 / CS:S + HL2)." >&2
  exit 1
fi

# Entries never to link: our build output, logs, Steam's own engine binaries.
declare -a EXCLUDE=(
  bin                 # engine .so files -- ours go here
  linux                # steam's 32-bit shim dir
  *.log
  demoheader.tmp
  cstrike_linux64
  cstrike.sh
  srcds_linux
  srcds_linux64
  steam_appid.txt     # we write our own
  thirdpartylegalnotices.txt
)

should_skip() {
  local name="$1" pat
  for pat in "${EXCLUDE[@]}"; do
    # shellcheck disable=SC2053
    [[ "$name" == $pat ]] && return 0
  done
  return 1
}

mkdir -p "$GAME_DIR"
linked=0; skipped=0

for subdir in hl2 cstrike platform; do
  src="${STEAM_DIR}/${subdir}"
  if [ ! -d "$src" ]; then
    echo "WARN: missing source subdir '$src' (skipped)" >&2
    continue
  fi
  dst="${GAME_DIR}/${subdir}"
  mkdir -p "$dst"
  for entry in "$src"/*; do
    name="$(basename "$entry")"
    if should_skip "$name"; then
      printf '  skip  %-16s (%s)\n' "$name" "${subdir}"
      skipped=$((skipped+1))
      continue
    fi
    target="${dst}/${name}"
    # Refresh idempotently: remove a stale link, never touch a real file.
    if [ -L "$target" ] || [ -e "$target" ]; then
      rm -rf "$target"
    fi
    ln -s "$entry" "$target"
    linked=$((linked+1))
  done
done

# Steam app id: 240 = Counter-Strike: Source.
echo "240" > "${GAME_DIR}/steam_appid.txt"

# Convenient launcher for a dedicated server, mirroring Valve's cstrike.sh
# argument handling (-game) and LD_LIBRARY_PATH setup.
cat > "${GAME_DIR}/srcds.sh" <<'EOS'
#!/bin/bash
# Dedicated server launcher for our native build. -game defaults to cstrike.
GAMEROOT="$(cd "$(dirname "$0")" && pwd)"
GAME="${GAME:-cstrike}"
BIN="${GAMEROOT}/bin"
[ -d "${BIN}/linux64" ] && BIN="${BIN}/linux64"
export LD_LIBRARY_PATH="${BIN}:${LD_LIBRARY_PATH:-}"
ulimit -n 2048
cd "$GAMEROOT"
exec "${BIN}/srcds_linux" -game "$GAME" "$@"
EOS
chmod +x "${GAME_DIR}/srcds.sh"

echo
echo "game root: ${GAME_DIR}"
echo "  linked:  ${linked} entries"
echo "  skipped: ${skipped} (would shadow our build)"
echo
echo "Sanity check: our build output goes to ${GAME_DIR}/bin/<platform>/"
