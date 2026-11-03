//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: A higher level link library for general use in the game and tools.
//
//===========================================================================//

#include "tier1/tier1.h"

#include "tier0/dbg.h"
#include "vstdlib/iprocessutils.h"
#include "icvar.h"
#include "filesystem.h"

// Defined here rather than in tier2/tier2.cpp: the datamodel and dmserializers
// modules read this global, and tier2 reads g_pDmSerializers from
// dmserializers, so a definition in tier2 formed a cycle of eagerly-bound
// data symbols (tier2 -> dmserializers -> datamodel -> tier2) that no dlopen
// order could satisfy. It is a settable handle, not tier2 code, so placing it
// one tier down costs nothing and keeps the dependency graph acyclic.
IFileSystem *g_pFullFileSystem = nullptr;


//-----------------------------------------------------------------------------
// These tier1 libraries must be set by any users of this library.
// They can be set by calling ConnectTier1Libraries or InitDefaultFileSystem.
// It is hoped that setting this, and using this library will be the common mechanism for
// allowing link libraries to access tier1 library interfaces
//-----------------------------------------------------------------------------
ICvar *cvar = nullptr;
ICvar *g_pCVar = nullptr;
IProcessUtils *g_pProcessUtils = nullptr;
static bool s_bConnected = false;


//-----------------------------------------------------------------------------
// Call this to connect to all tier 1 libraries.
// It's up to the caller to check the globals it cares about to see if ones are missing
//-----------------------------------------------------------------------------
void ConnectTier1Libraries( CreateInterfaceFn *pFactoryList, intp nFactoryCount )
{
	// Don't connect twice..
	if ( s_bConnected )
		return;

	s_bConnected = true;

	for ( intp i = 0; i < nFactoryCount; ++i )
	{
		if ( !g_pCVar )
		{
			cvar = g_pCVar = static_cast<ICvar *>( pFactoryList[i]( CVAR_INTERFACE_VERSION, nullptr ) );
		}
		if ( !g_pProcessUtils )
		{
			g_pProcessUtils = static_cast<IProcessUtils *>( pFactoryList[i]( PROCESS_UTILS_INTERFACE_VERSION, nullptr ) );
		}
	}
}

void DisconnectTier1Libraries()
{
	if ( !s_bConnected )
		return;

	g_pCVar = cvar = nullptr;
	g_pProcessUtils = nullptr;
	s_bConnected = false;
}
