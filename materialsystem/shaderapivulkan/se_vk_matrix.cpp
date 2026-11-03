// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_matrix.cpp -- Phase 2 native Vulkan backend, step S2.8-matrix.
//
// Original code written against mathlib VMatrix (row-major, matching the
// DX9 XMMATRIX convention the engine's call streams assume).

#include "se_vk_matrix.h"

#include <cmath>
#include <cstring>

namespace se::vulkan {
namespace {

// C = A * B, row-major (row-vector convention: v' = v*A*B).
void Mul(const VMatrix &a, const VMatrix &b, VMatrix &c) {
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      c.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] +
                  a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
    }
  }
}

void Identity(VMatrix &m) {
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      m.m[i][j] = (i == j) ? 1.0f : 0.0f;
    }
  }
}

}  // namespace

SeVkMatrixStacks &SeVkMatrix_Global() {
  static SeVkMatrixStacks s;
  static bool init = false;
  if (!init) {
    init = true;
    for (int i = 0; i < SeVkMatrixStacks::kModes; ++i) {
      Identity(s.current[i]);
    }
  }
  return s;
}

void SeVkMatrix_Commit(float vsConsts[256][4], const bool vsEngine[256],
                       bool &dirty) {
  SeVkMatrixStacks &st = SeVkMatrix_Global();
  const VMatrix &view = st.current[0];    // MATERIAL_VIEW
  const VMatrix &proj = st.current[1];    // MATERIAL_PROJECTION
  const VMatrix &model = st.current[10];  // MATERIAL_MODEL
  // NOTE: index 10 is MATERIAL_MODEL (VIEW=0, PROJECTION=1, TEXTURE0-7=2-9).
  VMatrix vp;
  Mul(view, proj, vp);
  // No Y flip: the engine's matrices are already Y-down (VGUI ortho carries
  // a negative slope), and DX9 keeps its own flip disabled for the same
  // reason. Flipping here double-negated Y and pushed everything off-screen.
  VMatrix mvp;
  Mul(model, vp, mvp);
  // Upload TRANSPOSED (slot = column). The fxc shaders do
  // dp4 oPos.x, pos, c8 with pos as a row vector, so c8 must be VP column 0
  // for the ortho's -1/+1 translation to land in gl_Position.xyw.
  // Engine-written slots win; the stack only fills the rest.
  for (int i = 0; i < 4; ++i) {
    int slot = 8 + i;
    if (!vsEngine[slot]) {
      vsConsts[slot][0] = vp.m[0][i];
      vsConsts[slot][1] = vp.m[1][i];
      vsConsts[slot][2] = vp.m[2][i];
      vsConsts[slot][3] = vp.m[3][i];
      dirty = true;
    }
  }
  if (!vsEngine[13]) {
    vsConsts[13][0] = vp.m[0][2];
    vsConsts[13][1] = vp.m[1][2];
    vsConsts[13][2] = vp.m[2][2];
    vsConsts[13][3] = vp.m[3][2];
    dirty = true;
  }
  for (int i = 0; i < 4; ++i) {
    int slot = 4 + i;
    if (!vsEngine[slot]) {
      vsConsts[slot][0] = mvp.m[0][i];
      vsConsts[slot][1] = mvp.m[1][i];
      vsConsts[slot][2] = mvp.m[2][i];
      vsConsts[slot][3] = mvp.m[3][i];
      dirty = true;
    }
  }
  for (int i = 0; i < 3; ++i) {
    int slot = 58 + i;
    if (!vsEngine[slot]) {
      vsConsts[slot][0] = model.m[0][i];
      vsConsts[slot][1] = model.m[1][i];
      vsConsts[slot][2] = model.m[2][i];
      vsConsts[slot][3] = model.m[3][i];
      dirty = true;
    }
  }
}

namespace {
float g_overbright = 1.0f;
}  // namespace

void SeVkMatrix_SetOverbright(float overbright) {
  g_overbright = overbright != 0.0f ? overbright : 1.0f;
}

float SeVkMatrix_Overbright() { return g_overbright; }

}  // namespace se::vulkan
