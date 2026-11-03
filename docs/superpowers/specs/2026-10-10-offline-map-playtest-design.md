# Offline Map Playtest — Design

Date: 2026-10-10
Status: draft, awaiting approval
Related: `doc/SHADER-PIPELINE.md` (Phase 2 Vulkan backend), `doc/STEPS.md`

## Intent and why

The Vulkan renderer (`shaderapivulkan`) can currently draw menu quads
through the real translated Source shaders, but nothing in-game is
verified. The partner wants the first real shakedown to be an ordinary
offline session: start the game, create a server on de_dust2 with no
bots, spawn, and do the usual stuff — walk, shoot, grenades, doors.
This is the milestone that proves the renderer is a game renderer, not
a menu renderer.

## Goals

1. Join an offline de_dust2 server from the menu, spawn, no crash.
2. World renders correctly: real wall/floor/prop textures (no magenta
   fallback, no checkerboard dummy), sane lighting, skybox present.
3. Player-facing essentials visible: viewmodel weapon + hands, muzzle
   flash, crosshair, HUD.
4. Interaction works end to end: walk (collision is engine-side, must
   keep working), shoot walls with decals/impact feedback, throw a
   smoke grenade (particle path), open doors (animated entities).
5. Clean exit back to menu/desktop, no crash, no core dump.
6. If FPS is bad: still counts as working, but an optimization plan is
   written up as a follow-up (not ignored).

## Non-goals

- Bots, multiplayer, server browser play.
- Sound (host SDL3 audio is broken environment-wide; engine degrades).
- Good performance (see goal 6).
- HDR, color correction volumes, flashlight shadows.

## Anti-goals (failure even if it technically runs)

- Magenta or checkerboard anywhere on screen.
- Missing world (blue clear color), missing weapon, missing HUD.
- Crash on map load, on shooting, on quitting to desktop.
- Validation layer errors (mandatory zero-error policy stays).

## Constraints

- Shipped renderer calls Vulkan directly; no DXVK-Native/ToGL/WineD3D
  code in the shipped path (PROMPT.md §2.1).
- Validation layers mandatory; every non-VK_SUCCESS logs and fails loud.
- Debug symbols, never strip (Phase 1 rule, still active).
- All headless testing on Xvfb :99 + lavapipe; interactive testing on
  the partner's display only when they ask.
- `-fvisibility=hidden` for game DLLs; RTLD_GLOBAL kept for engine
  modules (load-bearing: materialsystem resolves g_pHardwareConfig).

## Current state (what exists now)

- Translated VS+PS pairs link with zero validation errors; pipelines
  build; per-range descriptor updates flow.
- Menu quads rasterize with correct transforms, UVs, modulation, fog
  and lightscale defaults (proven with red-PS and checkerboard runs).
- Whole-VTF upload path exists (TexImageFromVTF, BC1-5 native, RGBA8
  converters, half/float converters); batch CreateTextures works.
- Known open issues: some VTF formats still hit the magenta fallback
  (fullscreen magenta seen); PS alpha computes ~0 so blending hides
  everything unless disabled; per-range descriptor refresh is behind
  an env gate pending verification.

## The parts decided by the partner

- Map: de_dust2, empty (no bots).
- Entry: normal menu flow (Create Server), not console commands.
- Interaction scope: walk, shoot, grenades, doors.
- FPS: correctness gates, speed does not (but gets a plan).

## Left to the builder

- Technical breakdown and order (textures → alpha/blend → world
  geometry → lightmaps → entities/props → particles → viewmodel).
- Exact handling of animated models (LoadBoneMatrix currently stakes
  only the base MODEL matrix; doors need more).
- Whether smoke grenades need the full particle path or a reduced one.
- The optimization plan if FPS is bad (deferred until measured).

## Verification

- No scripted input harness (partner decision 2026-10-10). Interaction
  testing (walk/shoot/grenades/doors) is manual on the partner's
  display; headless Xvfb captures serve as supporting evidence only.
- Acceptance is the partner's eyeball: goals 1-5 checked by playing.
- Headless pixel proofs per sub-milestone still expected as evidence.

## Calls made without asking (check while reading)

- The three known open issues (magenta VTF fallback, PS alpha ~0,
  descriptor refresh env gate) are in-scope blockers and lead the work.
- Headless environment treated as provisioned (proven by current runs).
- Audio stays enabled-but-degraded (no flag change).
- Non-goal paths (HDR, flashlight shadows, color correction volumes):
  silent stubs + log, matching existing neutral-stub practice.

## Standing builder liberties (unchanged)

- Temp diagnostics and env gates during bring-up (revert before done).
- Narrow port-owned engine patches in the established style (proven by
  log/gdb, minimal diff, comment marked).
