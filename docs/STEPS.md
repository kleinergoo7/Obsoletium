# STEPS.md — the complete step ladder

Native 64-bit Linux CS:S from Obsoletium, then a native Vulkan renderer.
This is the plan for review. Nothing in Phase 2 starts until Phase 1 is verified
(PROMPT.md: "Do not attempt Phase 2 until Phase 1 produces a verified, runnable,
64-bit native build").

Read `doc/ENVIRONMENT.md` first — it documents the two blockers that shape this plan.

**Legend:** `[ ]` todo · `[x]` done · `GATE` = stop and verify before continuing.
Every gate names a *positive* verification, never "it compiled".

---

## Decision log (already settled — do not re-litigate)

| Decision | Choice | Rationale |
|---|---|---|
| Source tree | Obsoletium `606c49a4d`, branch `port/linux-vulkan` | the port target |
| Reference tree | nillerusr `ed8209cc` | Linux build reference + fallback oracle, parallel |
| Build system | author CMake + generated modules from `.vpc` | Obsoletium's own CMake is a 215-line skeleton; 291 VPC files are the source of truth |
| Build order | server first, then client | a dedicated server proves far more of the engine per unit of work |
| Build type | Debug, symbols never stripped | PROMPT.md §1.3 |
| Renderer | native Vulkan, no DXVK/ANGLE/ToGL at runtime | PROMPT.md §2.1 |
| Shader pipeline | DXBC → HLSL → DXC/glslang → SPIR-V, **ahead of time** | PROMPT.md §2.2 explicitly allows AOT SPIR-V compilation |
| Audio | keep OpenAL Soft | in-tree 1.1 backend already works; miniaudio/Faudio need full rewrites for zero gain |
| Renderer for Phase 1 | togl, as a scaffold only | PROMPT.md §1.5; it must not survive into the shipped build |
| 32-bit | must be dead everywhere | PROMPT.md §1.2 |

---

## Phase 0 — Environment and source integrity (pre-flight)

Nothing in Phase 1 can start until this phase is green.

- [x] **S0.1** Clone Obsoletium, pin `606c49a4d`, create `port/linux-vulkan`
- [x] **S0.2** Clone nillerusr, pin `ed8209cc`
- [x] **S0.3** Write `setup-env.sh` (capability-based checks, loud non-zero failure)
- [x] **S0.4** Write `build.sh` (staged, logged, asserts ELF-64)
- [x] **S0.5** Write `make-gamelink.sh`; generate the 75-entry `game/` symlink farm
- [x] **S0.6** Generate `doc/env.lock`
- [x] **S0.7** Write `doc/ENVIRONMENT.md` and this file

- [ ] **S0.8 — BLOCKER: populate the 17 empty submodule worktrees**
  17 of 24 submodules contain only a `.git` pointer — including `zlib`, `libpng`,
  `celt`, `speex`, `squirrel`, `lua`, `shadercompile`, `external/vpc`.
  `git submodule status` is misleadingly clean; the objects are fetched but the
  worktrees were never checked out.
  ```bash
  git -C obsoletium submodule sync --recursive
  git -C obsoletium submodule update --init --recursive --force
  # verify: every submodule dir must have >1 file
  git -C obsoletium submodule foreach --quiet 'n=$(find . -type f | wc -l); [ "$n" -le 1 ] && echo "EMPTY: $name ($n)"'
  ```
  Acceptance: the verification loop prints nothing. Downloads a few hundred MB.

- [ ] **S0.9 Install the 5 missing packages** (needs your `sudo` password)
  ```bash
  sudo zypper --non-interactive install -y libcurl-devel openal-soft-devel \
    glslang-devel vulkan-validationlayers spirv-cross
  ```
  Deliberately **not** installed, and why: `zlib-devel` (conflicts with the installed
  `zlib-ng-compat-devel`, which already provides `zlib.h`), `nasm` (no `.s`/`.asm`
  sources in any VPC), `libjpeg8-devel` (`libjpeg62-devel` already installed),
  `libedit-devel` (`readline` already present).

- [ ] **S0.10 Fix the stale zypper line in `setup-env.sh:53-57`**
  Replace the `zlib-devel nasm libjpeg8-devel libedit-devel` list with the S0.9 line,
  and add a comment recording *why* zlib-devel is excluded so nobody "fixes" it back.

- [ ] **GATE G0** — `./setup-env.sh --check` exits 0, and S0.8's verification prints
  nothing. Regenerate `doc/env.lock` afterwards.

---

## Phase 1 — native 64-bit build

### 1A — Build system bring-up

- [ ] **S1.1 Toolchain spike (GCC 15 vs Clang 19)**
  Compile one real translation unit (`tier0/memstd.cpp`, `tier0/dbg.cpp`) plus a
  trivial main with both, `-m64`, Debug, no sanitizer. Record versions, warnings, and
  first-failure notes in `BUILD.md`. Pick one and document why.

- [ ] **S1.2 Fix `tools/vpc2cmake.py` — make conditions actually work**
  Four known defects (see ENVIRONMENT.md §10.2–10.5):
  wire `Directive.condition()` into `parse_block` so `$WIN32`/`$LINUXALL` filters work;
  parse top-level `$Configuration` compiler/linker blocks; filter the four empty-name
  pseudo-modules; give each VPC root its own macro context.
  Acceptance: a Linux run of `tier0.vpc` lists only POSIX sources, and a Windows run
  differs. Both must produce sane `includes`/`defines`/`libs`.

- [ ] **S1.3 Generate all 291 modules** into `cmake/generated/`.
  Acceptance: every one of the 291 `.vpc` files maps to a module or an explicit
  documented skip; nothing is silently dropped.

- [ ] **S1.4 Write the CMake glue**
  `cmake/SeVpc.cmake` with a `se_vpc_module(name SOURCES ... INCLUDES ... DEFINES ...
  LIBS ...)` helper, plus root `CMakeLists.txt` integration (target aliases, 64-bit
  assertion, Debug defaults, `POSITION_INDEPENDENT_CODE` on for shared modules).
  Acceptance: `./build.sh --list` resolves, and CMake configures with zero errors
  even before any module is enabled.

- [ ] **GATE G1** — CMake configures cleanly with the generated tree present.

### 1B — Core libraries, one at a time (dependency order)

Each step: build → fix → **verify** → then move on. Never batch.

- [ ] **S1.5 `tier0`** (first target; must be the canary for the whole build system)
- [ ] **S1.6 `vstdlib`**
- [ ] **GATE G2** — `libtier0.so` and `libvstdlib.so` exist, are ELF-64, and export
  their expected symbols (`nm -D` for `Tier0_` / `Vstdlib` entry points).
- [ ] **S1.7 `mathlib`**
- [ ] **S1.8 `public`**
- [ ] **S1.9 `tier1`**
- [ ] **GATE G3** — tier1 links against tier0/vstdlib/mathlib.
- [ ] **S1.10 `filesystem`** (VPK reading; needs the zlib/libpng submodules from S0.8)
- [ ] **GATE G4** — a test binary opens the VPK tree in `Counter-Strike Source/` and
  can list an entry. *This is the first real test that content is readable.*

### 1C — Engine modules

- [ ] **S1.11 `engine`**, `networksystem`, `soundemittersystem`, `utilities`,
  `inputsystem`, `gameui`
- [ ] **S1.12 Audio wiring — link-only, no new code**
  Link `openal` on `$LINUXALL` (already in `engine.vpc`). Exclude `soundsystem/`
  (Miles, Windows tools only) and `engine/voice_codecs/miles/`; use `celt` + `speex`.
  Record the exclusion in `BUILD.md` so nobody re-adds Miles.
- [ ] **S1.13 `vpklib` + a standalone VPK listing tool**
  Needed for the shader investigation (S1.16) and for diagnosing content issues.
- [ ] **GATE G5** — `vpk` tool lists entries from both a v1 and a v2 VPK
  (`cstrike_misc.vpk` and `hl2_misc_dir.vpk`).

### 1D — Server milestone (the first real product)

- [ ] **S1.14 `dedicated`** → `srcds_linux`
- [ ] **S1.15 Verify the dedicated server — positive test, not "it compiled"**
  - runs from `game/` against the symlinked assets
  - mounts `cstrike`, finds `mp_*` content
  - reaches a console/rcon prompt with no fatal error
  - steady state: no crash, no unbounded growth
  If it does not reach a prompt, this is where the debugging protocol
  (PROMPT.md §1.4) applies: gdb `bt full`, ASan variant, `dmesg` for OOM kills.
- [ ] **GATE G6 — SERVER MILESTONE.** Screenshot/log evidence of a running
  `srcds_linux` on the real content, plus a clean-from-scratch rebuild.

### 1E — Game code and client milestone

- [ ] **S1.16 `game_shared`**, `game_server`, `game_client`
- [ ] **S1.17 `client`** + SDL2 window + input
- [ ] **S1.18 togl as a Phase 1 scaffold only** (PROMPT.md §1.5)
  Minimum viable: a visible window. Do not invest in fixing togl. It must be deleted
  before the Phase 2 renderer lands.
- [ ] **S1.19 Verify the client — positive test**
  - window appears and stays up (Wayland/XWayland session)
  - CS:S content loads; a menu or console is reachable
  - **audio is audible**, not merely "the device opened" — this is possible because the
    host runs PipeWire with a live ALSA sink. Debug with `ALSOFT_LOGLEVEL=3`,
    `ALSOFT_DRIVERS=pulse`, `ALSOFT_TRAP_ERROR=1`
  - if the window is black, that is *expected and acceptable* at this stage — the
    shader question below is the reason, and is resolved in Phase 2/B0
- [ ] **GATE G7 — CLIENT MILESTONE.** Visible working window + audible sound + content
  loaded, on a from-clean rebuild.
- [ ] **S1.20 `studio`, `studiorender`, `vguimatsurface`, `particles`** as far as they
  come without a renderer

### 1F — Resolve the shader-content mismatch (may gate a visible client)

- [ ] **S1.21 Characterise the shader blobs** — the content has 0 `.fxc` but 3502
  `.dx90` + 3502 `.sw` + 3567 `.dx80`, while the engine loads `<name>.fxc`
  (`vertexshaderdx8.cpp:1110`) and caches `.vcs`.
  - extract a sample of `.dx90` / `.sw` / `.dx80` with the S1.13 tool
  - identify each: DXBC magic `DXBC`? SPIR-V magic `0x07230203`? GLSL? bytecode?
  - count how many of the 245 `.vmt` files reference shaders, and whether the
    referenced names resolve at all
  - decide the route (see Phase 2 B0; the answer is the same investigation)
- [ ] **GATE G8** — a written, evidence-backed answer on shader format compatibility in
  `BUILD.md`. Options: (a) convert blobs to what the loader wants, (b) teach the
  loader to read `.dx90` in our Vulkan backend, (c) regenerate from reconstructed HLSL.
  Option (b) is a legitimate native path; the *chosen* route is (c) unless evidence
  says otherwise.

### 1G — Phase 1 close-out

- [ ] **S1.22 Full clean rebuild** (`rm -rf build && ./build.sh`) at least once, to
  rule out stale-cache false positives (PROMPT.md §1.4)
- [ ] **S1.23 Write `BUILD.md`** — exact versions, every issue hit, every fix, and the
  reasoning. This is the debugging knowledge base for Phase 2.
- [ ] **S1.24 Write the Phase 1 status report** — what runs, what does not, what was
  skipped, anything still architecturally 32-bit (e.g. third-party `.so`s with no
  64-bit build)
- [ ] **GATE G9 — PHASE 1 COMPLETE.** Only now may Phase 2 begin.

---

## Phase 2 — native Vulkan renderer

### 2A — De-risk the scariest thing first

- [ ] **S2.1 B0 shader spike (do this before writing renderer code)**
  Prove, on real content, the whole chain: DXBC (`.dx90`) → HLSL → SPIR-V, validated
  with `spirv-val`. Questions to answer:
  - which tool does the HLSL: `dxc` (not packaged — build with LLVM, or use
    `glslangValidator` as the AOT compiler; needs your decision) or glslang
  - how well does DXBC decompilation survive Source's macros? (this is the risk)
  - what *is* `.sw` — could it remove this step entirely?
  - can the pixel and vertex stages be separated, and how are shader-model variants
    (`30.fxc` → `2x.fxc` style rewrites, `vertexshaderdx8.cpp:1119-1154`) handled?
  - fallback: `togl/linuxwin/dx9asmtogl2.cpp` (HLSL→GLSL) as a cross-check oracle
  Deliverable: a reproducible script + `doc/SHADER-PIPELINE.md`. If this fails, the
  project needs re-planning — which is exactly why it is first.

### 2B — Incremental renderer, in order, with validation on at all times

- [ ] **S2.2 Instance / device / queue, with `VK_LAYER_KHRONOS_validation` on by
  default.** Target **Vulkan 1.3** (device is 1.3.296). Every non-`VK_SUCCESS` return
  logged with call site, parameters, and result — never ignored.
- [ ] **S2.3 Swapchain + cleared color buffer — something on screen.**
  WSI decision here: start with XCB via XWayland (`DISPLAY=:1`) for speed, keep
  Wayland as a one-line swap later. RenderDoc capture to verify.
- [ ] **S2.4 Hardcoded-pipeline triangle** — proves pipeline/shader/VBO plumbing.
- [ ] **S2.5 Texture sampling.**
- [ ] **S2.6 The real shader system** wired to the S2.1 output. Keep the original
  DXBC/HLSL next to every generated SPIR-V module in the build log for traceability.
- [ ] **S2.7 Render targets, depth/stencil, multipass** (Source does RTT passes:
  water, HDR).
- [ ] **S2.8 World geometry → models → post-processing → vgui last.**
- [ ] **GATE per step:** visual/RenderDoc verification + zero validation errors or
  warnings, compared against the Phase 1 togl reference via screenshot diff.
- [ ] **S2.9 Feature-parity checklist** updated after every milestone
  (world ✅ / lighting ⚠ / water ❌ / HDR ❌ / particles ❌ …) — never claim broad
  completion early.
- [ ] **S2.10 Performance pass** — `perf` + Vulkan GPU timestamps; hunt redundant
  barriers and descriptor rebinds.
- [ ] **S2.11 Delete togl.** PROMPT.md §2.1: the shipped renderer must call Vulkan
  directly. No ToGL, DXVK, WineD3D, or ANGLE anywhere in the runtime path.
- [ ] **S2.12 Write the D3D9→Vulkan mapping document** — per concept, the Vulkan
  construct, and semantic differences compensated (clip-space Y-flip, depth 0–1 vs
  −1–1, descriptor set layout choices).
- [ ] **GATE G10 — PHASE 2 COMPLETE.** Playable frame rate on a real CS:S map with
  world geometry, models, basic lighting and UI, under validation layers with zero
  errors.

Reference material for the mapping (read-only inspiration, never vendored or linked):
WineD3D's Vulkan backend (primary — pure C maps cleanly onto the C API) and DXVK
(cross-check for pipeline-cache and descriptor-batching ideas).

---

## Risks, in priority order

| # | Risk | Impact | Mitigation |
|---|---|---|---|
| 1 | **DXBC → HLSL → SPIR-V does not survive Source's macros** | threatens the whole Phase 2 plan | S2.1 spike runs *first*; `.sw` may be a shortcut; togl GLSL path is an oracle |
| 2 | **Content is Source-1.13-era but the engine expects `.fxc`** | no shaders load; client window stays black | S1.21 characterizes it early; option (b) is a legitimate native fallback |
| 3 | **17 submodules empty** | nothing compiles | S0.8, verified by a positive check, not by exit code |
| 4 | **No Linux CMake exists in Obsoletium** (215-line skeleton, no `add_subdir`) | Phase 1 is a build-authoring project, not a port | S1.2–S1.4, staged, tier0 first as canary |
| 5 | **GCC 15 / Clang 19 strictness** on 2013-era C++ | many small fixes | S1.1 spike picks the friendlier compiler early; nillerusr is the oracle |
| 6 | **Third-party `.so` with no 64-bit build** | feature gaps | only `soundsystem`/Miles, already excluded; anything new goes in the status report |
| 7 | `dxc` not packaged | blocks S2.1 | decide early: build DXC with LLVM, or use glslang as the AOT compiler |

## Open questions for you

1. **S0.9/S0.8** — may I run the submodule re-fetch (~a few hundred MB) and do you
   want to run the `zypper` line yourself? (`sudo` needs your password.)
2. **S2.1 tooling** — `dxc` is not packaged. Build it with LLVM, or use
   `glslangValidator`'s HLSL frontend as the ahead-of-time compiler?
3. **Phase 1 visual bar** — is "window up + sound audible + shaders still black"
   acceptable at G7, given risk #2, or do you want the shader route resolved before
   calling the client milestone done?
4. **Track B** — keep nillerusr building in parallel as an oracle, or park it until
   Track A's `filesystem` + `vpklib` are proven?
