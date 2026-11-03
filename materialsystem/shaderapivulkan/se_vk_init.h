// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_init.h -- Phase 2 native Vulkan backend, step S2.2.
//
// Owns instance / physical device / logical device / graphics queue setup
// with VK_LAYER_KHRONOS_validation on by default. Every non-VK_SUCCESS
// return is logged with call site, parameters and result (PROMPT.md 2.3)
// and fails the step loudly instead of limping on.

#ifndef SE_VK_INIT_H_
#define SE_VK_INIT_H_

#include <vulkan/vulkan.h>

#include <cstdint>

namespace se::vulkan {

// Log helper: call site + VkResult string. Returns false always, so call
// sites read `if (...) return false` uniformly.
bool VkFail(const char *call, const char *params, VkResult result);

// Waits for a fence, but never forever. A fence that never signals means the
// GPU is wedged; blocking on it hangs the main thread, the window stops
// repainting, and the compositor blocking on that window freezes the whole
// desktop. On timeout we log the call site and exit, which is recoverable -
// a frozen desktop is not. `site` names the caller for the log.
void VkWaitFence(VkDevice device, VkFence fence, const char *site);

// S2.2 context: instance + one physical device + one logical device with a
// graphics queue. Swapchain etc. arrive in later steps and extend this.
struct VkContext {
  VkInstance instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {};
  uint32_t apiVersion = 0;
  VkDevice device = VK_NULL_HANDLE;
  uint32_t graphicsFamily = UINT32_MAX;
  VkQueue graphicsQueue = VK_NULL_HANDLE;
  // Validation message counters (PROMPT 2.3: warnings are bugs too).
  uint32_t validationErrors = 0;
  uint32_t validationWarnings = 0;
};

// Creates instance (Vulkan 1.3, validation layer mandatory), picks a physical
// device with a graphics queue (discrete preferred), creates the logical
// device. Returns true on full success; on any failure logs and returns
// false, releasing partial state.
bool VkContext_Create(VkContext *ctx);

// Tears everything down in reverse order. Safe on partial/empty state.
void VkContext_Destroy(VkContext *ctx);

}  // namespace se::vulkan

#endif  // !SE_VK_INIT_H_
