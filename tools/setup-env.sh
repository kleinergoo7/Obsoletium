#!/bin/bash
# ---------------------------------------------------------------------------
# setup-env.sh -- reproducible environment setup + verification for the
#                  native 64-bit Linux CS:S / Obsoletium port.
#
# Usage:
#   ./setup-env.sh --check     verify everything, print versions, exit non-zero
#                             on any missing dependency (default mode)
#   ./setup-env.sh --install   install missing packages (needs root)
#   ./setup-env.sh --lock      write doc/env.lock only
#
# Design notes:
#   * Verification is capability-based (does the binary/header/pkg-config module
#     exist?) rather than package-name based, so it works on any distro and
#     survives repackaging. When something is missing we *also* print the exact
#     native install command for the detected package manager.
#   * Nothing is ever installed silently: a missing dependency is a loud,
#     non-zero failure with a copy-pasteable fix line.
#   * --install requires root and re-runs --check afterwards.
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCK_FILE="${ROOT_DIR}/docs/env.lock"
MISSING=0

# --- pretty output ----------------------------------------------------------
if [ -t 1 ]; then
  R=$'\033[31m'; G=$'\033[32m'; Y=$'\033[33m'; B=$'\033[34m'; N=$'\033[0m'
else
  R=""; G=""; Y=""; B=""; N=""
fi
ok()   { printf "  ${G}ok${N}      %s\n" "$*"; }
warn() { printf "  ${Y}WARN${N}    %s\n" "$*"; }
bad()  { printf "  ${R}MISSING${N} %s\n" "$*"; MISSING=$((MISSING+1)); }
hdr()  { printf "\n${B}== %s ==${N}\n" "$*"; }

# --- package manager detection ---------------------------------------------
detect_pm() {
  for pm in zypper apt-get dnf pacman xbps-install emerge; do
    if command -v "$pm" >/dev/null 2>&1; then echo "$pm"; return 0; fi
  done
  echo "unknown"
}
PM="$(detect_pm)"

# Distro name, best effort.
DISTRO="$( ( . /etc/os-release 2>/dev/null; echo "${NAME:-unknown} ${VERSION:-}" ) | tr -s ' ')"

# Emit the native install command for a logical dependency name.
install_line_for() {
  case "$PM" in
    # NOTE: do NOT add zlib-devel here. On openSUSE the installed
    # zlib-ng-compat-devel owns /usr/include/zlib.h and provides
    # 'pkg-config zlib' (= 1.3.1.zlib-ng); zypper marks zlib-devel as
    # conflicting, and installing it risks breaking the toolchain.
    # Also omitted on purpose: nasm (no .s/.asm sources in any .vpc),
    # libjpeg8-devel (libjpeg62-devel already satisfies it) and
    # libedit-devel (readline is already present).
    zypper) cat <<'EOF'
      sudo zypper --non-interactive install -y libcurl-devel openal-soft-devel \
        glslang-devel vulkan-validationlayers spirv-cross
EOF
      ;;
    apt-get) cat <<'EOF'
      sudo apt-get install -y build-essential cmake ninja-build python3 pkg-config \
        libsdl2-dev libsdl2-net-dev libcurl4-openssl-dev zlib1g-dev libjpeg-dev \
        libopenal-dev libedit-dev libfreetype6-dev libfontconfig1-dev libpng-dev \
        libreadline-dev libx11-dev libxrandr-dev libxkbcommon-dev libwayland-dev \
        libgl1-mesa-dev libglx-mesa-dev mesa-vulkan-drivers vulkan-tools \
        vulkan-validationlayers spirv-tools glslang-tools spirv-cross
EOF
      ;;
    dnf) cat <<'EOF'
      sudo dnf install -y gcc-c++ cmake ninja-build python3 pkgconf-pkg-config \
        SDL3-devel libcurl-devel zlib-devel libjpeg-turbo-devel \
        openal-soft-devel libedit-devel freetype-devel fontconfig-devel libpng-devel \
        readline-devel libX11-devel libXrandr-devel libxkbcommon-devel wayland-devel \
        mesa-libGL-devel mesa-vulkan-drivers vulkan-tools vulkan-validationlayers \
        glslang spirv-tools spirv-cross
EOF
      ;;
    pacman) cat <<'EOF'
      sudo pacman -S --needed base-devel cmake ninja python pkgconf sdl2 sdl2_net \
        curl zlib libjpeg-turbo openal libedit freetype2 fontconfig libpng readline \
        libx11 libxrandr libxkbcommon wayland mesa vulkan-tools vulkan-validationlayers \
        glslang spirv-tools spirv-cross
EOF
      ;;
    *) echo "      (unknown package manager -- install the equivalent of the list in BUILD.md)" ;;
  esac
}

# --- capability checks ------------------------------------------------------
# check_cmd <binary> <human description> [package hint]
check_cmd() {
  local bin="$1" desc="$2" hint="${3:-}"
  if command -v "$bin" >/dev/null 2>&1; then
    ok "$desc  ->  $(command -v "$bin")"
    return 0
  fi
  bad "$desc  (no '$bin' on PATH${hint:+, hint: $hint})"
  return 1
}

# check_pkgconfig <module> <desc>
check_pkgconfig() {
  local mod="$1" desc="$2"
  if pkg-config --exists "$mod" 2>/dev/null; then
    ok "$desc  ->  $(pkg-config --modversion "$mod")"
    return 0
  fi
  bad "$desc  (pkg-config cannot find '$mod')"
  return 1
}

# check_header <header> <desc> [pkg-config module]
# Many distros (openSUSE, Debian multiarch, Homebrew) install headers outside
# /usr/include, so also consult the include paths reported by pkg-config.
check_header() {
  local hdr="$1" desc="$2" mod="${3:-}"
  local dirs=("/usr/include" "/usr/local/include")
  if [ -n "$mod" ] && command -v pkg-config >/dev/null 2>&1; then
    local d
    for d in $(pkg-config --cflags-only-I "$mod" 2>/dev/null); do dirs+=("${d#-I}"); done
  fi
  local d
  for d in "${dirs[@]}"; do
    if [ -e "$d/$hdr" ]; then
      ok "$desc  ->  $d/$hdr"
      return 0
    fi
  done
  bad "$desc  (header $hdr not found in: ${dirs[*]})"
  return 1
}

check_64bit_toolchain() {
  hdr "64-bit target preflight"
  local tmpd out
  tmpd="$(mktemp -d)"
  printf 'int main(void){return 0;}\n' > "$tmpd/t.c"
  for cc in gcc clang; do
    command -v "$cc" >/dev/null 2>&1 || continue
    if "$cc" -m64 "$tmpd/t.c" -o "$tmpd/t" 2>/dev/null; then
      out="$(file -b "$tmpd/t" | cut -d, -f1-2)"
      ok "$cc -m64 works  ->  $out"
    else
      bad "$cc cannot build 64-bit objects (missing 64-bit libc headers/libc.so?)"
    fi
  done
  rm -rf "$tmpd"

  # A 64-bit host default is what we want; flag an -m32 default outright.
  if [ -n "${CFLAGS:-}" ] && printf '%s' "${CFLAGS:-}" | grep -q -- '-m32'; then
    bad "CFLAGS in this environment contains -m32: '${CFLAGS}'"
  else
    ok "no -m32 in ambient CFLAGS/CXXFLAGS"
  fi
}

check_no_32bit_flags_in_tree() {
  hdr "source tree 32-bit flag audit"
  local tree="${1:-$ROOT_DIR}"
  if [ ! -d "$tree" ]; then
    warn "no source tree at $tree -- skipping -m32/ia32 audit (run after clone)"
    return 0
  fi
  local hits
  # Build-system files only; source comments about ia32 are not build flags.
  # Scope to tracked files: walking the filesystem also descends 55 GB of
  # ignored build output and can block on special files.
  local pattern='(^|[^a-zA-Z0-9_])(-m32|-march=ia32|-mtune=ia32|--target=i[3456]86)([^a-zA-Z0-9_]|$)'
  if git -C "$ROOT_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    hits="$(git -C "$ROOT_DIR" ls-files -- . |
      grep -E '(CMakeLists\.txt|Makefile.*|\.(cmake|mak|mk|vpc|sh|py|yml|bat))$' |
      (cd "$ROOT_DIR" && xargs -r grep -InE -- "$pattern" 2>/dev/null) |
      grep -vE ':[0-9]+:[[:space:]]*#' | head -20)"
  else
    hits="$(grep -rInE -- "$pattern" \
      --include='CMakeLists.txt' --include='*.cmake' --include='*.mak' \
      --include='*.mk' --include='*.vpc' --include='Makefile*' \
      --include='*.sh' --include='*.py' --include='*.yml' --include='*.bat' \
      --exclude-dir=.git --exclude-dir=build --exclude-dir=build-client \
      --exclude-dir=build-asan --exclude-dir=build-client-gl \
      "$tree" 2>/dev/null | grep -v '^\s*#' | head -20)"
  fi
  if [ -z "$hits" ]; then
    ok "no -m32 / ia32 / i386 target flags in build files under $tree"
  else
    warn "possible 32-bit build flags found (review each; some may be in comments/docs):"
    printf '%s\n' "$hits" | sed 's/^/           /'
  fi
}

check_vulkan_runtime() {
  hdr "Vulkan runtime"
  if command -v vulkaninfo >/dev/null 2>&1; then
    ok "vulkaninfo present"
    local ver
    ver="$(vulkaninfo --summary 2>/dev/null | grep -m1 'Vulkan Instance Version' | sed 's/.*: //')"
    [ -n "$ver" ] && ok "instance version: $ver"
  else
    bad "vulkaninfo (package: vulkan-tools) -- required for the Vulkan backend"
  fi

  # Validation layers: mandatory for all of Phase 2.
  # The manifest filename is lowercase on openSUSE
  # (VkLayer_khronos_validation.json), so match case-insensitively rather than
  # globbing for a capital-K '*Khronos_validation*.json' -- that silently misses
  # it and reports a false MISSING.
  local layerdir
  layerdir="$(ls -d /usr/share/vulkan/explicit_layer.d 2>/dev/null || true)"
  if [ -n "$layerdir" ] && ls "$layerdir"/*[Kk]hronos_validation*.json >/dev/null 2>&1; then
    ok "VK_LAYER_KHRONOS_validation layer manifest present ($(ls "$layerdir"/*[Kk]hronos_validation*.json | xargs -n1 basename))"
  else
    bad "VK_LAYER_KHRONOS_validation not installed (package: vulkan-validationlayers)"
  fi

  if ls /usr/share/vulkan/icd.d/*.json >/dev/null 2>&1; then
    ok "Vulkan ICDs: $(ls /usr/share/vulkan/icd.d/*.json | xargs -n1 basename | tr '\n' ' ')"
  else
    bad "no Vulkan ICD found in /usr/share/vulkan/icd.d (install the driver package)"
  fi
}

write_lock() {
  hdr "writing $LOCK_FILE"
  mkdir -p "$(dirname "$LOCK_FILE")"
  {
    echo "# env.lock -- generated by setup-env.sh on $(date -Is)"
    echo "# host: ${DISTRO}  kernel: $(uname -r)  arch: $(uname -m)"
    echo
    echo "[toolchain]"
    for t in gcc g++ clang clang++ cmake ninja python3 git perl nasm; do
      command -v "$t" >/dev/null 2>&1 && echo "$t = $("$t" --version 2>&1 | head -1)"
    done
    echo
    echo "[libraries (pkg-config)]"
    if command -v pkg-config >/dev/null 2>&1; then
      for m in sdl2 sdl2_net libcurl zlib libpng freetype2 fontconfig alsa openal \
               x11 xrandr xkbcommon wayland-client gl glesv2 vulkan; do
        pkg-config --exists "$m" 2>/dev/null && echo "$m = $(pkg-config --modversion "$m")"
      done
    fi
    echo
    echo "[shader + vulkan tools]"
    for t in glslangValidator spirv-val spirv-dis spirv-opt spirv-cross; do
      command -v "$t" >/dev/null 2>&1 && echo "$t = $("$t" --version 2>&1 | head -1)"
    done
    echo
    echo "[vulkan]"
    command -v vulkaninfo >/dev/null 2>&1 && \
      vulkaninfo --summary 2>/dev/null | grep -E 'Vulkan Instance Version|GPU[0-9]|deviceName|driverName|apiVersion' | sed 's/^/  /'
    echo
    echo "[game content]"
    echo "steam_content = ${ROOT_DIR}/Counter-Strike Source"
    echo "game_root     = ${ROOT_DIR}/game"
  } > "$LOCK_FILE"
  ok "wrote $LOCK_FILE"
}

do_install() {
  hdr "installing packages (needs root)"
  if [ "$(id -u)" -ne 0 ]; then
    printf "  ${R}ERROR${N} --install must run as root; re-run with: sudo $0 --install\n"
    exit 2
  fi
  if [ "$PM" = "unknown" ]; then
    printf "  ${R}ERROR${N} no supported package manager found (zypper/apt-get/dnf/pacman)\n"
    exit 2
  fi
  eval "$(install_line_for | sed 's/^ *//')"
  ok "package install finished"
}

MODE="${1:---check}"

printf "${B}=========================================================${N}\n"
printf "${B} CSS Vulkan port -- environment check${N}\n"
printf " host: %s\n pm:   %s\n mode: %s\n" "$DISTRO" "$PM" "$MODE"
printf "${B}=========================================================${N}\n"

hdr "build tools"
check_cmd gcc            "C compiler"            "gcc/gcc-c++"
check_cmd g++            "C++ compiler"          "gcc-c++"
check_cmd cmake          "CMake"                 "cmake"
check_cmd ninja          "Ninja"                 "ninja-build"
check_cmd python3        "Python (build scripts)"
check_cmd git            "Git (version stamping)"
check_cmd pkg-config     "pkg-config"

hdr "source engine dependencies (pkg-config)"
check_pkgconfig sdl3        "SDL3 (window + input)"
check_pkgconfig libcurl     "libcurl (Steam API / http)"
check_pkgconfig zlib        "zlib (VPK decompression)"
check_pkgconfig libpng      "libpng (VTF/PNG image IO)"
check_pkgconfig freetype2   "freetype (vgui text)"
check_pkgconfig fontconfig  "fontconfig (font lookup)"
check_pkgconfig alsa        "ALSA (audio backend)"
check_pkgconfig openal      "OpenAL (engine audio backend; pure OpenAL 1.1 in snd_dev_openal.cpp)"
check_pkgconfig x11         "X11 (Xlib surfaces)"
check_pkgconfig xrandr      "Xrandr (display info)"

hdr "shader toolchain (Phase 2)"
check_cmd glslangValidator "glslang (HLSL/GLSL -> SPIR-V)" "glslang-devel"
check_cmd spirv-val        "spirv-val (SPIR-V validation)"  "spirv-tools"
check_cmd spirv-dis        "spirv-dis (SPIR-V inspection)"  "spirv-tools"
check_cmd spirv-cross      "spirv-cross (SPIR-V -> GLSL/HLSL, shader debugging)" "spirv-cross"

hdr "headers"
check_header SDL3/SDL.h          "SDL3 headers"                       sdl3
check_header vulkan/vulkan.h     "Vulkan headers"                     vulkan
check_header zlib.h              "zlib headers"                       zlib
check_header png.h               "libpng headers"                     libpng
check_header jpeglib.h           "libjpeg headers"                    libjpeg
check_header ft2build.h          "freetype headers"                   freetype2
check_header alsa/asoundlib.h    "ALSA headers"                       alsa
check_header GL/gl.h             "OpenGL headers (togl sanity check, Phase 1 only)" gl
check_header curl/curl.h         "libcurl headers"                    libcurl

check_64bit_toolchain
check_vulkan_runtime
check_no_32bit_flags_in_tree "$ROOT_DIR/obsoletium"

if [ "$MODE" = "--install" ]; then
  do_install
  MISSING=0
  check_no_32bit_flags_in_tree "$ROOT_DIR/obsoletium"
fi

[ "$MODE" = "--lock" ] || write_lock

hdr "result"
if [ "$MISSING" -gt 0 ]; then
  printf "  ${R}%d dependency/dependencies missing.${N}\n\n" "$MISSING"
  printf "Fix with this one command (detected %s on %s):\n\n" "$PM" "$DISTRO"
  install_line_for
  printf "\nOr run: sudo %s --install\n\n" "$0"
  exit 1
fi
printf "  ${G}All required dependencies present.${N}\n"
exit 0
