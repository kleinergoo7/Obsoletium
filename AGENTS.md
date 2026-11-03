# Agent rules (CSS Linux/Vulkan port)

Canonical policy for every coding agent in this repo. Precedence: the user's current
request, then this file, then `docs/` (`PROMPT.md`, `BUILD.md`, `STEPS.md`,
`SHADER-PIPELINE.md`, `ENVIRONMENT.md`).

Project: native 64-bit Linux Counter-Strike: Source (Obsoletium tree) with a hand-written
Vulkan renderer in `materialsystem/shaderapivulkan/`. Current milestone: a
readable main menu; then a playable map.

## 0. Project facts

- Source tree: this repository's root (the Obsoletium tree). Reference tree:
  `research/nillerusr-source-engine/` — read-only oracle, never built or copied wholesale.
- Game assets: `css-steam-assets/` — **read-only**. `game/` is a generated symlink farm
  (`tools/make-gamelink.sh`). Never ship or call the shipped prebuilt `bin/`.
- Build dir: `build-client`. Build only the targets you touched:
  `ninja -C build-client libshaderapivulkan.so libmaterialsystem.so libengine.so`
  A bare `ninja` fails on utility targets; that is expected, not a regression.
- `game_client/bin/linux64/{libshaderapivulkan.so,shaderapivulkan.so}` are symlinks to
  `build-client/libshaderapivulkan.so`. Do not replace them with copies.
- Shader model: the shipped 2024 `stdshader_dx9.so` + shipped `.vcs` pair is the target.
  Swap with `tools/use_shipped_shader_model.sh [on|off]`. Never mix tree-model and
  shipped-model shaders.
- Shader pack: `game_client/vulkan/` (`static_index.json`, `manifest.json`, `*.spv`).
  Rebuild with `tools/vcs_pack.py` (~7 min); re-extract `.vcs` with `tools/vpk_extract.py`.
- Logs: `<repo>/logs/` (git-ignored). Probe harness and throwaway files:
  `<repo>/__tmp/` (git-ignored). **Never write logs or scratch outside the project
  directory** — not `/tmp`, not `/dev/shm`, not `$HOME`. This overrides any global
  scratch-dir default: the user's current request wins.
- Name logs for their purpose and keep them:
  `<repo>/logs/<tool>-<topic>-<YYYYMMDD-HHMMSS>.log`. Capture both stdout and stderr
  (`> log 2>&1`). Delete the previous run's log before a new run so a stale log is never
  read as evidence.

## 1. Build and verify

- Success comes from the process exit status, never from grepping tool output. This host
  emits German diagnostics (`Fehler:`), so `grep -E 'error:|FAILED'` reports zero errors on
  a failed build.
- After every rebuild, prove the artifact changed: `strings <so> | grep -c '<new marker>'`,
  or compare mtime/hash. A stale `.so` reads exactly like a successful build.
- Run from the repo root (or with the correct workdir). `ninja -C build-client` from
  `materialsystem/shaderapivulkan` fails; so does `ninja -C build-client`
  from the repository root.
- Never claim "done", "fixed", or "works" without the command, its exit status, and the
  observation that proves it.
- No new dependency without stating it. Prefer `brew`; then `zypper`. No hacky
  workarounds — name what the user must install.

## 2. Runtime verification

- Software testing on lavapipe is standing-authorized:
  `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json LP_NUM_THREADS=1`.
- A hardware GPU run needs explicit user go-ahead:
  `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json`.
- Every run needs `stdbuf -oL -eL` and `DISPLAY=:1 WAYLAND_DISPLAY=wayland-0`.
  Engine `Msg`/`Warning` are swallowed after init (~6.5 s); probe engine code with
  `printf(...); fflush(stdout);`. Never trust Msg-based counters.
- Only one game may run (fcntl lock). Kill by exact name:
  `kill -9 $(pgrep -x hl2_linux)`. Never use `pgrep -f`/`pkill -f` with a literal pattern
  in the same command — it matches the shell itself.
- A run is evidence only if the log comes from the build you just made. Delete the old log
  before each run; never read a stale one.
- Visual proof: `SE_VK_CAPTURE=/path.ppm SE_VK_CAPTURE_ON_BIND=1` (first engine-driven
  frame) or `SE_VK_CAPTURE_AT=<n>`. Write the PPM under `<repo>/__tmp/` and count distinct
  colours in it — one colour means nothing rendered.
- A desktop screenshot is not proof unless the game window's bounding box is analysed
  separately; the desktop contributes thousands of colours on its own.
- Zero tolerance: `grep -c 'validation]\[ERROR' <log>` must be 0. Any validation message is
  a bug to fix, not to suppress.
- `vkWaitForFences` timeouts and "GPU is wedged" paths must stay loud; never extend a
  timeout to hide a hang.
- Capture a baseline before a renderer change; keep the PPM so a regression can be diffed
  against the last known-good frame.

## 3. Errors must fail loudly

- Every non-`VK_SUCCESS` logs the call site, its parameters, and the `VkResult`, then fails.
  Never ignore a return code, never default to a plausible zero or a null handle.
- No empty catches, silent fallbacks, silent retries, or error-to-log downgrades that let a
  broken run continue. A stub that fakes success is a bug: return a named error instead.
- Never strip debug symbols. Debug symbols are mandatory in every build variant.
- Log at the point of failure. Messages name the input: file path, shader key/combo index,
  texture handle, pipeline id.

## 4. Debugging discipline

- Find the root cause before changing code. One hypothesis, one minimal change, one run.
  Three failed fixes means the model of the system is wrong — stop and re-investigate.
- Diagnose in batches. One run must answer many questions: gate probes by env var, cap their
  output, and write to a single log. Never sequence one-probe-per-build cycles.
- Isolate layers with decisive experiments before reading more code. Examples that worked
  here: swap in a trivial fragment shader to separate geometry from PS; force the fallback
  triangle to separate the draw path from the render pass; force blending off to separate
  alpha from geometry.
- Runtime probes are temporary. Remove every `SEVK-TEMP`, `[tmp]`, `[tmp-vk]`, `[tmp-mx]`,
  `[tmp-lm]`, `[tmp-lmm]` probe before any commit. A probe left always-on is churn.
- Reproduce with a plain run first. gdb changes timing; use it for stacks, not for timing
  claims. `timeout -s INT N gdb ... -ex run -ex "bt 25"` for a crash context.
- Never work around a root cause you do not understand. Report the exact error, what you
  tried, and what you suspect.

## 5. Renderer fidelity rules

- The shipped renderer calls Vulkan directly. No DXVK, WineD3D, ANGLE, or ToGL at runtime,
  and no code copied from them. WineD3D is read-only documentation.
- DX9 semantics must be reproduced, not approximated. Known traps in this codebase:
  - Matrix constants are uploaded **transposed** (slot = VMatrix column). The fxc shaders do
    `gl_Position.x = dot(pos, c8)`, so `c8` must be VP column 0 or the ortho translation is
    lost and every vertex clips.
  - The pack is keyed by the engine's flat combo index
    `flat = staticComboID * nDynamicCombos + dynamicComboID`. The engine's shadow-state index
    is the *static* combo id; the dynamic part arrives via `SetPixel/VertexShaderIndex` and
    the command buffer (`CBCMD_SET_PSHINDEX`/`SET_VSHINDEX`). Both halves are required.
  - Blend enable comes from the material shadow state (`EnableBlending`), never guessed.
  - Constants, textures, and draw ranges are snapshotted per bind; do not read post-frame
    engine state to decide per-draw behaviour.
- A visual fix that only makes pixels appear is not a fix. Name the DX9 state the Vulkan
  state corresponds to.

## 6. Assets, git, and commit hygiene

- Never commit game content, VPKs, extracted trees, `.vcs` sources, or shader packs derived
  from game content unless already tracked and intended.
- `css-steam-assets/` is read-only; assets flow through the `game/` symlink farm.
- `logs/` and `__tmp/` stay git-ignored. Never commit logs, traces, PPM captures, or probe
  output, and never move them into tracked paths.
- Small commits, one reason each. Conventional prefixes as used here:
  `vulkan:`, `materialsystem:`, `engine:`, `fix:`, `chore:`.
- Build fixes and behaviour fixes land separately. Never force-push or rewrite history.
- A workaround is labelled `WORKAROUND` (or `shortcut:`) with its ceiling and the upgrade
  path. Silent workarounds are bugs.

## 7. When unsure

- If an API, offset, shader constant, or DX9 fact cannot be verified, stop and say so.
  Do not guess a field, signature, or register index.
- Prefer refusing to claim a result over reporting one that may be wrong.
