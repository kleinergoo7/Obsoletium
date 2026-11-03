#!/bin/bash
# ---------------------------------------------------------------------------
# build.sh -- the single entry point for building the engine.
#
#   ./build.sh                  configure + build everything, Debug, logged
#   ./build.sh tier0 vstdlib    build only the given CMake targets (in order)
#   ./build.sh --verbose        add -v (full compiler command lines)
#   ./build.sh --asan           ASan+UBSan variant in a separate build dir
#   ./build.sh --cc clang       pick the toolchain (gcc | clang)
#   ./build.sh --clean          wipe the build dir and re-configure from scratch
#   ./build.sh --stage <t>      build dependency-ordered stage (see --list)
#
# Rules baked in (do not relax without a reason in BUILD.md):
#   * Debug is the default. Symbols are mandatory in Phase 1; never strip.
#   * Every invocation is teed to logs/build-YYYYMMDD-HHMM.log with both
#     stdout and stderr, so any attempt can be diffed against a known good one.
#   * The 64-bit target is asserted, not assumed.
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TREE="${ROOT_DIR}"
LOG_DIR="${ROOT_DIR}/logs"

# Dependency order. Each stage is only attempted after the previous one links,
# so the first failure identifies the first broken component (PROMPT.md 1.3).
#
# Names are CMake target names produced by tools/vpc2cmake.py, not VPC project
# labels -- "client (CStrike)" is emitted as client_CStrike, and the real
# filesystem targets are filesystem_stdio / FileSystem_Steam. Keep this list in
# sync with `ls cmake/generated/`.
#
# Note: every stage lists only the targets it *verifies*. Their dependencies are
# pulled in automatically by the generated CMake link graph.
declare -a STAGES=(
  "tier0 vstdlib"
  "mathlib tier1"
  "filesystem_stdio"
  "tier2 tier3 engine datamodel dmserializers Networksystem soundemittersystem scenefilecache"
  "dedicated dedicated_main"
  "server_CStrike"
  "client_CStrike"
  "inputsystem gameui"
  "materialsystem studiorender vguimatsurface particles shaderapiempty"
)

VERBOSE=0
SAN=""
CC_KIND="gcc"
CLEAN=0
STAGE=""
declare -a TARGETS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --verbose|-v) VERBOSE=1; shift ;;
    --clean)      CLEAN=1; shift ;;
    --asan)       SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"; shift ;;
    --release)    BUILD_TYPE=Release; shift ;;
    --cc)         CC_KIND="$2"; shift 2 ;;
    --stage)      STAGE="$2"; shift 2 ;;
    --list)       printf '%s\n' "${STAGES[@]}"; exit 0 ;;
    --help|-h)    sed -n '2,22p' "$0"; exit 0 ;;
    *)            TARGETS+=("$1"); shift ;;
  esac
done

# --stage N selects STAGES[N-1]. Without this, STAGE was recorded but never
# turned into a target list, so the build fell through to the default `all`
# target and dragged in every Windows-only tool in the tree.
if [ -n "$STAGE" ]; then
  case "$STAGE" in
    ''|*[!0-9]*) echo "ERROR: --stage takes a number 1..${#STAGES[@]} (got '$STAGE')" >&2; exit 2 ;;
  esac
  if [ "$STAGE" -lt 1 ] || [ "$STAGE" -gt "${#STAGES[@]}" ]; then
    echo "ERROR: --stage must be 1..${#STAGES[@]} (got '$STAGE')" >&2
    echo "       stages:" >&2
    i=1
    for s in "${STAGES[@]}"; do
      printf '         %d: %s\n' "$i" "$s" >&2
      i=$((i + 1))
    done
    exit 2
  fi
  if [ ${#TARGETS[@]} -gt 0 ]; then
    echo "ERROR: --stage and explicit targets are mutually exclusive" >&2
    exit 2
  fi
  # A stage must build every earlier stage's targets too, not just its own.
  # The comment above assumed the generated CMake link graph would pull them
  # in, but the link graph is incomplete: dedicated's LIBS are only
  # "SDL3 appframework edit mathlib tier2 tier3", so it declares no edge to
  # tier0/vstdlib/tier1 -- and none to engine at all. Without this, editing e.g.
  # tier0/mem_helpers.cpp and running --stage 5 reported "ninja: no work to
  # do" and left a stale libtier0.so in place, which then failed to load at
  # runtime with "undefined symbol: g_bInitMemory". Stages are still built in
  # order, so the first failure still identifies the first broken component.
  # shellcheck disable=SC2206 # deliberate word splitting: stages are space lists
  TARGETS=()
  i=0
  while [ "$i" -lt "$STAGE" ]; do
    # shellcheck disable=SC2206
    TARGETS+=(${STAGES[$i]})
    i=$((i + 1))
  done
  STAGE_DESC="stage ${STAGE}"
else
  STAGE_DESC="explicit"
fi

BUILD_TYPE="${BUILD_TYPE:-Debug}"
case "$CC_KIND" in
  gcc)   export CC=gcc  CXX=g++ ;;
  clang) export CC=clang CXX=clang++ ;;
  *) echo "ERROR: --cc must be gcc or clang (got '$CC_KIND')" >&2; exit 2 ;;
esac

# Sanitizer builds get their own directory so object files never mix.
if [ -n "$SAN" ]; then
  BUILD_DIR="${TREE}/build-asan"
  BUILD_TYPE=Debug
else
  BUILD_DIR="${TREE}/build-client"
fi

mkdir -p "$LOG_DIR"
TS="$(date +%Y%m%d-%H%M%S)"
LOG="${LOG_DIR}/build-${TS}.log"

log() { printf '%s\n' "$*" | tee -a "$LOG"; }

log "=========================================================="
log "build.sh  $(date -Is)"
log "  tree:      ${TREE}"
log "  build dir: ${BUILD_DIR}"
log "  type:      ${BUILD_TYPE}   cc: ${CC_KIND}   asan: $([ -n "$SAN" ] && echo yes || echo no)"
log "  stage:     ${STAGE_DESC}"
log "  targets:   ${TARGETS[*]:-<all>}"
log "=========================================================="

if [ ! -d "$TREE" ]; then
  log "ERROR: source tree missing at ${TREE}" | tee -a "$LOG" >&2
  log "       run tools/clone-track-a.sh first" | tee -a "$LOG" >&2
  exit 2
fi

if [ "$CLEAN" -eq 1 ]; then
  log "[clean] rm -rf ${BUILD_DIR}"
  rm -rf "$BUILD_DIR"
fi

# A fresh clone has no generated module tree; it is derived from the .vpc
# files and cheap to rebuild.
if [ ! -d "${ROOT_DIR}/cmake/generated" ]; then
  log "[gen] cmake/generated missing; running tools/vpc2cmake.py"
  python3 "${ROOT_DIR}/tools/vpc2cmake.py" --root "${ROOT_DIR}" \
    --out "${ROOT_DIR}/cmake/generated" 2>&1 | tee -a "$LOG"
  rc=${PIPESTATUS[0]}
  if [ $rc -ne 0 ]; then
    log "FAILED generating cmake/generated (rc=$rc)"
    exit $rc
  fi
fi

CMAKE_ARGS=(
  -S "$TREE" -B "$BUILD_DIR"
  -G Ninja
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  # Consumed by our module CMakeLists (see cmake/ in this repo).
  -DSE_TARGET_ARCH=x86_64
)
if [ -n "$SAN" ]; then
  # SE_SANITIZERS is consumed nowhere in the module CMake, so apply the flags
  # again as global compile/link flags. Both are needed: the compile flag makes
  # every module instrumented, and the linker flags pull the runtime into each
  # .so/.exe (otherwise ASan complains the runtime is missing in every DSO).
  CMAKE_ARGS+=( -DSE_SANITIZERS="$SAN"
    -DCMAKE_C_FLAGS="$SAN ${CFLAGS:-}"
    -DCMAKE_CXX_FLAGS="$SAN ${CXXFLAGS:-}"
    -DCMAKE_SHARED_LINKER_FLAGS="$SAN"
    -DCMAKE_EXE_LINKER_FLAGS="$SAN" )
fi

log "[configure] cmake ${CMAKE_ARGS[*]}"
cmake "${CMAKE_ARGS[@]}" 2>&1 | tee -a "$LOG"
rc=${PIPESTATUS[0]}
if [ $rc -ne 0 ]; then
  log "FAILED at configure (rc=$rc). Full log: ${LOG}"
  exit $rc
fi

# --- 64-bit assertion -------------------------------------------------------
# Cheap, and it catches the classic silent -m32 regression from a stray flag in
# a toolchain file or an inherited CFLAGS/CXXFLAGS.
log "[preflight] asserting 64-bit objects"
ARCH_MISMATCH=0
while IFS= read -r -d '' obj; do
  if ! file -b "$obj" | grep -qE 'ELF 64-bit'; then
    log "  32-BIT OBJECT: $obj -> $(file -b "$obj" | cut -d, -f1-2)"
    ARCH_MISMATCH=1
  fi
done < <(find "$BUILD_DIR" -name '*.o' -print0 2>/dev/null)
if [ "$ARCH_MISMATCH" -ne 0 ]; then
  log "FAILED: 32-bit objects present. Not a valid 64-bit build."
  exit 1
fi
log "  all objects are ELF 64-bit"

# --- build ------------------------------------------------------------------
BUILD_CMD=(cmake --build "$BUILD_DIR")
if [ "$VERBOSE" -eq 1 ]; then
  BUILD_CMD+=(--verbose)
  log "  (verbose: full compiler command lines)"
fi
if [ ${#TARGETS[@]} -gt 0 ]; then
  BUILD_CMD+=(--target "${TARGETS[@]}")
fi

log "[build] ${BUILD_CMD[*]}"
"${BUILD_CMD[@]}" 2>&1 | tee -a "$LOG"
rc=${PIPESTATUS[0]}
if [ $rc -ne 0 ]; then
  log "FAILED at build (rc=$rc)."
  log "  full log:      ${LOG}"
  log "  first errors:  grep -n -m 20 -E 'error:|Error|undefined reference' ${LOG}"
  exit $rc
fi

log "[ok] build finished"
log "  artifacts under ${BUILD_DIR}"
find "$BUILD_DIR" -maxdepth 3 -type f \( -name '*.so' -o -name '*.a' -o -perm -u+x -name 'srcds*' -o -perm -u+x -name 'hl2*' \) \
  2>/dev/null | head -40 | while read -r f; do
    log "    $(file -b "$f" | cut -d, -f1-2 | sed 's/^ *//')  $f"
  done
log "[ok] log: ${LOG}"
