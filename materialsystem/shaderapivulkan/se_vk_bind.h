// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_bind.h -- Phase 2 native Vulkan backend, step S2.8-bind.
//
// Engine-driven draw path: descriptor layout shared by every pack shader
// (set 0: bindings 0-15 samplers, 16 VS DynConsts UBO, 17 PS DynConsts UBO),
// per-stage constant storage fed by SetVertex/PixelShaderConstant, a
// pipeline cache keyed by bound VSxPS pair, and a per-frame vertex/index
// accumulation drained by Present.
//
// Fixed vertex format (S2.8 scope): pos -> location 20 (VS v0 slot),
// color -> location 21 (VS v1 slot), texcoord sets 0-3 -> locations 0-3.
// Shaders reading other locations get undefined values for those inputs;
// pipeline creation still succeeds.
//
// Limitation: constants are frame-global (one upload before all draws).
// Single-material frames (menu background) are exact; multi-material
// frames share the latest constants until per-range snapshots land.

#ifndef SE_VK_BIND_H_
#define SE_VK_BIND_H_

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "tier0/platform.h"  // intp (sampler binding handles)

#include "se_vk_shaderpack.h"

namespace se::vulkan {

// One vertex: 7 x vec4 = 112 bytes.
struct SeVkBindVertex {
  float pos[4];  // location 20
  float col[4];  // location 21
  float uv0[4];  // location 0
  float uv1[4];  // location 1
  float uv2[4];  // location 2
  float uv3[4];  // location 3
  float nrm[4];  // location 4
};

struct SeVkBindRange {
  uint32_t firstIndex = 0;
  uint32_t indexCount = 0;
  SeVkShader *vs = nullptr;
  SeVkShader *ps = nullptr;
  // Sampler bindings snapshot at commit time (DX9 texture binds are
  // immediate device state; each material rebinds before its draw).
  // Handles resolve to views at draw-record time so uploads landing
  // between bind and draw are picked up. -1 = dummy.
  intp texHandle[16];
  int texStd[16];
  // Shadow blend switch snapshotted with the range (pipelines are keyed
  // per blend state: opaque PS alpha is meaningless, often 0).
  bool blend = false;
  SeVkBindRange() {
    for (int i = 0; i < 16; ++i) {
      texHandle[i] = -1;
      texStd[i] = -1;
    }
  }
};

struct SeVkBindPipe {
  SeVkShader *vs = nullptr;
  SeVkShader *ps = nullptr;
  bool blend = false;
  VkPipeline pipeline = VK_NULL_HANDLE;
};

struct SeVkBind {
  static constexpr int kMaxVerts = 16384;
  static constexpr int kMaxIndices = 32768;
  static constexpr int kMaxPipes = 64;

  VkDevice device = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;

  VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
  VkDescriptorPool pool = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
  VkPipelineLayout pipeLayout = VK_NULL_HANDLE;

  VkBuffer vsUbo = VK_NULL_HANDLE;
  VkDeviceMemory vsMem = VK_NULL_HANDLE;
  VkBuffer psUbo = VK_NULL_HANDLE;
  VkDeviceMemory psMem = VK_NULL_HANDLE;

  VkBuffer vbo = VK_NULL_HANDLE;
  VkDeviceMemory vboMem = VK_NULL_HANDLE;
  VkBuffer ibo = VK_NULL_HANDLE;
  VkDeviceMemory iboMem = VK_NULL_HANDLE;

  float vsConsts[256][4] = {};
  float psConsts[256][4] = {};
  bool vsDirty = true;
  bool psDirty = true;
  // VS slots the engine wrote this frame (257-uploads, direct constants).
  // Commit skips them: engine values win over our matrix-stack fallback.
  bool vsEngine[256] = {};

  SeVkShader *boundVS = nullptr;
  SeVkShader *boundPS = nullptr;
  // Shadow blend switch for the current pass (set by BeginPass).
  bool curBlend = false;

  // Frame-current sampler bindings. Handles (-1 = dummy) snapshot per
  // range and resolve to views at draw time; standard ids ride alongside.
  intp boundTexHandle[16] = {};
  int boundTexStd[16] = {};
  // Last views written into the descriptor set (per-frame cache).
  VkImageView lastTex[16] = {};
  bool texDirty = true;
  // Set after the first vkCmdBindDescriptorSets in a submit. A descriptor
  // set updated after being bound in the same command buffer is a
  // validation error; when views change mid-submit we keep the old set
  // for this frame (one frame of stale texels) and retry next submit.
  // shortcut: converges only because uploads settle; a constantly
  // churning texture would alternate stale frames. Upgrade: per-range
  // descriptor sets if that ever matters.
  bool setBoundThisSubmit = false;

  SeVkBindPipe pipes[kMaxPipes] = {};
  int nPipes = 0;
  VkRenderPass pipePass = VK_NULL_HANDLE;
  VkExtent2D pipeExtent{0, 0};

  std::vector<SeVkBindVertex> verts;
  std::vector<uint32_t> indices;
  std::vector<SeVkBindRange> ranges;
  // One uncommitted mesh: UnlockMesh appends, Draw commits.
  uint32_t pendingFirst = 0;
  uint32_t pendingCount = 0;
  bool hasPending = false;

  bool ready = false;
};

// Process-global bind state (the engine's IShaderAPI is itself a singleton).
SeVkBind &SeVkBind_Global();

// Creates Vulkan objects on first use (needs a sampled image for the 16
// sampler bindings; callers pass the S2.5 checkerboard view+sampler until
// real textures land). Safe to call every Present; no-op when ready.
bool SeVkBind_Ensure(SeVkBind *b, VkDevice device, VkPhysicalDevice physical,
                     VkImageView view, VkSampler sampler);

// Releases everything. Safe on partial/empty state.
void SeVkBind_Shutdown(SeVkBind *b);

// Drops cached pipelines (call when the render pass dies, e.g. swapchain
// recreate, or when a shader module is destroyed).
void SeVkBind_InvalidatePipelines(SeVkBind *b);

// Drops cached pipelines that reference a destroyed shader module.
void SeVkBind_OnShaderDestroyed(SeVkBind *b, const SeVkShader *shader);

// Records one committed mesh into the frame. verts/nVerts use SeVkBindVertex;
// idx are unoffset (rebased by baseVertex here).
void SeVkBind_PushMesh(SeVkBind *b, const SeVkBindVertex *verts, size_t nVerts,
                       const uint32_t *idx, size_t nIdx, SeVkShader *vs,
                       SeVkShader *ps);

// Uploads dirty UBOs + frame VBO/IBO and records all ranges into cmd.
// renderPass/extent select (or rebuild) cached pipelines. Clears the frame
// afterwards so the next Present starts empty.
void SeVkBind_RecordDraws(SeVkBind *b, VkCommandBuffer cmd,
                          VkRenderPass renderPass, VkExtent2D extent);

}  // namespace se::vulkan

#endif  // !SE_VK_BIND_H_
