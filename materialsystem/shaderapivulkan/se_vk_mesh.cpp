// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_mesh.cpp -- Phase 2 native Vulkan backend, step S2.8-bind.
//
// Original code implementing the Source IMesh contract against the S2.8
// bind path (se_vk_bind.h).

#include "se_vk_mesh.h"

#include <cstring>

#include "imaterialinternal.h"
#include "materialsystem/imaterialsystem.h"

#include "se_vk_bind.h"
#include "tier0/dbg.h"

namespace se::vulkan {

void ConvertLocked(const std::vector<SeVkCpuVert> &lockBuf,
                   const std::vector<uint16_t> &lockedIdx, int lockedVerts,
                   int lockedIdxCount, int nVertexCount, int nIndexCount,
                   std::vector<SeVkBindVertex> &outVerts,
                   std::vector<uint32_t> &outIdx);

SeVkDynamicMesh::SeVkDynamicMesh() = default;
SeVkDynamicMesh::~SeVkDynamicMesh() = default;

SeVkDynamicMesh &SeVkMesh_Global() {
  static SeVkDynamicMesh mesh;
  return mesh;
}

void SeVkDynamicMesh::SetPrimitiveType(MaterialPrimitiveType_t type) {
  type_ = type;
}

// Clamps (first,count) into [0, total); false = empty range.
bool ClampDrawRange(size_t total, int nFirstIndex, int nIndexCount,
                    uint32_t &first, uint32_t &count) {
  first = 0;
  count = static_cast<uint32_t>(total);
  if (nFirstIndex >= 0) {
    first = static_cast<uint32_t>(nFirstIndex);
    if (first >= count) {
      return false;
    }
    if (nIndexCount > 0 &&
        first + static_cast<uint32_t>(nIndexCount) < count) {
      count = static_cast<uint32_t>(nIndexCount);
    } else {
      count -= first;
    }
  }
  return count > 0;
}

// Binds the material's shaders, then commits verts into the frame.
void CommitMesh(IMaterial *material, const SeVkBindVertex *verts,
                const uint32_t *idx, uint32_t first, uint32_t count,
                size_t nVerts) {
  if (material) {
    auto *internal = static_cast<IMaterialInternal *>(material);
    internal->DrawMesh(VERTEX_COMPRESSION_NONE);
  }
  SeVkBind &bind = SeVkBind_Global();
  SeVkBind_PushMesh(&bind, verts, nVerts, idx + first, count, bind.boundVS,
                    bind.boundPS);
}

// The batched world path keeps vertices and indices in two separate IMesh
// objects (BeginBatch index mesh + BindBatch vertex mesh). Returns the index
// array this mesh should draw with: its own unless an index source was set.
const std::vector<uint32_t> &DrawIndexArray(const SeVkDynamicMesh *me,
                                            const std::vector<uint32_t> &own) {
  if (me->IndexSource() && !me->IndexSource()->IndexArray().empty()) {
    return me->IndexSource()->IndexArray();
  }
  return own;
}

void SeVkStaticMesh::ModifyEnd(MeshDesc_t &desc) {
  (void)desc;
  if (modFlag_) {
    modFlag_ = false;
    std::vector<SeVkBindVertex> newVerts;
    std::vector<uint32_t> newIdx;
    ConvertLocked(lockBuf_, lockedIdx_, lockedVerts_, lockedIdxCount_,
                  lockedVerts_, lockedIdxCount_, newVerts, newIdx);
    verts_ = std::move(newVerts);
    idx_ = std::move(newIdx);
  }
}

void SeVkDynamicMesh::Draw(int nFirstIndex, int nIndexCount) {
  if (!hasPending_ || pendingVerts_.empty()) {
    return;
  }
  const std::vector<uint32_t> &idx = DrawIndexArray(this, pendingIdx_);
  uint32_t first = 0, count = 0;
  if (!ClampDrawRange(idx.size(), nFirstIndex, nIndexCount, first, count)) {
    return;
  }
  // Drive the material path first (snapshot -> BeginPass binds the pack
  // pair, dynamic constants upload); then commit with the bound pair.
  CommitMesh(material_, pendingVerts_.data(), idx.data(), first, count,
             pendingVerts_.size());
  hasPending_ = false;
  pendingVerts_.clear();
  pendingIdx_.clear();
}

void SeVkDynamicMesh::FillDesc(MeshDesc_t &desc) {
  memset(&desc, 0, sizeof(desc));
  constexpr int kStride = sizeof(SeVkCpuVert);
  desc.m_VertexSize_Position = kStride;
  desc.m_VertexSize_Normal = kStride;
  desc.m_VertexSize_Color = kStride;
  desc.m_VertexSize_TexCoord[0] = kStride;
  desc.m_VertexSize_TexCoord[1] = kStride;
  desc.m_VertexSize_TexCoord[2] = kStride;
  desc.m_VertexSize_TexCoord[3] = kStride;
  desc.m_VertexSize_TangentS = kStride;
  desc.m_VertexSize_TangentT = kStride;
  desc.m_VertexSize_UserData = kStride;
  desc.m_VertexSize_BoneWeight = kStride;
  desc.m_VertexSize_BoneMatrixIndex = kStride;
  desc.m_ActualVertexSize = kStride;
  desc.m_NumBoneWeights = 2;
  desc.m_nFirstVertex = 0;
  desc.VertexDesc_t::m_nOffset = 0;
  if (!lockBuf_.empty()) {
    desc.m_pPosition = lockBuf_.data()->pos;
    desc.m_pNormal = lockBuf_.data()->normal;
    desc.m_pColor = lockBuf_.data()->color;
    desc.m_pTexCoord[0] = lockBuf_.data()->tex0;
    desc.m_pTexCoord[1] = lockBuf_.data()->tex1;
    desc.m_pTexCoord[2] = lockBuf_.data()->tex2;
    desc.m_pTexCoord[3] = lockBuf_.data()->tex3;
    desc.m_pTangentS = lockBuf_.data()->tanS;
    desc.m_pTangentT = lockBuf_.data()->tanT;
    desc.m_pUserData = lockBuf_.data()->userData;
    desc.m_pBoneWeight = lockBuf_.data()->boneWeight;
    desc.m_pBoneMatrixIndex = lockBuf_.data()->boneMatrixIndex;
  }
  desc.m_pIndices = lockedIdx_.empty() ? nullptr : lockedIdx_.data();
  // Element stride, not bytes: CIndexBuilder::AdvanceIndex adds this to a
  // buffer index, and m_nMaxIndexCount counts indices. DX8 uses 1 for
  // 16-bit indices (meshdx8.cpp); a 2 here walks past the locked count and
  // smashes the buffer.
  desc.m_nIndexSize = 1;
  desc.m_nFirstIndex = 0;
}

void SeVkDynamicMesh::LockMesh(int nVertexCount, int nIndexCount,
                               MeshDesc_t &desc) {
  // Over-allocate: engine index/vertex writers can run slightly past the
  // locked counts (D3D buffers have granularity slack; exact-size vectors
  // turn that into heap corruption). Slack stays inside our buffers.
  constexpr int kVertSlack = 256;
  constexpr int kIdxSlack = 1024;
  lockBuf_.resize(nVertexCount > 0 ? nVertexCount + kVertSlack : 0);
  lockedIdx_.resize(nIndexCount > 0 ? nIndexCount + kIdxSlack : 0);
  lockedFlag_ = true;
  lockedVerts_ = nVertexCount;
  lockedIdxCount_ = nIndexCount;
  FillDesc(desc);
  if (!lockBuf_.empty()) {
    // Deterministic defaults: zero everything (unused texcoord sets sample
    // texel (0,0)), then opaque-white vertex color (see below).
    memset(lockBuf_.data(), 0,
           lockBuf_.size() * sizeof(lockBuf_[0]));
    // Default vertex color: opaque white. DrawScreenSpaceRectangle and
    // friends never set color; garbage alpha + alpha blending would make
    // their quads invisible. Writers overwrite as needed.
    for (auto &v : lockBuf_) {
      v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
    }
  }
  modFlag_ = false;
}

void SeVkDynamicMesh::UnlockMesh(int nVertexCount, int nIndexCount,
                                 MeshDesc_t &desc) {
  (void)desc;
  if (!lockedFlag_) {
    return;
  }
  lockedFlag_ = false;
  if (hasPending_) {
    // Previous mesh never drawn; drop it (stays out of the frame).
    pendingVerts_.clear();
    pendingIdx_.clear();
  }
  ConvertLocked(lockBuf_, lockedIdx_, lockedVerts_, lockedIdxCount_,
                nVertexCount, nIndexCount, pendingVerts_, pendingIdx_);
  hasPending_ = true;
}

void ConvertLocked(const std::vector<SeVkCpuVert> &lockBuf,
                   const std::vector<uint16_t> &lockedIdx, int lockedVerts,
                   int lockedIdxCount, int nVertexCount, int nIndexCount,
                   std::vector<SeVkBindVertex> &outVerts,
                   std::vector<uint32_t> &outIdx) {
  if (nVertexCount > lockedVerts) {
    nVertexCount = lockedVerts;
  }
  if (nIndexCount > lockedIdxCount) {
    nIndexCount = lockedIdxCount;
  }
  outVerts.reserve(nVertexCount);
  for (int i = 0; i < nVertexCount; ++i) {
    const SeVkCpuVert &v = lockBuf[i];
    SeVkBindVertex b{};
    b.pos[0] = v.pos[0];
    b.pos[1] = v.pos[1];
    b.pos[2] = v.pos[2];
    b.pos[3] = 1.0f;
    b.col[0] = v.color[0] / 255.0f;
    b.col[1] = v.color[1] / 255.0f;
    b.col[2] = v.color[2] / 255.0f;
    b.col[3] = v.color[3] / 255.0f;
    b.nrm[0] = v.normal[0];
    b.nrm[1] = v.normal[1];
    b.nrm[2] = v.normal[2];
    b.nrm[3] = 0.0f;
    // Each texcoord set passes through to its own location. Replicating set
    // 0 everywhere made the world sample its lightmap with base-texture
    // coordinates, so lighting came back black.
    b.uv0[0] = v.tex0[0];
    b.uv1[0] = v.tex1[0];
    b.uv2[0] = v.tex2[0];
    b.uv3[0] = v.tex3[0];
    b.uv0[1] = v.tex0[1];
    b.uv1[1] = v.tex1[1];
    b.uv2[1] = v.tex2[1];
    b.uv3[1] = v.tex3[1];
    b.uv0[2] = b.uv1[2] = b.uv2[2] = b.uv3[2] = 0.0f;
    b.uv0[3] = b.uv1[3] = b.uv2[3] = b.uv3[3] = 1.0f;
    outVerts.push_back(b);
  }
  outIdx.reserve(nIndexCount);
  for (int i = 0; i < nIndexCount; ++i) {
    outIdx.push_back(lockedIdx[i]);
  }
}

void SeVkStaticMesh::UnlockMesh(int nVertexCount, int nIndexCount,
                                MeshDesc_t &desc) {
  (void)desc;
  if (!lockedFlag_) {
    return;
  }
  lockedFlag_ = false;
  std::vector<SeVkBindVertex> newVerts;
  std::vector<uint32_t> newIdx;
  ConvertLocked(lockBuf_, lockedIdx_, lockedVerts_, lockedIdxCount_,
                nVertexCount, nIndexCount, newVerts, newIdx);
  // The batched world path re-locks a prebuilt vertex mesh with
  // nVertexCount == 0 purely to append this frame's indices; clobbering the
  // stored verts there erases the whole map.
  if (nVertexCount > 0) {
    verts_ = std::move(newVerts);
  }
  idx_ = std::move(newIdx);
}

void SeVkStaticMesh::Draw(int nFirstIndex, int nIndexCount) {
  if (verts_.empty()) {
    return;
  }
  const std::vector<uint32_t> &idx = DrawIndexArray(this, idx_);
  uint32_t first = 0, count = 0;
  if (!ClampDrawRange(idx.size(), nFirstIndex, nIndexCount, first, count)) {
    return;
  }
  CommitMesh(material_, verts_.data(), idx.data(), first, count,
             verts_.size());
}

// --- IMesh neutral methods ---

void SeVkDynamicMesh::SetColorMesh(IMesh *pColorMesh, int nVertexOffset) {
  (void)pColorMesh;
  (void)nVertexOffset;
}

void SeVkDynamicMesh::Draw(CPrimList *pLists, int nLists) {
  (void)pLists;
  (void)nLists;
}

void SeVkDynamicMesh::CopyToMeshBuilder(int iStartVert, int nVerts,
                                        int iStartIndex, int nIndices,
                                        int indexOffset,
                                        CMeshBuilder &builder) {
  (void)iStartVert;
  (void)nVerts;
  (void)iStartIndex;
  (void)nIndices;
  (void)indexOffset;
  (void)builder;
}

void SeVkDynamicMesh::Spew(int nVertexCount, int nIndexCount,
                            const MeshDesc_t &desc) {
  (void)nVertexCount;
  (void)nIndexCount;
  (void)desc;
}

void SeVkDynamicMesh::ValidateData(int nVertexCount, int nIndexCount,
                                   const MeshDesc_t &desc) {
  (void)nVertexCount;
  (void)nIndexCount;
  (void)desc;
}

void SeVkDynamicMesh::ExposeForModify(int nFirstVertex, int nVertexCount,
                                      int nFirstIndex, int nIndexCount,
                                      MeshDesc_t &desc) {
  if (nFirstVertex < 0) {
    nFirstVertex = 0;
  }
  if (nVertexCount < 0) {
    nVertexCount = 0;
  }
  if (nFirstIndex < 0) {
    nFirstIndex = 0;
  }
  if (nIndexCount < 0) {
    nIndexCount = 0;
  }
  size_t needV = (size_t)nFirstVertex + (size_t)nVertexCount;
  size_t needI = (size_t)nFirstIndex + (size_t)nIndexCount;
  if (lockBuf_.size() < needV) {
    lockBuf_.resize(needV, SeVkCpuVert{});
  }
  if (lockedIdx_.size() < needI) {
    lockedIdx_.resize(needI, 0);
  }
  if ((int)lockBuf_.size() > lockedVerts_) {
    lockedVerts_ = (int)lockBuf_.size();
  }
  if ((int)lockedIdx_.size() > lockedIdxCount_) {
    lockedIdxCount_ = (int)lockedIdx_.size();
  }
  FillDesc(desc);
  desc.m_nFirstVertex = (unsigned int)nFirstVertex;
  desc.m_nFirstIndex = (unsigned int)nFirstIndex;
  modFlag_ = true;
}

void SeVkDynamicMesh::RefreshFromLocked() {
  pendingVerts_.clear();
  pendingIdx_.clear();
  ConvertLocked(lockBuf_, lockedIdx_, lockedVerts_, lockedIdxCount_,
                lockedVerts_, lockedIdxCount_, pendingVerts_, pendingIdx_);
  hasPending_ = !pendingVerts_.empty() && !pendingIdx_.empty();
}

void SeVkDynamicMesh::ModifyBegin(int nFirstVertex, int nVertexCount,
                                  int nFirstIndex, int nIndexCount,
                                  MeshDesc_t &desc) {
  ExposeForModify(nFirstVertex, nVertexCount, nFirstIndex, nIndexCount, desc);
}

void SeVkDynamicMesh::ModifyEnd(MeshDesc_t &desc) {
  (void)desc;
  if (modFlag_) {
    modFlag_ = false;
    RefreshFromLocked();
  }
}

void SeVkDynamicMesh::ModifyBeginEx(bool bReadOnly, int nFirstVertex,
                                    int nVertexCount, int nFirstIndex,
                                    int nIndexCount, MeshDesc_t &desc) {
  (void)bReadOnly;
  ExposeForModify(nFirstVertex, nVertexCount, nFirstIndex, nIndexCount, desc);
}

void SeVkDynamicMesh::SetFlexMesh(IMesh *pMesh, int nVertexOffset) {
  (void)pMesh;
  (void)nVertexOffset;
}

void SeVkDynamicMesh::DisableFlexMesh() {}

void SeVkDynamicMesh::MarkAsDrawn() {}

unsigned SeVkDynamicMesh::ComputeMemoryUsed() { return 0; }

// --- IVertexBuffer / IIndexBuffer neutral methods ---

int SeVkDynamicMesh::VertexCount() const { return lockedVerts_; }

VertexFormat_t SeVkDynamicMesh::GetVertexFormat() const { return 0; }

bool SeVkDynamicMesh::IsDynamic() const { return true; }

void SeVkDynamicMesh::BeginCastBuffer(VertexFormat_t format) {
  (void)format;
}

void SeVkDynamicMesh::BeginCastBuffer(MaterialIndexFormat_t format) {
  (void)format;
}

void SeVkDynamicMesh::EndCastBuffer() {}

int SeVkDynamicMesh::GetRoomRemaining() const { return 1 << 20; }

bool SeVkDynamicMesh::Lock(int nVertexCount, bool bAppend,
                            VertexDesc_t &desc) {
  (void)nVertexCount;
  (void)bAppend;
  (void)desc;
  return false;
}

void SeVkDynamicMesh::Unlock(int nVertexCount, VertexDesc_t &desc) {
  (void)nVertexCount;
  (void)desc;
}

void SeVkDynamicMesh::Spew(int nVertexCount, const VertexDesc_t &desc) {
  (void)nVertexCount;
  (void)desc;
}

void SeVkDynamicMesh::ValidateData(int nVertexCount,
                                   const VertexDesc_t &desc) {
  (void)nVertexCount;
  (void)desc;
}

int SeVkDynamicMesh::IndexCount() const { return lockedIdxCount_; }

MaterialIndexFormat_t SeVkDynamicMesh::IndexFormat() const {
  return MATERIAL_INDEX_FORMAT_16BIT;
}

bool SeVkDynamicMesh::Lock(int nMaxIndexCount, bool bAppend,
                            IndexDesc_t &desc) {
  (void)nMaxIndexCount;
  (void)bAppend;
  (void)desc;
  return false;
}

void SeVkDynamicMesh::Unlock(int nWrittenIndexCount, IndexDesc_t &desc) {
  (void)nWrittenIndexCount;
  (void)desc;
}

void SeVkDynamicMesh::ModifyBegin(bool bReadOnly, int nFirstIndex,
                                   int nIndexCount, IndexDesc_t &desc) {
  (void)bReadOnly;
  // Displacement re-tessellation and friends edit index ranges in place.
  // Expose (growing to fit); ModifyEnd refreshes the converted store.
  if (nFirstIndex < 0) {
    nFirstIndex = 0;
  }
  if (nIndexCount < 0) {
    nIndexCount = 0;
  }
  size_t need = (size_t)nFirstIndex + (size_t)nIndexCount;
  if (lockedIdx_.size() < need) {
    lockedIdx_.resize(need);
  }
  if ((int)lockedIdx_.size() > lockedIdxCount_) {
    lockedIdxCount_ = (int)lockedIdx_.size();
  }
  desc.m_pIndices = lockedIdx_.empty() ? nullptr : lockedIdx_.data();
  desc.m_nFirstIndex = (unsigned int)nFirstIndex;
  modFlag_ = true;
}

void SeVkDynamicMesh::ModifyEnd(IndexDesc_t &desc) {
  (void)desc;
  if (modFlag_) {
    modFlag_ = false;
    RefreshFromLocked();
  }
}

void SeVkDynamicMesh::Spew(int nIndexCount, const IndexDesc_t &desc) {
  (void)nIndexCount;
  (void)desc;
}

void SeVkDynamicMesh::ValidateData(int nIndexCount, const IndexDesc_t &desc) {
  (void)nIndexCount;
  (void)desc;
}

}  // namespace se::vulkan
