// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// vk_matrix_test.cpp -- regression check for SeVkMatrix_Commit's upload
// convention (S2.8-matrix).
//
// The DX9 commit uploads the TRANSPOSED combined matrices: slot = column,
// component = row. The fxc shaders do `gl_Position.x = dot(pos, c8)`, so c8
// must be VP column 0. Uploading rows instead drops the ortho's -1/+1
// translation and clips every VGUI vertex; that regression shipped once.
//
// Self-checking: prints one line per failure and exits non-zero. Build:
//   ninja -C obsoletium/build-client vk_matrix_test
//   obsoletium/build-client/vk_matrix_test

#include <cmath>
#include <cstdio>

#include "se_vk_matrix.h"

namespace {

int failures = 0;

void Expect(float got, float want, const char *what) {
  if (std::fabs(got - want) > 1e-6f) {
    std::printf("FAIL %s: got %.6f want %.6f\n", what, got, want);
    ++failures;
  }
}

}  // namespace

int main() {
  using namespace se::vulkan;
  SeVkMatrixStacks &st = SeVkMatrix_Global();

  // View: identity. Projection: the screen ortho VGUI uses, with its
  // translation in the last row (VMatrix row-vector convention).
  st.current[0].Identity();  // MATERIAL_VIEW
  st.current[1].Identity();  // MATERIAL_PROJECTION
  VMatrix &p = st.current[1];
  p.m[0][0] = 2.0f / 640.0f;
  p.m[1][1] = -2.0f / 480.0f;
  p.m[2][2] = -0.5f;
  p.m[0][3] = -1.0f;  // x translation
  p.m[1][3] = 1.0f;   // y translation

  st.current[10].Identity();  // MATERIAL_MODEL

  float c[256][4] = {};
  bool engine[256] = {};
  bool dirty = false;
  SeVkMatrix_Commit(c, engine, dirty);

  if (!dirty) {
    std::printf("FAIL commit did not mark the constants dirty\n");
    ++failures;
  }

  // c8..c11 = VP columns 0..3. Component i is VMatrix row i.
  Expect(c[8][0], p.m[0][0], "c8.x (VP col0 row0)");
  Expect(c[8][1], p.m[1][0], "c8.y (VP col0 row1)");
  Expect(c[11][0], p.m[0][3], "c11.x (VP col3 row0 = -1)");
  Expect(c[11][1], p.m[1][3], "c11.y (VP col3 row1 = +1)");
  Expect(c[11][3], p.m[3][3], "c11.w (VP col3 row3 = 1)");
  // The exact bug: a row upload would put the translation in c8.w instead.
  Expect(c[8][3], p.m[3][0], "c8.w (VP col0 row3 = 0)");
  Expect(c[13][2], p.m[2][2], "c13.z (VP col2 row2 = -0.5)");

  // Engine-written slots win over the stack.
  float before[4] = {7.0f, 8.0f, 9.0f, 10.0f};
  for (int i = 0; i < 4; ++i) {
    c[8][i] = before[i];
  }
  engine[8] = true;
  dirty = false;
  SeVkMatrix_Commit(c, engine, dirty);
  for (int i = 0; i < 4; ++i) {
    Expect(c[8][i], before[i], "engine slot 8 preserved");
  }

  if (failures != 0) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("ok: matrix upload convention\n");
  return 0;
}
