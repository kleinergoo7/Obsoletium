Prompt: Native 64-bit Linux Build of Counter-Strike: Source with a Native Vulkan Renderer

Role
You are acting as a senior systems/graphics engineer working on the Source Engine. You have deep experience with C++ toolchains on Linux, CMake/GNU Make build systems, GDB/Valgrind/ASan debugging, and low-level graphics API porting (OpenGL/D3D9 → Vulkan). You work methodically, verify every step before moving to the next, and never silently skip a failing step.

Objective
Produce a functioning, natively-compiled 64-bit Linux build of Counter-Strike: Source (CS:S), based on the Source Engine fork at:

https://github.com/Source-Authors/Obsoletium

...and then replace its renderer with a native Vulkan backend (not a translation layer).

This is a two-phase project. Do not attempt Phase 2 until Phase 1 produces a verified, runnable, 64-bit native build.

Phase 1 — Get a working native 64-bit Linux build

1.1 Source and reference material
Primary source tree: Source-Authors/Obsoletium (fork of Source Engine SDK 2013 lineage).
Use nillerusr/source-engine's Linux build documentation as a reference/inspiration only for toolchain, dependency list, and build flags: https://github.com/nillerusr/source-engine/wiki/Source-Engine-(EN)#linux
Do not assume its instructions apply verbatim to Obsoletium — Obsoletium may have diverged (different CMake/waf/makefile structure, different dependency pinning, different branch state). Diff the build systems before trusting any specific flag or path.

1.2 Environment setup (must be reproducible on a "generic" distro)
Do not hardcode assumptions for one distro (e.g. only Ubuntu). Instead:
Detect the package manager (apt/dnf/pacman/zypper) or, preferably, build inside a pinned Docker/Podman container (e.g. Debian or Ubuntu LTS base) so the environment is reproducible regardless of the host distro.
Document exact toolchain versions used: GCC/Clang version, CMake version, Python version (for build scripts), SDL2 version, and any Source-Engine-specific dependencies (e.g. libcurl, openal, fontconfig, freetype, libjpeg, libpng, zlib, libedit, X11/libSDL2-dev).
Produce a single setup-env.sh (or Dockerfile) that installs everything needed, and fails loudly (non-zero exit, clear message) if any dependency is missing, rather than continuing silently.
Verify 64-bit target explicitly: confirm the build scripts are not defaulting to -m32 / 32-bit toolchain flags anywhere (this codebase has historically been 32-bit-only; confirm every CMakeLists/Makefile target and any inline -m32/ia32 flags before compiling).

1.3 Build execution
Build in Debug configuration first, not Release. Debug symbols are mandatory for Phase 1 — do not strip binaries.
Build incrementally, one module/library at a time if the build system allows it (e.g. tier0, vstdlib, mathlib, engine, dedicated, client, server, launcher in dependency order), rather than a single monolithic make -j$(nproc) on the first attempt. This isolates which component fails first.
Log every build invocation to a timestamped file (build-YYYYMMDD-HHMM.log), capturing both stdout and stderr, so failures can be diffed between attempts.

1.4 Debugging protocol for Phase 1 (apply at every failure)
When anything fails — configure step, compile step, link step, or runtime — do not guess. Instead:

Reproduce minimally: isolate the smallest failing compilation unit or link step.
Capture full context: exact compiler command line (make VERBOSE=1 / cmake --build . -v), full error text (not truncated), compiler/linker versions (gcc -v, ld -v), and relevant CMakeCache.txt / CMakeError.log / CMakeOutput.log contents.
Check ABI/architecture mismatches first: 32-bit vs 64-bit object files (file <object>.o), mismatched -m32/-m64, missing lib64 vs lib paths, wrong pkg-config results (pkg-config --cflags --libs <lib>).
Check symbol resolution failures with nm -D, ldd, and readelf -d before assuming source code is broken — many Source Engine Linux port issues are link-order or missing-.so issues, not logic bugs.
Bisect regressions: if a step worked before and now fails after a change, use git bisect or manual diffing of the last known-good log vs the current log.
When a build succeeds but the binary crashes at runtime:
Run under gdb --args ./binary <args>, capture a full backtrace (bt full) on crash.
Run under valgrind --leak-check=full --track-origins=yes for memory corruption suspects.
Compile with -fsanitize=address,undefined in a dedicated debug variant if a crash is hard to pin down via GDB alone.
Check dmesg/journalctl for OOM-kills or segfault kernel logs if the crash looks like a silent kill.
Never mark a step "done" without a positive verification: for Phase 1 this means the compiled hl2_linux/srcds_linux (or Obsoletium's equivalent binary names) actually launches, loads cstrike/CS:S content, and reaches a menu or console prompt without a fatal error — not just "compiled without error."
After every fix, re-run the full build from clean (rm -rf build && ...) at least once before declaring Phase 1 complete, to rule out stale-cache false positives.

1.5 Optional early sanity check: ToGL
It is acceptable, for Phase 1 verification purposes only, to build and run the engine using its existing togl (D3D9-to-OpenGL) translation layer if it's already present in the Obsoletium tree. This is purely a sanity check to confirm the underlying 64-bit build, asset loading, and game logic all work correctly before any Vulkan work begins — it isolates "is the port itself broken" from "is the renderer broken."
Do not spend significant effort improving or fixing togl beyond what's needed to get a visible, working game window. It is a scaffold to de-risk Phase 1, not a deliverable, and it must not be used as the shipping renderer or as a stand-in for the native Vulkan work in Phase 2.
Once togl confirms the non-rendering parts of the port are solid, proceed to Phase 2 using the native Vulkan backend as the real target.

1.6 Phase 1 deliverables
A build script (or Dockerfile + script) that takes a clean checkout of Obsoletium + CS:S game content and produces a working 64-bit native Linux binary.
A BUILD.md documenting exact dependency versions, known issues encountered, and their fixes (this becomes the debugging knowledge base for Phase 2).
A short status report at the end of Phase 1 stating: what runs, what doesn't yet, what was skipped, and what remains architecturally 32-bit if anything (e.g. third-party .sos without 64-bit builds).

Phase 2 — Port the renderer to native Vulkan

2.1 Constraint (critical, do not violate)
The target is a native Vulkan renderer written directly against the Vulkan API, replacing the existing OpenGL/D3D9-shim (shaderapidx9) render backend.
Do NOT use DXVK-native, WineD3D, D3D9-on-Vulkan translation, ANGLE, ToGL, or any other translation/shim layer as the runtime renderer. The shipped renderer must call Vulkan directly. (ToGL's only sanctioned use is the Phase 1 sanity check in §1.5 — it must not survive into the shipped Vulkan build.)
Reference sources for the D3D9→Vulkan mapping strategy (read-only inspiration, never linked or vendored):
Primary: WineD3D's Vulkan backend (part of Wine, dlls/wined3d). Prefer this as the main reference because it is written in pure C, which maps far more directly onto Source Engine's own C/C++ style and onto raw Vulkan C API usage than DXVK's C++/Rust-adjacent object model does. Study how WineD3D translates D3D9 state objects, shader bytecode, sampler/texture state, render targets, and presentation onto VkPipeline, VkDescriptorSet, VkRenderPass/dynamic rendering, and VkSwapchainKHR.
Secondary: DXVK's source code, used only to cross-check or fill gaps where WineD3D's approach is unclear, incomplete, or where DXVK's design is simply cleaner to reason about for a specific mapping (e.g. pipeline state cache invalidation strategies, descriptor set layout batching).
In both cases, treat the project as documentation of a mapping strategy, not as a source of code to copy. Reimplement the concept in original code written directly against the Vulkan API — do not copy WineD3D or DXVK source into the native renderer, and do not link against either project's libraries at runtime.

2.2 Architecture approach
Identify the existing renderer abstraction boundary in Source Engine (materialsystem/shaderapi interfaces — IShaderAPI, IMaterialSystem, CShaderAPIDx8/Dx9, IShaderShadow, etc.).
Design a new shaderapivulkan (or similarly named) module implementing the same interface contract, so the rest of the engine (materials, studio render, vgui, etc.) does not need to change.
Build the Vulkan backend incrementally against real functionality, in roughly this order (do not skip ahead):
Instance/device/queue creation, debug/validation layers enabled by default in this phase.
Swapchain + a cleared color buffer (get something on screen).
Fixed-function-equivalent triangle rendering with a hardcoded pipeline (prove the pipeline/shader/vertex-buffer plumbing works).
Texture sampling.
Source Engine's actual shader system (VCS/HLSL-derived shaders) translated to SPIR-V (via a build-time cross-compiler such as SPIRV-Cross/glslang/DXC as a build tool, not a runtime translation layer — clarify this distinction in your work: compiling shaders ahead of time to SPIR-V is standard and fine; it is not the same as a runtime D3D9→Vulkan translation layer).
Render targets, depth/stencil, multipass effects (Source uses several render-to-texture passes — water, HDR, etc.).
World geometry, then models, then post-processing/UI (vgui) last.

2.3 Debugging protocol for Phase 2 (apply at every stage)
Always run with Vulkan validation layers enabled (VK_LAYER_KHRONOS_validation) during development. Treat any validation error or warning as a bug to fix immediately, not to suppress.
At each of the incremental steps in 2.2, stop and verify visually or via a capture tool before proceeding:
Use RenderDoc (Linux build) to capture and inspect frames at each stage — verify draw calls, bound resources, and pipeline state match expectations.
Compare against the OpenGL/D3D9 reference renderer's output when possible for the same scene (screenshot diff) to catch subtle mapping bugs (wrong texture filtering, incorrect coordinate space, wrong blend state, etc.).
Log every Vulkan object creation failure (VkResult != VK_SUCCESS) with the exact call site, parameters, and VkResult value — never ignore a non-VK_SUCCESS return.
For GPU hangs/driver crashes: capture with VK_LAYER_LUNARG_crash_diagnostic or vendor tools (RGP for AMD, Nsight for NVIDIA), and check dmesg for GPU reset messages.
For shader translation issues: validate every generated SPIR-V module with spirv-val before use, and keep the original HLSL/VCS source alongside the SPIR-V output in build logs so mismatches can be traced back to the source shader.
Maintain a feature parity checklist (e.g. "world rendering ✅ / dynamic lighting ⚠ partial / water shader ❌ / HDR ❌ / particles ❌") and update it after every milestone rather than declaring broad completion prematurely.
Performance regressions matter too: once a feature renders correctly, profile with perf and Vulkan GPU timestamps before assuming the port is "done" for that feature — silent 10x slowdowns from redundant pipeline barriers or descriptor rebinding are a common porting bug.

2.4 Phase 2 deliverables
A shaderapivulkan module reaching, at minimum, a playable frame rate on a real CS:S map with world geometry, models, basic lighting, and UI rendering correctly under validation layers with zero errors.
A written mapping document: for each D3D9 concept ported, what Vulkan construct replaced it and any semantic differences that needed compensation (e.g. clip-space Y-flip, depth range 0–1 vs -1–1, descriptor set layout choices).
An updated feature parity checklist showing current state and remaining gaps.

General working rules for both phases
Never claim something "works" without a reproducible verification step (build succeeds AND runs AND does the expected thing).
Prefer small, verifiable increments over large speculative changes.
When stuck after reasonable debugging effort, report the exact error, what was tried, and what you suspect — do not silently work around a root cause you don't understand.
Keep all build/debug logs so any regression can be diffed against a prior known-good state.
