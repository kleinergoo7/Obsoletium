# ENVIRONMENT.md — native 64-bit Linux CS:S port (Obsoletium)

Status: verified by inspection on 2026-09-28. Every claim below was checked against
this machine; anything unverified is explicitly marked **UNVERIFIED** or **OPEN**.

Companion documents: `doc/STEPS.md` (the full step ladder), `doc/env.lock` (generated
version snapshot), `PROMPT.md` (the specification this project implements).

---

## 1. Host

| Item | Value |
|---|---|
| Distro | openSUSE Leap 16.0 |
| Kernel | 6.12.0-160000.38-default |
| Arch | x86_64 |
| glibc | 2.40 |
| CPU threads | 16 |
| RAM | 23 GiB |
| Session | Wayland (`WAYLAND_DISPLAY=wayland-0`), XWayland on `DISPLAY=:1` |
| Audio server | PipeWire + `pipewire-pulse`; ALSA sinks: `analog-stereo` RUNNING, `hdmi-stereo-extra3` SUSPENDED |
| Container tooling | none (no Docker, no Podman) — host-native build it is |

**Consequence:** the Wayland session matters only for Phase 2's WSI choice. `DISPLAY`
is set, so XCB/Xlib surfaces work through XWayland; `VK_KHR_wayland_surface` is the
native alternative. Decision deferred to Phase 2 step S2.2.

## 2. Toolchain

| Tool | Version | Status |
|---|---|---|
| gcc / g++ | 15.3.0 (SUSE) | present |
| clang / clang++ | 19.1.7 | present |
| cmake | 3.31.7 | present |
| ninja | 1.12.1 | present |
| python3 | 3.13.14 | present (build scripts) |
| git | 2.51.0 | present |
| pkg-config | present | present |
| 64-bit preflight | `gcc -m64` and `clang -m64` both link | verified |
| `-m32` audit of build files in the tree | no hits | verified |

Both compilers are candidates; the winner is not yet decided (step S1.1). Debug is the
default build type and symbols are never stripped (PROMPT.md §1.3).

## 3. Source trees

| Tree | Path | Pin | Role |
|---|---|---|---|
| Obsoletium (Track A) | repository root | `606c49a4d`, branch `port/linux-vulkan` | the port target |
| nillerusr/source-engine (Track B) | `nillerusr-source-engine/` | `ed8209cc` | Linux build reference + fallback oracle |

The tree has 291 `.vpc` files. Its own `CMakeLists.txt` is a 215-line skeleton
with no `add_subdirectory()` and its CI is Windows-only — i.e. **there is no working
Linux build to reuse; one has to be authored.** That is the central engineering task
of Phase 1.

### 3.1 BLOCKER — 17 of 24 submodules are empty

Every submodule directory contains only its `.git` pointer file, i.e. no working-tree
content at all. Populated vs. not:

| Populated | files | Empty (blocking) |
|---|---|---|
| `thirdparty/SDL` | 2327 | `thirdparty/stb` |
| `thirdparty/7zip` | 1294 | `thirdparty/zlib` |
| `thirdparty/libjpeg-turbo` | 623 | `thirdparty/libpng` |
| `thirdparty/protobuf` | 493 | `thirdparty/bzip2` |
| `thirdparty/openvr` | 198 | `thirdparty/cryptopp` |
| `thirdparty/DirectXMath` | 88 | `ivp` |
| `thirdparty/zip-utils` | 53 | `thirdparty/snappy` |
| `thirdparty/nvtristrip` | 14 | `thirdparty/mpaheaderinfo` |
| | | `thirdparty/minimp3` |
| | | `thirdparty/celt` ← voice codec |
| | | `thirdparty/speex` ← voice codec |
| | | `thirdparty/speedtree` |
| | | `thirdparty/lua` |
| | | `thirdparty/shadercompile` |
| | | `thirdparty/squirrel` |
| | | `external/vpc` ← Valve's VPC tool |

`git submodule status` reports the recorded commits as matching, and
`tools/fetch-submodules.sh` exited 0, so the earlier "submodules done" conclusion was
wrong: the objects are present in `.git` but the worktrees were never checked out.
`external/vpc` and `ivp` additionally show every file staged as deleted (`D`) in their
index.

Nothing can compile until this is fixed — step **S0.1**. Note the two casualties that
matter most: `zlib` and `libpng` (needed by `vpklib`/`filesystem`), and `celt`+`speex`
(needed for the voice codecs that replace Miles).

## 4. Game content (read-only)

Path: `Counter-Strike Source/`, Steam appid 240, 5.0 GB, 44 VPKs.
**Never written to.** Never use or ship the prebuilt `bin/`, `cstrike/bin/`, or the
launchers. Assets are consumed through the generated symlink farm instead.

Loose content on disk: 616 `.txt`, 316 `.vtf`, 297 `.res`, 245 `.vmt`, 189 `.vtx`,
91 `.mp3`, 68 `.dat`, 67 `.vvd`, 67 `.mdl`, 63 `.phy`, 92 `.so` (Windows binaries,
ignored), 44 `.vpk`.

VPK format split: 38 v1 (tree at end of file) + 6 v2 (tree in a `_dir.vpk` companion:
`cstrike_pak`, `hl2_misc`, `hl2_sound_misc`, `hl2_sound_vo_english`, `hl2_textures`,
`platform_misc`).

`game/` is the generated read-only view: 75 symlinks (cstrike 32, hl2 36,
platform 7) plus `srcds.sh` and `steam_appid.txt`. `cstrike/bin` and `demoheader.tmp`
are excluded by `make-gamelink.sh`.

## 5. BLOCKER — the content's shader blobs do not match the engine

Verified by scanning every VPK's directory region for shader-entry filenames:

| Format | Count in content | What the engine wants |
|---|---|---|
| `.fxc` | **0** | `<name>.fxc` — `materialsystem/shaderapidx9/vertexshaderdx8.cpp:1110` |
| `.dx90` | 3502 | not read by this engine version |
| `.sw` | 3502 | not read by this engine version |
| `.dx80` | 3567 | not read by this engine version |

Shader entries are flat (`2x4walls01.dx90`, `2x4walls01.sw`, …), and each `.dx90` has a
matching `.sw`. The engine also caches compiled shaders as `.vcs`
(`vertexshaderdx8.cpp:125`, `SHADER_FNAME_EXTENSION`).

Two consequences, and they are the reason the shader plan matters:

1. **The installed content is Source-1.13-era** (Valve moved CS:S to the 1.13 engine,
   which ships `.dx80`/`.dx90`/`.sw` blobs), while Obsoletium descends from the
   Source 2013 lineage that expects `.fxc`. The material system reads `.vmt` as text so
   materials themselves are fine, but **no shader can load** until this is bridged.
2. `.dx90` is DXBC (shader model 4/5) and `.dx80` is DXBC (SM1–3) — i.e. real DXBC
   bytecode, which is exactly the input our agreed Phase 2 strategy expects
   (DXBC → HLSL → SPIR-V, ahead of time, no runtime translation).

**OPEN:** what is `.sw`? If it is a Valve-pre-cross-compiled portable form it could
sidestep most of the reverse-engineering effort. Answering this is the first item of
the B0 spike: extract a handful of blobs (needs a VPK lister → `vpklib`, step S1.10),
then `file`/`xxd` them looking for the DXBC magic `DXBC`, the SPIR-V magic
`0x07230203`, or GLSL source.

Fallback option if `.sw` turns out to be unusable and DXBC decompilation proves too
lossy: teach the shader loader to read `.dx90` directly and consume it in the Vulkan
backend (legitimate, still native — it is our own loader, not a translation layer).
Noted for the record; not the chosen path.

## 6. Audio — resolved, no new code required

`engine/audio/private/snd_dev_openal.cpp` is Valve's own OpenAL 1.1 backend (614
lines) and `engine/engine.vpc` already links `openal` under `$LINUXALL`. The exact
OpenAL surface the engine uses is small and pure 1.1:

```
alGenSources alDeleteSources alGenBuffers alDeleteBuffers alBufferData
alBufferDataStatic(Proc) alSourcePlay alSourceStop alSourceQueueBuffers
alSourceUnqueueBuffers alSource3f alSourcef alGetSourcei alGetError alGetProcAddress
alListener3f        <- exactly one call, hardcoded to (0,0,0) at line 209
alc{Capture*,SuspendContext,MakeContextCurrent}
```

No EFX, no HRTF, no doppler, no effect slots. The only non-1.1 item is
`alBufferDataStatic`, fetched through `alGetProcAddress`
(`AL_EXT_buffer_data_static`, line 363).

Host audio: `libopenal1-1.24.3` (OpenAL Soft) **already installed**; only the
`-devel` headers are missing. Debug knobs if audio misbehaves:
`ALSOFT_LOGLEVEL=3`, `ALSOFT_DRIVERS=pulse`, `ALSOFT_TRAP_ERROR=1`, plus the engine's
`snd_mixahead` cvar.

### 6.1 Miles: not needed, do not look for it

- Valve's own 64-bit Linux `engine.so` links OpenAL/SDL2 and has **no** Miles
  dependency. There is no 64-bit Miles to hunt for.
- `soundsystem/` is the only Miles consumer, and it is used exclusively by Windows
  tools (Hammer, hlmv) and tests. **Excluded on Linux.**
- `engine/voice_codecs/miles/` is replaced by the in-tree `celt` and `speex` codecs.
  **Excluded on Linux.**

### 6.2 Alternatives evaluated and rejected

| Option | Why not |
|---|---|
| **miniaudio** (single-header, public domain/MIT-0) | A different API, not an OpenAL implementation — requires rewriting the whole 614-line device backend against `ma_sound_engine`. The GeneralsX port did exactly this swap: ~1,100 new lines, a build flag, f32/PCM16 format bugs, and they had to keep FFmpeg anyway because miniaudio's MP3 decoder hung on VFS streaming. Zero gain, since the engine only uses 1.1 core. |
| **Faudio** (FNA-XNA) | Not an OpenAL replacement at all: an XAudio2/X3DAudio/XAPO/XACT3 reimplementation, zlib-licensed, **depends on SDL 3**. Would require an OpenAL→XAudio2 translation layer *plus* a new dependency. Strictly worse. |
| **SDL audio** | No SDL audio device exists in Obsoletium (nillerusr has one), and we would still write a device backend. Not worth it when OpenAL is already written. |

**Decision: keep OpenAL Soft.** Escape hatch if the system library ever misbehaves —
because the surface is pure OpenAL 1.1, we can vendor `openal-soft` source (nillerusr's
approach) or use **mojoAL** (single-file OpenAL 1.1 over SDL3) with *zero* changes to
the device code.

## 7. Libraries

Present (pkg-config): `sdl2 2.30.7`, `zlib 1.3.1.zlib-ng`, `libpng 1.6.58`,
`freetype2 26.6.20`, `fontconfig 2.16.0`, `alsa 1.2.14`, `x11 1.8.10`,
`xrandr 1.5.4`, `xkbcommon 1.8.0`, `wayland-client 1.24.0`, `gl 1.2`, `glesv2 3.2`,
`vulkan 1.4.309`.

Missing and required: `libcurl`, `openal-soft-devel`, `glslang`,
`vulkan-validationlayers`, `spirv-cross`.

`spirv-val` / `spirv-dis` / `spirv-opt` (SPIRV-Tools 2025.1) and `vulkan-tools` are
already installed.

### 7.1 The zlib trap

`zlib-devel` must **not** be installed. The host has `zlib-ng-compat-devel`, which owns
`/usr/include/zlib.h` and provides `pkg-config zlib = 1.3.1.zlib-ng`; zypper reports
`zlib-devel` as conflicting. Installing it risks a broken toolchain, so both
`setup-env.sh` and any install line must omit it.

`libjpeg62-devel` already satisfies the JPEG dependency — do not look for
`libjpeg8-devel`. No `.s`/`.asm` sources appear in any VPC, so NASM is not needed, and
`readline` is present so `libedit-devel` is not needed either.

## 8. Vulkan

| Item | Value |
|---|---|
| Loader | 1.4.309 |
| GPU0 | AMD Radeon RX 580 Series (RADV POLARIS10), device API **1.3.296** |
| GPU1 | llvmpipe (LLVM 19.1.7, 256 bits), device API 1.3.296 |
| `VK_LAYER_KHRONOS_validation` | **not installed** — mandatory for all of Phase 2 |
| `glslangValidator` | **not installed** |

**Target Vulkan 1.3, not 1.4** — the device only exposes 1.3.296 even though the
loader reports 1.4.309. llvmpipe is present and is a useful second device for
cross-checking.

## 9. Repository layout

```
css-port-experiment/
├── PROMPT.md                    the specification
├── setup-env.sh                 capability checks + installer + env.lock writer
├── build.sh                     single build entry point, staged, logged
├── make-gamelink.sh             regenerates the read-only game/ symlink farm
├── doc/
│   ├── ENVIRONMENT.md           this file
│   ├── STEPS.md                 the full step ladder (review this one)
│   └── env.lock                 generated version snapshot
├── tools/
│   ├── vpc2cmake.py             VPC → CMake module generator
│   ├── clone-track-a.sh         Obsoletium clone (not idempotent)
│   ├── clone-track-b.sh         nillerusr clone
│   └── fetch-submodules.sh      submodule fetch (worktrees still empty — see §3.1)
├── logs/                        timestamped build/clone logs
├── cmake/                       EMPTY — SeVpc.cmake and helpers go here
├── game/                        generated asset symlink farm (read-only view)
├── (engine tree)               Track A
└── nillerusr-source-engine/     Track B
```

## 10. Known defects in the current tooling

These are recorded, not fixed — they await the go-ahead in `doc/STEPS.md`.

1. `setup-env.sh:53-57` — the zypper install line is stale: it lists `zlib-devel`
   (conflicts, §7.1), `nasm` (unnecessary) and `libjpeg8-devel` (already satisfied).
   The correct line is:
   ```bash
   sudo zypper --non-interactive install -y libcurl-devel openal-soft-devel \
     glslang-devel vulkan-validationlayers spirv-cross
   ```
2. `tools/vpc2cmake.py` — `parse_block()` constructs every `Directive` with
   `cond=None`, while the evaluators in `parse_vpc`/`walk_project_block` test
   `d.cond`. Platform conditions are therefore **not applied**: a Linux run still
   collects Windows sources. The `Directive.condition()` helper exists but is not
   wired in.
3. `tools/vpc2cmake.py` — top-level `$Configuration` blocks are not parsed, so per
   target `includes`/`defines`/`libs` come out incomplete.
4. `tools/vpc2cmake.py` — base scripts (`source_posix_base.vpc` etc.) emit four
   pseudo-modules with empty names that must be filtered.
5. `tools/vpc2cmake.py` — one macro context is shared across the whole repository
   walk, so macros can leak between VPC roots; each root should get a fresh context.
6. `build.sh:36` lists `shaderapivulkan` in a stage — that target does not exist
   yet; the stage will fail until Phase 2 creates it.
7. `tools/clone-track-a.sh` is not idempotent: a second run aborts on the existing
   directory.
