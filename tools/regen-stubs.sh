#!/bin/bash
# regen-stubs.sh -- regenerate shaderapivulkan neutral stubs (S2.2 scaffolding).
# Run after editing tools/vkstubgen.py. Outputs are checked in.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/materialsystem/shaderapivulkan"
G="$ROOT/tools/vkstubgen.py"
O="$ROOT/materialsystem/shaderapivulkan"
P="$ROOT/public"
python3 "$G" CShaderAPIVulkan \
  "$P/shaderapi/ishaderapi.h" --class IShaderAPI \
  "$P/shaderapi/ishaderdynamic.h" --class IShaderDynamicAPI \
  "$ROOT/materialsystem/IHardwareConfigInternal.h" --class IHardwareConfigInternal \
  "$P/materialsystem/imaterialsystemhardwareconfig.h" --class IMaterialSystemHardwareConfig \
  "$P/materialsystem/idebugtextureinfo.h" --class IDebugTextureInfo \
  --base IShaderAPI --base IHardwareConfigInternal --base IDebugTextureInfo \
  --skip SetMode,GetMaxDXSupportLevel,SupportsShaderModel_3_0,SupportsPixelShaders_2_0,SupportsVertexShaders_2_0,SupportsPixelShaders_2_b,ActuallySupportsPixelShaders_2_b,GetDXSupportLevel,BindVertexShader,BindPixelShader,SetVertexShaderConstant,SetPixelShaderConstant,GetDynamicMesh,GetDynamicMeshEx,TakeSnapshot,BeginPass,SetVertexShaderIndex,SetPixelShaderIndex,ExecuteCommandBuffer,SetViewports,GetViewports,MatrixMode,PushMatrix,PopMatrix,LoadMatrix,MultMatrix,MultMatrixLocal,GetMatrix,LoadIdentity,LoadCameraToWorld,Ortho,PerspectiveX,PerspectiveOffCenterX,PickMatrix,Rotate,Translate,Scale,ScaleXY,SetStandardVertexShaderConstants,LoadBoneMatrix,CreateTexture,CreateTextures,DeleteTexture,ModifyTexture,TexImageFromVTF,TexImage2D,TexSubImage2D,TexLock,TexUnlock,ComputeVertexDescription,ComputeVertexUsage,ComputeVertexFormat,GetCurrentDynamicVBSize,GetMaxIndicesToRender,BindTexture,BindStandardTexture,IsTexture,IsTextureResident,CanDownloadTextures,GetBackBufferDimensions,MaxTextureWidth,MaxTextureHeight,MaxTextureDepth,MaxTextureAspectRatio,GetMaxLights,MaxBlendMatrices,MaxVertexShaderBlendMatrices,TextureMemorySize \
  --out "$O/api_stubs"
python3 "$G" CShaderDeviceVulkan \
  "$P/shaderapi/IShaderDevice.h" --class IShaderDevice \
  --base IShaderDevice \
  --skip Present,GetBackBufferDimensions,CreateStaticMesh,DestroyStaticMesh,CreateVertexShader,DestroyVertexShader,CreatePixelShader,DestroyPixelShader,CreateGeometryShader,DestroyGeometryShader,IsUsingGraphics \
  --out "$O/device_stubs"
python3 "$G" CShaderDeviceMgrVulkan \
  "$P/shaderapi/IShaderDevice.h" --class IShaderDeviceMgr \
  "$P/appframework/IAppSystem.h" --class IAppSystem \
  --base IShaderDeviceMgr \
  --skip Connect,Init,SetAdapter,SetMode,QueryInterface,GetRecommendedConfigurationInfo \
  --out "$O/devmgr_stubs"
python3 "$G" CShaderShadowVulkan \
  "$P/shaderapi/ishadershadow.h" --class IShaderShadow \
  --base IShaderShadow \
  --skip SetVertexShader,SetPixelShader,EnableBlending \
  --out "$O/shadow_stubs"
grep -c '^.*::.*(' "$O"/*.cpp
