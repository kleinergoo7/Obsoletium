// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_texture.cpp -- Phase 2 native Vulkan backend, step S2.5.
//
// Original code written directly against the Vulkan C API.

#include "se_vk_texture.h"
#include "se_vk_init.h"

#include <cstring>

#include "tier0/dbg.h"

namespace se::vulkan {
namespace {

constexpr uint32_t kSize = 64;
constexpr uint32_t kCheck = 8;

// Classic black/white checkerboard, opaque. Row-major RGBA8.
void FillChecker(uint8_t out[kSize][kSize][4]) {
  for (uint32_t y = 0; y < kSize; ++y) {
    for (uint32_t x = 0; x < kSize; ++x) {
      uint8_t v = (((x / kCheck) + (y / kCheck)) & 1) != 0 ? 255 : 30;
      out[y][x][0] = v;
      out[y][x][1] = v;
      out[y][x][2] = v;
      out[y][x][3] = 255;
    }
  }
}

uint32_t FindMemory(VkPhysicalDevice physical, uint32_t typeBits,
                    VkMemoryPropertyFlags want) {
  VkPhysicalDeviceMemoryProperties props{};
  vkGetPhysicalDeviceMemoryProperties(physical, &props);
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) != 0u &&
        (props.memoryTypes[i].propertyFlags & want) == want) {
      return i;
    }
  }
  return UINT32_MAX;
}

bool OneShot(VkDevice device, uint32_t family, VkQueue queue,
             void (*record)(VkCommandBuffer, void *), void *user) {
  VkCommandPoolCreateInfo pci{};
  pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  pci.queueFamilyIndex = family;
  VkCommandPool pool = VK_NULL_HANDLE;
  if (vkCreateCommandPool(device, &pci, nullptr, &pool) != VK_SUCCESS) {
    Warning("[vulkan] texture: one-shot pool failed\n");
    return false;
  }
  VkCommandBufferAllocateInfo abi{};
  abi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  abi.commandPool = pool;
  abi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  abi.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  bool ok = vkAllocateCommandBuffers(device, &abi, &cmd) == VK_SUCCESS;
  if (ok) {
    VkCommandBufferBeginInfo bbi{};
    bbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ok = vkBeginCommandBuffer(cmd, &bbi) == VK_SUCCESS;
  }
  if (ok) {
    record(cmd, user);
    ok = vkEndCommandBuffer(cmd) == VK_SUCCESS;
  }
  if (ok) {
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    ok = vkCreateFence(device, &fci, nullptr, &fence) == VK_SUCCESS;
    if (ok) {
      VkSubmitInfo si{};
      si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      si.commandBufferCount = 1;
      si.pCommandBuffers = &cmd;
      ok = vkQueueSubmit(queue, 1, &si, fence) == VK_SUCCESS;
      if (ok) {
        VkWaitFence(device, fence, "texture upload");
      }
      vkDestroyFence(device, fence, nullptr);
    }
  }
  vkDestroyCommandPool(device, pool, nullptr);
  return ok;
}

struct UploadArgs {
  VkBuffer staging;
  VkImage image;
};

void RecordUpload(VkCommandBuffer cmd, void *user) {
  const auto *a = static_cast<const UploadArgs *>(user);
  VkImageMemoryBarrier toDst{};
  toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toDst.image = a->image;
  toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  toDst.subresourceRange.levelCount = 1;
  toDst.subresourceRange.layerCount = 1;
  toDst.srcAccessMask = 0;
  toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toDst);
  VkBufferImageCopy copy{};
  copy.bufferRowLength = 0;
  copy.bufferImageHeight = 0;
  copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy.imageSubresource.layerCount = 1;
  copy.imageExtent.width = kSize;
  copy.imageExtent.height = kSize;
  copy.imageExtent.depth = 1;
  vkCmdCopyBufferToImage(cmd, a->staging, a->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  VkImageMemoryBarrier toShader = toDst;
  toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toShader);
}

}  // namespace

bool SeVkTexture_Create(VkPhysicalDevice physical, VkDevice device,
                        uint32_t graphicsFamily, VkQueue queue,
                        SeVkTexture *tex) {
  static uint8_t pixels[kSize][kSize][4];
  static bool filled = false;
  if (!filled) {
    FillChecker(pixels);
    filled = true;
  }

  VkBufferCreateInfo bci{};
  bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bci.size = sizeof(pixels);
  bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkBuffer staging = VK_NULL_HANDLE;
  if (vkCreateBuffer(device, &bci, nullptr, &staging) != VK_SUCCESS) {
    Warning("[vulkan] texture: staging buffer failed\n");
    return false;
  }
  VkMemoryRequirements stagingReq{};
  vkGetBufferMemoryRequirements(device, staging, &stagingReq);
  uint32_t stagingMem =
      FindMemory(physical, stagingReq.memoryTypeBits,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
  bool ok = stagingMem != UINT32_MAX;
  if (ok) {
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = stagingReq.size;
    mai.memoryTypeIndex = stagingMem;
    ok = vkAllocateMemory(device, &mai, nullptr, &stagingMemory) == VK_SUCCESS;
  }
  void *mapped = nullptr;
  if (ok) {
    ok = vkMapMemory(device, stagingMemory, 0, sizeof(pixels), 0, &mapped) ==
         VK_SUCCESS;
  }
  if (ok) {
    memcpy(mapped, pixels, sizeof(pixels));
    vkUnmapMemory(device, stagingMemory);
    ok = vkBindBufferMemory(device, staging, stagingMemory, 0) == VK_SUCCESS;
  }
  if (!ok) {
    Warning("[vulkan] texture: staging upload setup failed\n");
    if (stagingMemory != VK_NULL_HANDLE) {
      vkFreeMemory(device, stagingMemory, nullptr);
    }
    vkDestroyBuffer(device, staging, nullptr);
    return false;
  }

  VkImageCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent.width = kSize;
  ici.extent.height = kSize;
  ici.extent.depth = 1;
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(device, &ici, nullptr, &tex->image) != VK_SUCCESS) {
    Warning("[vulkan] texture: vkCreateImage failed\n");
    vkFreeMemory(device, stagingMemory, nullptr);
    vkDestroyBuffer(device, staging, nullptr);
    return false;
  }
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(device, tex->image, &req);
  uint32_t memType =
      FindMemory(physical, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (memType == UINT32_MAX) {
    // Integrated/UMA fallback: host-visible is fine for 16 KiB.
    memType = FindMemory(physical, req.memoryTypeBits,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  }
  if (memType == UINT32_MAX) {
    Warning("[vulkan] texture: no suitable image memory\n");
    vkDestroyImage(device, tex->image, nullptr);
    tex->image = VK_NULL_HANDLE;
    vkFreeMemory(device, stagingMemory, nullptr);
    vkDestroyBuffer(device, staging, nullptr);
    return false;
  }
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = memType;
  ok = vkAllocateMemory(device, &mai, nullptr, &tex->memory) == VK_SUCCESS;
  if (ok) {
    ok = vkBindImageMemory(device, tex->image, tex->memory, 0) == VK_SUCCESS;
  }
  UploadArgs args{staging, tex->image};
  if (ok) {
    ok = OneShot(device, graphicsFamily, queue, RecordUpload, &args);
  }
  vkFreeMemory(device, stagingMemory, nullptr);
  vkDestroyBuffer(device, staging, nullptr);
  if (!ok) {
    Warning("[vulkan] texture: upload failed\n");
    vkDestroyImage(device, tex->image, nullptr);
    tex->image = VK_NULL_HANDLE;
    return false;
  }

  VkImageViewCreateInfo vci{};
  vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vci.image = tex->image;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = VK_FORMAT_R8G8B8A8_UNORM;
  vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vci.subresourceRange.levelCount = 1;
  vci.subresourceRange.layerCount = 1;
  if (vkCreateImageView(device, &vci, nullptr, &tex->view) != VK_SUCCESS) {
    Warning("[vulkan] texture: view failed\n");
    return false;
  }
  VkSamplerCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sci.magFilter = VK_FILTER_LINEAR;
  sci.minFilter = VK_FILTER_LINEAR;
  sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sci.maxLod = 0.0f;
  if (vkCreateSampler(device, &sci, nullptr, &tex->sampler) != VK_SUCCESS) {
    Warning("[vulkan] texture: sampler failed\n");
    return false;
  }
  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo lci{};
  lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  lci.bindingCount = 1;
  lci.pBindings = &binding;
  if (vkCreateDescriptorSetLayout(device, &lci, nullptr, &tex->setLayout) !=
      VK_SUCCESS) {
    Warning("[vulkan] texture: set layout failed\n");
    return false;
  }
  VkDescriptorPoolSize poolSize{};
  poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSize.descriptorCount = 1;
  VkDescriptorPoolCreateInfo pci{};
  pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pci.maxSets = 1;
  pci.poolSizeCount = 1;
  pci.pPoolSizes = &poolSize;
  if (vkCreateDescriptorPool(device, &pci, nullptr, &tex->pool) != VK_SUCCESS) {
    Warning("[vulkan] texture: pool failed\n");
    return false;
  }
  VkDescriptorSetAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  ai.descriptorPool = tex->pool;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &tex->setLayout;
  if (vkAllocateDescriptorSets(device, &ai, &tex->set) != VK_SUCCESS) {
    Warning("[vulkan] texture: set alloc failed\n");
    return false;
  }
  VkDescriptorImageInfo imageInfo{};
  imageInfo.sampler = tex->sampler;
  imageInfo.imageView = tex->view;
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = tex->set;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &imageInfo;
  vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

  tex->ready = true;
  Msg("[vulkan] S2.5 checkerboard texture ready (64x64 RGBA8)\n");
  return true;
}

void SeVkTexture_Destroy(VkDevice device, SeVkTexture *tex) {
  if (device == VK_NULL_HANDLE) {
    return;
  }
  if (tex->pool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, tex->pool, nullptr);
    tex->pool = VK_NULL_HANDLE;
  }
  tex->set = VK_NULL_HANDLE;
  if (tex->setLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, tex->setLayout, nullptr);
    tex->setLayout = VK_NULL_HANDLE;
  }
  if (tex->sampler != VK_NULL_HANDLE) {
    vkDestroySampler(device, tex->sampler, nullptr);
    tex->sampler = VK_NULL_HANDLE;
  }
  if (tex->view != VK_NULL_HANDLE) {
    vkDestroyImageView(device, tex->view, nullptr);
    tex->view = VK_NULL_HANDLE;
  }
  if (tex->image != VK_NULL_HANDLE) {
    vkDestroyImage(device, tex->image, nullptr);
    tex->image = VK_NULL_HANDLE;
  }
  if (tex->memory != VK_NULL_HANDLE) {
    vkFreeMemory(device, tex->memory, nullptr);
    tex->memory = VK_NULL_HANDLE;
  }
  tex->ready = false;
}

}  // namespace se::vulkan
