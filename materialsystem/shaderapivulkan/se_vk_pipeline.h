// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_pipeline.h -- Phase 2 native Vulkan backend, step S2.4.
//
// Hardcoded graphics pipeline + vertex buffer for one triangle. Proves the
// pipeline/shader/VBO plumbing; the real shader system (S2.6) replaces the
// hardcoded shaders with compiled .vcs output.

#ifndef SE_VK_PIPELINE_H_
#define SE_VK_PIPELINE_H_

#include <vulkan/vulkan.h>

namespace se::vulkan {

struct SeVkPipeline {
  VkShaderModule vert = VK_NULL_HANDLE;
  VkShaderModule frag = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkBuffer vertexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
  bool ready = false;
};

// Creates pipeline + uploads the triangle VBO. renderPass/extent come from
// the S2.3 swapchain. Safe to call once; records nothing by itself.
bool SeVkPipeline_Create(VkDevice device, VkPhysicalDevice physical,
                         VkRenderPass renderPass, VkExtent2D extent,
                         const uint32_t *vertSpv, size_t vertWords,
                         const uint32_t *fragSpv, size_t fragWords,
                         VkDescriptorSetLayout setLayout, SeVkPipeline *pipe);

// Records clear + textured triangle draw into an already-begun command
// buffer. set is the texture descriptor set for set 0.
void SeVkPipeline_RecordDraw(SeVkPipeline *pipe, VkCommandBuffer cmd,
                             VkDescriptorSet set);

// Releases everything. Safe on partial state.
void SeVkPipeline_Destroy(VkDevice device, SeVkPipeline *pipe);

}  // namespace se::vulkan

#endif  // !SE_VK_PIPELINE_H_
