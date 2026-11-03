// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_swapchain.cpp -- Phase 2 native Vulkan backend, step S2.3.
//
// Original code written directly against the Vulkan C API.

#include "se_vk_swapchain.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "se_vk_bind.h"
#include "se_vk_texcache.h"
#include "tier0/dbg.h"
#include "tier0/platform.h"

#include <cstdlib>

// S2.4 triangle SPIR-V, AOT-compiled at build time (see CMake).
#include "se_tri_spv.h"

namespace se::vulkan {

// Last established swapchain extent (1x1 before first SetMode).
// Backs GetBackBufferDimensions, which the engine queries before the
// device exists (render-target restore); ones terminate its halve loops
// instead of spinning on garbage, and no zero viewport can reach Vulkan.
// Written on swapchain create, read anywhere; worst case is one stale-sized frame.
static VkExtent2D g_lastExtent{1, 1};

namespace {

// Records the capture copy for the current swapchain image and transitions
// it back for present. Staging resources are created here and released by
// WriteCapture after the GPU drains.
bool CaptureFrame(VkContext *ctx, SeVkSwapchain *swap, VkCommandBuffer cmd,
                  uint32_t image) {
  VkImageCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = swap->format;
  ici.extent.width = swap->extent.width;
  ici.extent.height = swap->extent.height;
  ici.extent.depth = 1;
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_LINEAR;
  ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(ctx->device, &ici, nullptr, &swap->captureImage) != VK_SUCCESS) {
    Warning("[vulkan] capture: vkCreateImage failed\n");
    return false;
  }
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(ctx->device, swap->captureImage, &req);
  VkPhysicalDeviceMemoryProperties props{};
  vkGetPhysicalDeviceMemoryProperties(ctx->physical, &props);
  uint32_t memType = UINT32_MAX;
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
    if ((req.memoryTypeBits & (1u << i)) != 0u &&
        (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u) {
      memType = i;
      break;
    }
  }
  if (memType == UINT32_MAX) {
    Warning("[vulkan] capture: no host-visible memory\n");
    return false;
  }
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = memType;
  if (vkAllocateMemory(ctx->device, &mai, nullptr, &swap->captureMemory) != VK_SUCCESS) {
    Warning("[vulkan] capture: vkAllocateMemory failed\n");
    return false;
  }
  vkBindImageMemory(ctx->device, swap->captureImage, swap->captureMemory, 0);

  VkImageMemoryBarrier toTransfer{};
  toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.image = swap->images[image];
  toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  toTransfer.subresourceRange.levelCount = 1;
  toTransfer.subresourceRange.layerCount = 1;
  toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  VkImageMemoryBarrier stagingBarrier = toTransfer;
  stagingBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  stagingBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  stagingBarrier.image = swap->captureImage;
  stagingBarrier.srcAccessMask = 0;
  stagingBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  VkImageMemoryBarrier preBarriers[2] = {toTransfer, stagingBarrier};
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                       2, preBarriers);
  VkImageCopy copy{};
  copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy.srcSubresource.layerCount = 1;
  copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy.dstSubresource.layerCount = 1;
  copy.extent.width = swap->extent.width;
  copy.extent.height = swap->extent.height;
  copy.extent.depth = 1;
  vkCmdCopyImage(cmd, swap->images[image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 swap->captureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  VkImageMemoryBarrier toPresent = toTransfer;
  toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  toPresent.dstAccessMask = 0;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toPresent);
  return true;
}

// Maps the staging image written by CaptureFrame and saves binary PPM
// (converting B8G8R8A8 to RGB). Runs after the present + drain.
void WriteCapture(VkContext *ctx, SeVkSwapchain *swap) {
  char path[256];
  int written = snprintf(path, sizeof(path), "%s", swap->capturePath);
  swap->capturePath[0] = '\0';
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(path)) {
    Warning("[vulkan] capture: path too long (%d bytes), nothing written\n",
            written);
    return;
  }
  VkImageSubresource sub{};
  sub.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  VkSubresourceLayout layout{};
  vkGetImageSubresourceLayout(ctx->device, swap->captureImage, &sub, &layout);
  const uint8_t *mapped = nullptr;
  if (vkMapMemory(ctx->device, swap->captureMemory, 0, VK_WHOLE_SIZE, 0,
                  reinterpret_cast<void **>(const_cast<uint8_t **>(&mapped))) !=
      VK_SUCCESS) {
    Warning("[vulkan] capture: vkMapMemory failed\n");
    return;
  }
  FILE *f = fopen(path, "wb");
  if (!f) {
    Warning("[vulkan] capture: cannot open %s for writing: %s\n", path,
            strerror(errno));
  }
  if (f) {
    bool ok = fprintf(f, "P6\n%u %u\n255\n", swap->extent.width,
                      swap->extent.height) > 0;
    // P6, top row first: Vulkan images are top-down already for presents.
    for (uint32_t y = 0; ok && y < swap->extent.height; ++y) {
      const uint8_t *row = mapped + layout.offset + y * layout.rowPitch;
      for (uint32_t x = 0; x < swap->extent.width; ++x) {
        uint8_t b = row[4 * x + 0], g = row[4 * x + 1], r = row[4 * x + 2];
        uint8_t px[3] = {r, g, b};
        if (fwrite(px, 1, 3, f) != 3) {
          ok = false;
          break;
        }
      }
    }
    if (fclose(f) != 0) {
      ok = false;
    }
    // printf, not Msg: the engine swallows Msg after init, and a capture that
    // silently produced nothing is indistinguishable from one never armed.
    if (ok) {
      printf("[vulkan] capture wrote %s (%ux%u)\n", path, swap->extent.width,
             swap->extent.height);
      fflush(stdout);
    } else {
      Warning("[vulkan] capture: write failed for %s: %s\n", path,
              strerror(errno));
    }
  }
  vkUnmapMemory(ctx->device, swap->captureMemory);
  vkDestroyImage(ctx->device, swap->captureImage, nullptr);
  swap->captureImage = VK_NULL_HANDLE;
  vkFreeMemory(ctx->device, swap->captureMemory, nullptr);
  swap->captureMemory = VK_NULL_HANDLE;
}

bool CreateSurface(VkContext *ctx, SeVkSwapchain *swap, SDL_Window *window) {
  if (!window) {
    return false;
  }
  // SDL3 reports bool; surface creation itself is the check.
  if (!SDL_Vulkan_CreateSurface(window, ctx->instance, nullptr,
                                &swap->surface)) {
    Warning("[vulkan] SDL_Vulkan_CreateSurface(engine SDL window) -> false: %s\n",
            SDL_GetError());
    return false;
  }
  VkBool32 supported = VK_FALSE;
  VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
      ctx->physical, ctx->graphicsFamily, swap->surface, &supported);
  if (result != VK_SUCCESS || !supported) {
    if (result == VK_SUCCESS) {
      Warning("[vulkan] graphics family cannot present to this surface\n");
    } else {
      VkFail("vkGetPhysicalDeviceSurfaceSupportKHR", "graphics family",
             result);
    }
    vkDestroySurfaceKHR(ctx->instance, swap->surface, nullptr);
    swap->surface = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

bool CreateSwapchain(VkContext *ctx, SeVkSwapchain *swap, SDL_Window *window) {
  VkSurfaceCapabilitiesKHR caps{};
  VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      ctx->physical, swap->surface, &caps);
  if (result != VK_SUCCESS) {
    return VkFail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", "", result);
  }

  uint32_t formatCount = 0;
  result = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical, swap->surface,
                                               &formatCount, nullptr);
  if (result != VK_SUCCESS || formatCount == 0) {
    return VkFail("vkGetPhysicalDeviceSurfaceFormatsKHR", "count", result);
  }
  VkSurfaceFormatKHR formats[32];
  if (formatCount > 32) {
    formatCount = 32;
  }
  // VK_INCOMPLETE just means truncation to our fixed buffer: usable.
  result = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical, swap->surface,
                                               &formatCount, formats);
  if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || formatCount == 0) {
    return VkFail("vkGetPhysicalDeviceSurfaceFormatsKHR", "list", result);
  }
  // Prefer sRGB non-linear 8-bit; fall back to the first offered.
  VkSurfaceFormatKHR picked = formats[0];
  for (uint32_t i = 0; i < formatCount; ++i) {
    if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
        formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      picked = formats[i];
      break;
    }
  }

  // FIFO (vsync) is mandatory per spec, so no capability query is needed.
  // Mailbox and immediate have no rate limit: the renderer free-runs, and
  // with a full world drawing that pins the GPU and starves the compositor of
  // a windowed session - the desktop stops responding. FIFO costs at most one
  // frame of latency and keeps the desktop alive.
  VkPresentModeKHR pickedMode = VK_PRESENT_MODE_FIFO_KHR;

  VkExtent2D extent = caps.currentExtent;
  if (extent.width == UINT32_MAX) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    extent.width = std::clamp<uint32_t>(
        static_cast<uint32_t>(w > 0 ? w : 640), caps.minImageExtent.width,
        caps.maxImageExtent.width);
    extent.height = std::clamp<uint32_t>(
        static_cast<uint32_t>(h > 0 ? h : 480), caps.minImageExtent.height,
        caps.maxImageExtent.height);
  }

  uint32_t want = caps.minImageCount + 1;
  if (caps.maxImageCount != 0 && want > caps.maxImageCount) {
    want = caps.maxImageCount;
  }
  if (want > SeVkSwapchain::kMaxImages) {
    want = SeVkSwapchain::kMaxImages;
  }

  VkSwapchainCreateInfoKHR sci{};
  sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  sci.surface = swap->surface;
  sci.minImageCount = want;
  sci.imageFormat = picked.format;
  sci.imageColorSpace = picked.colorSpace;
  sci.imageExtent = extent;
  sci.imageArrayLayers = 1;
  // TRANSFER_SRC supports the S2.4 pixel-capture verification path.
  sci.imageUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  sci.presentMode = pickedMode;
  sci.clipped = VK_TRUE;
  sci.oldSwapchain = swap->swapchain;
  char param[128];
  snprintf(param, sizeof(param), "extent=%ux%u images=%u mode=%d", extent.width,
           extent.height, want, static_cast<int>(pickedMode));
  result = vkCreateSwapchainKHR(ctx->device, &sci, nullptr, &swap->swapchain);
  if (result != VK_SUCCESS) {
    return VkFail("vkCreateSwapchainKHR", param, result);
  }
  if (sci.oldSwapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(ctx->device, sci.oldSwapchain, nullptr);
  }
  swap->extent = extent;
  swap->format = picked.format;
  g_lastExtent = extent;

  result = vkGetSwapchainImagesKHR(ctx->device, swap->swapchain,
                                  &swap->imageCount, nullptr);
  if (result != VK_SUCCESS || swap->imageCount == 0 ||
      swap->imageCount > SeVkSwapchain::kMaxImages) {
    VkFail("vkGetSwapchainImagesKHR", "count", result);
    return false;
  }
  result = vkGetSwapchainImagesKHR(ctx->device, swap->swapchain,
                                  &swap->imageCount, swap->images);
  if (result != VK_SUCCESS) {
    return VkFail("vkGetSwapchainImagesKHR", "list", result);
  }
  Msg("[vulkan] swapchain: %ux%u x%u (mode %d)\n", extent.width, extent.height,
      swap->imageCount, static_cast<int>(pickedMode));
  return true;
}

bool CreateViews(VkContext *ctx, SeVkSwapchain *swap) {
  for (uint32_t i = 0; i < swap->imageCount; ++i) {
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = swap->images[i];
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = swap->format;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    char param[32];
    snprintf(param, sizeof(param), "image %u", i);
    VkResult result =
        vkCreateImageView(ctx->device, &vci, nullptr, &swap->views[i]);
    if (result != VK_SUCCESS) {
      return VkFail("vkCreateImageView", param, result);
    }
  }
  return true;
}

bool CreateRenderPass(VkContext *ctx, SeVkSwapchain *swap) {
  VkAttachmentDescription color{};
  color.format = swap->format;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference ref{};
  ref.attachment = 0;
  ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &ref;
  // Present-source handoff needs the subpass dependency (no pipeline yet).
  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.srcAccessMask = 0;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo rpi{};
  rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpi.attachmentCount = 1;
  rpi.pAttachments = &color;
  rpi.subpassCount = 1;
  rpi.pSubpasses = &sub;
  rpi.dependencyCount = 1;
  rpi.pDependencies = &dep;
  VkResult result =
      vkCreateRenderPass(ctx->device, &rpi, nullptr, &swap->renderPass);
  if (result != VK_SUCCESS) {
    return VkFail("vkCreateRenderPass", "clear-to-present", result);
  }
  for (uint32_t i = 0; i < swap->imageCount; ++i) {
    VkFramebufferCreateInfo fbi{};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = swap->renderPass;
    fbi.attachmentCount = 1;
    fbi.pAttachments = &swap->views[i];
    fbi.width = swap->extent.width;
    fbi.height = swap->extent.height;
    fbi.layers = 1;
    char param[32];
    snprintf(param, sizeof(param), "framebuffer %u", i);
    result = vkCreateFramebuffer(ctx->device, &fbi, nullptr, &swap->framebuffers[i]);
    if (result != VK_SUCCESS) {
      return VkFail("vkCreateFramebuffer", param, result);
    }
  }
  return true;
}

bool CreateCommands(VkContext *ctx, SeVkSwapchain *swap) {
  VkCommandPoolCreateInfo pci{};
  pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pci.queueFamilyIndex = ctx->graphicsFamily;
  VkResult result =
      vkCreateCommandPool(ctx->device, &pci, nullptr, &swap->commandPool);
  if (result != VK_SUCCESS) {
    return VkFail("vkCreateCommandPool", "resettable", result);
  }
  VkCommandBufferAllocateInfo abi{};
  abi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  abi.commandPool = swap->commandPool;
  abi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  abi.commandBufferCount = swap->imageCount;
  result = vkAllocateCommandBuffers(ctx->device, &abi, swap->commands);
  if (result != VK_SUCCESS) {
    return VkFail("vkAllocateCommandBuffers", "primary", result);
  }
  VkSemaphoreCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  if (vkCreateSemaphore(ctx->device, &sci, nullptr, &swap->acquireSem) != VK_SUCCESS) {
    Warning("[vulkan] vkCreateSemaphore failed\n");
    return false;
  }
  for (uint32_t i = 0; i < swap->imageCount; ++i) {
    if (vkCreateSemaphore(ctx->device, &sci, nullptr, &swap->presentSem[i]) != VK_SUCCESS) {
      Warning("[vulkan] vkCreateSemaphore failed\n");
      return false;
    }
  }
  VkFenceCreateInfo fci{};
  fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  if (vkCreateFence(ctx->device, &fci, nullptr, &swap->frameFence) != VK_SUCCESS) {
    Warning("[vulkan] vkCreateFence failed\n");
    return false;
  }
  return true;
}

void DestroySwapchainObjects(VkContext *ctx, SeVkSwapchain *swap) {
  if (ctx->device == VK_NULL_HANDLE) {
    return;
  }
  vkDeviceWaitIdle(ctx->device);
  SeVkBind_InvalidatePipelines(&SeVkBind_Global());
  SeVkPipeline_Destroy(ctx->device, &swap->tri);
  for (uint32_t i = 0; i < SeVkSwapchain::kMaxImages; ++i) {
    if (swap->framebuffers[i] != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(ctx->device, swap->framebuffers[i], nullptr);
      swap->framebuffers[i] = VK_NULL_HANDLE;
    }
    if (swap->views[i] != VK_NULL_HANDLE) {
      vkDestroyImageView(ctx->device, swap->views[i], nullptr);
      swap->views[i] = VK_NULL_HANDLE;
    }
  }
  if (swap->renderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(ctx->device, swap->renderPass, nullptr);
    swap->renderPass = VK_NULL_HANDLE;
  }
  if (swap->swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(ctx->device, swap->swapchain, nullptr);
    swap->swapchain = VK_NULL_HANDLE;
  }
  swap->imageCount = 0;
}

bool EnsureSwapchain(VkContext *ctx, SeVkSwapchain *swap, SDL_Window *window) {
  if (swap->swapchain != VK_NULL_HANDLE) {
    return true;
  }
  if (!window) {
    if (!swap->loggedNoWindow) {
      DevMsg("[vulkan] no engine window yet; present deferred\n");
      swap->loggedNoWindow = true;
    }
    return false;
  }
  swap->loggedNoWindow = false;
  if (!CreateSurface(ctx, swap, window)) {
    return false;
  }
  // S2.5 texture is device-lifetime: create once, keep across swapchain
  // recreates (the pipeline layout references its set layout).
  if (!swap->diffuse.ready &&
      !SeVkTexture_Create(ctx->physical, ctx->device, ctx->graphicsFamily,
                          ctx->graphicsQueue, &swap->diffuse)) {
    SeVkSwapchain_Destroy(ctx, swap);
    return false;
  }
  if (!CreateSwapchain(ctx, swap, window) || !CreateViews(ctx, swap) ||
      !CreateRenderPass(ctx, swap) || !CreateCommands(ctx, swap) ||
      !SeVkPipeline_Create(ctx->device, ctx->physical, swap->renderPass,
                           swap->extent, kTriTexVertSpv,
                           kTriTexVertSpv_word_count, kTriTexFragSpv,
                           kTriTexFragSpv_word_count,
                           swap->diffuse.setLayout, &swap->tri)) {
    SeVkSwapchain_Destroy(ctx, swap);
    return false;
  }
  Msg("[vulkan] S2.3 swapchain ready\n");
  return true;
}

}  // namespace

VkExtent2D SeVkSwapchain_LastExtent() { return g_lastExtent; }

void SeVkSwapchain_Present(VkContext *ctx, SeVkSwapchain *swap,
                           SDL_Window *window) {
  if (!EnsureSwapchain(ctx, swap, window)) {
    return;
  }
  // Autonomous pixel proof: SE_VK_CAPTURE=/path.ppm dumps one presented
  // frame (past swapchain warmup) via the S2.4 capture path. No X
  // screenshot tooling needed. SE_VK_CAPTURE_AT=N selects the frame
  // (default 2); 0 disables.
  if (swap->capturePath[0] == '\0') {
    uint64_t at = 2;
    if (const char *atEnv = getenv("SE_VK_CAPTURE_AT")) {
      at = strtoull(atEnv, nullptr, 10);
    }
    if (at != 0 && swap->frameIndex == at) {
      if (const char *cap = getenv("SE_VK_CAPTURE")) {
        snprintf(swap->capturePath, sizeof(swap->capturePath), "%s", cap);
        Msg("[vulkan] capture armed at frame %llu -> %s\n",
            (unsigned long long)at, cap);
      }
    }
  }
  VkWaitFence(ctx->device, swap->frameFence, "frame present");
  vkResetFences(ctx->device, 1, &swap->frameFence);
  // Queue is idle here (single frameFence): parked texture resources whose
  // views were rewritten out of the descriptor set can now be destroyed.
  SeVkTexCache_DrainTrash(false);

  uint32_t image = 0;
  VkResult result = vkAcquireNextImageKHR(
      ctx->device, swap->swapchain, UINT64_MAX, swap->acquireSem,
      VK_NULL_HANDLE, &image);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    DestroySwapchainObjects(ctx, swap);
    return;
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    VkFail("vkAcquireNextImageKHR", "", result);
    return;
  }

  VkCommandBuffer cmd = swap->commands[image % SeVkSwapchain::kMaxImages];
  VkCommandBufferBeginInfo bbi{};
  bbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  result = vkBeginCommandBuffer(cmd, &bbi);
  if (result != VK_SUCCESS) {
    VkFail("vkBeginCommandBuffer", "clear", result);
    return;
  }
  // S2.8-textures: staged texture uploads + staging-garbage drain, outside
  // the render pass (transfers are illegal inside one).
  SeVkTexCache_Init(ctx->device, ctx->physical, ctx->graphicsFamily,
                    ctx->graphicsQueue);
  SeVkTexCache_Flush(cmd);
  VkClearValue clear{};
  clear.color.float32[0] = swap->clear.r;
  clear.color.float32[1] = swap->clear.g;
  clear.color.float32[2] = swap->clear.b;
  clear.color.float32[3] = swap->clear.a;
  VkRenderPassBeginInfo rbi{};
  rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rbi.renderPass = swap->renderPass;
  rbi.framebuffer = swap->framebuffers[image];
  rbi.renderArea.extent = swap->extent;
  rbi.clearValueCount = 1;
  rbi.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  // S2.8-bind: engine-driven draws when a frame was committed with bound
  // pack shaders; otherwise the S2.4 triangle fallback (proves present).
  SeVkBind &bind = SeVkBind_Global();
  bool bindReady = bind.ready ||
                   SeVkBind_Ensure(&bind, ctx->device, ctx->physical,
                                   swap->diffuse.view, swap->diffuse.sampler);
  if (bindReady && !bind.ranges.empty() && bind.boundVS != nullptr &&
      bind.boundPS != nullptr) {
    static bool loggedFirstBind = false;
    if (!loggedFirstBind) {
      loggedFirstBind = true;
      Msg("[vulkan] S2.8 first engine-driven frame: %zu ranges (vs=%s ps=%s)\n",
          bind.ranges.size(), bind.boundVS->name, bind.boundPS->name);
    }
    // Deterministic pixel proof: SE_VK_CAPTURE_ON_BIND=1 captures the first
    // engine-driven frame (frameIndex lotteries kept missing the rare draw
    // frames while the engine stalls in assert floods).
    static bool bindCaptured = false;
    if (!bindCaptured && getenv("SE_VK_CAPTURE_ON_BIND")) {
      if (const char *cap = getenv("SE_VK_CAPTURE")) {
        snprintf(swap->capturePath, sizeof(swap->capturePath), "%s", cap);
        bindCaptured = true;
      }
    }
    SeVkBind_RecordDraws(&bind, cmd, swap->renderPass, swap->extent);
  } else {
    bind.verts.clear();
    bind.indices.clear();
    bind.ranges.clear();
    bind.hasPending = false;
    SeVkPipeline_RecordDraw(&swap->tri, cmd, swap->diffuse.set);
  }
  vkCmdEndRenderPass(cmd);
  bool capturing = swap->capturePath[0] != '\0';
  if (capturing && !CaptureFrame(ctx, swap, cmd, image)) {
    capturing = false;
    swap->capturePath[0] = '\0';
  }
  result = vkEndCommandBuffer(cmd);
  if (result != VK_SUCCESS) {
    VkFail("vkEndCommandBuffer", "clear", result);
    return;
  }

  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &swap->acquireSem;
  si.pWaitDstStageMask = &waitStage;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &swap->presentSem[image];
  result = vkQueueSubmit(ctx->graphicsQueue, 1, &si, swap->frameFence);
  if (result != VK_SUCCESS) {
    VkFail("vkQueueSubmit", "clear", result);
    return;
  }
  VkPresentInfoKHR pi{};
  pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &swap->presentSem[image];
  pi.swapchainCount = 1;
  pi.pSwapchains = &swap->swapchain;
  pi.pImageIndices = &image;
  result = vkQueuePresentKHR(ctx->graphicsQueue, &pi);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    DestroySwapchainObjects(ctx, swap);
  } else if (result != VK_SUCCESS) {
    VkFail("vkQueuePresentKHR", "", result);
  } else {
    ++swap->frameIndex;
  }
  if (swap->frameIndex % 600 == 0) {
    const SeVkBind &bind = SeVkBind_Global();
    Msg("[vulkan] presents=%u ranges_pending=%zu bound=%s/%s\n",
        swap->frameIndex, bind.ranges.size(),
        bind.boundVS ? bind.boundVS->name : "-",
        bind.boundPS ? bind.boundPS->name : "-");
  }
  {
    // Heartbeat: at most one line per second so stalls are visible even
    // when the 600-frame counter never fires (S2.8 bring-up).
    static double lastBeat = 0.0;
    double now = Plat_FloatTime();
    if (now - lastBeat >= 1.0) {
      lastBeat = now;
      Msg("[vulkan] heartbeat presents=%u\n", swap->frameIndex);
    }
  }
  if (capturing) {
    vkDeviceWaitIdle(ctx->device);
    WriteCapture(ctx, swap);
  }
}

void SeVkSwapchain_Destroy(VkContext *ctx, SeVkSwapchain *swap) {
  // Drain in-flight work first: destroying semaphores/fences/pools with a
  // pending frame is a validation error (VUID-05149/01120/00041).
  if (ctx->device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(ctx->device);
  }
  SeVkTexture_Destroy(ctx->device, &swap->diffuse);
  if (swap->acquireSem != VK_NULL_HANDLE && ctx->device != VK_NULL_HANDLE) {
    vkDestroySemaphore(ctx->device, swap->acquireSem, nullptr);
    swap->acquireSem = VK_NULL_HANDLE;
  }
  for (uint32_t i = 0; i < swap->imageCount; ++i) {
    if (swap->presentSem[i] != VK_NULL_HANDLE && ctx->device != VK_NULL_HANDLE) {
      vkDestroySemaphore(ctx->device, swap->presentSem[i], nullptr);
      swap->presentSem[i] = VK_NULL_HANDLE;
    }
  }
  if (swap->frameFence != VK_NULL_HANDLE && ctx->device != VK_NULL_HANDLE) {
    vkDestroyFence(ctx->device, swap->frameFence, nullptr);
    swap->frameFence = VK_NULL_HANDLE;
  }
  if (swap->commandPool != VK_NULL_HANDLE && ctx->device != VK_NULL_HANDLE) {
    vkDestroyCommandPool(ctx->device, swap->commandPool, nullptr);
    swap->commandPool = VK_NULL_HANDLE;
  }
  DestroySwapchainObjects(ctx, swap);
  if (swap->surface != VK_NULL_HANDLE && ctx->instance != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(ctx->instance, swap->surface, nullptr);
    swap->surface = VK_NULL_HANDLE;
  }
}

}  // namespace se::vulkan
