// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// vk_present_test.cpp -- S2.3 gate: standalone swapchain + clear verification.
//
// Presents 60 cleared frames on its own SDL window using the exact backend
// sources (se_vk_init + se_vk_swapchain), outside the engine. Pass criteria:
// exit 0, 60 presents, zero validation errors/warnings. This is the S2.3
// "something on screen" proof; engine-driven Present waits on S2.6 shaders.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL.h>

#include "se_vk_init.h"
#include "se_vk_swapchain.h"
#include "se_vk_shaderpack.h"
#include "tier0/dbg.h"

namespace {

constexpr int kFrames = 60;

}  // namespace

int main(int argc, char **argv) {
  // Mode 2: shader-pack proof. Args: --shader <manifest-dir> <tokens.bin> <vs|ps>
  // Loads the pack, feeds real content token bytecode, expects a VkShaderModule.
  if (argc == 5 && strcmp(argv[1], "--shader") == 0) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      fprintf(stderr, "SDL_Init(video) failed: %s\n", SDL_GetError());
      return 2;
    }
    const char *dir = argv[2];
    FILE *f = fopen(argv[3], "rb");
    if (!f) {
      fprintf(stderr, "cannot open %s\n", argv[3]);
      return 2;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *tokens = new char[size];
    size_t got = fread(tokens, 1, size, f);
    fclose(f);
    if (got != (size_t)size) {
      return 2;
    }
    se::vulkan::VkContext ctx;
    if (!se::vulkan::VkContext_Create(&ctx)) {
      return 2;
    }
    se::vulkan::SeVkShaderPack pack;
    se::vulkan::SeVkShaderPack_Load(ctx.device, dir, &pack);
    bool isVertex = strcmp(argv[4], "vs") == 0;
    se::vulkan::SeVkShader *rec = se::vulkan::SeVkShaderPack_Create(
        ctx.device, &pack, tokens, size, isVertex, "spike");
    delete[] tokens;
    int rc = 0;
    if (!rec || rec->module == VK_NULL_HANDLE) {
      fprintf(stderr, "pack lookup/proof FAILED\n");
      rc = 1;
    } else {
      Msg("[vulkan-test] pack proof ok: module %p errors=%u warnings=%u\n",
          (void *)rec->module, ctx.validationErrors, ctx.validationWarnings);
      if (ctx.validationErrors != 0 || ctx.validationWarnings != 0) {
        rc = 1;
      }
    }
    se::vulkan::SeVkShaderPack_Unload(ctx.device, &pack);
    se::vulkan::VkContext_Destroy(&ctx);
    SDL_Quit();
    return rc;
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init(video) failed: %s\n", SDL_GetError());
    return 2;
  }
  SDL_Window *window = SDL_CreateWindow("shaderapivulkan S2.3", 640, 480,
                                         SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);
  if (!window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 2;
  }
  // Hidden is fine: presents still execute the full acquire/clear/submit/
  // present path against the real driver.

  se::vulkan::VkContext ctx;
  if (!se::vulkan::VkContext_Create(&ctx)) {
    fprintf(stderr, "VkContext_Create failed\n");
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 2;
  }
  se::vulkan::SeVkSwapchain swap;
  // Capture path: SE_VK_CAPTURE when set, otherwise repo-local scratch.
  const char *capturePath = getenv("SE_VK_CAPTURE");
  if (!capturePath || capturePath[0] == '\0') {
    capturePath = "__tmp/tri.ppm";
  }
  for (int i = 0; i < kFrames; ++i) {
    if (i == kFrames - 1) {
      snprintf(swap.capturePath, sizeof(swap.capturePath), "%s", capturePath);
    }
    se::vulkan::SeVkSwapchain_Present(&ctx, &swap, window);
  }
  uint32_t presented = swap.frameIndex;
  se::vulkan::SeVkSwapchain_Destroy(&ctx, &swap);
  se::vulkan::VkContext_Destroy(&ctx);
  uint32_t errors = ctx.validationErrors;
  uint32_t warnings = ctx.validationWarnings;
  SDL_DestroyWindow(window);
  SDL_Quit();

  Msg("[vulkan-test] presented %u/%d frames, validation errors=%u warnings=%u\n",
      presented, kFrames, errors, warnings);
  if (presented != kFrames || errors != 0 || warnings != 0) {
    return 1;
  }
  return 0;
}
