// intrin.h -- Linux port shim for MSVC's <intrin.h>.
//
// Every other engine header that needs MSVC intrinsics guards the include
// correctly (bitvec.h, gcsdk.h, fasttimer.h and ia32detect.h test _WIN32 or
// _MSC_VER; threadtools.h tests PLATFORM_WINDOWS_PC; platform.h tests
// _MSC_VER). Only public/mathlib/math_pfns.h includes it unconditionally, yet
// the only intrinsics it actually uses are
//     _mm_rsqrt_ps  _mm_rsqrt_ss  _mm_sqrt_ps  _mm_sqrt_ss
// all of which GCC and Clang export from <x86intrin.h> (and which
// <xmmintrin.h> already pulls in -- public/mathlib/ssemath.h includes that
// one). So no MSVC-name remapping is required today, and providing none keeps
// this shim from colliding with the engine's own definitions.
//
// This file is port-owned, mirroring cmake/compat/sal.h, so the Obsoletium
// checkout stays unmodified and every local addition is auditable.

#ifndef INTRIN_H_PORT_SHIM
#define INTRIN_H_PORT_SHIM

#include <x86intrin.h>

// MSVC's _InterlockedXxx / _BitScanXxx / __readcr spellings are deliberately
// NOT mapped here. Nothing on the Linux include path needs them at present,
// and defining them pervasively would risk colliding with the platform layer
// that is expected to own them. If a future translation unit genuinely needs
// one, map just that name, in this file, with a comment saying which
// intrinsic backs it.

#endif  // INTRIN_H_PORT_SHIM
