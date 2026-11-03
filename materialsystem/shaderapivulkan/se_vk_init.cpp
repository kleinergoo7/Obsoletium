// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_init.cpp -- Phase 2 native Vulkan backend, step S2.2.
//
// Original code written directly against the Vulkan C API. The D3D9->Vulkan
// mapping strategy is informed by WineD3D's Vulkan backend (primary) and
// DXVK (secondary) as read-only references; no code is copied from either.

#include "se_vk_init.h"

#include "se_vk_texcache.h"

#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "tier0/dbg.h"

namespace se::vulkan {
namespace {

// VkResult to string for the mandatory failure logging.
const char *VkResultString(VkResult result) {
#define SE_VK_CASE(x) \
  case x:             \
    return #x
  switch (result) {
    SE_VK_CASE(VK_SUCCESS);
    SE_VK_CASE(VK_NOT_READY);
    SE_VK_CASE(VK_TIMEOUT);
    SE_VK_CASE(VK_EVENT_SET);
    SE_VK_CASE(VK_EVENT_RESET);
    SE_VK_CASE(VK_INCOMPLETE);
    SE_VK_CASE(VK_ERROR_OUT_OF_HOST_MEMORY);
    SE_VK_CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY);
    SE_VK_CASE(VK_ERROR_INITIALIZATION_FAILED);
    SE_VK_CASE(VK_ERROR_DEVICE_LOST);
    SE_VK_CASE(VK_ERROR_MEMORY_MAP_FAILED);
    SE_VK_CASE(VK_ERROR_LAYER_NOT_PRESENT);
    SE_VK_CASE(VK_ERROR_EXTENSION_NOT_PRESENT);
    SE_VK_CASE(VK_ERROR_FEATURE_NOT_PRESENT);
    SE_VK_CASE(VK_ERROR_INCOMPATIBLE_DRIVER);
    SE_VK_CASE(VK_ERROR_TOO_MANY_OBJECTS);
    SE_VK_CASE(VK_ERROR_FORMAT_NOT_SUPPORTED);
    SE_VK_CASE(VK_ERROR_FRAGMENTED_POOL);
    SE_VK_CASE(VK_ERROR_UNKNOWN);
    SE_VK_CASE(VK_ERROR_OUT_OF_POOL_MEMORY);
    SE_VK_CASE(VK_ERROR_INVALID_EXTERNAL_HANDLE);
    SE_VK_CASE(VK_ERROR_FRAGMENTATION);
    SE_VK_CASE(VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);
    SE_VK_CASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
    SE_VK_CASE(VK_ERROR_VALIDATION_FAILED_EXT);
    SE_VK_CASE(VK_ERROR_INVALID_SHADER_NV);
    SE_VK_CASE(VK_SUBOPTIMAL_KHR);
    SE_VK_CASE(VK_ERROR_OUT_OF_DATE_KHR);
    default:
      break;
  }
  return "<unlisted VkResult>";
#undef SE_VK_CASE
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
  auto *ctx = static_cast<VkContext *>(user);
  const char *tag =
      (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR"
      : (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
          ? "WARNING"
          : "info";
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    ++ctx->validationErrors;
    Warning("[vulkan][validation][%s] %s\n", tag,
            data && data->pMessage ? data->pMessage : "(no message)");
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    ++ctx->validationWarnings;
    Msg("[vulkan][validation][%s] %s\n", tag,
        data && data->pMessage ? data->pMessage : "(no message)");
  } else {
    DevMsg("[vulkan][validation][%s] %s\n", tag,
           data && data->pMessage ? data->pMessage : "(no message)");
  }
  return VK_FALSE;
}

}  // namespace

bool VkFail(const char *call, const char *params, VkResult result) {
  Warning("[vulkan] %s(%s) -> %s (%d)\n", call, params ? params : "",
          VkResultString(result), static_cast<int>(result));
  return false;
}

void VkWaitFence(VkDevice device, VkFence fence, const char *site) {
  // shortcut: fixed 5s budget per wait. A frame or a texture upload takes
  // milliseconds; 5s of silence means the GPU is wedged, not slow. Upgrade:
  // raise it if a deliberately slow path ever needs longer.
  const uint64_t kTimeoutMs = 5000;
  VkResult r =
      vkWaitForFences(device, 1, &fence, VK_TRUE, kTimeoutMs * 1000000ull);
  if (r == VK_SUCCESS) {
    return;
  }
  Plat_FatalError(
      "[vulkan] %s: vkWaitForFences unsignalled after %llums (%s, %d); GPU is "
      "wedged - exiting rather than freezing the desktop\n",
      site, static_cast<unsigned long long>(kTimeoutMs), VkResultString(r),
      static_cast<int>(r));
}

namespace {

bool HasLayer(const char *name) {
  uint32_t count = 0;
  if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) {
    return false;
  }
  // Fixed-size scratch: layer counts are small (single digits here).
  VkLayerProperties available[64];
  if (count > 64) {
    return false;
  }
  if (vkEnumerateInstanceLayerProperties(&count, available) != VK_SUCCESS) {
    return false;
  }
  for (uint32_t i = 0; i < count; ++i) {
    if (strcmp(available[i].layerName, name) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool VkContext_Create(VkContext *ctx) {
  constexpr const char *kValidationLayer = "VK_LAYER_KHRONOS_validation";
  if (!HasLayer(kValidationLayer)) {
    Warning(
        "[vulkan] required layer %s absent (package "
        "vulkan-validationlayers). Phase 2 mandates validation; refusing to "
        "create a device without it.\n",
        kValidationLayer);
    return false;
  }

  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "cstrike-vulkan";
  app.applicationVersion = VK_MAKE_VERSION(0, 2, 2);
  app.pEngineName = "obsoletium-shaderapivulkan";
  app.engineVersion = VK_MAKE_VERSION(0, 2, 2);
  // Target Vulkan 1.3: the host device (RADV POLARIS10, llvmpipe) exposes
  // 1.3.296 even though the loader reports 1.4.309.
  app.apiVersion = VK_API_VERSION_1_3;

  const char *layers[] = {kValidationLayer};
  // WSI extensions: SDL may not be initialized when the instance is created
  // (SetMode can precede video init), so always enable the Linux triple and
  // append whatever SDL reports, deduplicated. Unused ones are harmless.
  const char *extensions[16];
  uint32_t nExt = 0;
  const char *wsi[] = {
      VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_XLIB_KHR
      VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
      VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
      VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#endif
  };
  for (size_t i = 0; i < sizeof(wsi) / sizeof(wsi[0]) && nExt < 14; ++i) {
    extensions[nExt++] = wsi[i];
  }
  uint32_t sdlExtCount = 0;
  const char *const *sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
  if (sdlExts) {
    for (uint32_t i = 0; i < sdlExtCount && nExt < 15; ++i) {
      bool dup = false;
      for (uint32_t j = 0; j < nExt; ++j) {
        if (strcmp(extensions[j], sdlExts[i]) == 0) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        extensions[nExt++] = sdlExts[i];
      }
    }
  }
  extensions[nExt++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  ici.enabledLayerCount = 1;
  ici.ppEnabledLayerNames = layers;
  ici.enabledExtensionCount = nExt;
  ici.ppEnabledExtensionNames = extensions;
  VkResult result = vkCreateInstance(&ici, nullptr, &ctx->instance);
  if (result != VK_SUCCESS) {
    return VkFail("vkCreateInstance", "api=1.3, validation on", result);
  }

  auto vkCreateDebugUtilsMessengerEXT =
      reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(ctx->instance, "vkCreateDebugUtilsMessengerEXT"));
  if (!vkCreateDebugUtilsMessengerEXT) {
    Warning("[vulkan] vkGetInstanceProcAddr(vkCreateDebugUtilsMessengerEXT) -> null\n");
    VkContext_Destroy(ctx);
    return false;
  }
  VkDebugUtilsMessengerCreateInfoEXT mci{};
  mci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
  mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  mci.pfnUserCallback = DebugCallback;
  mci.pUserData = ctx;
  result = vkCreateDebugUtilsMessengerEXT(ctx->instance, &mci, nullptr, &ctx->messenger);
  if (result != VK_SUCCESS) {
    VkFail("vkCreateDebugUtilsMessengerEXT", "errors+warnings", result);
    VkContext_Destroy(ctx);
    return false;
  }

  uint32_t gpuCount = 0;
  result = vkEnumeratePhysicalDevices(ctx->instance, &gpuCount, nullptr);
  if (result != VK_SUCCESS || gpuCount == 0) {
    VkFail("vkEnumeratePhysicalDevices", "count", result);
    VkContext_Destroy(ctx);
    return false;
  }
  VkPhysicalDevice gpus[16];
  if (gpuCount > 16) {
    gpuCount = 16;
  }
  result = vkEnumeratePhysicalDevices(ctx->instance, &gpuCount, gpus);
  if (result != VK_SUCCESS) {
    VkFail("vkEnumeratePhysicalDevices", "list", result);
    VkContext_Destroy(ctx);
    return false;
  }

  // Pick: discrete GPU first, then anything with a graphics queue.
  // RADV POLARIS10 (RX 580) is the host target; llvmpipe is the fallback.
  uint32_t best = UINT32_MAX;
  bool bestDiscrete = false;
  for (uint32_t i = 0; i < gpuCount; ++i) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(gpus[i], &props);
    Msg("[vulkan] gpu%u: %s (api 0x%x, type %d)\n", i, props.deviceName,
        props.apiVersion, static_cast<int>(props.deviceType));
    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &qCount, nullptr);
    VkQueueFamilyProperties queues[16];
    if (qCount > 16) {
      qCount = 16;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &qCount, queues);
    bool hasGraphics = false;
    for (uint32_t q = 0; q < qCount; ++q) {
      if (queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        hasGraphics = true;
        break;
      }
    }
    if (!hasGraphics) {
      continue;
    }
    bool discrete =
        props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    if (best == UINT32_MAX || (discrete && !bestDiscrete)) {
      best = i;
      bestDiscrete = discrete;
    }
  }
  if (best == UINT32_MAX) {
    Warning("[vulkan] no physical device with a graphics queue\n");
    VkContext_Destroy(ctx);
    return false;
  }
  ctx->physical = gpus[best];
  VkPhysicalDeviceProperties props{};
  vkGetPhysicalDeviceProperties(ctx->physical, &props);
  ctx->apiVersion = props.apiVersion;
  strncpy(ctx->deviceName, props.deviceName, sizeof(ctx->deviceName) - 1);
  Msg("[vulkan] selected gpu%u: %s (api %u.%u.%u)\n", best, ctx->deviceName,
      VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
      VK_VERSION_PATCH(props.apiVersion));

  uint32_t qCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical, &qCount, nullptr);
  VkQueueFamilyProperties queues[16];
  if (qCount > 16) {
    qCount = 16;
  }
  vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical, &qCount, queues);
  for (uint32_t q = 0; q < qCount; ++q) {
    if (queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      ctx->graphicsFamily = q;
      break;
    }
  }
  if (ctx->graphicsFamily == UINT32_MAX) {
    Warning("[vulkan] selected device has no graphics queue family\n");
    VkContext_Destroy(ctx);
    return false;
  }

  // Robust buffer access: without it an out-of-range vertex or texel fetch is
  // undefined behaviour and the driver is free to fault - llvmpipe's
  // LLVM-generated AVX2 gather segfaults on one. Clamping is a trust boundary
  // we do not cut. Requested only when the device reports support.
  VkPhysicalDeviceFeatures supported{};
  vkGetPhysicalDeviceFeatures(ctx->physical, &supported);
  VkPhysicalDeviceFeatures features{};
  features.robustBufferAccess = supported.robustBufferAccess;
  Msg("[vulkan] robustBufferAccess: supported=%d enabled=%d\n",
      (int)supported.robustBufferAccess, (int)features.robustBufferAccess);

  float priority = 1.0f;
  VkDeviceQueueCreateInfo qci{};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = ctx->graphicsFamily;
  qci.queueCount = 1;
  qci.pQueuePriorities = &priority;
  // Swapchain support is required from S2.3 on (present path).
  const char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo dci{};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = 1;
  dci.ppEnabledExtensionNames = deviceExtensions;
  dci.pEnabledFeatures = &features;
  char famParam[32];
  snprintf(famParam, sizeof(famParam), "graphicsFamily=%u", ctx->graphicsFamily);
  result = vkCreateDevice(ctx->physical, &dci, nullptr, &ctx->device);
  if (result != VK_SUCCESS) {
    VkFail("vkCreateDevice", famParam, result);
    VkContext_Destroy(ctx);
    return false;
  }
  vkGetDeviceQueue(ctx->device, ctx->graphicsFamily, 0, &ctx->graphicsQueue);
  if (ctx->graphicsQueue == VK_NULL_HANDLE) {
    Warning("[vulkan] vkGetDeviceQueue(graphicsFamily=%u) -> null\n",
            ctx->graphicsFamily);
    VkContext_Destroy(ctx);
    return false;
  }
  Msg("[vulkan] S2.2 context ready: device + graphics queue (family %u)\n",
      ctx->graphicsFamily);
  return true;
}

void VkContext_Destroy(VkContext *ctx) {
  if (ctx->device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(ctx->device);
    SeVkTexCache_Shutdown();
    vkDestroyDevice(ctx->device, nullptr);
    ctx->device = VK_NULL_HANDLE;
  }
  ctx->graphicsQueue = VK_NULL_HANDLE;
  ctx->physical = VK_NULL_HANDLE;
  if (ctx->messenger != VK_NULL_HANDLE && ctx->instance != VK_NULL_HANDLE) {
    auto vkDestroyDebugUtilsMessengerEXT =
        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(
            ctx->instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (vkDestroyDebugUtilsMessengerEXT) {
      vkDestroyDebugUtilsMessengerEXT(ctx->instance, ctx->messenger, nullptr);
    }
    ctx->messenger = VK_NULL_HANDLE;
  }
  if (ctx->instance != VK_NULL_HANDLE) {
    vkDestroyInstance(ctx->instance, nullptr);
    ctx->instance = VK_NULL_HANDLE;
  }
}

}  // namespace se::vulkan
