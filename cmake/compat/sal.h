// sal.h -- Linux port shim for the Windows SDK's "Standard Annotation
// Language" header.
//
// thirdparty/DirectXMath/Inc/DirectXMath.h does `#include "sal.h"`
// unconditionally (line 156), and public/mathlib/{vector,vmatrix,vector2d,
// vector4d,mathlib,math_pfns,ssemath,halton}.h all pull DirectXMath in, so
// tier0 cannot compile without something by that name on the include path.
//
// The SAL annotations describe contract metadata for the Windows Static
// Analysis tools. They carry no runtime meaning, so the correct Linux
// translation is "expand to nothing" -- that keeps every declaration valid
// without changing codegen.
//
// This file is port-owned: it lives outside the Obsoletium checkout so that
// `git status` in obsoletium/ stays clean and the additions remain auditable.
// The identifier list below was extracted from actual references in
// thirdparty/DirectXMath/Inc/*.{h,inl} and public/**, not guessed.
//
// NOTE: _Interlocked* is deliberately absent -- those are real intrinsics with
// function semantics, not annotations. obsoletium/public/tier0/portability.h
// implements them for GCC/Clang.

#ifndef SAL_H_PORT_SHIM
#define SAL_H_PORT_SHIM

// --- format-string annotations ---------------------------------------------
// MSVC's <sal.h> spells these _Printf_format_string_ / _Scanf_s_format_string_
// and places them on the *parameter*. GCC and Clang instead take
// __attribute__((format(...))) on the function (which is what FMTFUNCTION in
// tier0/platform.h already emits), so there is nothing for the parameter tag
// to do and it expands to nothing here.
#define _Printf_format_string_
#define _Scanf_s_format_string_
#define _Scanf_format_string_impl_
#define _Printf_format_string_impl_

// --- null / size annotations (all no-ops) ---------------------------------
#define _In_
#define _In_z_
#define _In_opt_
#define _In_opt_z_
#define _In_bytecount_(n)
#define _In_count_(n)
#define _In_z_count_(n)
#define _In_opt_count_(n)
#define _In_opt_z_count_(n)
#define _In_z_count_c_(n)
#define _In_reads_(n)
#define _In_reads_bytes_(n)
#define _Inout_
#define _Inout_z_
#define _Inout_bytecap_(n)
#define _Inout_z_bytecap_(n)
#define _Inout_z_cap_(n)
#define _Inout_updates_(n)
#define _Out_
#define _Out_z_
#define _Out_opt_
#define _Out_cap_(n)
#define _Out_cap_c_(n)
#define _Out_z_cap_(n)
#define _Out_opt_cap_(n)
#define _Out_opt_z_cap_(n)
#define _Out_bytecap_(n)
#define _Out_bytecap_c_(n)
#define _Out_z_bytecap_(n)
#define _Out_opt_bytecap_(n)
#define _Out_writes_(n)
#define _Out_writes_bytes_(n)
#define _Out_writes_opt_(n)
#define _Out_writes_z_(n)
#define _Out_z_bytecapcount_(n)
#define _Ret_
#define _Ret_maybenull_
#define _Ret_notnull_
#define _Field_size_(n)
#define _Field_size_opt_(n)
#define _Element_type_(t)
#define _Null_terminated_
#define _Deref_out_
#define _Deref_out_opt_
#define _Deref_pre_z_
#define _Deref_post_z_
#define _Deref_prepost_z_
#define _Post_z_
#define _Post_invalid_
#define _Post_ptr_invalid_
#define _Post_writable_byte_size_(n)
#define _Pre_z_
#define _Pre_notnull_
#define _Pre_maybenull_
#define _Prepost_z_
#define _Return_type_success_(ret) ret
#define _Success_(e)
#define _Check_return_
#define _Use_decl_annotations_
// Double-underscore legacy spelling (used by launcher/launcher_main.cpp).
// NOTE: do NOT add __in/__out/__inout here -- libstdc++ uses those as
// parameter names, so empty defines corrupt every STL header.
#define __out_z
#define _Unbounded_
#define _When_(c)

// _Analysis_assume_ takes an argument and is used as a statement. GCC 13+ and
// Clang both understand __attribute__((assume)); fall back to evaluating the
// expression in a discarded context so it is still type-checked.
#if defined(__has_attribute)
#  if __has_attribute(assume)
#    define _Analysis_assume_(cond) __attribute__((assume(cond)))
#  endif
#endif
#if !defined(_Analysis_assume_)
#  if defined(__GNUC__)
#    define _Analysis_assume_(cond) ((void)sizeof((cond) ? 1 : 0))
#  else
#    define _Analysis_assume_(cond) ((void)0)
#  endif
#endif

#endif  // SAL_H_PORT_SHIM
