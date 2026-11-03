// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_matrix.h -- Phase 2 native Vulkan backend, step S2.8-matrix.
//
// CPU-side matrix stacks (VIEW/PROJECTION/TEXTURE0-7/MODEL) backing the
// IShaderAPI matrix methods, plus the per-draw constant commit that feeds
// the DynConsts UBO. Mirrors the DX9 backend exactly: row-major VMatrix
// storage, MultMatrix post-multiplies (Top = Top * m), MultMatrixLocal
// pre-multiplies, and uploads are the TRANSPOSED combined matrices
// (view*proj -> c8, model -> c58), matching CShaderAPIDx8's commit.

#ifndef SE_VK_MATRIX_H_
#define SE_VK_MATRIX_H_

#include <vector>

#include "mathlib/vmatrix.h"

namespace se::vulkan {

struct SeVkMatrixStacks {
  static constexpr int kModes = 11;  // NUM_MATRIX_MODES
  static constexpr int kDepth = 32;
  int mode = 0;  // MATERIAL_VIEW until the engine selects otherwise
  VMatrix current[kModes];
  std::vector<VMatrix> stack[kModes];
};

// Process-global stacks (the engine's IShaderAPI is itself a singleton).
SeVkMatrixStacks &SeVkMatrix_Global();

// Per-draw commit: writes VIEWPROJ (c8-11) + third row (c13), MODELVIEWPROJ
// (c4-7), MODEL rows (c58-60) into vsConsts, marking dirty. Mirrors the DX9
// per-pass commit exactly (same slots, same transpose convention).
// Deliberately does NOT touch math constants (c0/c1, owned by
// SetStandardVertexShaderConstants), modulation (c47, white default) or
// shader-specific slots (command-buffer/dynamic uploads win).
// vsEngine[slot] marks slots the engine wrote this frame (257-uploads,
// direct constants); those win over the matrix-stack fallback below.
void SeVkMatrix_Commit(float vsConsts[256][4], const bool vsEngine[256],
                       bool &dirty);

// Overbright factor for the math constants (SetStandardVertexShaderConstants).
void SeVkMatrix_SetOverbright(float overbright);
float SeVkMatrix_Overbright();

}  // namespace se::vulkan

#endif  // !SE_VK_MATRIX_H_
