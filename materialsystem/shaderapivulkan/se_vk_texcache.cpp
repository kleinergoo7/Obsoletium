// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_texcache.cpp -- Phase 2 native Vulkan backend, step S2.8-textures.
//
// Original code: handle table, format conversion (uncompressed to RGBA8,
// D3D block-compressed formats uploaded natively as BC), staging uploads.

#include <vulkan/vulkan.h>

#include "se_vk_bind.h"
#include "se_vk_texcache.h"

#include <cstring>
#include <cstdio>
#include <mutex>
#include <vector>

#include "se_vk_init.h"
#include "tier0/dbg.h"
#include "pixelwriter.h"
#include "shaderapi/ishaderdynamic.h"
#include "vtf/vtf.h"

namespace se::vulkan {
namespace {

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  int w = 0;
  int h = 0;
  int mips = 0;
  VkFormat format = VK_FORMAT_UNDEFINED;
  // True once an upload for this image has been flushed (layout is
  // SHADER_READ_ONLY); later uploads must transition from there, not
  // from UNDEFINED. Reset by DestroyImage's blank assignment.
  bool layoutDefined = false;
};

struct Entry {
  bool used = false;
  int w = 0;
  int h = 0;
  int mips = 1;
  int curFrame = 0;
  std::vector<Image> frames;
  // TexLock CPU mirror (RGBA8, entry dims); uploaded on Unlock.
  std::vector<uint8_t> lockMirror;
};

struct PendingUpload {
  // Resolved at Flush (frame indices, not pointers: the frames vector may
  // reallocate between queueing and flushing).
  intp handleIdx = -1;
  int frame = 0;
  int w = 0;
  int h = 0;
  int mips = 0;
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory stagingMem = VK_NULL_HANDLE;
  std::vector<VkBufferImageCopy> regions;
  // Image layout at queue time: a re-uploaded image is already
  // SHADER_READ_ONLY, a fresh one UNDEFINED. Recorded so Flush emits a
  // valid oldLayout (wrong oldLayout = validation error + garbage).
  VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct Garbage {
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory stagingMem = VK_NULL_HANDLE;
};

VkDevice g_device = VK_NULL_HANDLE;
VkPhysicalDevice g_physical = VK_NULL_HANDLE;
uint32_t g_family = UINT32_MAX;
VkQueue g_queue = VK_NULL_HANDLE;
std::mutex g_mutex;
std::vector<Entry> g_entries;
std::vector<PendingUpload> g_pending;
std::vector<Garbage> g_garbage;
VkSampler g_sampler = VK_NULL_HANDLE;
VkCommandPool g_initPool = VK_NULL_HANDLE;
// Standard solids + dummy, created at Init (1x1 RGBA8).
Image g_dummy;
Image g_white;
Image g_black;
Image g_grey;
Image g_greyAlphaZero;
Image g_flatNormal;
bool g_loggedStd[64] = {};
bool g_loggedFmt[static_cast<int>(NUM_IMAGE_FORMATS)] = {};
bool g_loggedFace = false;
bool g_loggedDepth = false;

uint32_t FindMemory(uint32_t typeBits, VkMemoryPropertyFlags flags) {
  VkPhysicalDeviceMemoryProperties props{};
  vkGetPhysicalDeviceMemoryProperties(g_physical, &props);
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) != 0u &&
        (props.memoryTypes[i].propertyFlags & flags) == flags) {
      return i;
    }
  }
  return UINT32_MAX;
}

bool CreateImage2D(int w, int h, int mips, VkFormat format, Image *out) {
  VkImageCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = format;
  ici.extent = {(uint32_t)w, (uint32_t)h, 1};
  ici.mipLevels = (uint32_t)mips;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(g_device, &ici, nullptr, &out->image) != VK_SUCCESS) {
    return false;
  }
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(g_device, out->image, &req);
  uint32_t memType = FindMemory(req.memoryTypeBits,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (memType == UINT32_MAX) {
    vkDestroyImage(g_device, out->image, nullptr);
    out->image = VK_NULL_HANDLE;
    return false;
  }
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = memType;
  if (vkAllocateMemory(g_device, &mai, nullptr, &out->mem) != VK_SUCCESS ||
      vkBindImageMemory(g_device, out->image, out->mem, 0) != VK_SUCCESS) {
    if (out->mem != VK_NULL_HANDLE) {
      vkFreeMemory(g_device, out->mem, nullptr);
      out->mem = VK_NULL_HANDLE;
    }
    vkDestroyImage(g_device, out->image, nullptr);
    out->image = VK_NULL_HANDLE;
    return false;
  }
  VkImageViewCreateInfo ivci{};
  ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  ivci.image = out->image;
  ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  ivci.format = format;
  ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  ivci.subresourceRange.levelCount = (uint32_t)mips;
  ivci.subresourceRange.layerCount = 1;
  if (vkCreateImageView(g_device, &ivci, nullptr, &out->view) != VK_SUCCESS) {
    vkDestroyImage(g_device, out->image, nullptr);
    vkFreeMemory(g_device, out->mem, nullptr);
    *out = Image{};
    return false;
  }
  out->w = w;
  out->h = h;
  out->mips = mips;
  out->format = format;
  return true;
}

void DestroyImage(Image *img) {
  if (img->view != VK_NULL_HANDLE) {
    vkDestroyImageView(g_device, img->view, nullptr);
  }
  if (img->image != VK_NULL_HANDLE) {
    vkDestroyImage(g_device, img->image, nullptr);
  }
  if (img->mem != VK_NULL_HANDLE) {
    vkFreeMemory(g_device, img->mem, nullptr);
  }
  *img = Image{};
}

// Synchronous upload used at Init (no frames in flight): staging copy +
// layout transitions on a transient command buffer, fenced.
bool UploadNow(Image *img, const void *pixels, size_t size,
               const std::vector<VkBufferImageCopy> &regions) {
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkBufferCreateInfo bci{};
  bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bci.size = size;
  bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(g_device, &bci, nullptr, &staging) != VK_SUCCESS) {
    return false;
  }
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(g_device, staging, &req);
  uint32_t mt = FindMemory(req.memoryTypeBits,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = mt;
  void *mapped = nullptr;
  bool ok = mt != UINT32_MAX &&
            vkAllocateMemory(g_device, &mai, nullptr, &mem) == VK_SUCCESS &&
            vkMapMemory(g_device, mem, 0, size, 0, &mapped) == VK_SUCCESS;
  if (ok) {
    memcpy(mapped, pixels, size);
    vkUnmapMemory(g_device, mem);
    ok = vkBindBufferMemory(g_device, staging, mem, 0) == VK_SUCCESS;
  }
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  if (ok) {
    VkCommandBufferAllocateInfo aci{};
    aci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    aci.commandPool = g_initPool;
    aci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    aci.commandBufferCount = 1;
    ok = vkAllocateCommandBuffers(g_device, &aci, &cmd) == VK_SUCCESS;
  }
  VkFence fence = VK_NULL_HANDLE;
  if (ok) {
    VkCommandBufferBeginInfo bbi{};
    bbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bbi);
    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = img->image;
    toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toDst.subresourceRange.levelCount = (uint32_t)img->mips;
    toDst.subresourceRange.layerCount = 1;
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toDst);
    vkCmdCopyBufferToImage(cmd, staging, img->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           (uint32_t)regions.size(), regions.data());
    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &toRead);
    vkEndCommandBuffer(cmd);
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    ok = vkCreateFence(g_device, &fci, nullptr, &fence) == VK_SUCCESS;
  }
  if (ok) {
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    ok = vkQueueSubmit(g_queue, 1, &si, fence) == VK_SUCCESS;
    if (ok) {
      VkWaitFence(g_device, fence, "texture cache upload");
    }
  }
  if (cmd != VK_NULL_HANDLE) {
    vkFreeCommandBuffers(g_device, g_initPool, 1, &cmd);
  }
  if (fence != VK_NULL_HANDLE) {
    vkDestroyFence(g_device, fence, nullptr);
  }
  vkDestroyBuffer(g_device, staging, nullptr);
  if (mem != VK_NULL_HANDLE) {
    vkFreeMemory(g_device, mem, nullptr);
  }
  return ok;
}

bool UploadSolid(Image *img, uint8_t r, uint8_t gg, uint8_t b, uint8_t a) {
  if (!CreateImage2D(1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, img)) {
    return false;
  }
  uint8_t px[4] = {r, gg, b, a};
  VkBufferImageCopy region{};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = 1;
  region.imageExtent.height = 1;
  region.imageExtent.depth = 1;
  std::vector<VkBufferImageCopy> regions{region};
  if (!UploadNow(img, px, sizeof(px), regions)) {
    DestroyImage(img);
    return false;
  }
  return true;
}

// --- format mapping -------------------------------------------------------
// Uncompressed formats convert to RGBA8 on the CPU; D3D block-compressed
// formats upload natively (BC1/BC2/BC3/BC4/BC5), preserving mip tails.
struct FmtInfo {
  VkFormat vkfmt = VK_FORMAT_UNDEFINED;
  bool compressed = false;
  int blockBytes = 0;  // per 4x4 block when compressed
};

FmtInfo MapFormat(ImageFormat fmt) {
  FmtInfo fi;
  switch (fmt) {
    case IMAGE_FORMAT_DXT1:
    case IMAGE_FORMAT_DXT1_ONEBITALPHA:
    case IMAGE_FORMAT_DXT1_RUNTIME:
      fi.vkfmt = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
      fi.compressed = true;
      fi.blockBytes = 8;
      return fi;
    case IMAGE_FORMAT_DXT3:
      fi.vkfmt = VK_FORMAT_BC2_UNORM_BLOCK;
      fi.compressed = true;
      fi.blockBytes = 16;
      return fi;
    case IMAGE_FORMAT_DXT5:
    case IMAGE_FORMAT_DXT5_RUNTIME:
      fi.vkfmt = VK_FORMAT_BC3_UNORM_BLOCK;
      fi.compressed = true;
      fi.blockBytes = 16;
      return fi;
    case IMAGE_FORMAT_ATI1N:
      fi.vkfmt = VK_FORMAT_BC4_UNORM_BLOCK;
      fi.compressed = true;
      fi.blockBytes = 8;
      return fi;
    case IMAGE_FORMAT_ATI2N:
      fi.vkfmt = VK_FORMAT_BC5_UNORM_BLOCK;
      fi.compressed = true;
      fi.blockBytes = 16;
      return fi;
    default:
      break;
  }
  fi.vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
  fi.compressed = false;
  return fi;
}

// IEEE-754 half to float (for 16F textures: gamma LUTs, HDR).
float HalfToFloat(uint16_t h) {
  uint32_t sign = (uint32_t)(h & 0x8000) << 16;
  uint32_t exp = (h >> 10) & 0x1f;
  uint32_t mant = h & 0x3ff;
  uint32_t f;
  if (exp == 0) {
    if (mant == 0) {
      f = sign;
    } else {
      // Subnormal: normalize.
      exp = 1;
      while ((mant & 0x400) == 0) {
        mant <<= 1;
        exp--;
      }
      mant &= 0x3ff;
      f = sign | ((uint32_t)(exp + 112) << 23) | (mant << 13);
    }
  } else if (exp == 31) {
    f = sign | 0x7f800000 | (mant << 13);
  } else {
    f = sign | ((uint32_t)(exp + 112) << 23) | (mant << 13);
  }
  float out = 0.0f;
  memcpy(&out, &f, 4);
  return out;
}

uint8_t FloatTo8(float f) {
  if (!(f > 0.0f)) {
    return 0;
  }
  if (f >= 1.0f) {
    return 255;
  }
  return (uint8_t)(f * 255.0f);
}

// Converts one uncompressed source row to RGBA8. Returns false when the
// format needs the magenta fallback.
bool ConvertRow(ImageFormat fmt, const uint8_t *src, uint8_t *dst, int w) {
  switch (fmt) {
    case IMAGE_FORMAT_RGBA8888:
    case IMAGE_FORMAT_UVWQ8888:
      memcpy(dst, src, (size_t)w * 4);
      return true;
    case IMAGE_FORMAT_BGRA8888:
      for (int i = 0; i < w; ++i) {
        dst[4 * i + 0] = src[4 * i + 2];
        dst[4 * i + 1] = src[4 * i + 1];
        dst[4 * i + 2] = src[4 * i + 0];
        dst[4 * i + 3] = src[4 * i + 3];
      }
      return true;
    case IMAGE_FORMAT_ABGR8888:
      for (int i = 0; i < w; ++i) {
        dst[4 * i + 0] = src[4 * i + 3];
        dst[4 * i + 1] = src[4 * i + 2];
        dst[4 * i + 2] = src[4 * i + 1];
        dst[4 * i + 3] = src[4 * i + 0];
      }
      return true;
    case IMAGE_FORMAT_ARGB8888:
      for (int i = 0; i < w; ++i) {
        dst[4 * i + 0] = src[4 * i + 1];
        dst[4 * i + 1] = src[4 * i + 2];
        dst[4 * i + 2] = src[4 * i + 3];
        dst[4 * i + 3] = src[4 * i + 0];
      }
      return true;
    case IMAGE_FORMAT_BGRX8888:
      for (int i = 0; i < w; ++i) {
        dst[4 * i + 0] = src[4 * i + 2];
        dst[4 * i + 1] = src[4 * i + 1];
        dst[4 * i + 2] = src[4 * i + 0];
        dst[4 * i + 3] = 255;
      }
      return true;
    case IMAGE_FORMAT_RGB888:
    case IMAGE_FORMAT_RGB888_BLUESCREEN:
      for (int i = 0; i < w; ++i) {
        dst[4 * i + 0] = src[3 * i + 0];
        dst[4 * i + 1] = src[3 * i + 1];
        dst[4 * i + 2] = src[3 * i + 2];
        dst[4 * i + 3] = 255;
      }
      return true;
    case IMAGE_FORMAT_BGR888:
    case IMAGE_FORMAT_BGR888_BLUESCREEN:
      for (int i = 0; i < w; ++i) {
        dst[4 * i + 0] = src[3 * i + 2];
        dst[4 * i + 1] = src[3 * i + 1];
        dst[4 * i + 2] = src[3 * i + 0];
        dst[4 * i + 3] = 255;
      }
      return true;
    case IMAGE_FORMAT_RGB565:
      for (int i = 0; i < w; ++i) {
        uint16_t v = (uint16_t)(src[2 * i] | (src[2 * i + 1] << 8));
        dst[4 * i + 0] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
        dst[4 * i + 1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
        dst[4 * i + 2] = (uint8_t)((v & 31) * 255 / 31);
        dst[4 * i + 3] = 255;
      }
      return true;
    case IMAGE_FORMAT_BGR565:
      for (int i = 0; i < w; ++i) {
        uint16_t v = (uint16_t)(src[2 * i] | (src[2 * i + 1] << 8));
        dst[4 * i + 0] = (uint8_t)((v & 31) * 255 / 31);
        dst[4 * i + 1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
        dst[4 * i + 2] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
        dst[4 * i + 3] = 255;
      }
      return true;
    case IMAGE_FORMAT_BGRA4444:
      for (int i = 0; i < w; ++i) {
        uint16_t v = (uint16_t)(src[2 * i] | (src[2 * i + 1] << 8));
        dst[4 * i + 0] = (uint8_t)(((v >> 8) & 15) * 255 / 15);
        dst[4 * i + 1] = (uint8_t)(((v >> 4) & 15) * 255 / 15);
        dst[4 * i + 2] = (uint8_t)((v & 15) * 255 / 15);
        dst[4 * i + 3] = (uint8_t)(((v >> 12) & 15) * 255 / 15);
      }
      return true;
    case IMAGE_FORMAT_BGRA5551:
    case IMAGE_FORMAT_BGRX5551:
      for (int i = 0; i < w; ++i) {
        uint16_t v = (uint16_t)(src[2 * i] | (src[2 * i + 1] << 8));
        dst[4 * i + 0] = (uint8_t)(((v >> 10) & 31) * 255 / 31);
        dst[4 * i + 1] = (uint8_t)(((v >> 5) & 31) * 255 / 31);
        dst[4 * i + 2] = (uint8_t)((v & 31) * 255 / 31);
        dst[4 * i + 3] = 255;
      }
      return true;
    case IMAGE_FORMAT_I8:
      for (int i = 0; i < w; ++i) {
        dst[4 * i + 0] = dst[4 * i + 1] = dst[4 * i + 2] = src[i];
        dst[4 * i + 3] = 255;
      }
      return true;
    case IMAGE_FORMAT_IA88:
      for (int i = 0; i < w; ++i) {
        dst[4 * i + 0] = dst[4 * i + 1] = dst[4 * i + 2] = src[2 * i];
        dst[4 * i + 3] = src[2 * i + 1];
      }
      return true;
    case IMAGE_FORMAT_A8:
      for (int i = 0; i < w; ++i) {
        dst[4 * i + 0] = dst[4 * i + 1] = dst[4 * i + 2] = 255;
        dst[4 * i + 3] = src[i];
      }
      return true;
    case IMAGE_FORMAT_UV88:
      for (int i = 0; i < w; ++i) {
        dst[4 * i + 0] = src[2 * i];
        dst[4 * i + 1] = src[2 * i + 1];
        dst[4 * i + 2] = 128;
        dst[4 * i + 3] = 255;
      }
      return true;
    case IMAGE_FORMAT_UVLX8888:
      for (int i = 0; i < w; ++i) {
        dst[4 * i + 0] = src[4 * i];
        dst[4 * i + 1] = src[4 * i + 1];
        dst[4 * i + 2] = 128;
        dst[4 * i + 3] = 255;
      }
      return true;
    case IMAGE_FORMAT_RGBA16161616:
      for (int i = 0; i < w; ++i) {
        for (int c = 0; c < 4; ++c) {
          uint16_t v = (uint16_t)(src[8 * i + 2 * c] |
                                  (src[8 * i + 2 * c + 1] << 8));
          dst[4 * i + c] = (uint8_t)(v >> 8);
        }
      }
      return true;
    case IMAGE_FORMAT_RGBA16161616F:
      for (int i = 0; i < w; ++i) {
        for (int c = 0; c < 4; ++c) {
          uint16_t h = (uint16_t)(src[8 * i + 2 * c] |
                                  (src[8 * i + 2 * c + 1] << 8));
          dst[4 * i + c] = FloatTo8(HalfToFloat(h));
        }
      }
      return true;
    case IMAGE_FORMAT_R32F:
      for (int i = 0; i < w; ++i) {
        float f = 0.0f;
        memcpy(&f, src + 4 * i, 4);
        uint8_t b = FloatTo8(f);
        dst[4 * i + 0] = dst[4 * i + 1] = dst[4 * i + 2] = b;
        dst[4 * i + 3] = 255;
      }
      return true;
    case IMAGE_FORMAT_RGB323232F:
      for (int i = 0; i < w; ++i) {
        for (int c = 0; c < 3; ++c) {
          float f = 0.0f;
          memcpy(&f, src + 12 * i + 4 * c, 4);
          dst[4 * i + c] = FloatTo8(f);
        }
        dst[4 * i + 3] = 255;
      }
      return true;
    case IMAGE_FORMAT_RGBA32323232F:
      for (int i = 0; i < w; ++i) {
        for (int c = 0; c < 4; ++c) {
          float f = 0.0f;
          memcpy(&f, src + 16 * i + 4 * c, 4);
          dst[4 * i + c] = FloatTo8(f);
        }
      }
      return true;
    default:
      return false;
  }
}

void LogFormatOnce(ImageFormat fmt) {
  int idx = static_cast<int>(fmt);
  if (idx >= 0 && idx < static_cast<int>(NUM_IMAGE_FORMATS)) {
    if (g_loggedFmt[idx]) {
      return;
    }
    g_loggedFmt[idx] = true;
  }
  // stderr (unbuffered): engine Warning() gets lost to launcher buffering.
  // Permanent alarm: this format renders magenta until converted.
  fprintf(stderr, "[vulkan] texture format %d not converted (magenta)\n", idx);
}

// Deferred destroy queue for deleted textures (see SeVkTexCache_Delete).
// Holds whole Image records so the destroy path is DestroyImage's, not a
// second copy of it.
std::vector<Image> g_trash;

}  // namespace

bool g_loggedRawMismatch = false;

// Pre-device raw uploads: the engine downloads standard textures before
// SetMode exists, so there is no queue yet. Converted bytes are stashed
// (bounded) and drained by Init via UploadNow.
struct PreRaw {
  intp handleIdx = -1;
  int level = 0;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int ew = 0;
  int eh = 0;
  int emips = 0;
  VkFormat vkfmt = VK_FORMAT_UNDEFINED;
  size_t rowBytes = 0;
  std::vector<uint8_t> bytes;
};
std::vector<PreRaw> g_preDeviceRaw;
bool g_loggedPreDrop = false;

void PackRows(uint8_t *dst, const uint8_t *src, size_t sRow, size_t rowBytes,
              size_t rows) {
  if (sRow == rowBytes) {
    memcpy(dst, src, sRow * rows);
  } else {
    for (size_t r = 0; r < rows; ++r) {
      memcpy(dst + r * rowBytes, src + r * sRow, rowBytes);
    }
  }
}

void StashPreDeviceRaw(intp idx, int level, int x, int y, int w, int h,
                       const Entry &e, VkFormat vkfmt, const uint8_t *src,
                       size_t sRow, size_t rowBytes, size_t rows,
                       size_t total) {
  if (g_preDeviceRaw.size() >= 256) {
    if (!g_loggedPreDrop) {
      g_loggedPreDrop = true;
      Warning("[vulkan] texture: pre-device stash full, dropping (handle "
              "%d)\n",
              (int)idx);
    }
    return;
  }
  PreRaw p;
  p.handleIdx = idx;
  p.level = level;
  p.x = x;
  p.y = y;
  p.w = w;
  p.h = h;
  p.ew = e.w;
  p.eh = e.h;
  p.emips = e.mips;
  p.vkfmt = vkfmt;
  p.rowBytes = rowBytes;
  p.bytes.resize(total);
  PackRows(p.bytes.data(), src, sRow, rowBytes, rows);
  g_preDeviceRaw.push_back(std::move(p));
}
void DrainPreDeviceRaw() {
  if (g_preDeviceRaw.empty()) {
    return;
  }
  for (auto &p : g_preDeviceRaw) {
    if (p.handleIdx < 0 || (size_t)p.handleIdx >= g_entries.size() ||
        !g_entries[p.handleIdx].used) {
      continue;
    }
    Entry &e = g_entries[p.handleIdx];
    // Entry recycled since stash: geometry must still match.
    if (e.w != p.ew || e.h != p.eh || e.mips != p.emips) {
      Warning("[vulkan] texture: pre-device stash stale (handle %d), "
              "dropping\n",
              (int)p.handleIdx);
      continue;
    }
    if (e.frames.empty()) {
      e.frames.resize(1);
    }
    Image &img = e.frames[0];
    if (img.view == VK_NULL_HANDLE || img.w != e.w || img.h != e.h ||
        img.mips != e.mips || img.format != p.vkfmt) {
      DestroyImage(&img);
      if (!CreateImage2D(e.w, e.h, e.mips, p.vkfmt, &img)) {
        Warning("[vulkan] texture: image creation failed (%dx%d mips=%d)\n",
                e.w, e.h, e.mips);
        continue;
      }
    }
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = (uint32_t)p.level;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = p.x;
    region.imageOffset.y = p.y;
    region.imageExtent.width = (uint32_t)p.w;
    region.imageExtent.height = (uint32_t)p.h;
    region.imageExtent.depth = 1;
    std::vector<VkBufferImageCopy> regions{region};
    if (UploadNow(&img, p.bytes.data(), p.bytes.size(), regions)) {
      img.layoutDefined = true;
      e.curFrame = 0;
    }
  }
  g_preDeviceRaw.clear();
}

void SeVkTexCache_Init(VkDevice device, VkPhysicalDevice physical,
                       uint32_t queueFamily, VkQueue queue) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_device != VK_NULL_HANDLE) {
    return;
  }
  g_device = device;
  g_physical = physical;
  g_family = queueFamily;
  g_queue = queue;
  VkSamplerCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sci.magFilter = VK_FILTER_LINEAR;
  sci.minFilter = VK_FILTER_LINEAR;
  sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sci.maxLod = 0.0f;
  if (vkCreateSampler(device, &sci, nullptr, &g_sampler) != VK_SUCCESS) {
    Warning("[vulkan] texture: sampler creation failed\n");
    return;
  }
  VkCommandPoolCreateInfo pci{};
  pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pci.queueFamilyIndex = queueFamily;
  if (vkCreateCommandPool(device, &pci, nullptr, &g_initPool) !=
      VK_SUCCESS) {
    Warning("[vulkan] texture: init pool creation failed\n");
    return;
  }
  // 1x1 solids + magenta dummy (unhandled formats).
  UploadSolid(&g_dummy, 255, 0, 255, 255);
  UploadSolid(&g_white, 255, 255, 255, 255);
  UploadSolid(&g_black, 0, 0, 0, 255);
  UploadSolid(&g_grey, 128, 128, 128, 255);
  UploadSolid(&g_greyAlphaZero, 128, 128, 128, 0);
  UploadSolid(&g_flatNormal, 128, 128, 255, 255);
  DrainPreDeviceRaw();
}

ShaderAPITextureHandle_t SeVkTexCache_Create(int width, int height,
                                             int numMipLevels, int numCopies) {
  (void)numCopies;
  std::lock_guard<std::mutex> lock(g_mutex);
  for (size_t i = 0; i < g_entries.size(); ++i) {
    if (!g_entries[i].used) {
      g_entries[i].used = true;
      g_entries[i].w = width;
      g_entries[i].h = height;
      g_entries[i].mips = numMipLevels > 0 ? numMipLevels : 1;
      g_entries[i].curFrame = 0;
      return (ShaderAPITextureHandle_t)i;
    }
  }
  Entry e;
  e.used = true;
  e.w = width;
  e.h = height;
  e.mips = numMipLevels > 0 ? numMipLevels : 1;
  g_entries.push_back(std::move(e));
  return (ShaderAPITextureHandle_t)(g_entries.size() - 1);
}

void SeVkTexCache_Delete(ShaderAPITextureHandle_t handle) {
  std::lock_guard<std::mutex> lock(g_mutex);
  intp idx = (intp)handle;
  if (idx < 0 || (size_t)idx >= g_entries.size() || !g_entries[idx].used) {
    return;
  }
  for (auto &f : g_entries[idx].frames) {
    // Park instead of destroying: the shared descriptor set may still
    // reference these views and a submit can still be in flight. Valid
    // log evidence: VUID-vkDestroyImageView-imageView-01026 followed by
    // VUID-vkCmdDrawIndexed-None-08114, then lavapipe faults inside the
    // JIT'd fragment shader sampling the freed view.
    g_trash.push_back(f);
    f = Image{};
  }
  g_entries[idx] = Entry{};
  // Present drain skips while the set still references parked views;
  // force the rewrite at the next draw record.
  SeVkBind_Global().texDirty = true;
}

void SeVkTexCache_DrainTrash(bool force) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_trash.empty()) {
    return;
  }
  if (!force && SeVkBind_Global().texDirty) {
    return;  // shared set still references parked views until rewritten
  }
  for (Image &t : g_trash) {
    DestroyImage(&t);
  }
  g_trash.clear();
}

void SeVkTexCache_Shutdown() {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (Image &t : g_trash) {
    DestroyImage(&t);
  }
  g_trash.clear();
  for (Entry &e : g_entries) {
    for (Image &f : e.frames) {
      DestroyImage(&f);
    }
  }
  g_entries.clear();
  // The device is about to go away: drop the handle so a second init does
  // not silently keep using a destroyed VkDevice.
  g_device = VK_NULL_HANDLE;
}

bool SeVkTexCache_Valid(ShaderAPITextureHandle_t handle) {
  std::lock_guard<std::mutex> lock(g_mutex);
  intp idx = (intp)handle;
  return idx >= 0 && (size_t)idx < g_entries.size() && g_entries[idx].used;
}

void SeVkTexCache_UploadVTF(ShaderAPITextureHandle_t handle, IVTFTexture *vtf,
                            int frame) {
  if (!vtf || g_device == VK_NULL_HANDLE) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  intp idx = (intp)handle;
  if (idx < 0 || (size_t)idx >= g_entries.size() || !g_entries[idx].used) {
    return;
  }
  Entry &e = g_entries[idx];
  ImageFormat fmt = vtf->Format();
  FmtInfo fi = MapFormat(fmt);
  if (fi.vkfmt == VK_FORMAT_UNDEFINED) {
    LogFormatOnce(fmt);
    return;
  }
  int faces = vtf->FaceCount();
  if (faces > 1 && !g_loggedFace) {
    g_loggedFace = true;
    Warning("[vulkan] texture: cubemap/volume approximated by face 0 "
            "(handle %d)\n",
            (int)idx);
  }
  int depth = 0;
  {
    int dw = 0, dh = 0, dd = 0;
    vtf->ComputeMipLevelDimensions(0, &dw, &dh, &dd);
    depth = dd;
  }
  if (depth > 1 && !g_loggedDepth) {
    g_loggedDepth = true;
    Warning("[vulkan] texture: volume texture approximated by slice 0 "
            "(handle %d)\n",
            (int)idx);
  }
  int mips = vtf->MipCount();
  if (mips <= 0) {
    return;
  }
  int w0 = 0, h0 = 0, d0 = 0;
  vtf->ComputeMipLevelDimensions(0, &w0, &h0, &d0);
  if (w0 <= 0 || h0 <= 0) {
    return;
  }
  if (frame < 0) {
    frame = 0;
  }
  if ((size_t)frame >= e.frames.size()) {
    if (frame >= 128) {
      Warning("[vulkan] texture: frame %d exceeds cap (handle %d)\n", frame,
              (int)idx);
      return;
    }
    e.frames.resize(frame + 1);
  }
  Image &img = e.frames[frame];
  if (img.view == VK_NULL_HANDLE || img.w != w0 || img.h != h0 ||
      img.mips != mips || img.format != fi.vkfmt) {
    DestroyImage(&img);
    if (!CreateImage2D(w0, h0, mips, fi.vkfmt, &img)) {
      Warning("[vulkan] texture: image creation failed (%dx%d mips=%d)\n",
              w0, h0, mips);
      return;
    }
  }
  // Stage all mip levels, then queue one copy.
  size_t total = 0;
  struct Level {
    int w, h;
    size_t size;
    std::vector<uint8_t> pixels;  // converted (uncompressed) or empty
    const uint8_t *src;           // compressed passthrough pointer
    size_t srcRow;
  };
  std::vector<Level> levels;
  bool fallback = false;
  for (int m = 0; m < mips; ++m) {
    int w = 0, h = 0, d = 0;
    vtf->ComputeMipLevelDimensions(m, &w, &h, &d);
    if (w <= 0 || h <= 0) {
      break;
    }
    Level lv;
    lv.w = w;
    lv.h = h;
    unsigned char *bits = vtf->ImageData(frame, 0, m);
    if (!bits) {
      break;
    }
    size_t row = (size_t)vtf->RowSizeInBytes(m);
    if (fi.compressed) {
      int blockRows = (h + 3) / 4;
      lv.size = row * (size_t)blockRows;
      lv.src = bits;
      lv.srcRow = row;
    } else {
      lv.size = (size_t)w * h * 4;
      lv.pixels.resize(lv.size);
      // Magenta fallback rows when the format has no converter.
      bool ok = true;
      for (int y = 0; y < h; ++y) {
        if (!ConvertRow(fmt, bits + (size_t)y * row,
                        lv.pixels.data() + (size_t)y * w * 4, w)) {
          ok = false;
          break;
        }
      }
      if (!ok) {
        LogFormatOnce(fmt);
        fallback = true;
        break;
      }
      lv.src = lv.pixels.data();
      lv.srcRow = (size_t)w * 4;
    }
    total += lv.size;
    levels.push_back(std::move(lv));
  }
  if (levels.empty()) {
    return;
  }
  if (fallback) {
    // 1x1 magenta: keep dims, single-level staging of 4 bytes.
    levels.clear();
    Level lv;
    lv.w = 1;
    lv.h = 1;
    lv.size = 4;
    lv.pixels = {255, 0, 255, 255};
    lv.src = lv.pixels.data();
    lv.srcRow = 4;
    levels.push_back(std::move(lv));
    total = 4;
  }
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkBufferCreateInfo bci{};
  bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bci.size = total;
  bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(g_device, &bci, nullptr, &staging) != VK_SUCCESS) {
    return;
  }
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(g_device, staging, &req);
  uint32_t mt = FindMemory(req.memoryTypeBits,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = mt;
  void *mapped = nullptr;
  bool ok = mt != UINT32_MAX &&
            vkAllocateMemory(g_device, &mai, nullptr, &mem) == VK_SUCCESS &&
            vkMapMemory(g_device, mem, 0, total, 0, &mapped) == VK_SUCCESS;
  PendingUpload up;
  if (ok) {
    size_t off = 0;
    for (size_t i = 0; i < levels.size(); ++i) {
      Level &lv = levels[i];
      uint8_t *dst = (uint8_t *)mapped + off;
      if (fi.compressed || lv.srcRow == (size_t)lv.w * 4) {
        // Tightly packed (or block) rows: single copy.
        size_t rows = fi.compressed ? (size_t)(lv.h + 3) / 4 : (size_t)lv.h;
        memcpy(dst, lv.src, lv.srcRow * rows);
      } else {
        for (int y = 0; y < lv.h; ++y) {
          memcpy(dst + (size_t)y * lv.w * 4,
                 lv.src + (size_t)y * lv.srcRow, (size_t)lv.w * 4);
        }
      }
      VkBufferImageCopy region{};
      region.bufferOffset = off;
      region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.mipLevel = (uint32_t)i;
      region.imageSubresource.layerCount = 1;
      region.imageExtent.width = (uint32_t)lv.w;
      region.imageExtent.height = (uint32_t)lv.h;
      region.imageExtent.depth = 1;
      up.regions.push_back(region);
      off += lv.size;
    }
    vkUnmapMemory(g_device, mem);
    ok = vkBindBufferMemory(g_device, staging, mem, 0) == VK_SUCCESS;
  }
  if (!ok) {
    if (mem != VK_NULL_HANDLE) {
      vkFreeMemory(g_device, mem, nullptr);
    }
    vkDestroyBuffer(g_device, staging, nullptr);
    return;
  }
  up.handleIdx = idx;
  up.frame = frame;
  up.w = w0;
  up.h = h0;
  up.mips = mips;
  up.staging = staging;
  up.stagingMem = mem;
  up.oldLayout = img.layoutDefined
                     ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                     : VK_IMAGE_LAYOUT_UNDEFINED;
  img.layoutDefined = true;
  g_pending.push_back(std::move(up));
  e.curFrame = frame;
}

// Bytes per pixel for packed source rows (TexImage2D path); 0 when the
// format is block-compressed (see MapFormat) or unknown.
int SrcBpp(ImageFormat fmt) {
  switch (fmt) {
    case IMAGE_FORMAT_RGBA8888:
    case IMAGE_FORMAT_BGRA8888:
    case IMAGE_FORMAT_ABGR8888:
    case IMAGE_FORMAT_ARGB8888:
    case IMAGE_FORMAT_BGRX8888:
    case IMAGE_FORMAT_UVWQ8888:
    case IMAGE_FORMAT_UVLX8888:
    case IMAGE_FORMAT_R32F:
      return 4;
    case IMAGE_FORMAT_RGB888:
    case IMAGE_FORMAT_BGR888:
    case IMAGE_FORMAT_RGB888_BLUESCREEN:
    case IMAGE_FORMAT_BGR888_BLUESCREEN:
      return 3;
    case IMAGE_FORMAT_RGB565:
    case IMAGE_FORMAT_BGR565:
    case IMAGE_FORMAT_BGRA4444:
    case IMAGE_FORMAT_BGRA5551:
    case IMAGE_FORMAT_BGRX5551:
    case IMAGE_FORMAT_IA88:
    case IMAGE_FORMAT_UV88:
      return 2;
    case IMAGE_FORMAT_I8:
    case IMAGE_FORMAT_A8:
      return 1;
    case IMAGE_FORMAT_RGBA16161616:
    case IMAGE_FORMAT_RGBA16161616F:
      return 8;
    case IMAGE_FORMAT_RGB323232F:
      return 12;
    case IMAGE_FORMAT_RGBA32323232F:
      return 16;
    default:
      return 0;
  }
}


void SeVkTexCache_UploadRaw(ShaderAPITextureHandle_t handle, int level,
                            int x, int y, int w, int h, ImageFormat dstFmt,
                            ImageFormat srcFmt, const void *srcData,
                            int srcStride) {
  if (!srcData) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  intp idx = (intp)handle;
  if (idx < 0 || (size_t)idx >= g_entries.size() || !g_entries[idx].used) {
    return;
  }
  Entry &e = g_entries[idx];
  if (e.w <= 0 || e.h <= 0 || e.mips <= 0) {
    return;
  }
  if (level < 0 || level >= e.mips) {
    Warning("[vulkan] texture: level %d out of range (mips=%d, handle %d)\n",
            level, e.mips, (int)idx);
    return;
  }
  // Cube/volume slices approximate to face/slice 0 (same scope as VTF path).
  int ew = e.w >> level;
  int eh = e.h >> level;
  if (ew <= 0) {
    ew = 1;
  }
  if (eh <= 0) {
    eh = 1;
  }
  if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > ew || y + h > eh) {
    if (!g_loggedRawMismatch) {
      g_loggedRawMismatch = true;
      Warning("[vulkan] texture: raw upload rect (%d,%d %dx%d level %d) "
              "outside %dx%d (handle %d)\n",
              x, y, w, h, level, e.w, e.h, (int)idx);
    }
    return;
  }
  FmtInfo fi = MapFormat(dstFmt);
  if (fi.vkfmt == VK_FORMAT_UNDEFINED) {
    LogFormatOnce(dstFmt);
    return;
  }
  const uint8_t *src = (const uint8_t *)srcData;
  std::vector<uint8_t> conv;
  size_t rowBytes = 0;  // bytes per row in staging
  size_t sRow = 0;      // bytes per row in source
  if (fi.compressed) {
    if (srcFmt != dstFmt) {
      Warning("[vulkan] texture: cannot convert %d to compressed %d "
              "(handle %d)\n",
              (int)srcFmt, (int)dstFmt, (int)idx);
      return;
    }
    rowBytes = (size_t)((w + 3) / 4) * (size_t)fi.blockBytes;
    sRow = srcStride > 0 ? (size_t)srcStride : rowBytes;
  } else {
    int bpp = SrcBpp(srcFmt);
    if (bpp <= 0) {
      LogFormatOnce(srcFmt);
      return;
    }
    sRow = srcStride > 0 ? (size_t)srcStride : (size_t)w * (size_t)bpp;
    if (sRow < (size_t)w * (size_t)bpp) {
      Warning("[vulkan] texture: stride %d too small for width %d (handle "
              "%d)\n",
              srcStride, w, (int)idx);
      return;
    }
    conv.resize((size_t)w * (size_t)h * 4);
    for (int r = 0; r < h; ++r) {
      if (!ConvertRow(srcFmt, src + (size_t)r * sRow,
                      conv.data() + (size_t)r * w * 4, w)) {
        LogFormatOnce(srcFmt);
        return;
      }
    }
    src = conv.data();
    sRow = (size_t)w * 4;
    rowBytes = sRow;
  }
  size_t rows = fi.compressed ? (size_t)(h + 3) / 4 : (size_t)h;
  size_t total = fi.compressed ? sRow * rows : rowBytes * (size_t)h;
  if (g_device == VK_NULL_HANDLE) {
    // Device not up yet (engine downloads standard textures before
    // SetMode): stash converted bytes, drained by Init. Bounded; beyond
    // the cap the texture stays dummy (loud).
    // shortcut: VTF downloads get no stash; the async loader re-issues
    // them post-device (observed: 40+ uploads land). Upgrade: same stash
    // for VTF if a visible texture ever stays dummy.
    StashPreDeviceRaw(idx, level, x, y, w, h, e, fi.vkfmt, src, sRow,
                      rowBytes, rows, total);
    return;
  }
  if (e.frames.empty()) {
    e.frames.resize(1);
  }
  Image &img = e.frames[0];
  if (img.view == VK_NULL_HANDLE || img.w != e.w || img.h != e.h ||
      img.mips != e.mips || img.format != fi.vkfmt) {
    DestroyImage(&img);
    if (!CreateImage2D(e.w, e.h, e.mips, fi.vkfmt, &img)) {
      Warning("[vulkan] texture: image creation failed (%dx%d mips=%d)\n",
              e.w, e.h, e.mips);
      return;
    }
  }
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkBufferCreateInfo bci{};
  bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bci.size = total;
  bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(g_device, &bci, nullptr, &staging) != VK_SUCCESS) {
    return;
  }
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(g_device, staging, &req);
  uint32_t mt = FindMemory(req.memoryTypeBits,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = mt;
  void *mapped = nullptr;
  bool ok = mt != UINT32_MAX &&
            vkAllocateMemory(g_device, &mai, nullptr, &mem) == VK_SUCCESS &&
            vkMapMemory(g_device, mem, 0, total, 0, &mapped) == VK_SUCCESS;
  PendingUpload up;
  if (ok) {
    uint8_t *dst = (uint8_t *)mapped;
    PackRows(dst, src, sRow, rowBytes, rows);
    vkUnmapMemory(g_device, mem);
    ok = vkBindBufferMemory(g_device, staging, mem, 0) == VK_SUCCESS;
  }
  if (!ok) {
    if (mem != VK_NULL_HANDLE) {
      vkFreeMemory(g_device, mem, nullptr);
    }
    vkDestroyBuffer(g_device, staging, nullptr);
    return;
  }
  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = (uint32_t)level;
  region.imageSubresource.layerCount = 1;
  region.imageOffset.x = x;
  region.imageOffset.y = y;
  region.imageExtent.width = (uint32_t)w;
  region.imageExtent.height = (uint32_t)h;
  region.imageExtent.depth = 1;
  up.regions.push_back(region);
  up.handleIdx = idx;
  up.frame = 0;
  up.w = e.w;
  up.h = e.h;
  up.mips = e.mips;
  up.staging = staging;
  up.stagingMem = mem;
  up.oldLayout = img.layoutDefined
                     ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                     : VK_IMAGE_LAYOUT_UNDEFINED;
  img.layoutDefined = true;
  g_pending.push_back(std::move(up));
  e.curFrame = 0;
}

void SeVkTexCache_Flush(VkCommandBuffer cmd) {
  std::lock_guard<std::mutex> lock(g_mutex);
  // Previous frame submitted before this cmd began: staging is free.
  for (auto &gb : g_garbage) {
    vkDestroyBuffer(g_device, gb.staging, nullptr);
    vkFreeMemory(g_device, gb.stagingMem, nullptr);
  }
  g_garbage.clear();
  for (auto &up : g_pending) {
    // Resolve + validate (the image may have been recreated since queue).
    Image *img = nullptr;
    if (up.handleIdx >= 0 &&
        (size_t)up.handleIdx < g_entries.size() &&
        g_entries[up.handleIdx].used) {
      Entry &e = g_entries[up.handleIdx];
      if (up.frame >= 0 && (size_t)up.frame < e.frames.size()) {
        Image &cand = e.frames[up.frame];
        if (cand.view != VK_NULL_HANDLE && cand.w == up.w &&
            cand.h == up.h && cand.mips == up.mips) {
          img = &cand;
        }
      }
    }
    if (!img) {
      g_garbage.push_back({up.staging, up.stagingMem});
      continue;
    }
    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = up.oldLayout;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = img->image;
    toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toDst.subresourceRange.levelCount = (uint32_t)img->mips;
    toDst.subresourceRange.layerCount = 1;
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (up.oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      // Re-upload of a sampled image: transition from reads, not UNDEFINED.
      toDst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
      toDst.srcAccessMask = 0;
    }
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toDst);
    vkCmdCopyBufferToImage(cmd, up.staging, img->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           (uint32_t)up.regions.size(), up.regions.data());
    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &toRead);
    g_garbage.push_back({up.staging, up.stagingMem});
  }
  g_pending.clear();
}

VkImageView SeVkTexCache_View(ShaderAPITextureHandle_t handle) {
  std::lock_guard<std::mutex> lock(g_mutex);
  intp idx = (intp)handle;
  if (idx < 0 || (size_t)idx >= g_entries.size() || !g_entries[idx].used) {
    return g_dummy.view;
  }
  Entry &e = g_entries[idx];
  if (e.frames.empty()) {
    return g_dummy.view;
  }
  int f = e.curFrame;
  if (f < 0 || (size_t)f >= e.frames.size() ||
      e.frames[f].view == VK_NULL_HANDLE) {
    f = 0;
  }
  if ((size_t)f >= e.frames.size() ||
      e.frames[f].view == VK_NULL_HANDLE) {
    return g_dummy.view;
  }
  return e.frames[f].view;
}

VkImageView SeVkTexCache_Standard(int standardId) {
  // Flat colors + flat normal; everything else (lightmaps at menu time are
  // real handles, frame buffers, color correction, noise, morph) uses the
  // dummy until its own path lands.
  switch (standardId) {
    case TEXTURE_WHITE:
      return g_white.view != VK_NULL_HANDLE ? g_white.view : g_dummy.view;
    case TEXTURE_BLACK:
      return g_black.view != VK_NULL_HANDLE ? g_black.view : g_dummy.view;
    case TEXTURE_GREY:
    case TEXTURE_GREY_ALPHA_ZERO:  // alpha approximated; follow-up
      return g_grey.view != VK_NULL_HANDLE ? g_grey.view : g_dummy.view;
    case TEXTURE_NORMALMAP_FLAT:
      return g_flatNormal.view != VK_NULL_HANDLE ? g_flatNormal.view
                                                 : g_dummy.view;
    default: {
      if (standardId >= 0 && standardId < 64 && !g_loggedStd[standardId]) {
        g_loggedStd[standardId] = true;
        Warning("[vulkan] texture: standard id %d uses dummy (follow-up)\n",
                standardId);
      }
      return g_dummy.view;
    }
  }
}

VkImageView SeVkTexCache_Dummy() { return g_dummy.view; }

VkSampler SeVkTexCache_Sampler() { return g_sampler; }

// Pending TexLock state (single outstanding lock, like DX9).
struct LockedRect {
  bool active = false;
  intp handleIdx = -1;
  int level = 0;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};
LockedRect g_locked;

bool SeVkTexCache_Lock(ShaderAPITextureHandle_t handle, int level, int x,
                       int y, int w, int h, CPixelWriter &writer) {
  std::lock_guard<std::mutex> lock(g_mutex);
  intp idx = (intp)handle;
  if (idx < 0 || (size_t)idx >= g_entries.size() || !g_entries[idx].used) {
    return false;
  }
  Entry &e = g_entries[idx];
  if (e.w <= 0 || e.h <= 0 || e.mips <= 0 || level < 0 || level >= e.mips) {
    return false;
  }
  int ew = e.w >> level;
  int eh = e.h >> level;
  if (ew <= 0) {
    ew = 1;
  }
  if (eh <= 0) {
    eh = 1;
  }
  if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > ew || y + h > eh) {
    Warning("[vulkan] texture: lock rect (%d,%d %dx%d level %d) outside "
            "%dx%d (handle %d)\n",
            x, y, w, h, level, e.w, e.h, (int)idx);
    return false;
  }
  if (e.lockMirror.empty()) {
    e.lockMirror.assign((size_t)e.w * (size_t)e.h * 4, 0);
  }
  writer.SetPixelMemory(IMAGE_FORMAT_RGBA8888,
                        e.lockMirror.data() + ((size_t)y * e.w + x) * 4,
                        (unsigned)(e.w * 4));
  g_locked.active = true;
  g_locked.handleIdx = idx;
  g_locked.level = level;
  g_locked.x = x;
  g_locked.y = y;
  g_locked.w = w;
  g_locked.h = h;
  return true;
}

void SeVkTexCache_Unlock() {
  intp idx = -1;
  int level = 0, x = 0, y = 0, w = 0, h = 0;
  std::vector<uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_locked.active) {
      return;
    }
    g_locked.active = false;
    idx = g_locked.handleIdx;
    if (idx < 0 || (size_t)idx >= g_entries.size() ||
        !g_entries[idx].used) {
      return;
    }
    Entry &e = g_entries[idx];
    if (e.lockMirror.size() < (size_t)e.w * (size_t)e.h * 4) {
      return;
    }
    level = g_locked.level;
    x = g_locked.x;
    y = g_locked.y;
    w = g_locked.w;
    h = g_locked.h;
    // Copy under the lock: the entry may die before UploadRaw runs.
    bytes.resize((size_t)w * h * 4);
    const uint8_t *src =
        e.lockMirror.data() + ((size_t)y * e.w + x) * 4;
    for (int r = 0; r < h; ++r) {
      memcpy(bytes.data() + (size_t)r * w * 4, src + (size_t)r * e.w * 4,
             (size_t)w * 4);
    }
  }
  // Outside the mutex (UploadRaw locks it itself).
  if (!bytes.empty()) {
    SeVkTexCache_UploadRaw(idx, level, x, y, w, h, IMAGE_FORMAT_RGBA8888,
                           IMAGE_FORMAT_RGBA8888, bytes.data(), w * 4);
  }
}

}  // namespace se::vulkan
