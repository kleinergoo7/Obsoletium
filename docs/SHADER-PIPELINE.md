# SHADER-PIPELINE.md — B0 spike result + G8 answer

Status: B0 spike **done** (end-to-end proof below). This document is the
deliverable for STEPS S2.1 and the written answer for GATE G8.

Tooling (all in `tools/`): `vpk_extract.py` (VPK v1/v2 list+extract),
`vcs_inspect.py` (container probe), `vcs_survey.py` (full census),
`sm3_disasm.py` (SM3 token → DX9 asm text).

---

## 1. What the content actually contains (corrects ENVIRONMENT.md §5)

ENVIRONMENT.md §5 claims 3502 `.dx90` + 3502 `.sw` + 3567 `.dx80` blobs and
0 `.fxc`, found "by scanning every VPK's directory region". **That does not
match this content drop.** Re-checked 2026-10-09 with a real VPK parser
(`tools/vpk_extract.py`, v1 tree-at-end + v2 `_dir` companions, all 44
archives) plus loose-file search:

| Format | In VPK directories | Loose on disk |
|---|---|---|
| `.dx90` / `.sw` / `.dx80` | **0** | **0** |
| `.fxc` | 0 | 0 |
| `.vcs` | **566** (`hl2_misc_dir` 563, `platform_misc_dir` 3) | 0 |

The 44 files are 6× v2 sets (`_dir` + numbered data archives), not "38 v1 +
6 v2" — numbered v2 archives have no header at all, which is presumably what
the earlier raw-byte scan miscounted. There is no `.sw` question to answer
and no DXBC anywhere: `DXBC` magic appears in **zero** blobs.

What exists: `shaders/vsh/*.vcs` and `shaders/fxc/*.vcs` — exactly the two
paths the engine tries (`vertexshaderdx8.cpp:2409`: `shaders\vsh|psh\*.vcs`,
then `shaders\fxc\*.vcs`). **No conversion is needed to make the loader find
files.** The work is translating their *contents* to SPIR-V.

## 2. VCS container formats (verified by decoding)

Header: 28 bytes. Content files are versions **v4** (169), **v5** (1),
**v6** (369), **v2** (2). Engine accepts 4/5/6 (`OpenFileAndLoadHeader`).

- **v4**: dictionary of `(offset, size)` pairs at byte 28 (one per dynamic
  combo), combo payloads are **raw shader token bytecode**, tiling the file
  exactly (verified: `writez.vcs` 44+96 / 140+96 = 236).
- **v6**: static-combo records at 28 (`(staticID, fileOffset)` + `0xFFFFFFFF`
  sentinel), each blob = `u32 size|type` + payload. All observed payloads are
  **LZMA** (`0x40000000` + `LZMA` magic + Valve `lzma_header_t`, decoded with
  stock LZMA1 `lc=3,lp=0,pb=2`, 256 KiB dict). No bzip2/raw blocks observed
  in the surveyed files.

⚠ Field-order note: in these v4 files, header word[5] reads as a CRC32 and
word[6] as the diff-reference size — i.e. swapped vs this tree's
`ShaderHeader_t_v4` struct. Decoding by "dictionary at 28, combos tile the
file" is what validates, not the struct order. Flagging for the renderer
loader (which must use the same empirical layout).

## 3. Shader models in the blobs (census over 541 files)

| Model | Combos | Notes |
|---|---|---|
| `ps_2_x` | ~14000 | bulk of pixel shading |
| `ps_2_0` | ~5200 | |
| `ps_3_0` | ~7100 | incl. CTAB constant tables |
| `vs_2_0` | ~500 | |
| `vs_3_0` | ~270 | |
| `ps_1_1` / `vs_1_1` | ~560 | oldest fixed-function-era |
| `ps_1_4` / `vs_1_4` | few | |

All are **SM1–3 token bytecode** (version token + instruction words +
`0x0000FFFF` end). No DXBC, no SPIR-V, no GLSL/HLSL source in content.

## 4. Translation route (replaces the STEPS S2.1 DXBC premise)

There is no DXBC→HLSL step: the input is tokens, and no stock tool consumes
SM1–3 tokens. The route, each stage verified or directly scoped:

```
.vcs → combo bytes → DX9 asm text → GLSL → SPIR-V → spirv-val
   (1)      (2)          (3)          (4)      (5)       (6)
```

1. **Container decode** — done (`vcs_survey.py` implements v4 + v6/LZMA).
2. **Tokens → DX9 asm** — proven for SM3 (`tools/sm3_disasm.py`: opcode =
   low 16 bits, length = bits[27:24], dst/src operand decode, DEF/DCL,
   COMMENT/CTAB skip). Example real output for `weapon_sheen_pass_vs30`
   (mov/mad/dp3/rsq/dp4, `dcl v0`…`dcl o8`) in §6. SM1.x needs its own
   arity rules (no length field, coissue) — scoped, not yet implemented.
3. **DX9 asm → GLSL** — to be (re)implemented as original code in
   `shaderapivulkan`, using WineD3D's Vulkan backend as the primary mapping
   reference (pure C, closest to raw Vulkan usage) and DXVK secondarily,
   per PROMPT §2.1. In-tree `togl/linuxwin/dx9asmtogl2.cpp` is a further
   cross-check oracle for asm→GLSL idioms. Nothing vendored or linked.
4. **GLSL → SPIR-V** — `glslangValidator` as an ahead-of-time build tool
   (present: 15.2.0). This is standard shader compilation, not a runtime
   translation layer.
5. **Validation** — `spirv-val` on every module before use (present: 2025.1);
   keep DXBC/HLSL/token sources next to outputs in build logs.
6. Runtime uses precompiled SPIR-V only. No ToGL/DXVK/WineD3D code or
   libraries in the shipped path.

## 5. End-to-end proof (reproducible)

Smallest real SM3 combo (`depthwrite_ps30.vcs` → LZMA → `ps_3_0`):

```
ps_3_0
def c0, 1, 0, 0, 0
mov oC0, c0.xyyx
```

Hand GLSL (scratch copy under `__tmp/shaders/depthwrite.frag`, not committed):

```glsl
#version 450
layout(location = 0) out vec4 outColor;
void main() {
    vec4 c0 = vec4(1.0, 0.0, 0.0, 0.0);
    outColor = c0.xyyx;
}
```

```
$ glslangValidator -V depthwrite.frag -o depthwrite.spv   # exit 0
$ spirv-val depthwrite.spv                                # exit 0, silent
```

Tokens→asm proven on real content (§6 example), asm→GLSL by direct
mapping, GLSL→SPIR-V→val by tool. The remaining work is the systematic
translator (§4.3), not feasibility.

## 6. Reference disassembly (real content, `weapon_sheen_pass_vs30`)

```
vs_3_0
dcl v0
dcl v1
...
dcl o8
mov r0.xyz, v4.xyzw
mad r0.xyz, r0.xyzw, c3.xxxx, v1.xyzw
dp3 r1.x, r0.xyzw, c58.xyzw
...
(full output: `tools/sm3_disasm.py` on the extracted `wsp30` shader payload)
```

## 7. Open items for the renderer phase

- SM1.x token walker (arity table, coissue) — needed for the ~560 oldest combos.
- Static/dynamic combo selection mirroring `CShaderManager` semantics.
- v4 `diffRefSize`/reference application if any combo proves non-self-contained
  (all sampled combos so far are complete).
- `stdshader_dx9` fallback: engine warns, continues — fine until Phase 2 needs
  the full set.
- ToGL scaffold officially **dropped** (2026-10-09 decision): its
  `shaderapidx9` was modernized to Windows-only D3D9Ex (`61bbcfbfe`) and
  restoration exceeds the scaffold's value. G7 stands on the null renderer.
