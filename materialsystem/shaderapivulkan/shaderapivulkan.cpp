// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// shaderapivulkan.cpp -- Phase 2 native Vulkan backend module entry.
//
// Implements the same interface contract as shaderapidx9/shaderapiempty
// (IShaderAPI SHADERAPI_INTERFACE_VERSION etc.) so materialsystem, studio
// render and vgui need no changes. S2.2: Vulkan instance/device/queue come
// up here; every other method is a neutral stub (see *_stubs, generated)
// until its feature lands.

#include "api_stubs.h"
#include "device_stubs.h"
#include "devmgr_stubs.h"
#include "shadow_stubs.h"
#include "shader_api_common/meshbase.h"
#include "se_vk_bind.h"
#include "se_vk_init.h"
#include "se_vk_mesh.h"
#include "se_vk_swapchain.h"
#include "se_vk_shaderpack.h"
#include "se_vk_texcache.h"

#include "tier0/dbg.h"
#include "tier1/interface.h"
#include "tier1/strtools.h"
#include "appframework/ilaunchermgr.h"

#include <cstring>
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include "shaderapi/IShaderDevice.h"
#include "shaderapi/ishadershadow.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/idebugtextureinfo.h"

static CShaderAPIVulkan g_ApiVulkan;
static CShaderShadowVulkan g_ShadowVulkan;
static CShaderDeviceVulkan g_DeviceVulkan;
static CShaderDeviceMgrVulkan g_DeviceMgrVulkan;
static se::vulkan::VkContext g_VkContext;
static se::vulkan::SeVkSwapchain g_VkSwapchain;
static se::vulkan::SeVkShaderPack g_VkPack;

EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CShaderAPIVulkan, IShaderAPI,
                                  SHADERAPI_INTERFACE_VERSION, g_ApiVulkan)
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CShaderDeviceMgrVulkan, IShaderDeviceMgr,
                                  SHADER_DEVICE_MGR_INTERFACE_VERSION,
                                  g_DeviceMgrVulkan)
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CShaderDeviceVulkan, IShaderDevice,
                                  SHADER_DEVICE_INTERFACE_VERSION,
                                  g_DeviceVulkan)
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CShaderShadowVulkan, IShaderShadow,
                                  SHADERSHADOW_INTERFACE_VERSION, g_ShadowVulkan)
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CShaderAPIVulkan, IMaterialSystemHardwareConfig,
                                  MATERIALSYSTEM_HARDWARECONFIG_INTERFACE_VERSION,
                                  g_ApiVulkan)
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CShaderAPIVulkan, IDebugTextureInfo,
                                  DEBUG_TEXTURE_INFO_VERSION, g_ApiVulkan)

namespace {

void *VulkanShaderInterfaceFactory(const char *pInterfaceName, int *pReturnCode) {
  if (pReturnCode) {
    *pReturnCode = IFACE_OK;
  }
  if (V_strieq(pInterfaceName, SHADER_DEVICE_MGR_INTERFACE_VERSION)) {
    return static_cast<IShaderDeviceMgr *>(&g_DeviceMgrVulkan);
  }
  if (V_strieq(pInterfaceName, SHADER_DEVICE_INTERFACE_VERSION)) {
    return static_cast<IShaderDevice *>(&g_DeviceVulkan);
  }
  if (V_strieq(pInterfaceName, SHADERAPI_INTERFACE_VERSION)) {
    return static_cast<IShaderAPI *>(&g_ApiVulkan);
  }
  if (V_strieq(pInterfaceName, SHADERSHADOW_INTERFACE_VERSION)) {
    return static_cast<IShaderShadow *>(&g_ShadowVulkan);
  }
  if (pReturnCode) {
    *pReturnCode = IFACE_FAILED;
  }
  return nullptr;
}

}  // namespace

// Hand-implemented device-manager lifecycle (skipped by the stub
// generator): these must succeed for materialsystem CONNECTION, mirroring
// the trivial shaderapiempty bodies. SetMode hands out our factory so the
// engine resolves IShaderDevice/IShaderAPI against this module.
bool CShaderDeviceMgrVulkan::Connect(CreateInterfaceFn factory) {
  (void)factory;
  return true;
}

InitReturnVal_t CShaderDeviceMgrVulkan::Init() { return INIT_OK; }

bool CShaderDeviceMgrVulkan::SetAdapter(unsigned nAdapter, int nFlags) {
  (void)nAdapter;
  (void)nFlags;
  return true;
}

CreateInterfaceFn CShaderDeviceMgrVulkan::SetMode(
    void *hWnd, unsigned nAdapter, const ShaderDeviceInfo_t &mode) {
  (void)hWnd;
  (void)nAdapter;
  (void)mode;
  return VulkanShaderInterfaceFactory;
}

void *CShaderDeviceMgrVulkan::QueryInterface(const char *pInterfaceName) {  if (V_strieq(pInterfaceName, SHADER_DEVICE_MGR_INTERFACE_VERSION)) {
    return static_cast<IShaderDeviceMgr *>(this);
  }
  if (V_strieq(pInterfaceName, MATERIALSYSTEM_HARDWARECONFIG_INTERFACE_VERSION)) {
    return static_cast<IMaterialSystemHardwareConfig *>(&g_ApiVulkan);
  }
  return nullptr;
}

// Hand-implemented (skipped by the stub generator). The DX9 device manager
// fills pConfiguration from dxsupport_linux.cfg and returns an empty
// configuration when that file is missing - which it is for the CS:S content.
// The material system treats a false return as "no configuration at all" and
// then asserts m_bGeneratedConfig on every frame, so return true and leave
// the mat_* convars at their defaults, like the base class does.
bool CShaderDeviceMgrVulkan::GetRecommendedConfigurationInfo(
    unsigned nAdapter, int nDXLevel, KeyValues *pConfiguration) {
  (void)nAdapter;
  (void)nDXLevel;
  (void)pConfiguration;
  static bool said = false;
  if (!said) {
    said = true;
    Msg("[vulkan] no recommended configuration (no %s); using convvar defaults\n",
        SUPPORT_CFG_FILE);
  }
  return true;
}

// Hand-implemented (skipped by the stub generator): S2.3 present path --
// acquire, clear, submit, present via the lazy swapchain. The window comes
// from SDL itself (g_pLauncherMgr is a process-global data reference that
// plain Sys_LoadModule/RTLD_NOW cannot satisfy at load time); null before
// the engine shows anything.
void CShaderDeviceVulkan::Present() {
  SDL_Window *window = nullptr;
  int windowCount = 0;
  SDL_Window **windows = SDL_GetWindows(&windowCount);
  if (windows && windowCount > 0) {
    window = windows[0];
  }
  SDL_free(windows);
  se::vulkan::SeVkSwapchain_Present(&g_VkContext, &g_VkSwapchain, window);
}

// Hand-implemented S2.6: serve real SPIR-V from the AOT pack (hashed token
// bytecode lookup). Missing entries stay neutral (null handle) so uncovered
// shaders degrade exactly like before instead of crashing.
VertexShaderHandle_t CShaderDeviceVulkan::CreateVertexShader(
    IShaderBuffer *pShaderBuffer) {
  if (!pShaderBuffer) {
    return VertexShaderHandle_t{};
  }
  auto *rec = se::vulkan::SeVkShaderPack_Create(
      g_VkContext.device, &g_VkPack, pShaderBuffer->GetBits(),
      pShaderBuffer->GetSize(), true, "vs");
  return reinterpret_cast<VertexShaderHandle_t>(rec);
}

void CShaderDeviceVulkan::DestroyVertexShader(VertexShaderHandle_t hShader) {
  auto *shader = reinterpret_cast<se::vulkan::SeVkShader *>(hShader);
  se::vulkan::SeVkBind_OnShaderDestroyed(&se::vulkan::SeVkBind_Global(),
                                         shader);
  se::vulkan::SeVkShaderPack_Destroy(g_VkContext.device, &g_VkPack, shader);
}

PixelShaderHandle_t CShaderDeviceVulkan::CreatePixelShader(
    IShaderBuffer *pShaderBuffer) {
  if (!pShaderBuffer) {
    return PixelShaderHandle_t{};
  }
  auto *rec = se::vulkan::SeVkShaderPack_Create(
      g_VkContext.device, &g_VkPack, pShaderBuffer->GetBits(),
      pShaderBuffer->GetSize(), false, "ps");
  return reinterpret_cast<PixelShaderHandle_t>(rec);
}

void CShaderDeviceVulkan::DestroyPixelShader(PixelShaderHandle_t hShader) {
  auto *shader = reinterpret_cast<se::vulkan::SeVkShader *>(hShader);
  se::vulkan::SeVkBind_OnShaderDestroyed(&se::vulkan::SeVkBind_Global(),
                                         shader);
  se::vulkan::SeVkShaderPack_Destroy(g_VkContext.device, &g_VkPack, shader);
}

// Neutral: geometry shaders are SM4+ (no SM1-3 content uses them).
GeometryShaderHandle_t CShaderDeviceVulkan::CreateGeometryShader(
    IShaderBuffer *pShaderBuffer) {
  (void)pShaderBuffer;
  return GeometryShaderHandle_t{};
}

void CShaderDeviceVulkan::DestroyGeometryShader(GeometryShaderHandle_t hShader) {
  (void)hShader;
}

// Hand-implemented capability reports (skipped by the stub generator).
// The translator covers SM2.0/2.x/3.0 fully (B0), SM1.x partially, so the
// backend advertises DX9 SM2.0 baseline + SM3.0. In particular
// GetMaxDXSupportLevel drives stdshader_dx*.dll loading (LoadAllShaderDLLs
// loads dx6..level); reporting 0 meant no shader DLL ever loaded.
int CShaderAPIVulkan::GetMaxDXSupportLevel() const { return 90; }

bool CShaderAPIVulkan::SupportsShaderModel_3_0() const { return true; }

bool CShaderAPIVulkan::SupportsPixelShaders_2_0() const { return true; }

bool CShaderAPIVulkan::SupportsVertexShaders_2_0() const { return true; }

bool CShaderAPIVulkan::SupportsPixelShaders_2_b() const { return true; }

bool CShaderAPIVulkan::ActuallySupportsPixelShaders_2_b() const { return true; }

int CShaderAPIVulkan::GetDXSupportLevel() const { return 90; }

// Hand-implemented (skipped by the stub generator): brings up the S2.2
// Vulkan context. Validation layers are mandatory; any failure returns
// false so the engine errors loudly instead of rendering into the void.
bool CShaderAPIVulkan::SetMode(void *hwnd, unsigned nAdapter,
                               const ShaderDeviceInfo_t &info) {
  (void)hwnd;
  (void)nAdapter;
  (void)info;
  Msg("[vulkan] CShaderAPIVulkan::SetMode: creating S2.2 context\n");
  if (!se::vulkan::VkContext_Create(&g_VkContext)) {
    Warning("[vulkan] CShaderAPIVulkan::SetMode: context creation failed\n");
    return false;
  }
  // S2.8-textures: sampler + dummy/standard solids at device creation, so
  // no bind can ever resolve a null view (early binds precede Presents).
  se::vulkan::SeVkTexCache_Init(g_VkContext.device, g_VkContext.physical,
                               g_VkContext.graphicsFamily,
                               g_VkContext.graphicsQueue);
  // S2.6 AOT shader pack: without it no engine material can draw, so a
  // missing or empty pack is a startup failure, not a quiet downgrade.
  if (!se::vulkan::SeVkShaderPack_Load(g_VkContext.device, "vulkan",
                                       &g_VkPack)) {
    Warning("[vulkan] SetMode: shader pack unavailable, refusing to start\n");
    se::vulkan::VkContext_Destroy(&g_VkContext);
    return false;
  }
  if (g_VkContext.validationErrors != 0) {
    Warning("[vulkan] %u validation errors during init\n",
            g_VkContext.validationErrors);
  }
  Msg("[vulkan] context ready on %s\n", g_VkContext.deviceName);
  return true;
}

// Hand-implemented S2.8-bind: engine-driven draw path. Bound shaders select
// the pipeline (cached per VSxPS pair); constants feed the shared DynConsts
// UBOs; dynamic meshes accumulate into the frame drained by Present.
void CShaderAPIVulkan::BindVertexShader(VertexShaderHandle_t hVertexShader) {
  se::vulkan::SeVkBind_Global().boundVS =
      reinterpret_cast<se::vulkan::SeVkShader *>(hVertexShader);
}

void CShaderAPIVulkan::BindPixelShader(PixelShaderHandle_t hPixelShader) {
  se::vulkan::SeVkBind_Global().boundPS =
      reinterpret_cast<se::vulkan::SeVkShader *>(hPixelShader);
}

void CShaderAPIVulkan::SetVertexShaderConstant(int var, float const *pVec,
                                               int numConst, bool bForce) {
  (void)bForce;
  if (!pVec || var < 0 || numConst <= 0) {
    return;
  }
  auto &bind = se::vulkan::SeVkBind_Global();
  if (var >= 256) {
    return;
  }
  if (var + numConst > 256) {
    numConst = 256 - var;
  }
  memcpy(&bind.vsConsts[var], pVec, sizeof(float) * 4 * numConst);
  for (int k = var; k < var + numConst; ++k) {
    bind.vsEngine[k] = true;
  }
  bind.vsDirty = true;
}

void CShaderAPIVulkan::SetPixelShaderConstant(int var, float const *pVec,
                                              int numConst, bool bForce) {
  (void)bForce;
  if (!pVec || var < 0 || numConst <= 0) {
    return;
  }
  auto &bind = se::vulkan::SeVkBind_Global();
  if (var >= 256) {
    return;
  }
  if (var + numConst > 256) {
    numConst = 256 - var;
  }
  memcpy(&bind.psConsts[var], pVec, sizeof(float) * 4 * numConst);
  bind.psDirty = true;
}

IMesh *CShaderAPIVulkan::GetDynamicMesh(IMaterial *pMaterial,
                                        int nHWSkinBoneCount, bool bBuffered,
                                        IMesh *pVertexOverride,
                                        IMesh *pIndexOverride) {
  (void)nHWSkinBoneCount;
  (void)bBuffered;
  if (pVertexOverride) {
    // Batched world surfaces: the override mesh carries the pre-built verts,
    // pIndexOverride the batch's index run, pMaterial the auto-bind
    // material. Without all three the whole world draws nothing.
    auto *mesh = static_cast<se::vulkan::SeVkDynamicMesh *>(pVertexOverride);
    mesh->SetMaterial(pMaterial);
    mesh->SetIndexSource(pIndexOverride
                             ? static_cast<se::vulkan::SeVkDynamicMesh *>(
                                   pIndexOverride)
                             : nullptr);
    return pVertexOverride;
  }
  se::vulkan::SeVkMesh_Global().SetMaterial(pMaterial);
  return &se::vulkan::SeVkMesh_Global();
}

IMesh *CShaderAPIVulkan::GetDynamicMeshEx(IMaterial *pMaterial,
                                          VertexFormat_t vertexFormat,
                                          int nHWSkinBoneCount, bool bBuffered,
                                          IMesh *pVertexOverride,
                                          IMesh *pIndexOverride) {
  (void)vertexFormat;
  return GetDynamicMesh(pMaterial, nHWSkinBoneCount, bBuffered, pVertexOverride,
                        pIndexOverride);
}

// Hand-implemented S2.8-bind: this backend drives real graphics output.
// The neutral stub (false) selects null-renderer paths all over
// materialsystem (skipped texture loads, silent shader warnings, ...).
bool CShaderDeviceVulkan::IsUsingGraphics() const { return true; }

// Hand-implemented S2.8-shaderload: snapshot-time shader resolution.
// The stdshader snapshot records (file, staticIdx) pairs via SetVertex/
// SetPixelShader; TakeSnapshot freezes the resolved pack modules and
// BeginPass rebinds them at draw time. Dynamic indices are stored for the
// follow-up (S2.8 uses the static combo's dynamic-0 variant throughout).
namespace {
struct SeVkSnapshot {
  // Deferred resolution: snapshots are taken before SetMode loads the pack
  // (early material precache), so store (file, staticIdx) and resolve the
  // pack module at bind time, when the device is guaranteed ready.
  char vsFile[96] = {};
  int vsIdx = -1;
  char psFile[96] = {};
  int psIdx = -1;
  // Shadow blend switch (opaque materials must not blend: their PS alpha
  // is meaningless, often 0). DX9 default is off; materials opt in.
  bool blendEnable = false;
};
constexpr int kMaxSnapshots = 2048;
SeVkSnapshot g_Snapshots[kMaxSnapshots];
int g_nSnapshots = 0;
char g_ShadowVSFile[96] = {};
int g_ShadowVSIdx = -1;
char g_ShadowPSFile[96] = {};
int g_ShadowPSIdx = -1;
int g_VSIndex = 0;
int g_PSIndex = 0;
// Current snapshot's unresolved (file, staticIdx) pair, kept so the dynamic
// combo index (set by the shader via SetPixel/VertexShaderIndex or the
// command buffer) can be resolved at draw time: the engine's static index is
// strided by the shader's dynamic-combo count, so the program the engine
// addresses is staticIdx + dynamicIdx.
char g_CurVSFile[96] = {};
int g_CurVSIdx = -1;
char g_CurPSFile[96] = {};
int g_CurPSIdx = -1;
// Shadow blend switch being recorded (DX9 default off).
bool g_ShadowBlendEnable = false;

// Re-resolve the bound shaders with the live dynamic combo index. The
// engine's static index is strided by the shader's dynamic-combo count, so
// the program it addresses is staticIdx + dynamicIdx. A pack that lacks the
// requested dynamic combo keeps the static (dyn-0) variant and says so.
void SeVkRebindDynamicCombos() {
  // Early material precache runs before SetMode loads the pack; a lookup now
  // can only miss and must not be cached or reported.
  if (g_VkPack.nStatics == 0) {
    return;
  }
  auto &bind = se::vulkan::SeVkBind_Global();
  struct Slot {
    char *file;
    int staticIdx;
    int dynIdx;
    bool isVertex;
    se::vulkan::SeVkShader **out;
  } slots[2] = {
      {g_CurVSFile, g_CurVSIdx, g_VSIndex, true, &bind.boundVS},
      {g_CurPSFile, g_CurPSIdx, g_PSIndex, false, &bind.boundPS},
  };
  // Most draws repeat the same (file, static, dynamic) triple; a miss is kept
  // so the next draw does not pay for the same failed lookup.
  static char cachedFile[2][96] = {};
  static int cachedStatic[2] = {-1, -1};
  static int cachedDyn[2] = {-1, -1};
  static se::vulkan::SeVkShader *cachedShader[2] = {nullptr, nullptr};
  static int missLogged = 0;
  for (int i = 0; i < 2; ++i) {
    Slot &s = slots[i];
    if (s.staticIdx < 0 || s.file[0] == '\0') {
      continue;
    }
    if (cachedShader[i] && cachedStatic[i] == s.staticIdx &&
        cachedDyn[i] == s.dynIdx && strcmp(cachedFile[i], s.file) == 0) {
      *s.out = cachedShader[i];
      continue;
    }
    snprintf(cachedFile[i], sizeof(cachedFile[i]), "%s", s.file);
    cachedStatic[i] = s.staticIdx;
    cachedDyn[i] = s.dynIdx;
    se::vulkan::SeVkShader *sh = se::vulkan::SeVkShaderPack_CreateStatic(
        g_VkContext.device, &g_VkPack, s.file, s.staticIdx + s.dynIdx,
        s.isVertex);
    cachedShader[i] = sh;
    if (sh) {
      *s.out = sh;
      continue;
    }
    if (missLogged < 8) {
      ++missLogged;
      Warning("[vulkan] %s %s: dynamic combo %d absent from pack, using "
              "static variant\n",
              s.isVertex ? "vertex" : "pixel", s.file, s.dynIdx);
    }
  }
}

void StoreShadowFile(char (&dst)[96], int &idxDst, const char *file, int idx) {
  snprintf(dst, sizeof(dst), "%s", file ? file : "");
  idxDst = idx;
}
}  // namespace

void CShaderShadowVulkan::SetVertexShader(const char *pFileName,
                                          int nStaticVshIndex) {
  StoreShadowFile(g_ShadowVSFile, g_ShadowVSIdx, pFileName, nStaticVshIndex);
}

void CShaderShadowVulkan::SetPixelShader(const char *pFileName,
                                         int nStaticPshIndex) {
  StoreShadowFile(g_ShadowPSFile, g_ShadowPSIdx, pFileName, nStaticPshIndex);
}

void CShaderShadowVulkan::EnableBlending(bool bEnable) {
  g_ShadowBlendEnable = bEnable;
}

StateSnapshot_t CShaderAPIVulkan::TakeSnapshot() {
  // Dedup identical shader states. Every bind re-bakes; without dedup the
  // 2048-entry table exhausts within seconds in the menu, later snapshots
  // degrade to 0, BeginPass(0) no-ops and blend state freezes off (all VGUI
  // text renders opaque). DX9 dedups states the same way.
  for (int i = 0; i < g_nSnapshots; ++i) {
    const SeVkSnapshot &s = g_Snapshots[i];
    if (s.vsIdx == g_ShadowVSIdx && s.psIdx == g_ShadowPSIdx &&
        s.blendEnable == g_ShadowBlendEnable &&
        strcmp(s.vsFile, g_ShadowVSFile) == 0 &&
        strcmp(s.psFile, g_ShadowPSFile) == 0) {
      return static_cast<StateSnapshot_t>(i + 1);
    }
  }
  if (g_nSnapshots >= kMaxSnapshots) {
    return StateSnapshot_t{};
  }
  snprintf(g_Snapshots[g_nSnapshots].vsFile,
           sizeof(g_Snapshots[g_nSnapshots].vsFile), "%s", g_ShadowVSFile);
  g_Snapshots[g_nSnapshots].vsIdx = g_ShadowVSIdx;
  snprintf(g_Snapshots[g_nSnapshots].psFile,
           sizeof(g_Snapshots[g_nSnapshots].psFile), "%s", g_ShadowPSFile);
  g_Snapshots[g_nSnapshots].psIdx = g_ShadowPSIdx;
  g_Snapshots[g_nSnapshots].blendEnable = g_ShadowBlendEnable;
  return static_cast<StateSnapshot_t>(++g_nSnapshots);
}

void CShaderAPIVulkan::BeginPass(StateSnapshot_t snapshot) {
  int id = static_cast<int>(snapshot);
  if (id <= 0 || id > g_nSnapshots) {
    return;
  }
  const SeVkSnapshot &s = g_Snapshots[id - 1];
  se::vulkan::SeVkShader *vs = nullptr;
  se::vulkan::SeVkShader *ps = nullptr;
  if (s.vsIdx >= 0) {
    vs = se::vulkan::SeVkShaderPack_CreateStatic(
        g_VkContext.device, &g_VkPack, s.vsFile, s.vsIdx, true);
  }
  if (s.psIdx >= 0) {
    ps = se::vulkan::SeVkShaderPack_CreateStatic(
        g_VkContext.device, &g_VkPack, s.psFile, s.psIdx, false);
  }
  se::vulkan::SeVkBind_Global().boundVS = vs;
  se::vulkan::SeVkBind_Global().boundPS = ps;
  se::vulkan::SeVkBind_Global().curBlend = s.blendEnable;
  snprintf(g_CurVSFile, sizeof(g_CurVSFile), "%s", s.vsFile);
  g_CurVSIdx = s.vsIdx;
  snprintf(g_CurPSFile, sizeof(g_CurPSFile), "%s", s.psFile);
  g_CurPSIdx = s.psIdx;
  SeVkRebindDynamicCombos();
}

void CShaderAPIVulkan::SetVertexShaderIndex(int vshIndex) {
  g_VSIndex = vshIndex;
  SeVkRebindDynamicCombos();
}

void CShaderAPIVulkan::SetPixelShaderIndex(int pshIndex) {
  g_PSIndex = pshIndex;
  SeVkRebindDynamicCombos();
}

// Hand-implemented S2.8-bind: stdshader command-buffer interpreter.
// Mirrors CShaderAPIDx8::ExecuteCommandBuffer field-for-field (see
// shaderapidx9/shaderapidx8.cpp): constant uploads feed the DynConsts
// UBOs, index commands update the dynamic-combo selectors. Lighting/fog
// helpers are size-correct neutral (visual follow-up, logged once).
// JSR recurses with a depth cap; JUMP loops are iteration-capped.
namespace {
int g_ExecuteDepth = 0;
bool g_ExecuteLoggedSkip = false;
}  // namespace

void CShaderAPIVulkan::ExecuteCommandBuffer(uint8 *pCmdBuffer) {
  if (!pCmdBuffer) {
    return;
  }
  if (++g_ExecuteDepth > 8) {
    Warning("[vulkan] ExecuteCommandBuffer: JSR nesting overflow\n");
    --g_ExecuteDepth;
    return;
  }
  auto &bind = se::vulkan::SeVkBind_Global();
  uint8 *retStack[8];
  int sp = 0;  // count of pushed return addresses
  uint64_t steps = 0;
  auto rd32 = [](uint8 *p) {
    int v = 0;
    memcpy(&v, p, 4);
    return v;
  };
  while (pCmdBuffer) {
    if (++steps > (1 << 20)) {
      Warning("[vulkan] ExecuteCommandBuffer: iteration cap hit\n");
      break;
    }
    int cmd = rd32(pCmdBuffer);
    switch (cmd) {
      case 0:  // CBCMD_END
        if (sp == 0) {
          --g_ExecuteDepth;
          return;
        }
        pCmdBuffer = retStack[--sp];
        continue;
      case 1:  // CBCMD_JUMP
        memcpy(&pCmdBuffer, pCmdBuffer + 4, sizeof(pCmdBuffer));
        continue;
      case 2: {  // CBCMD_JSR
        if (sp >= 8) {
          Warning("[vulkan] ExecuteCommandBuffer: return stack overflow\n");
          --g_ExecuteDepth;
          return;
        }
        uint8 *dst = nullptr;
        memcpy(&dst, pCmdBuffer + 4, sizeof(dst));
        retStack[sp++] = pCmdBuffer + 4 + sizeof(dst);
        pCmdBuffer = dst;
        continue;
      }
      case 256: {  // CBCMD_SET_PIXEL_SHADER_FLOAT_CONST
        int first = rd32(pCmdBuffer + 4);
        int n = rd32(pCmdBuffer + 8);
        float vals[1024];
        if (n < 0 || n > 256) {
          Warning("[vulkan] ExecuteCommandBuffer: bad PS const count %d\n",
                  n);
          --g_ExecuteDepth;
          return;
        }
        memcpy(vals, pCmdBuffer + 12, (size_t)n * 16);
        if (first >= 0 && first + n <= 256) {
          memcpy(&bind.psConsts[first], vals, (size_t)n * 16);
          bind.psDirty = true;
        }
        pCmdBuffer += 12 + (size_t)n * 16;
        continue;
      }
      case 257: {  // CBCMD_SET_VERTEX_SHADER_FLOAT_CONST
        int first = rd32(pCmdBuffer + 4);
        int n = rd32(pCmdBuffer + 8);
        float vals[1024];
        if (n < 0 || n > 256) {
          Warning("[vulkan] ExecuteCommandBuffer: bad VS const count %d\n",
                  n);
          --g_ExecuteDepth;
          return;
        }
        memcpy(vals, pCmdBuffer + 12, (size_t)n * 16);
        if (first >= 0 && first + n <= 256) {
          memcpy(&bind.vsConsts[first], vals, (size_t)n * 16);
          for (int k = first; k < first + n; ++k) {
            bind.vsEngine[k] = true;
          }
          bind.vsDirty = true;
        }
        pCmdBuffer += 12 + (size_t)n * 16;
        continue;
      }
      case 259: {  // CBCMD_SETPIXELSHADERFOGPARAMS
        // Fog-state tracking is follow-up work; all current content is
        // unfogged, so serve the DX9 MATERIAL_FOG_NONE vector directly
        // (CalcRangeFog returns 0). Mirrors the none-branch of
        // CShaderAPIDx8::SetPixelShaderFogParams.
        int reg = rd32(pCmdBuffer + 4);
        if (reg >= 0 && reg < 256) {
          bind.psConsts[reg][0] = 0.0f;
          bind.psConsts[reg][1] = 0.0f;
          bind.psConsts[reg][2] = 1.0f;
          bind.psConsts[reg][3] = 0.0f;
          bind.psDirty = true;
        }
        pCmdBuffer += 8;
        continue;
      }
      case 260:  // CBCMD_STORE_EYE_POS_IN_PSCONST
      case 261:  // CBCMD_COMMITPIXELSHADERLIGHTING
      case 262:  // CBCMD_SETPIXELSHADERSTATEAMBIENTLIGHTCUBE
        pCmdBuffer += 8;
        continue;
      case 263:  // CBCMD_SETAMBIENTCUBEDYNAMICSTATEVERTEXSHADER
        pCmdBuffer += 4;
        continue;
      case 264:  // CBCMD_SET_DEPTH_FEATHERING_CONST
        pCmdBuffer += 12;
        continue;
      case 512: {  // CBCMD_BIND_STANDARD_TEXTURE
        int sampler = rd32(pCmdBuffer + 4);
        int texId = rd32(pCmdBuffer + 8);
        BindStandardTexture((Sampler_t)sampler, (StandardTextureId_t)texId);
        pCmdBuffer += 12;
        continue;
      }
      // cmd + idx = 2 ints (commandbuilder.h SetPixelShaderIndex); advancing
      // 12 desynced every later command and produced garbage jump targets.
      case 1024: {  // CBCMD_SET_PSHINDEX
        g_PSIndex = rd32(pCmdBuffer + 4);
        SeVkRebindDynamicCombos();
        pCmdBuffer += 8;
        continue;
      }
      case 1025: {  // CBCMD_SET_VSHINDEX
        g_VSIndex = rd32(pCmdBuffer + 4);
        SeVkRebindDynamicCombos();
        pCmdBuffer += 8;
        continue;
      }
      case 513: {  // CBCMD_BIND_SHADERAPI_TEXTURE_HANDLE
        int sampler = rd32(pCmdBuffer + 4);
        ShaderAPITextureHandle_t handle = 0;
        memcpy(&handle, pCmdBuffer + 8, sizeof(handle));
        if (handle != INVALID_SHADERAPI_TEXTURE_HANDLE) {
          BindTexture((Sampler_t)sampler, handle);
        }
        pCmdBuffer += 4 + 4 + sizeof(handle);
        continue;
      }
      default:
        if (!g_ExecuteLoggedSkip) {
          g_ExecuteLoggedSkip = true;
          Warning("[vulkan] ExecuteCommandBuffer: unhandled opcode %d\n",
                  cmd);
        }
        --g_ExecuteDepth;
        return;
    }
  }
  --g_ExecuteDepth;
}

// Hand-implemented S2.8-matrix: viewport + matrix stacks + standard
// constants. These feed the per-draw UBO commit (VIEWPROJ c8, MODEL c58);
// the engine drives them every frame (DrawScreenSpaceRectangle sets
// VIEW/PROJECTION identity, world rendering sets MODEL).
namespace {
ShaderViewport_t g_Viewport;
struct ViewportInit {
  ViewportInit() {
    g_Viewport.m_nTopLeftX = 0;
    g_Viewport.m_nTopLeftY = 0;
    // No guess: GetViewports() reports the swapchain extent until the engine
    // sets an explicit viewport.
    g_Viewport.m_nWidth = 0;
    g_Viewport.m_nHeight = 0;
  }
};
ViewportInit g_ViewportInit;
}  // namespace

#include "mathlib/vmatrix.h"
#include "se_vk_matrix.h"

void CShaderAPIVulkan::SetViewports(int nCount,
                                    const ShaderViewport_t *pViewports) {
  if (nCount > 0 && pViewports) {
    g_Viewport = pViewports[0];
  }
}

int CShaderAPIVulkan::GetViewports(ShaderViewport_t *pViewports,
                                   int nMax) const {
  if (nMax > 0 && pViewports) {
    pViewports[0] = g_Viewport;
    if (pViewports[0].m_nWidth == 0 || pViewports[0].m_nHeight == 0) {
      VkExtent2D e = se::vulkan::SeVkSwapchain_LastExtent();
      pViewports[0].m_nWidth = static_cast<int>(e.width);
      pViewports[0].m_nHeight = static_cast<int>(e.height);
    }
    return 1;
  }
  return 0;
}

void CShaderAPIVulkan::MatrixMode(MaterialMatrixMode_t matrixMode) {
  if (matrixMode >= 0 && matrixMode < se::vulkan::SeVkMatrixStacks::kModes) {
    se::vulkan::SeVkMatrix_Global().mode = (int)matrixMode;
  }
}

void CShaderAPIVulkan::PushMatrix() {
  auto &st = se::vulkan::SeVkMatrix_Global();
  if ((int)st.stack[st.mode].size() >= se::vulkan::SeVkMatrixStacks::kDepth) {
    Warning("[vulkan] PushMatrix: stack overflow (mode %d)\n", st.mode);
    return;
  }
  st.stack[st.mode].push_back(st.current[st.mode]);
}

void CShaderAPIVulkan::PopMatrix() {
  auto &st = se::vulkan::SeVkMatrix_Global();
  if (st.stack[st.mode].empty()) {
    Warning("[vulkan] PopMatrix: stack underflow (mode %d)\n", st.mode);
    return;
  }
  st.current[st.mode] = st.stack[st.mode].back();
  st.stack[st.mode].pop_back();
}

void CShaderAPIVulkan::LoadMatrix(float *m) {
  if (!m) {
    return;
  }
  auto &st = se::vulkan::SeVkMatrix_Global();
  memcpy(st.current[st.mode].m, m, sizeof(float) * 16);
}

void CShaderAPIVulkan::MultMatrix(float *m) {
  if (!m) {
    return;
  }
  auto &st = se::vulkan::SeVkMatrix_Global();
  VMatrix rhs;
  memcpy(rhs.m, m, sizeof(float) * 16);
  st.current[st.mode] = st.current[st.mode] * rhs;
}

void CShaderAPIVulkan::MultMatrixLocal(float *m) {
  if (!m) {
    return;
  }
  auto &st = se::vulkan::SeVkMatrix_Global();
  VMatrix lhs;
  memcpy(lhs.m, m, sizeof(float) * 16);
  st.current[st.mode] = lhs * st.current[st.mode];
}

void CShaderAPIVulkan::GetMatrix(MaterialMatrixMode_t matrixMode,
                                 float *dst) {
  if (!dst || matrixMode < 0 ||
      matrixMode >= se::vulkan::SeVkMatrixStacks::kModes) {
    return;
  }
  memcpy(dst, se::vulkan::SeVkMatrix_Global().current[(int)matrixMode].m,
         sizeof(float) * 16);
}

void CShaderAPIVulkan::LoadIdentity() {
  auto &st = se::vulkan::SeVkMatrix_Global();
  VMatrix &m = st.current[st.mode];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      m.m[i][j] = (i == j) ? 1.0f : 0.0f;
    }
  }
}

void CShaderAPIVulkan::LoadCameraToWorld() {
  // Rotation-only inverse (translation zeroed), matching DX9.
  auto &st = se::vulkan::SeVkMatrix_Global();
  VMatrix src = st.current[st.mode];
  VMatrix &m = st.current[st.mode];
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      m.m[i][j] = src.m[j][i];
    }
  }
  m.m[3][0] = m.m[3][1] = m.m[3][2] = 0.0f;
  m.m[0][3] = m.m[1][3] = m.m[2][3] = 0.0f;
  m.m[3][3] = 1.0f;
}

void CShaderAPIVulkan::Ortho(double left, double right, double bottom,
                              double top, double zNear, double zFar) {
  // D3DXMatrixOrthoOffCenterLH.
  float l = (float)left, r = (float)right, b = (float)bottom,
        t = (float)top, zn = (float)zNear, zf = (float)zFar;
  auto &st = se::vulkan::SeVkMatrix_Global();
  VMatrix &m = st.current[st.mode];
  memset(m.m, 0, sizeof(m.m));
  m.m[0][0] = 2.0f / (r - l);
  m.m[1][1] = 2.0f / (t - b);
  m.m[2][2] = 1.0f / (zf - zn);
  m.m[3][0] = (l + r) / (l - r);
  m.m[3][1] = (t + b) / (b - t);
  m.m[3][2] = zn / (zn - zf);
  m.m[3][3] = 1.0f;
}

void CShaderAPIVulkan::PerspectiveX(double fovx, double aspect, double zNear,
                                    double zFar) {
  // D3DXMatrixPerspectiveFovXLH.
  float t = tanf((float)fovx * 0.5f);
  float zn = (float)zNear, zf = (float)zFar;
  auto &st = se::vulkan::SeVkMatrix_Global();
  VMatrix &m = st.current[st.mode];
  memset(m.m, 0, sizeof(m.m));
  m.m[0][0] = 1.0f / t;
  m.m[1][1] = (float)aspect / t;
  m.m[2][2] = zf / (zf - zn);
  m.m[2][3] = 1.0f;
  m.m[3][2] = -zn * zf / (zf - zn);
}

void CShaderAPIVulkan::PerspectiveOffCenterX(double fovx, double aspect,
                                             double zNear, double zFar,
                                             double bottom, double top,
                                             double left, double right) {
  // Port-owned: off-center frustum from explicit bounds (fovx/aspect carried
  // for signature parity; the bounds define the frustum).
  (void)fovx;
  (void)aspect;
  float l = (float)left, r = (float)right, b = (float)bottom,
        t = (float)top, zn = (float)zNear, zf = (float)zFar;
  auto &st = se::vulkan::SeVkMatrix_Global();
  VMatrix &m = st.current[st.mode];
  memset(m.m, 0, sizeof(m.m));
  m.m[0][0] = 2.0f * zn / (r - l);
  m.m[1][1] = 2.0f * zn / (t - b);
  m.m[2][0] = (l + r) / (r - l);
  m.m[2][1] = (t + b) / (t - b);
  m.m[2][2] = zf / (zf - zn);
  m.m[2][3] = 1.0f;
  m.m[3][2] = -zn * zf / (zf - zn);
}

void CShaderAPIVulkan::PickMatrix(int x, int y, int width, int height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  float vw = (float)g_Viewport.m_nWidth, vh = (float)g_Viewport.m_nHeight;
  float vx = (float)g_Viewport.m_nTopLeftX,
        vy = (float)g_Viewport.m_nTopLeftY;
  float sx = vw / (float)width, sy = vh / (float)height;
  float tx = (vw - 2.0f * ((float)x - vx)) / (float)width;
  float ty = (vh - 2.0f * ((float)y - vy)) / (float)height;
  Scale(sx, sy, 1.0f);
  Translate(tx, ty, 0.0f);
}

void CShaderAPIVulkan::Rotate(float angle, float x, float y, float z) {
  Vector axis(x, y, z);
  VMatrix rot = SetupMatrixAxisRot(axis, angle);
  auto &st = se::vulkan::SeVkMatrix_Global();
  st.current[st.mode] = st.current[st.mode] * rot;
}

void CShaderAPIVulkan::Translate(float x, float y, float z) {
  VMatrix t = SetupMatrixTranslation(Vector(x, y, z));
  auto &st = se::vulkan::SeVkMatrix_Global();
  st.current[st.mode] = st.current[st.mode] * t;
}

void CShaderAPIVulkan::Scale(float x, float y, float z) {
  VMatrix s = SetupMatrixScale(Vector(x, y, z));
  auto &st = se::vulkan::SeVkMatrix_Global();
  st.current[st.mode] = st.current[st.mode] * s;
}

void CShaderAPIVulkan::ScaleXY(float x, float y) {
  Scale(x, y, 1.0f);
}

void CShaderAPIVulkan::SetStandardVertexShaderConstants(float fOverbright) {
  se::vulkan::SeVkMatrix_SetOverbright(fOverbright);
  auto &bind = se::vulkan::SeVkBind_Global();
  bind.vsConsts[0][0] = 0.0f;
  bind.vsConsts[0][1] = 1.0f;
  bind.vsConsts[0][2] = 2.0f;
  bind.vsConsts[0][3] = 0.5f;
  float ob = se::vulkan::SeVkMatrix_Overbright();
  bind.vsConsts[1][0] = 1.0f / 2.2f;
  bind.vsConsts[1][1] = ob;
  bind.vsConsts[1][2] = 1.0f / 3.0f;
  bind.vsConsts[1][3] = 1.0f / ob;
  bind.vsDirty = true;
}

void CShaderAPIVulkan::LoadBoneMatrix(int boneIndex, const float *m) {
  if (!m || boneIndex != 0) {
    return;  // S2.8: only the base MODEL matrix; skinning is follow-up.
  }
  // DX9 stores the transposed 3x4 into the MODEL stack slot.
  auto &st = se::vulkan::SeVkMatrix_Global();
  VMatrix &dst = st.current[10];  // MATERIAL_MODEL
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 4; ++j) {
      dst.m[i][j] = m[j * 3 + i];
    }
  }
  dst.m[3][0] = dst.m[3][1] = dst.m[3][2] = 0.0f;
  dst.m[3][3] = 1.0f;
}

// Hand-implemented S2.8-textures: texture handle table + uploads + binds.
// The engine delivers decoded texels through TexImageFromVTF (whole VTF);
// BindTexture resolves sampler bindings to views for the frame's draws.
// See se_vk_texcache.h for scope (2D, one linear sampler, no render
// targets yet).
namespace {
// Mirrors DX9's m_ModifyTextureHandle: ModifyTexture records the target,
// TexImageFromVTF consumes it.
ShaderAPITextureHandle_t g_ModifyHandle =
    (ShaderAPITextureHandle_t)-1;
}  // namespace

ShaderAPITextureHandle_t CShaderAPIVulkan::CreateTexture(
    int width, int height, int depth, ImageFormat dstImageFormat,
    int numMipLevels, int numCopies, int flags, const char *pDebugName,
    const char *pTextureGroupName) {
  (void)depth;
  (void)dstImageFormat;
  (void)flags;
  (void)pDebugName;
  (void)pTextureGroupName;
  return se::vulkan::SeVkTexCache_Create(width, height, numMipLevels,
                                        numCopies);
}

void CShaderAPIVulkan::DeleteTexture(ShaderAPITextureHandle_t textureHandle) {
  // Drop any frame-current bindings first (views die with the entry).
  auto &bind = se::vulkan::SeVkBind_Global();
  for (int i = 0; i < 16; ++i) {
    if (bind.boundTexHandle[i] == (intp)textureHandle) {
      bind.boundTexHandle[i] = -1;
      bind.boundTexStd[i] = -1;
    }
  }
  se::vulkan::SeVkTexCache_Delete(textureHandle);
}

void CShaderAPIVulkan::ModifyTexture(ShaderAPITextureHandle_t textureHandle) {
  // Multi-copy ping-pong is follow-up; record the target for TexImageFromVTF
  // (mirrors DX9's m_ModifyTextureHandle).
  g_ModifyHandle = textureHandle;
}

void CShaderAPIVulkan::TexImageFromVTF(IVTFTexture *pVTF, int iVTFFrame) {
  se::vulkan::SeVkTexCache_UploadVTF(g_ModifyHandle, pVTF, iVTFFrame);
}

void CShaderAPIVulkan::TexImage2D(int level, int cubeFaceID,
                                  ImageFormat dstFormat, int zOffset, int width,
                                  int height, ImageFormat srcFormat,
                                  bool bSrcIsTiled, void *imageData) {
  // Raw-bits upload behind standard textures and procedurals (see
  // SeVkTexCache_UploadRaw for scope: face/slice 0 approx).
  if (cubeFaceID != 0) {
    static bool loggedFace = false;
    if (!loggedFace) {
      loggedFace = true;
      Warning("[vulkan] texture: cubemap face %d aliased to face 0 "
              "(follow-up)\n",
              cubeFaceID);
    }
  }
  if (zOffset != 0) {
    static bool loggedSlice = false;
    if (!loggedSlice) {
      loggedSlice = true;
      Warning("[vulkan] texture: volume slice %d aliased to slice 0 "
              "(follow-up)\n",
              zOffset);
    }
  }
  (void)bSrcIsTiled;  // D3D tiled-memory flag; PC uploads are linear.
  se::vulkan::SeVkTexCache_UploadRaw(g_ModifyHandle, level, 0, 0, width,
                                     height, dstFormat, srcFormat, imageData,
                                     0);
}

void CShaderAPIVulkan::TexSubImage2D(int level, int cubeFaceID, int xOffset,
                                     int yOffset, int zOffset, int width,
                                     int height, ImageFormat srcFormat,
                                     int srcStride, bool bSrcIsTiled,
                                     void *imageData) {
  // Partial update of the modify-handle texture (procedurals). The upload
  // targets dstFormat == srcFormat (sub-images never convert).
  if (cubeFaceID != 0 || zOffset != 0) {
    static bool loggedSub = false;
    if (!loggedSub) {
      loggedSub = true;
      Warning("[vulkan] texture: sub-image face/slice aliased to 0 "
              "(follow-up)\n");
    }
  }
  (void)bSrcIsTiled;
  se::vulkan::SeVkTexCache_UploadRaw(g_ModifyHandle, level, xOffset, yOffset,
                                     width, height, srcFormat, srcFormat,
                                     imageData, srcStride);
}

bool CShaderAPIVulkan::IsTexture(ShaderAPITextureHandle_t textureHandle) {
  return se::vulkan::SeVkTexCache_Valid(textureHandle);
}

bool CShaderAPIVulkan::IsTextureResident(
    ShaderAPITextureHandle_t textureHandle) {
  return se::vulkan::SeVkTexCache_Valid(textureHandle);
}

#include <cstdio>

void CShaderAPIVulkan::BindTexture(Sampler_t sampler,
                                   ShaderAPITextureHandle_t textureHandle) {
  if (sampler < 0 || sampler >= 16) {
    return;
  }
  auto &bind = se::vulkan::SeVkBind_Global();
  bind.boundTexHandle[sampler] = (intp)textureHandle;
  bind.boundTexStd[sampler] = -1;
}

void CShaderAPIVulkan::BindStandardTexture(Sampler_t sampler,
                                           StandardTextureId_t id) {
  if (sampler < 0 || sampler >= 16) {
    return;
  }
  auto &bind = se::vulkan::SeVkBind_Global();
  bind.boundTexHandle[sampler] = -1;
  bind.boundTexStd[sampler] = (int)id;
}

void CShaderAPIVulkan::CreateTextures(
    ShaderAPITextureHandle_t *pHandles, int count, int width, int height,
    int depth, ImageFormat dstImageFormat, int numMipLevels, int numCopies,
    int flags, const char *pDebugName, const char *pTextureGroupName) {
  for (int i = 0; i < count; ++i) {
    pHandles[i] = CreateTexture(width, height, depth, dstImageFormat,
                                numMipLevels, numCopies, flags, pDebugName,
                                pTextureGroupName);
  }
}

bool CShaderAPIVulkan::CanDownloadTextures(void) const {
  // S2.8-textures: the whole-VTF upload path (TexImageFromVTF) is live.
  return true;
}

// S2.8-textures: real hardware limits. The zero-stubs these replaced caused
// CTexture::ComputeActualDimensions to clamp every texture to 4x4, after
// which procedural regenerators (e.g. the 128x256 color-correction lookup
// writer) overflowed their VTF buffers and smashed the heap. Limits come
// from the physical device; aspect ratio stays permissive so no content is
// ever aspect-clamped into a mismatched writer.
namespace {
VkPhysicalDeviceLimits g_CachedLimits{};
bool g_CachedLimitsValid = false;
const VkPhysicalDeviceLimits &SeVkLimits() {
  if (!g_CachedLimitsValid && g_VkContext.physical != VK_NULL_HANDLE) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(g_VkContext.physical, &props);
    g_CachedLimits = props.limits;
    g_CachedLimitsValid = true;
  }
  return g_CachedLimits;
}
}  // namespace

int CShaderAPIVulkan::MaxTextureWidth(void) const {
  const auto &lim = SeVkLimits();
  return lim.maxImageDimension2D > 0 ? (int)lim.maxImageDimension2D : 4096;
}

int CShaderAPIVulkan::MaxTextureHeight(void) const {
  const auto &lim = SeVkLimits();
  return lim.maxImageDimension2D > 0 ? (int)lim.maxImageDimension2D : 4096;
}

int CShaderAPIVulkan::MaxTextureDepth(void) const {
  const auto &lim = SeVkLimits();
  return lim.maxImageDimension3D > 0 ? (int)lim.maxImageDimension3D : 512;
}

int CShaderAPIVulkan::MaxTextureAspectRatio(void) const { return 8192; }

int CShaderAPIVulkan::GetMaxLights(void) const { return 8; }

int CShaderAPIVulkan::MaxBlendMatrices(void) const { return 4; }

int CShaderAPIVulkan::MaxVertexShaderBlendMatrices(void) const {
  // Mirrors the DX9 formula cap (NUM_MODEL_TRANSFORMS); our VS UBO carries
  // the same 256-vec4 constant file the formula assumes.
  return 53;
}

size_t CShaderAPIVulkan::TextureMemorySize(void) const {
  return (size_t)512 << 20;
}

namespace {
// Single source for both GetBackBufferDimensions overrides below.
void SwapchainBackBufferDims(int &width, int &height) {
  VkExtent2D e = se::vulkan::SeVkSwapchain_LastExtent();
  width = (int)e.width;
  height = (int)e.height;
}
}  // namespace

void CShaderAPIVulkan::GetBackBufferDimensions(int &width,
                                               int &height) const {
  // Live swapchain extent (1x1 before first SetMode; callers tolerate it,
  // and ones terminate the engine's halve loops instead of spinning).
  SwapchainBackBufferDims(width, height);
}

void CShaderDeviceVulkan::GetBackBufferDimensions(int &width,
                                                  int &height) const {
  // Same source: CMaterialSystem delegates here for render-target sizing.
  SwapchainBackBufferDims(width, height);
}

bool CShaderAPIVulkan::TexLock(int level, int cubeFaceID, int xOffset,
                               int yOffset, int width, int height,
                               CPixelWriter &writer) {
  if (cubeFaceID != 0) {
    return false;
  }
  // CPU mirror behind the modify-handle texture; Unlock uploads it.
  return se::vulkan::SeVkTexCache_Lock(g_ModifyHandle, level, xOffset,
                                       yOffset, width, height, writer);
}

void CShaderAPIVulkan::TexUnlock(void) {
  se::vulkan::SeVkTexCache_Unlock();
}

IMesh *CShaderDeviceVulkan::CreateStaticMesh(VertexFormat_t vertexFormat,
                                             const char *pTextureBudgetGroup,
                                             IMaterial *pMaterial) {
  (void)vertexFormat;
  (void)pTextureBudgetGroup;
  // Persistent mesh: studio static props and world geometry lock once via
  // LockMesh/UnlockMesh, then Draw re-submits every frame (S2.8 fixed
  // vertex format, same as the dynamic mesh).
  auto *mesh = new se::vulkan::SeVkStaticMesh();
  mesh->SetMaterial(pMaterial);
  return mesh;
}

void CShaderDeviceVulkan::DestroyStaticMesh(IMesh *mesh) {
  delete static_cast<se::vulkan::SeVkStaticMesh *>(mesh);
}

// S2.9: CMatQueuedMesh::LockMesh builds its descriptor through this hook
// (the queued render context is what VGUI and the world draw through). The
// neutral stub left every pointer/stride zero and the first vertex write
// segfaulted. Same helper the DX9 backend uses.
void CShaderAPIVulkan::ComputeVertexDescription(unsigned char *pBuffer,
                                                VertexFormat_t vertexFormat,
                                                MeshDesc_t &desc) const {
  ComputeVertexDesc( pBuffer, vertexFormat, desc );
}

// The queued render context sizes its scratch buffers from
// IMaterial::GetVertexFormat(), which the shader system fills from here. With
// the neutral stub every material reported format 0, so
// ComputeVertexDescription(0, 0, desc) produced a zero vertex size and
// GetMaxVerticesToRender divided by zero on the first world frame.
//
// DX9 aggregates each pass's usage out of its transition table, which this
// backend does not keep. shortcut: one fixed format for every material; the
// descriptor is generated from this same format, so buffer size and writes
// stay consistent. Upgrade: store m_VertexUsage per SeVkSnapshot and
// aggregate like ComputeVertexUsage does.
VertexFormat_t CShaderAPIVulkan::ComputeVertexUsage(int num,
                                                    StateSnapshot_t *pIds) const {
  (void)num;
  (void)pIds;
  return VERTEX_POSITION | VERTEX_NORMAL | VERTEX_COLOR |
         VERTEX_TEXCOORD_SIZE( 0, 2 ) | VERTEX_FORMAT_VERTEX_SHADER;
}

VertexFormat_t CShaderAPIVulkan::ComputeVertexFormat(int num,
                                                    StateSnapshot_t *pIds) const {
  return ComputeVertexUsage( num, pIds );
}

int CShaderAPIVulkan::GetCurrentDynamicVBSize(void) {
  return DYNAMIC_VERTEX_BUFFER_MEMORY;
}

// Same value CMatQueuedRenderContext::GetMaxIndicesToRender returns. The
// neutral stub returned 0, and callers use this as a batch ceiling: the decal
// batcher (r_decal.cpp) computed a zero index limit, so its first overflow
// check fired before any mesh was begun and CMeshBuilder::End dereferenced a
// null mesh.
int CShaderAPIVulkan::GetMaxIndicesToRender(void) {
  return INDEX_BUFFER_SIZE;
}
