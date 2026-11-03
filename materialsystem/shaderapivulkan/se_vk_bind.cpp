// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_bind.cpp -- Phase 2 native Vulkan backend, step S2.8-bind.
//
// Original code written directly against the Vulkan C API.

#include "se_vk_bind.h"

#include <cstring>
#include <cstdio>

#include "tier0/dbg.h"

#include "se_vk_init.h"
#include "se_vk_matrix.h"
#include "se_vk_pipeline.h"
#include "se_vk_texcache.h"

namespace se::vulkan {
namespace {

uint32_t FindHostVisible(VkPhysicalDevice physical, uint32_t typeBits) {
  VkPhysicalDeviceMemoryProperties props{};
  vkGetPhysicalDeviceMemoryProperties(physical, &props);
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) != 0u &&
        (props.memoryTypes[i].propertyFlags &
         (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      return i;
    }
  }
  return UINT32_MAX;
}

bool CreateBuffer(SeVkBind *b, VkDeviceSize size, VkBufferUsageFlags usage,
                  VkBuffer *buf, VkDeviceMemory *mem, const char *name) {
  VkBufferCreateInfo bci{};
  bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bci.size = size;
  bci.usage = usage;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  char param[64];
  snprintf(param, sizeof(param), "%s size=%llu", name,
           static_cast<unsigned long long>(size));
  VkResult result = vkCreateBuffer(b->device, &bci, nullptr, buf);
  if (result != VK_SUCCESS) {
    VkFail("vkCreateBuffer", param, result);
    return false;
  }
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(b->device, *buf, &req);
  uint32_t memType = FindHostVisible(b->physical, req.memoryTypeBits);
  if (memType == UINT32_MAX) {
    Warning("[vulkan] bind: no host-visible memory for %s\n", name);
    return false;
  }
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = memType;
  result = vkAllocateMemory(b->device, &mai, nullptr, mem);
  if (result != VK_SUCCESS) {
    VkFail("vkAllocateMemory", param, result);
    return false;
  }
  result = vkBindBufferMemory(b->device, *buf, *mem, 0);
  if (result != VK_SUCCESS) {
    VkFail("vkBindBufferMemory", param, result);
    return false;
  }
  return true;
}

void UploadBuffer(SeVkBind *b, VkDeviceMemory mem, const void *data,
                  size_t size, const char *name) {
  void *mapped = nullptr;
  VkResult result = vkMapMemory(b->device, mem, 0, size, 0, &mapped);
  if (result != VK_SUCCESS) {
    VkFail("vkMapMemory", name, result);
    return;
  }
  memcpy(mapped, data, size);
  vkUnmapMemory(b->device, mem);
}

VkPipeline MakePipeline(SeVkBind *b, VkRenderPass renderPass,
                        VkExtent2D extent, SeVkShader *vs, SeVkShader *ps,
                        bool blendEnable) {
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs->module;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = ps->module;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(SeVkBindVertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attrs[7]{};
  const struct {
    uint32_t loc;
    uint32_t off;
  } kAttrs[7] = {
      {20, offsetof(SeVkBindVertex, pos)}, {21, offsetof(SeVkBindVertex, col)},
      {0, offsetof(SeVkBindVertex, uv0)},  {1, offsetof(SeVkBindVertex, uv1)},
      {2, offsetof(SeVkBindVertex, uv2)},  {3, offsetof(SeVkBindVertex, uv3)},
      {4, offsetof(SeVkBindVertex, nrm)},
  };
  // Declare only what the vertex shader reads: a declared-but-unused
  // attribute is a Vulkan validation warning on every pipeline creation.
  uint32_t attrCount = 0;
  for (int i = 0; i < 7; ++i) {
    if (vs->inputMask != 0 && !(vs->inputMask & (1u << kAttrs[i].loc))) {
      continue;
    }
    VkVertexInputAttributeDescription &a = attrs[attrCount++];
    a.location = kAttrs[i].loc;
    a.binding = 0;
    a.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    a.offset = kAttrs[i].off;
  }
  VkPipelineVertexInputStateCreateInfo vii{};
  vii.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vii.vertexBindingDescriptionCount = 1;
  vii.pVertexBindingDescriptions = &binding;
  vii.vertexAttributeDescriptionCount = attrCount;
  vii.pVertexAttributeDescriptions = attrs;

  VkPipelineInputAssemblyStateCreateInfo iai{};
  iai.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  iai.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkViewport viewport{};
  viewport.width = static_cast<float>(extent.width);
  viewport.height = static_cast<float>(extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  VkRect2D scissor{};
  scissor.extent = extent;
  VkPipelineViewportStateCreateInfo vpi{};
  vpi.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vpi.viewportCount = 1;
  vpi.pViewports = &viewport;
  vpi.scissorCount = 1;
  vpi.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rsi{};
  rsi.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rsi.polygonMode = VK_POLYGON_MODE_FILL;
  rsi.cullMode = VK_CULL_MODE_NONE;
  rsi.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rsi.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo msi{};
  msi.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  msi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blend{};
  // From the material's shadow state (snapshot): opaque PS alpha is
  // meaningless (often 0), so opaque passes must not blend.
  // shortcut: blend func/op stay (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)/ADD.
  // Upgrade with EnableBlendingSeparateAlpha/BlendFunc/BlendOp shadow
  // calls when a translucent material needs more.
  blend.blendEnable = blendEnable ? VK_TRUE : VK_FALSE;
  blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.colorBlendOp = VK_BLEND_OP_ADD;
  blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.alphaBlendOp = VK_BLEND_OP_ADD;
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo bsi{};
  bsi.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  bsi.attachmentCount = 1;
  bsi.pAttachments = &blend;

  VkGraphicsPipelineCreateInfo gpi{};
  gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gpi.stageCount = 2;
  gpi.pStages = stages;
  gpi.pVertexInputState = &vii;
  gpi.pInputAssemblyState = &iai;
  gpi.pViewportState = &vpi;
  gpi.pRasterizationState = &rsi;
  gpi.pMultisampleState = &msi;
  gpi.pColorBlendState = &bsi;
  gpi.layout = b->pipeLayout;
  gpi.renderPass = renderPass;
  gpi.subpass = 0;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkResult result = vkCreateGraphicsPipelines(b->device, VK_NULL_HANDLE, 1,
                                              &gpi, nullptr, &pipeline);
  if (result != VK_SUCCESS) {
    VkFail("vkCreateGraphicsPipelines", "bind vs+x ps", result);
    return VK_NULL_HANDLE;
  }
  return pipeline;
}

}  // namespace

SeVkBind &SeVkBind_Global() {
  static SeVkBind b;
  return b;
}

bool SeVkBind_Ensure(SeVkBind *b, VkDevice device, VkPhysicalDevice physical,
                     VkImageView view, VkSampler sampler) {
  if (b->ready) {
    return true;
  }
  b->device = device;
  b->physical = physical;

  VkDescriptorSetLayoutBinding bindings[18]{};
  for (int i = 0; i < 16; ++i) {
    bindings[i].binding = i;
    bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  bindings[16].binding = 16;
  bindings[16].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  bindings[16].descriptorCount = 1;
  bindings[16].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  bindings[17].binding = 17;
  bindings[17].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  bindings[17].descriptorCount = 1;
  bindings[17].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo lci{};
  lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  lci.bindingCount = 18;
  lci.pBindings = bindings;
  VkResult result =
      vkCreateDescriptorSetLayout(device, &lci, nullptr, &b->setLayout);
  if (result != VK_SUCCESS) {
    return VkFail("vkCreateDescriptorSetLayout", "bind 16+2", result);
  }
  VkDescriptorPoolSize sizes[2]{};
  sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  sizes[0].descriptorCount = 16;
  sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  sizes[1].descriptorCount = 2;
  VkDescriptorPoolCreateInfo pci{};
  pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pci.maxSets = 1;
  pci.poolSizeCount = 2;
  pci.pPoolSizes = sizes;
  result = vkCreateDescriptorPool(device, &pci, nullptr, &b->pool);
  if (result != VK_SUCCESS) {
    return VkFail("vkCreateDescriptorPool", "bind", result);
  }
  VkDescriptorSetAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  ai.descriptorPool = b->pool;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &b->setLayout;
  result = vkAllocateDescriptorSets(device, &ai, &b->set);
  if (result != VK_SUCCESS) {
    return VkFail("vkAllocateDescriptorSets", "bind", result);
  }

  if (!CreateBuffer(b, sizeof(b->vsConsts), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    &b->vsUbo, &b->vsMem, "vsConsts") ||
      !CreateBuffer(b, sizeof(b->psConsts), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    &b->psUbo, &b->psMem, "psConsts") ||
      !CreateBuffer(b, SeVkBind::kMaxVerts * sizeof(SeVkBindVertex),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &b->vbo, &b->vboMem,
                    "bindVBO") ||
      !CreateBuffer(b, SeVkBind::kMaxIndices * sizeof(uint32_t),
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &b->ibo, &b->iboMem,
                    "bindIBO")) {
    return false;
  }

  VkPipelineLayoutCreateInfo pli{};
  pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pli.setLayoutCount = 1;
  pli.pSetLayouts = &b->setLayout;
  result = vkCreatePipelineLayout(device, &pli, nullptr, &b->pipeLayout);
  if (result != VK_SUCCESS) {
    return VkFail("vkCreatePipelineLayout", "bind", result);
  }

  VkDescriptorImageInfo imageInfo{};
  imageInfo.sampler = sampler;
  imageInfo.imageView = view;
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet writes[18]{};
  VkDescriptorBufferInfo bufInfos[2]{};
  for (int i = 0; i < 16; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = b->set;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[i].pImageInfo = &imageInfo;
  }
  bufInfos[0].buffer = b->vsUbo;
  bufInfos[0].range = sizeof(b->vsConsts);
  bufInfos[1].buffer = b->psUbo;
  bufInfos[1].range = sizeof(b->psConsts);
  writes[16].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[16].dstSet = b->set;
  writes[16].dstBinding = 16;
  writes[16].descriptorCount = 1;
  writes[16].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  writes[16].pBufferInfo = &bufInfos[0];
  writes[17].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[17].dstSet = b->set;
  writes[17].dstBinding = 17;
  writes[17].descriptorCount = 1;
  writes[17].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  writes[17].pBufferInfo = &bufInfos[1];
  vkUpdateDescriptorSets(device, 18, writes, 0, nullptr);

  b->verts.reserve(SeVkBind::kMaxVerts);
  b->indices.reserve(SeVkBind::kMaxIndices);
  b->ranges.reserve(64);
  // Modulation default (white); the commit never touches c47 so stdshader
  // uploads win when present.
  b->vsConsts[47][0] = b->vsConsts[47][1] = b->vsConsts[47][2] =
      b->vsConsts[47][3] = 1.0f;
  // Math-constant defaults (DX9 SetStandardVertexShaderConstants values
  // with overbright 1): c0.y in particular feeds gl_Position.w in every
  // VS. Real values overwrite when the engine calls in.
  b->vsConsts[0][0] = 0.0f;
  b->vsConsts[0][1] = 1.0f;
  b->vsConsts[0][2] = 2.0f;
  b->vsConsts[0][3] = 0.5f;
  b->vsConsts[1][0] = 1.0f / 2.2f;
  b->vsConsts[1][1] = 1.0f;
  b->vsConsts[1][2] = 1.0f / 3.0f;
  b->vsConsts[1][3] = 1.0f;
  // PS system-constant defaults (common_ps_fxc.h register map). The DX9
  // backend serves these per-frame from dynamic state (fog off, light
  // scales unity); until the state setters + opcode handlers land, the
  // defaults below are correct for all current (unfogged, unlit) content:
  // c29 linear fog color (black, OO_DESTALPHA_DEPTH_RANGE 1),
  // c30 light scales (linear/lightmap/envmap/gamma all unity),
  // c21 fog params (fog-none: CalcRangeFog returns 0).
  b->psConsts[29][0] = b->psConsts[29][1] = b->psConsts[29][2] = 0.0f;
  b->psConsts[29][3] = 1.0f;
  b->psConsts[30][0] = b->psConsts[30][1] = b->psConsts[30][2] =
      b->psConsts[30][3] = 1.0f;
  b->psConsts[21][0] = 0.0f;
  b->psConsts[21][1] = 0.0f;
  b->psConsts[21][2] = 1.0f;
  b->psConsts[21][3] = 0.0f;
  b->psDirty = true;
  // Sampler bindings start unbound (-1 = dummy); BindTexture resolves.
  // (TexCache_Init runs at SetMode, so the dummy exists.)
  for (int i = 0; i < 16; ++i) {
    b->boundTexHandle[i] = -1;
    b->boundTexStd[i] = -1;
    b->lastTex[i] = VK_NULL_HANDLE;
  }
  b->texDirty = true;
  b->vsDirty = true;
  b->ready = true;
  Msg("[vulkan] S2.8 bind path ready (shared set layout 16+2)\n");
  return true;
}

void SeVkBind_Shutdown(SeVkBind *b) {
  if (b->device == VK_NULL_HANDLE) {
    return;
  }
  SeVkBind_InvalidatePipelines(b);
  if (b->pipeLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(b->device, b->pipeLayout, nullptr);
    b->pipeLayout = VK_NULL_HANDLE;
  }
  VkBuffer *bufs[4] = {&b->vsUbo, &b->psUbo, &b->vbo, &b->ibo};
  VkDeviceMemory *mems[4] = {&b->vsMem, &b->psMem, &b->vboMem, &b->iboMem};
  for (int i = 0; i < 4; ++i) {
    if (*bufs[i] != VK_NULL_HANDLE) {
      vkDestroyBuffer(b->device, *bufs[i], nullptr);
      *bufs[i] = VK_NULL_HANDLE;
    }
    if (*mems[i] != VK_NULL_HANDLE) {
      vkFreeMemory(b->device, *mems[i], nullptr);
      *mems[i] = VK_NULL_HANDLE;
    }
  }
  if (b->pool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(b->device, b->pool, nullptr);
    b->pool = VK_NULL_HANDLE;
  }
  b->set = VK_NULL_HANDLE;
  if (b->setLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(b->device, b->setLayout, nullptr);
    b->setLayout = VK_NULL_HANDLE;
  }
  b->ready = false;
}

void SeVkBind_InvalidatePipelines(SeVkBind *b) {
  if (b->device == VK_NULL_HANDLE) {
    b->nPipes = 0;
    return;
  }
  for (int i = 0; i < b->nPipes; ++i) {
    if (b->pipes[i].pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(b->device, b->pipes[i].pipeline, nullptr);
      b->pipes[i].pipeline = VK_NULL_HANDLE;
    }
    b->pipes[i].vs = nullptr;
    b->pipes[i].ps = nullptr;
  }
  b->nPipes = 0;
  b->pipePass = VK_NULL_HANDLE;
}

void SeVkBind_OnShaderDestroyed(SeVkBind *b, const SeVkShader *shader) {
  if (b->device == VK_NULL_HANDLE || !shader) {
    return;
  }
  for (int i = 0; i < b->nPipes;) {
    if (b->pipes[i].vs == shader || b->pipes[i].ps == shader) {
      vkDestroyPipeline(b->device, b->pipes[i].pipeline, nullptr);
      b->pipes[i] = b->pipes[--b->nPipes];
    } else {
      ++i;
    }
  }
  if (b->boundVS == shader) {
    b->boundVS = nullptr;
  }
  if (b->boundPS == shader) {
    b->boundPS = nullptr;
  }
}

void SeVkBind_PushMesh(SeVkBind *b, const SeVkBindVertex *verts, size_t nVerts,
                       const uint32_t *idx, size_t nIdx, SeVkShader *vs,
                       SeVkShader *ps) {
  if (!vs || !ps || nVerts == 0 || nIdx == 0) {
    return;
  }
  if (b->verts.size() + nVerts > SeVkBind::kMaxVerts ||
      b->indices.size() + nIdx > SeVkBind::kMaxIndices ||
      b->ranges.size() >= 256) {
    Warning("[vulkan] bind frame overflow (%zu verts %zu idx); dropping mesh\n",
            b->verts.size(), b->indices.size());
    return;
  }
  uint32_t base = static_cast<uint32_t>(b->verts.size());
  b->verts.insert(b->verts.end(), verts, verts + nVerts);
  uint32_t first = static_cast<uint32_t>(b->indices.size());
  for (size_t i = 0; i < nIdx; ++i) {
    b->indices.push_back(base + idx[i]);
  }
  SeVkBindRange r;
  r.firstIndex = first;
  r.indexCount = static_cast<uint32_t>(nIdx);
  r.vs = vs;
  r.ps = ps;
  r.blend = b->curBlend;
  // Commit here, not at Present: the engine pops its matrix stacks in
  // FinishDrawing, so by Present only restored (identity) state remains.
  // Last-draw-wins for multi-matrix frames (same frame-global limitation
  // as the other constants; per-range snapshots are follow-up work).
  // shortcut: multi-model frames (world + viewmodel) share the last
  // draw's matrices. Upgrade: stash matrix slots per range like texHandle.
  SeVkMatrix_Commit(b->vsConsts, b->vsEngine, b->vsDirty);
  for (int i = 0; i < 16; ++i) {
    r.texHandle[i] = b->boundTexHandle[i];
    r.texStd[i] = b->boundTexStd[i];
  }
  b->ranges.push_back(r);
}

void SeVkBind_RecordDraws(SeVkBind *b, VkCommandBuffer cmd,
                          VkRenderPass renderPass, VkExtent2D extent) {
  if (!b->ready || b->ranges.empty()) {
    return;
  }
  if (renderPass != b->pipePass || extent.width != b->pipeExtent.width ||
      extent.height != b->pipeExtent.height) {
    SeVkBind_InvalidatePipelines(b);
    b->pipePass = renderPass;
    b->pipeExtent = extent;
  }
  // Per-draw matrix commit lives in PushMesh (draw time), when the
  // engine's push/pop-scoped matrices are live. Committing here at
  // Present would read post-FinishDrawing restored (identity) state.
  // Stdshader and command-buffer uploads for other slots are preserved.
  b->setBoundThisSubmit = false;
  if (b->vsDirty) {
    UploadBuffer(b, b->vsMem, b->vsConsts, sizeof(b->vsConsts), "vsConsts");
    b->vsDirty = false;
  }
  if (b->psDirty) {
    UploadBuffer(b, b->psMem, b->psConsts, sizeof(b->psConsts), "psConsts");
    b->psDirty = false;
  }
  UploadBuffer(b, b->vboMem, b->verts.data(),
               b->verts.size() * sizeof(SeVkBindVertex), "bindVBO");
  UploadBuffer(b, b->iboMem, b->indices.data(),
               b->indices.size() * sizeof(uint32_t), "bindIBO");

  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &b->vbo, &offset);
  vkCmdBindIndexBuffer(cmd, b->ibo, 0, VK_INDEX_TYPE_UINT32);
  for (const auto &r : b->ranges) {
    VkPipeline pipeline = VK_NULL_HANDLE;
    for (int i = 0; i < b->nPipes; ++i) {
      if (b->pipes[i].vs == r.vs && b->pipes[i].ps == r.ps &&
          b->pipes[i].blend == r.blend) {
        pipeline = b->pipes[i].pipeline;
        break;
      }
    }
    if (pipeline == VK_NULL_HANDLE) {
      if (b->nPipes >= SeVkBind::kMaxPipes) {
        Warning("[vulkan] bind pipeline cache full; skipping range\n");
        continue;
      }
      pipeline = MakePipeline(b, renderPass, extent, r.vs, r.ps, r.blend);
      if (pipeline == VK_NULL_HANDLE) {
        continue;
      }
      b->pipes[b->nPipes].vs = r.vs;
      b->pipes[b->nPipes].ps = r.ps;
      b->pipes[b->nPipes].blend = r.blend;
      b->pipes[b->nPipes].pipeline = pipeline;
      ++b->nPipes;
      Msg("[vulkan] bind pipeline %d: vs=%s ps=%s blend=%d\n", b->nPipes,
          r.vs->name, r.ps->name, (int)r.blend);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    // Sampler bindings snapshot per range as handles, resolved to views
    // here at draw time so uploads landing between bind and draw are
    // picked up. UBO bindings (16/17) point at stable buffers and were
    // written once at Ensure.
    bool texChanged = b->texDirty;
    VkImageView resolved[16] = {};
    if (texChanged) {
      for (int i = 0; i < 16; ++i) {
        if (r.texStd[i] >= 0) {
          resolved[i] = SeVkTexCache_Standard(r.texStd[i]);
        } else if (r.texHandle[i] >= 0) {
          resolved[i] =
              SeVkTexCache_View((ShaderAPITextureHandle_t)r.texHandle[i]);
        } else {
          resolved[i] = SeVkTexCache_Dummy();
        }
      }
      texChanged = b->texDirty;
      if (!texChanged) {
        for (int i = 0; i < 16; ++i) {
          if (b->lastTex[i] != resolved[i]) {
            texChanged = true;
            break;
          }
        }
      }
      if (texChanged && b->setBoundThisSubmit) {
        // Updating now would invalidate the already-bound set in this
        // submit; defer to the next one (see setBoundThisSubmit).
        b->texDirty = true;
        texChanged = false;
      }
    }
    if (texChanged) {
      VkWriteDescriptorSet writes[16]{};
      VkDescriptorImageInfo imageInfos[16]{};
      for (int i = 0; i < 16; ++i) {
        VkImageView view = resolved[i] != VK_NULL_HANDLE
                               ? resolved[i]
                               : SeVkTexCache_Dummy();
        imageInfos[i].sampler = SeVkTexCache_Sampler();
        imageInfos[i].imageView = view;
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = b->set;
        writes[i].dstBinding = (uint32_t)i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &imageInfos[i];
        b->lastTex[i] = view;
      }
      vkUpdateDescriptorSets(b->device, 16, writes, 0, nullptr);
      b->texDirty = false;
    }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            b->pipeLayout, 0, 1, &b->set, 0, nullptr);
    b->setBoundThisSubmit = true;
    vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);
  }
  b->verts.clear();
  b->indices.clear();
  b->ranges.clear();
  b->hasPending = false;
  memset(b->vsEngine, 0, sizeof(b->vsEngine));
}

}  // namespace se::vulkan
