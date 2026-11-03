// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_mesh.h -- Phase 2 native Vulkan backend, step S2.8-bind.
//
// Single dynamic mesh behind GetDynamicMesh/GetDynamicMeshEx. LockMesh hands
// the engine CPU-side field buffers (interleaved); UnlockMesh converts to
// the fixed bind-vertex format; Draw commits the pending mesh into the
// frame with the currently bound VSxPS pair.
//
// S2.8 scope: one outstanding lock at a time (the screen-space quad path
// uses exactly that). ModifyBegin/ModifyEnd are neutral.

#ifndef SE_VK_MESH_H_
#define SE_VK_MESH_H_

#include "materialsystem/imesh.h"

#include <cstdint>
#include <vector>

namespace se::vulkan {

// CPU-side lock buffer: one interleaved vertex, per-field strides equal
// sizeof so CVertexBuilder pointer arithmetic lands correctly.
struct SeVkCpuVert {
  float pos[3];
  float normal[3];
  uint8_t color[4];
  float tex0[2];
  float tex1[2];
  float tex2[2];
  float tex3[2];
  float tanS[3];
  float tanT[3];
  // Tangent-space user data (CompressedUserData writes 4 floats/vertex;
  // studio static-mesh builds need it; S2.8 drops it at conversion).
  float userData[4];
  // Bone write-sinks (BoneMatrix/CompressedBoneWeight write here; S2.8
  // renders rigid, skinning is follow-up work).
  float boneWeight[4];
  uint8_t boneMatrixIndex[4];
};

class SeVkDynamicMesh : public IMesh {
 public:
  SeVkDynamicMesh();
  ~SeVkDynamicMesh() override;

  // Material currently being drawn into this mesh (set by GetDynamicMesh).
  // Draw() drives its DrawMesh first so snapshots bind (BeginPass) before
  // the pending verts commit with the bound pair.
  void SetMaterial(IMaterial *material) { material_ = material; }

  // Batched world path: vertices and indices live in separate IMesh objects
  // (BeginBatch's index mesh + BindBatch's vertex mesh). IndexArray() is
  // virtual because the two kinds keep indices in different stores - reading
  // a static source's dynamic array came back empty and dropped every world
  // batch on the floor.
  void SetIndexSource(SeVkDynamicMesh *src) { idxSrc_ = src; }
  SeVkDynamicMesh *IndexSource() const { return idxSrc_; }
  virtual const std::vector<uint32_t> &IndexArray() const {
    return pendingIdx_;
  }

  // IMesh.
  void SetPrimitiveType(MaterialPrimitiveType_t type) override;
  void Draw(int nFirstIndex = -1, int nIndexCount = 0) override;
  void SetColorMesh(IMesh *pColorMesh, int nVertexOffset) override;
  void Draw(CPrimList *pLists, int nLists) override;
  void CopyToMeshBuilder(int iStartVert, int nVerts, int iStartIndex,
                         int nIndices, int indexOffset,
                         CMeshBuilder &builder) override;
  void Spew(int nVertexCount, int nIndexCount,
            const MeshDesc_t &desc) override;
  void ValidateData(int nVertexCount, int nIndexCount,
                    const MeshDesc_t &desc) override;
  void LockMesh(int nVertexCount, int nIndexCount, MeshDesc_t &desc) override;
  void ModifyBegin(int nFirstVertex, int nVertexCount, int nFirstIndex,
                   int nIndexCount, MeshDesc_t &desc) override;
  virtual void ModifyEnd(MeshDesc_t &desc);
  void UnlockMesh(int nVertexCount, int nIndexCount,
                  MeshDesc_t &desc) override;
  void ModifyBeginEx(bool bReadOnly, int nFirstVertex, int nVertexCount,
                     int nFirstIndex, int nIndexCount,
                     MeshDesc_t &desc) override;
  void SetFlexMesh(IMesh *pMesh, int nVertexOffset) override;
  void DisableFlexMesh() override;
  void MarkAsDrawn() override;
  unsigned ComputeMemoryUsed() override;

  // IVertexBuffer.
  int VertexCount() const override;
  VertexFormat_t GetVertexFormat() const override;
  bool IsDynamic() const override;
  void BeginCastBuffer(VertexFormat_t format) override;
  void EndCastBuffer() override;
  bool Lock(int nVertexCount, bool bAppend, VertexDesc_t &desc) override;
  void Unlock(int nVertexCount, VertexDesc_t &desc) override;
  void Spew(int nVertexCount, const VertexDesc_t &desc) override;
  void ValidateData(int nVertexCount, const VertexDesc_t &desc) override;

  // IIndexBuffer.
  int IndexCount() const override;
  MaterialIndexFormat_t IndexFormat() const override;
  void BeginCastBuffer(MaterialIndexFormat_t format) override;
  int GetRoomRemaining() const override;
  bool Lock(int nMaxIndexCount, bool bAppend, IndexDesc_t &desc) override;
  void Unlock(int nWrittenIndexCount, IndexDesc_t &desc) override;
  void ModifyBegin(bool bReadOnly, int nFirstIndex, int nIndexCount,
                   IndexDesc_t &desc) override;
  void ModifyEnd(IndexDesc_t &desc) override;
  void Spew(int nIndexCount, const IndexDesc_t &desc) override;
  void ValidateData(int nIndexCount, const IndexDesc_t &desc) override;

 protected:
  void FillDesc(MeshDesc_t &desc);
  // In-place edit path (displacement re-tessellation): grows the buffers
  // to the requested range and points desc at them.
  void ExposeForModify(int nFirstVertex, int nVertexCount, int nFirstIndex,
                       int nIndexCount, MeshDesc_t &desc);
  void RefreshFromLocked();
  // Lock state shared with SeVkStaticMesh (below).
  MaterialPrimitiveType_t type_ = MATERIAL_TRIANGLES;
  IMaterial *material_ = nullptr;
  std::vector<SeVkCpuVert> lockBuf_;
  std::vector<uint16_t> lockedIdx_;
  bool lockedFlag_ = false;
  int lockedVerts_ = 0;
  int lockedIdxCount_ = 0;
  // Set by the Modify* pair; ModifyEnd reconverts the edited buffers.
  bool modFlag_ = false;

 private:
  // Uncommitted mesh (converted at UnlockMesh, committed at Draw).
  std::vector<struct SeVkBindVertex> pendingVerts_;
  std::vector<uint32_t> pendingIdx_;
  bool hasPending_ = false;
  SeVkDynamicMesh *idxSrc_ = nullptr;
};

class SeVkStaticMesh : public SeVkDynamicMesh {
 public:
  // Permanent mesh: UnlockMesh stores converted verts, Draw re-submits
  // them every frame (studio static props, world geometry).
  void UnlockMesh(int nVertexCount, int nIndexCount,
                  MeshDesc_t &desc) override;
  void Draw(int nFirstIndex = -1, int nIndexCount = 0) override;
  void ModifyEnd(MeshDesc_t &desc) override;
  // Static meshes keep converted indices in their own store.
  const std::vector<uint32_t> &IndexArray() const override { return idx_; }

 private:
  std::vector<struct SeVkBindVertex> verts_;
  std::vector<uint32_t> idx_;
};

SeVkDynamicMesh &SeVkMesh_Global();

}  // namespace se::vulkan

#endif  // !SE_VK_MESH_H_
