// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_shaderpack.h -- Phase 2 native Vulkan backend, step S2.6.
//
// Runtime side of the AOT shader pipeline (doc/SHADER-PIPELINE.md): loads
// the manifest + validated SPIR-V packs built by tools/vcs_pack.py, and
// serves VkShaderModules keyed by sha1 of the incoming token bytecode.
// No JIT translation happens in the module.

#ifndef SE_VK_SHADERPACK_H_
#define SE_VK_SHADERPACK_H_

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

namespace se::vulkan {

// Opaque shader record; the engine-visible handle is a pointer to one.
struct SeVkShader {
  VkShaderModule module = VK_NULL_HANDLE;
  bool isVertex = false;
  char name[64];
  char model[16];
  // Content hash (sha1 hex of token bytecode); used to dedupe modules so
  // per-frame snapshot resolution never creates duplicates.
  char hash[41] = {};
  // Vertex inputs the SPIR-V actually reads, one bit per location (0 =
  // unknown, keep the full vertex layout). Declaring attributes a shader never
  // reads is a Vulkan validation warning, so the pipeline is built from this.
  uint32_t inputMask = 0;
  // Snapshot sharing: modules are refcounted, destroyed at zero.
  int refs = 1;
};

struct SeVkShaderPack {
  // Full-content pack holds ~42k unique programs; size for headroom.
  // (The table is a global, so this is ~11 MB of BSS.)
  static constexpr int kMaxShaders = 65536;
  // hash (sha1 hex, 40 chars) -> index, linear scan is fine at load.
  struct Entry {
    char hash[41];
    char file[128];
  };
  Entry entries[kMaxShaders];
  int count = 0;
  char dir[512];
  SeVkShader *shaders[kMaxShaders];
  int nShaders = 0;
  // Static index: shader key ("shaders/vsh/unlitgeneric_vs20") -> the
  // combos of that shader, so SetVertexShader(file, staticIdx) resolves
  // without parsing .vcs. Loaded from static_index.json; missing file =
  // disabled.
  // Heap-allocated (largest file holds ~8k combos; total ~27k hashes).
  static constexpr int kMaxStatics = 1024;
  // One combo: the flat index the engine addresses it by and the hash of
  // its program. The flat index is staticComboID * nDynamicCombos +
  // dynamicComboID - the same arithmetic the DX9 device uses (it computes
  // staticComboID = staticIndex / nDynamicCombos and adds the dynamic
  // combo the command buffer set). Sorted by flat index; the static combo
  // ids are sparse, so this is a list, not an array.
  struct StaticCombo {
    int flat = 0;
    char hash[41] = {};
  };
  struct StaticEntry {
    char name[128];
    StaticCombo *combos = nullptr;
    int nCombos = 0;
  };
  StaticEntry statics[kMaxStatics];
  int nStatics = 0;
};

// Loads manifest.json from dir (e.g. game vulkan/ dir). Missing manifest =
// pack disabled (returns true, count 0); engine falls back to stubs.
bool SeVkShaderPack_Load(VkDevice device, const char *dir, SeVkShaderPack *pack);

// Finds SPIR-V for token bytecode (size bytes), creates the module, records
// it. Returns null when absent (caller keeps neutral behavior).
SeVkShader *SeVkShaderPack_Create(VkDevice device, SeVkShaderPack *pack,
                                  const void *tokens, size_t size,
                                  bool isVertex, const char *debugName);

// Loads a validated .spv by pack-relative path + hash (for logging).
// Shared by the token-hash and static-index creation paths.
SeVkShader *SeVkShaderPack_CreateFromFile(VkDevice device,
                                           SeVkShaderPack *pack,
                                           const char *file, const char *hex,
                                           bool isVertex,
                                           const char *debugName);

// Resolves a static combo by shader name + static index (mirrors the DX9
// vsh/psh-then-fxc search order) and creates the module. name may carry a
// directory or .vcs extension; both are stripped before lookup. Returns
// null when absent.
SeVkShader *SeVkShaderPack_CreateStatic(VkDevice device, SeVkShaderPack *pack,
                                        const char *name, int staticIndex,
                                        bool isVertex);

// Releases one shader record + module.
void SeVkShaderPack_Destroy(VkDevice device, SeVkShaderPack *pack,
                            SeVkShader *shader);

// Releases everything. Safe on partial state.
void SeVkShaderPack_Unload(VkDevice device, SeVkShaderPack *pack);

}  // namespace se::vulkan

#endif  // !SE_VK_SHADERPACK_H_
