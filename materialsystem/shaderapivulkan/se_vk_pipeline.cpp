// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_pipeline.cpp -- Phase 2 native Vulkan backend, step S2.4.
//
// Original code written directly against the Vulkan C API. Triangle vertex
// data: positions in pixels-independent NDC, colors per corner.

#include "se_vk_pipeline.h"

#include <cstring>

#include "tier0/dbg.h"

namespace se::vulkan {
namespace {

// pos.xy + color.rgb + uv, matching tri_tex.vert locations 0/1/2.
struct TriVertex {
  float pos[2];
  float color[3];
  float uv[2];
};

constexpr TriVertex kTriangle[3] = {
    {{0.0f, -0.6f}, {1.0f, 0.2f, 0.15f}, {0.5f, 0.0f}},
    {{0.6f, 0.5f}, {0.15f, 0.45f, 1.0f}, {1.0f, 1.0f}},
    {{-0.6f, 0.5f}, {0.2f, 0.9f, 0.25f}, {0.0f, 1.0f}},
};

bool CreateShaderModule(VkDevice device, const uint32_t *words, size_t nwords,
                        VkShaderModule *out, const char *name) {
  VkShaderModuleCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  sci.codeSize = nwords * 4;
  sci.pCode = words;
  if (vkCreateShaderModule(device, &sci, nullptr, out) != VK_SUCCESS) {
    Warning("[vulkan] vkCreateShaderModule(%s) failed\n", name);
    return false;
  }
  return true;
}

uint32_t FindHostVisibleMemory(VkPhysicalDevice physical, uint32_t typeBits) {
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

}  // namespace

bool SeVkPipeline_Create(VkDevice device, VkPhysicalDevice physical,
                         VkRenderPass renderPass, VkExtent2D extent,
                         const uint32_t *vertSpv, size_t vertWords,
                         const uint32_t *fragSpv, size_t fragWords,
                         VkDescriptorSetLayout setLayout, SeVkPipeline *pipe) {
  if (!CreateShaderModule(device, vertSpv, vertWords, &pipe->vert, "tri.vert") ||
      !CreateShaderModule(device, fragSpv, fragWords, &pipe->frag, "tri.frag")) {
    return false;
  }

  VkPipelineLayoutCreateInfo pli{};
  pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pli.setLayoutCount = 1;
  pli.pSetLayouts = &setLayout;
  if (vkCreatePipelineLayout(device, &pli, nullptr, &pipe->layout) != VK_SUCCESS) {
    Warning("[vulkan] vkCreatePipelineLayout(tri) failed\n");
    return false;
  }

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = pipe->vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = pipe->frag;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(TriVertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attrs[3]{};
  attrs[0].location = 0;
  attrs[0].binding = 0;
  attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
  attrs[0].offset = offsetof(TriVertex, pos);
  attrs[1].location = 1;
  attrs[1].binding = 0;
  attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[1].offset = offsetof(TriVertex, color);
  attrs[2].location = 2;
  attrs[2].binding = 0;
  attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
  attrs[2].offset = offsetof(TriVertex, uv);
  VkPipelineVertexInputStateCreateInfo vii{};
  vii.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vii.vertexBindingDescriptionCount = 1;
  vii.pVertexBindingDescriptions = &binding;
  vii.vertexAttributeDescriptionCount = 3;
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
  gpi.layout = pipe->layout;
  gpi.renderPass = renderPass;
  gpi.subpass = 0;
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpi, nullptr,
                                &pipe->pipeline) != VK_SUCCESS) {
    Warning("[vulkan] vkCreateGraphicsPipelines(tri) failed\n");
    return false;
  }

  VkBufferCreateInfo bci{};
  bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bci.size = sizeof(kTriangle);
  bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device, &bci, nullptr, &pipe->vertexBuffer) != VK_SUCCESS) {
    Warning("[vulkan] vkCreateBuffer(tri VBO) failed\n");
    return false;
  }
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(device, pipe->vertexBuffer, &req);
  uint32_t memType = FindHostVisibleMemory(physical, req.memoryTypeBits);
  if (memType == UINT32_MAX) {
    Warning("[vulkan] no host-visible memory for tri VBO\n");
    return false;
  }
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = memType;
  if (vkAllocateMemory(device, &mai, nullptr, &pipe->vertexMemory) != VK_SUCCESS) {
    Warning("[vulkan] vkAllocateMemory(tri VBO) failed\n");
    return false;
  }
  void *mapped = nullptr;
  if (vkMapMemory(device, pipe->vertexMemory, 0, sizeof(kTriangle), 0, &mapped) !=
      VK_SUCCESS) {
    Warning("[vulkan] vkMapMemory(tri VBO) failed\n");
    return false;
  }
  memcpy(mapped, kTriangle, sizeof(kTriangle));
  vkUnmapMemory(device, pipe->vertexMemory);
  if (vkBindBufferMemory(device, pipe->vertexBuffer, pipe->vertexMemory, 0) !=
      VK_SUCCESS) {
    Warning("[vulkan] vkBindBufferMemory(tri VBO) failed\n");
    return false;
  }

  pipe->ready = true;
  Msg("[vulkan] S2.4 triangle pipeline ready\n");
  return true;
}

void SeVkPipeline_RecordDraw(SeVkPipeline *pipe, VkCommandBuffer cmd,
                             VkDescriptorSet set) {
  if (!pipe->ready) {
    return;
  }
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->pipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->layout,
                          0, 1, &set, 0, nullptr);
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &pipe->vertexBuffer, &offset);
  vkCmdDraw(cmd, 3, 1, 0, 0);
}

void SeVkPipeline_Destroy(VkDevice device, SeVkPipeline *pipe) {
  if (device == VK_NULL_HANDLE) {
    return;
  }
  if (pipe->pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, pipe->pipeline, nullptr);
    pipe->pipeline = VK_NULL_HANDLE;
  }
  if (pipe->layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, pipe->layout, nullptr);
    pipe->layout = VK_NULL_HANDLE;
  }
  if (pipe->vert != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, pipe->vert, nullptr);
    pipe->vert = VK_NULL_HANDLE;
  }
  if (pipe->frag != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, pipe->frag, nullptr);
    pipe->frag = VK_NULL_HANDLE;
  }
  if (pipe->vertexBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, pipe->vertexBuffer, nullptr);
    pipe->vertexBuffer = VK_NULL_HANDLE;
  }
  if (pipe->vertexMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, pipe->vertexMemory, nullptr);
    pipe->vertexMemory = VK_NULL_HANDLE;
  }
  pipe->ready = false;
}

}  // namespace se::vulkan
