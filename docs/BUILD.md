# BUILD.md — native 64-bit Linux CS:S port (Obsoletium)

Debugging knowledge base for this project (PROMPT.md §1.6). Newest entries appended
at the bottom of each section. Companion docs: `doc/ENVIRONMENT.md` (host + asset
inventory + tooling defects), `doc/STEPS.md` (the step ladder and gates), `doc/env.lock`
(generated version snapshot).

---

## 1. Pinned versions

Host: openSUSE Leap 16.0, kernel 6.12.0-160000.38-default, x86_64, glibc 2.40, 16 threads,
23 GiB RAM. Wayland session with XWayland on `DISPLAY=:1`. PipeWire + `pipewire-pulse`.

| Component | Version |
|---|---|
| gcc / g++ | 15.3.0 |
| clang / clang++ | 19.1.7 |
| cmake | 3.31.7 |
| ninja | 1.12.1 |
| python3 | 3.13.14 |
| SDL3 | 3.4.18 (renderer; the engine also builds against SDL2-era usage via the SDL3 legacy aliases) |
| libcurl | 8.14.1 |
| zlib | 1.3.1.zlib-ng (**not** zlib-devel — see §3.1) |
| libpng | 1.6.58 |
| freetype2 | 26.6.20 |
| fontconfig | 2.16.0 |
| ALSA | 1.2.14 |
| OpenAL | 1.24.3 (OpenAL Soft; `libopenal1` was preinstalled, `-devel` added) |
| X11 / Xrandr | 1.8.10 / 1.5.4 |
| Vulkan loader | 1.4.309 |
| Vulkan device | **1.3.296** — Radeon RX 580 (RADV POLARIS10), plus llvmpipe |
| Validation layer | 1.4.309 (`VK_LAYER_KHRONOS_validation`) |
| glslang | 15.2.0 |
| spirv-cross | 1.3.296.0 (2024-10-08) |
| SPIRV-Tools | 2025.1 |

Track A: Obsoletium `606c49a4d`, branch `port/linux-vulkan`, 291 `.vpc` files.
Track B: nillerusr/source-engine `ed8209cc`.

## 2. Gates

| Gate | Meaning | State |
|---|---|---|
| G0 | environment complete, all deps present, all submodules populated | **PASSED** |
| G1 | CMake configures cleanly against the generated module tree | pending |
| G2 | `libtier0`, `libvstdlib` link, ELF-64, correct exports | pending |
| G3 | tier1 links against tier0/vstdlib/mathlib | pending |
| G4 | a test binary lists entries inside the real VPKs | pending |
| G5 | standalone VPK tool lists v1 and v2 archives | pending |
| G6 | **SERVER MILESTONE** — `srcds_linux` runs on real content, reaches a prompt | pending |
| G7 | **CLIENT MILESTONE** — visible window + audible sound + content loaded | pending |
| G8 | shader-format compatibility answered in writing | pending |
| G9 | **PHASE 1 COMPLETE** — clean rebuild + status report | pending |
| G10 | **PHASE 2 COMPLETE** — playable map under validation layers, zero errors | pending |

## 3. Issues encountered, and their fixes

### 3.1 Do not install `zlib-devel` (trap)

The host has `zlib-ng-compat-devel`, which owns `/usr/include/zlib.h` and provides
`pkg-config zlib = 1.3.1.zlib-ng`. zypper reports `zlib-devel` as conflicting with it.
Installing it risks a broken toolchain. `setup-env.sh` therefore omits it, with a
comment explaining why so nobody "fixes" it back.

Similarly omitted, and why: `nasm` (no `.s`/`.asm` sources in any `.vpc`),
`libjpeg8-devel` (`libjpeg62-devel` already installed), `libedit-devel` (`readline`
already present).

### 3.2 17 of 24 submodule worktrees were empty

**Symptom:** `git submodule status` looked clean and `tools/fetch-submodules.sh` exited
0, but 17 submodule directories contained nothing but their `.git` pointer. Missing
included `zlib`, `libpng`, `celt`, `speex`, `squirrel`, `lua`, `shadercompile` and
`external/vpc` — i.e. nothing could compile.

**Root cause:** two separate bugs, in the script and in the assumption.
1. `git submodule status` reports a submodule whose *objects* are fetched but whose
   *worktree* was never checked out as clean, because a prefix only marks
   *uninitialised* (`-`) or *mismatched* (`+`) submodules. The old verification
   (`git submodule status --recursive | grep '^-'`) could therefore never detect this.
2. `thirdparty/shadercompile/shared/*` declare `git@github.com:` SSH URLs, and this
   host has no SSH key, so those nested clones failed.

**Fix:** rewrote `tools/fetch-submodules.sh` to
- inject the https rewrite via `GIT_CONFIG_*` env vars (propagates into nested clones,
  touches no global config — the same approach as before but now actually sufficient),
- verify by **counting real files in every worktree at every depth** and failing if any
  is ≤ 1, rather than by parsing `git submodule status`.

**Command that fixed the tree:**
```bash
git -C obsoletium submodule sync --recursive
git -C obsoletium submodule update --init --recursive --force
git -c url."https://github.com/".insteadOf="git@github.com:" \
    -C obsoletium submodule update --init --recursive --force thirdparty/shadercompile
```
(The `-c` form is needed because the superproject's local config does not propagate
into nested submodule clones; the env-var form used by the script does.)

**Verification:** 29 submodules at all depths, zero empty worktrees.

### 3.3 `setup-env.sh` reported the validation layer as missing when it was installed

**Symptom:** `bad VK_LAYER_KHRONOS_validation not installed` despite
`vulkan-validationlayers-1.4.309` being present and the loader enumerating the layer.

**Root cause:** the check globbed `/usr/share/vulkan/explicit_layer.d/*Khronos_validation*.json`
with a capital **K**, but openSUSE ships `VkLayer_khronos_validation.json` — lowercase
`h`. On a case-sensitive filesystem the glob never matched, so the check reported a false
negative.

**Fix:** match case-insensitively with `*[Kk]hronos_validation*.json` and print the
matched filename so the result is verifiable rather than a bare "ok".

### 3.4 32-bit flag audit — all hits are harmless

`setup-env.sh` greps the tree for `-m32`/`ia32`/`--target=i386`. It reports a WARN with
19 hits; every one is inert for our build:

| File | Why it is harmless |
|---|---|
| `devtools/makefile_base_posix.mak:229,233` | inside the `else` of `ifeq ($(TARGET_PLATFORM),linux64)`. The `linux64` branch sets `LD_SO = ld-linux-x86-64.so.2` and static `libstdc++.a` — i.e. **Valve's own dormant 64-bit Linux intent**. No `.vpc` file references this makefile (verified: 0), and our CMake path never reads it. |
| `external/vpc/utils/vpc/Makefile:26,27` | inside `ifeq ($(OS),Darwin)` — macOS only. |
| everything under `thirdparty/` | test harnesses, CI helpers and alternate-arch build presets of vendored libraries that we do not build (`7zip`, `DirectXMath`, `cryptopp`, `minimp3`, `snappy`, `SDL`, `libjpeg-turbo`). |

`build.sh` additionally asserts every produced object is `ELF 64-bit` after each
configure/build, so a regression would be caught even if a flag slipped in.

## 4. Landmines to remember

- `Counter-Strike Source/` is read-only; never write, and never ship/call its prebuilt
  `bin/`, `cstrike/bin/` or launchers. Assets go through the `game/` symlink farm.
- `build.sh:36` lists a `shaderapivulkan` target that does not exist until Phase 2, so
  that stage will fail until then. Expected.
- `tools/clone-track-a.sh` is not idempotent; use `tools/fetch-submodules.sh` for
  re-syncs instead of re-running the clone.
- Audio: never re-add Miles. `soundsystem/` and `engine/voice_codecs/miles/` are
  excluded on Linux; voice uses the in-tree `celt` + `speex` codecs. The engine uses
  only OpenAL 1.1 core plus `AL_EXT_buffer_data_static`, so a vendored `openal-soft` or
  `mojoAL` is a zero-code-change fallback.
