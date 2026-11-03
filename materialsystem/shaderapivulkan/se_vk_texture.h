// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_texture.h -- Phase 2 native Vulkan backend, step S2.5.
//
// Procedural checkerboard texture (no content dependency yet): staging
// upload, sampler, and descriptor set. S2.6 wires real .vcs texels through
// the same upload path.

#ifndef SE_VK_TEXTURE_H_
#define SE_VK_TEXTURE_H_

#include <vulkan/vulkan.h>

namespace se::vulkan {

struct SeVkTexture {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
  VkDescriptorPool pool = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
  bool ready = false;
};

// Builds the checkerboard, uploads it, and creates sampler + descriptor
// set. queue must accept graphics/transfer work.
bool SeVkTexture_Create(VkPhysicalDevice physical, VkDevice device,
                        uint32_t graphicsFamily, VkQueue queue,
                        SeVkTexture *tex);

// Frees everything. Safe on partial state.
void SeVkTexture_Destroy(VkDevice device, SeVkTexture *tex);

}  // namespace se::vulkan

#endif  // !SE_VK_TEXTURE_H_
