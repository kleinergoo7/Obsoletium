//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#include "mathlib/vector.h"

// vec3_origin is already provided by public/mathlib/mathlib.h as
// "constexpr inline Vector vec3_origin". This non-inline definition emitted a
// second strong symbol for it, and every other mathlib TU that includes
// mathlib.h also instantiates the inline one, so libmathlib.so failed to link
// with "multiple definition of `vec3_origin'".

