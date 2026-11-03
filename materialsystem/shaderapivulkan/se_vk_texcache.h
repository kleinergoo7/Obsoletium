// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_texcache.h -- Phase 2 native Vulkan backend, step S2.8-textures.
//
// CPU-side texture handle table + pending-upload queue behind the
// IShaderAPI texture methods. The engine delivers decoded texels through
// TexImageFromVTF (whole VTF: all mips/faces/frames); uploads are staged
// and recorded into the frame command buffer by Flush (render thread),
// so loader-thread uploads never touch the queue directly.
//
// S2.8 scope: 2D textures (cubemaps/volumes fall back to face/slice 0),
// one shared linear sampler, per-frame current sampler bindings resolved
// to views at draw time. Mip-mapped sampling, sampler states, render
// targets and lockable/procedural updates are follow-up work.

#ifndef SE_VK_TEXCACHE_H_
#define SE_VK_TEXCACHE_H_

#include <vulkan/vulkan.h>

#include "bitmap/imageformat.h"
#include "shaderapi/ishaderapi.h"

class IVTFTexture;

namespace se::vulkan {

// Must be called once the device exists (idempotent). Sampler + standard
// solid textures are created here.
void SeVkTexCache_Init(VkDevice device, VkPhysicalDevice physical,
                       uint32_t queueFamily, VkQueue queue);

// Handle-table management (mirrors the IShaderAPI texture methods).
ShaderAPITextureHandle_t SeVkTexCache_Create(int width, int height,
                                             int numMipLevels, int numCopies);
void SeVkTexCache_Delete(ShaderAPITextureHandle_t handle);
bool SeVkTexCache_Valid(ShaderAPITextureHandle_t handle);

// Destroys parked resources from SeVkTexCache_Delete once the shared
// descriptor set has been rewritten (texDirty cleared) and no submit is
// in flight. force=true (shutdown) skips the set check after WaitIdle.
void SeVkTexCache_DrainTrash(bool force = false);

// Releases every cached texture and clears the device handle. Call before
// destroying the VkDevice.
void SeVkTexCache_Shutdown();

// Whole-VTF upload into a frame slot (thread-safe; queued for Flush).
void SeVkTexCache_UploadVTF(ShaderAPITextureHandle_t handle, IVTFTexture *vtf,
                            int frame);

// Raw-bits upload behind TexImage2D/TexSubImage2D (standard textures,
// procedurals). Rows are packed unless srcStride != 0 (sub-image path).
void SeVkTexCache_UploadRaw(ShaderAPITextureHandle_t handle, int level,
                            int x, int y, int w, int h, ImageFormat dstFmt,
                            ImageFormat srcFmt, const void *srcData,
                            int srcStride);

// Lockable CPU mirror behind TexLock/TexUnlock (font glyph cache,
// procedural previews). The engine writes into the mirror through
// CPixelWriter; Unlock uploads the locked rect. RGBA8 throughout.
bool SeVkTexCache_Lock(ShaderAPITextureHandle_t handle, int level, int x,
                       int y, int w, int h, class CPixelWriter &writer);
void SeVkTexCache_Unlock();

// Records pending uploads into cmd (call before BeginRenderPass) and frees
// the previous frame's staging. Safe with no pending work.
void SeVkTexCache_Flush(VkCommandBuffer cmd);

// Resolves a bound view for sampling (dummy checkerboard when absent).
VkImageView SeVkTexCache_View(ShaderAPITextureHandle_t handle);
VkImageView SeVkTexCache_Standard(int standardId);
VkImageView SeVkTexCache_Dummy();
VkSampler SeVkTexCache_Sampler();

}  // namespace se::vulkan

#endif  // !SE_VK_TEXCACHE_H_
