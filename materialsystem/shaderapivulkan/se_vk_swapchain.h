// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_swapchain.h -- Phase 2 native Vulkan backend, step S2.3.
//
// Swapchain + cleared color buffer ("something on screen"). Lazy: the SDL
// window only exists once the engine shows it, so the surface/swapchain is
// created on first Present and recreated on resize/out-of-date. WSI goes
// through SDL_Vulkan_CreateSurface (the engine owns its SDL3 window; raw
// XCB would fight it for the same surface).

#ifndef SE_VK_SWAPCHAIN_H_
#define SE_VK_SWAPCHAIN_H_

#include <vulkan/vulkan.h>

#include <SDL3/SDL_video.h>

#include "se_vk_init.h"
#include "se_vk_pipeline.h"
#include "se_vk_texture.h"

namespace se::vulkan {

// Clear color, linear. Distinctive engine-blue so a screenshot proves the
// Vulkan path presented (vs black null-renderer output).
struct SeVkClearColor {
  float r = 0.10f;
  float g = 0.16f;
  float b = 0.38f;
  float a = 1.0f;
};

struct SeVkSwapchain {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkExtent2D extent{0, 0};
  VkFormat format = VK_FORMAT_UNDEFINED;
  static constexpr int kMaxImages = 8;
  uint32_t imageCount = 0;
  VkImage images[kMaxImages]{};
  VkImageView views[kMaxImages]{};
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkFramebuffer framebuffers[kMaxImages]{};
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandBuffer commands[kMaxImages]{};
  // The acquire semaphore is waited on by the submit that consumes the
  // acquired image; the frame fence guarantees the previous submit consumed
  // it, so one is enough.
  VkSemaphore acquireSem = VK_NULL_HANDLE;
  // One present semaphore per swapchain image: the swapchain still holds a
  // presented image's semaphore when the next image is acquired, so a single
  // reused one trips VUID-vkQueueSubmit-pSignalSemaphores-00067.
  VkSemaphore presentSem[kMaxImages] = {};
  VkFence frameFence = VK_NULL_HANDLE;
  uint32_t frameIndex = 0;
  bool loggedNoWindow = false;
  SeVkClearColor clear{};
  // S2.4 hardcoded triangle (replaced by the real shader system in S2.6).
  SeVkPipeline tri{};
  // S2.5 texture (device-lifetime; survives swapchain recreates).
  SeVkTexture diffuse{};
  // S2.4 verification scaffolding: when set, the next presented frame is
  // copied to a host image and written as binary PPM, then cleared. Never
  // set by the engine; the standalone test uses it for pixel proof.
  char capturePath[256] = {};
  VkImage captureImage = VK_NULL_HANDLE;
  VkDeviceMemory captureMemory = VK_NULL_HANDLE;
};

// Presents one cleared frame, creating/recreating the swapchain as needed.
// The window is injected (engine passes its SDL window; the test passes its
// own) so this code never reaches process globals. Null window = engine not
// showing anything yet: no-op with a one-time note.
void SeVkSwapchain_Present(VkContext *ctx, SeVkSwapchain *swap,
                           SDL_Window *window);

// Last established swapchain extent, or 1x1 before first SetMode (see .cpp).
VkExtent2D SeVkSwapchain_LastExtent();

// Releases everything. Safe on partial/empty state.
void SeVkSwapchain_Destroy(VkContext *ctx, SeVkSwapchain *swap);

}  // namespace se::vulkan

#endif  // !SE_VK_SWAPCHAIN_H_
