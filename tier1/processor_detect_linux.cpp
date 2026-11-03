//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: linux dependant ASM code for CPU capability detection
//
// $Workfile:     $
// $NoKeywords: $
//============================================================================//
//
// The original implementation drove cpuid with hand-written 32-bit inline
// assembly ("pushl %%ebx / cpuid / movl %%ebx,%%esi / pop %%ebx"). The
// assembler rejects that in a 64-bit build -- "invalid instruction suffix for
// `push'" and "operand type mismatch for `pop'" -- and the EBX save/restore
// dance is only needed because EBX is the PIC register on i386.
//
// GCC's <cpuid.h> already picks the right constraint for each ABI (RBX on
// x86-64, the EBX dance on i386), so use it rather than reimplementing the
// register allocation by hand.

#include <cpuid.h>

bool CheckMMXTechnology()
{
	unsigned int eax, ebx, ecx, edx;

	if ( !__get_cpuid( 1, &eax, &ebx, &ecx, &edx ) )
		return false;

	return ( edx & bit_MMX ) != 0;
}

bool CheckSSETechnology()
{
	unsigned int eax, ebx, ecx, edx;

	if ( !__get_cpuid( 1, &eax, &ebx, &ecx, &edx ) )
		return false;

	return ( edx & bit_SSE ) != 0;
}

bool CheckSSE2Technology()
{
	unsigned int eax, ebx, ecx, edx;

	if ( !__get_cpuid( 1, &eax, &ebx, &ecx, &edx ) )
		return false;

	return ( edx & bit_SSE2 ) != 0;
}

bool Check3DNowTechnology()
{
	unsigned int eax, ebx, ecx, edx;

	// The 3DNow! bit lives in leaf 0x80000001, which is only meaningful if the
	// extended leaf 0x80000000 is present. Note ">=" here: the original test was
	// "eax > 0x80000000L", which spuriously rejected an exactly-0x80000001
	// capable CPU.
	if ( !__get_cpuid( 0x80000000, &eax, &ebx, &ecx, &edx ) || eax < 0x80000001 )
		return false;

	if ( !__get_cpuid( 0x80000001, &eax, &ebx, &ecx, &edx ) )
		return false;

	return ( edx & bit_3DNOW ) != 0;
}
