#!/bin/bash
# ---------------------------------------------------------------------------
# prepare-run-tree.sh -- rebuild a runnable game directory from the read-only
# steam assets plus this repository's build output.
#
# The run tree contains only symlinks + generated launchers, except for the
# shader pack (derived from the game's .vcs files, ~1 GB). It is git-ignored
# and can always be recreated with this script.
#
# Usage:
#   tools/prepare-run-tree.sh [dir] [--no-pack]
#
#   dir       target directory, default <repo>/game_client
#   --no-pack skip the shader-pack build (fast; the engine will refuse to
#             start until the pack exists, by design)
#
# Requires: a finished build in build-client, css-steam-assets
# (Steam app 240), and glslangValidator + spirv-val on PATH for the pack.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="${1:-${ROOT}/game_client}"
if [ "${DIR}" = "--no-pack" ]; then
  DIR="${ROOT}/game_client"
else
  shift || true
fi
PACK=1
if [ "${1:-}" = "--no-pack" ]; then
  PACK=0
fi

BUILD="${ROOT}/build-client"
STEAM="${ROOT}/css-steam-assets"
BIN="${DIR}/bin/linux64"

die() { echo "ERROR: $*" >&2; exit 1; }

[ -d "${STEAM}" ] || die "game content missing at ${STEAM} (Steam appid 240)"
[ -x "${BUILD}/launcher_main" ] || die "no build at ${BUILD}; run ./tools/build.sh first"

echo "[1/4] game content links"
"${ROOT}/tools/make-gamelink.sh" "${DIR}"

echo "[2/4] engine binaries"
mkdir -p "${BIN}"
for f in "${BUILD}"/*.so; do
  ln -sfn "${f}" "${BIN}/$(basename "${f}")"
done
ln -sfn "${BUILD}/launcher_main" "${BIN}/hl2_linux"
ln -sfn "bin/linux64/hl2_linux" "${DIR}/hl2_linux"

cat > "${DIR}/hl2.sh" <<'EOS'
#!/bin/bash
# Client launcher for our native build. Mirrors srcds.sh setup.
# NOTE: hl2_linux lives at the game root (like Valve's layout), NOT in
# bin/linux64: launcher_main chdirs to the exe directory, and only the root
# has cstrike/ next to it.
GAMEROOT="$(cd "$(dirname "$0")" && pwd)"
GAME="${GAME:-cstrike}"
BIN="${GAMEROOT}/bin"
[ -d "${BIN}/linux64" ] && BIN="${BIN}/linux64"
export LD_LIBRARY_PATH="${BIN}:${LD_LIBRARY_PATH:-}"
ulimit -n 2048
cd "$GAMEROOT"
exec "${GAMEROOT}/hl2_linux" -game "$GAME" "$@"
EOS
chmod +x "${DIR}/hl2.sh"

echo "[3/4] shader model (2024 shipped stdshader_dx9)"
GAME_CLIENT_DIR="${DIR}" "${ROOT}/tools/use_shipped_shader_model.sh" on

if [ "${PACK}" -eq 1 ]; then
  if [ -f "${DIR}/vulkan/manifest.json" ]; then
    echo "[4/4] shader pack already present (delete ${DIR}/vulkan to rebuild)"
  else
    echo "[4/4] shader pack: extracting .vcs from hl2_misc_dir.vpk"
    VPK="${STEAM}/hl2/hl2_misc_dir.vpk"
    [ -f "${VPK}" ] || die "missing ${VPK}"
    VCS="${DIR}/__vcs"
    rm -rf "${VCS}"
    python3 "${ROOT}/tools/vpk_extract.py" list "${VPK}" .vcs |
      tail -n +2 |
      while read -r _size path; do
        out="${VCS}/${path}"
        mkdir -p "$(dirname "${out}")"
        python3 "${ROOT}/tools/vpk_extract.py" extract "${VPK}" "${path}" "${out}"
      done
    echo "      converting (this takes several minutes)"
    python3 "${ROOT}/tools/vcs_pack.py" "${VCS}" "${DIR}/vulkan"
  fi
else
  echo "[4/4] shader pack skipped (--no-pack)"
fi

echo
echo "run tree ready: ${DIR}"
echo "  start: ${DIR}/hl2.sh -novid -shaderapi shaderapivulkan.so -windowed -w 640 -h 480"
