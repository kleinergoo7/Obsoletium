# Native 64-bit Linux Counter-Strike: Source with a Vulkan renderer

Port of the [Obsoletium](https://github.com/Source-Authors/Obsoletium) Source-engine
fork to 64-bit Linux, with the D3D9 shader API replaced by a hand-written Vulkan
backend (`materialsystem/shaderapivulkan/`). No DXVK, WineD3D, ANGLE or
ToGL at runtime.

Status: the engine boots, loads CS:S content, and renders through Vulkan with zero
validation errors. The main menu is the current milestone; maps do not render yet.

## Quick start

```bash
./tools/setup-env.sh --check                 # host dependencies
./tools/build.sh                             # build everything (logs/ holds the log)
./tools/prepare-run-tree.sh                  # run tree + shader pack
cd game_client && ./hl2.sh -novid -shaderapi shaderapivulkan.so -windowed -w 640 -h 480
```

Full reproduction from a fresh clone, including what is not in git:
**`docs/REPRODUCE.md`**.

## Layout

| Path | What |
|---|---|
| `.` (root) | Engine source (the repo root is the engine root) |
| `materialsystem/shaderapivulkan/` | Native Vulkan renderer |
| `tools/` | Build, asset, shader-pack and verification tooling |
| `docs/` | `PROMPT.md` (brief), `BUILD.md` (host facts), `STEPS.md` (gates), `SHADER-PIPELINE.md`, `REPRODUCE.md` |
| `css-steam-assets/` | Game content (not in git) |
| `game_client/` | Generated run tree (not in git) |

## Verification

```bash
build-client/vk_matrix_test      # constant upload convention
build-client/vk_present_test     # swapchain + clear, 60 frames
```

Agent rules for working in this repo: `AGENTS.md`.
