//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: A higher level link library for general use in the game and tools.
//
//===========================================================================//

#include <tier2/tier2.h>
#include "tier0/dbg.h"
#include "filesystem.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/IColorCorrection.h"
#include "materialsystem/idebugtextureinfo.h"
#include "materialsystem/ivballoctracker.h"
#include "inputsystem/iinputsystem.h"
#include "networksystem/inetworksystem.h"
// dimhotepus: No perforce
// #include "p4lib/ip4.h"
#include "mdllib/mdllib.h"
#include "filesystem/IQueuedLoader.h"


//-----------------------------------------------------------------------------
// These tier2 libraries must be set by any users of this library.
// They can be set by calling ConnectTier2Libraries or InitDefaultFileSystem.
// It is hoped that setting this, and using this library will be the common mechanism for
// allowing link libraries to access tier2 library interfaces
//-----------------------------------------------------------------------------
// g_pFullFileSystem moved to tier1/tier1.cpp: datamodel and dmserializers read it,
// and tier2 reads g_pDmSerializers from dmserializers, so leaving it here made a
// cycle of eagerly-bound data symbols (tier2 -> dmserializers -> datamodel ->
// tier2) that no dlopen order can satisfy.
IMaterialSystem *materials = nullptr;
IMaterialSystem *g_pMaterialSystem = nullptr;
IInputSystem *g_pInputSystem = nullptr;
INetworkSystem *g_pNetworkSystem = nullptr;
IMaterialSystemHardwareConfig *g_pMaterialSystemHardwareConfig = nullptr;
IDebugTextureInfo *g_pMaterialSystemDebugTextureInfo = nullptr;
IVBAllocTracker *g_VBAllocTracker = nullptr;
IP4 *p4 = nullptr;
IColorCorrectionSystem *colorcorrection = nullptr;
IMdlLib *mdllib = nullptr;
IQueuedLoader *g_pQueuedLoader = nullptr;

namespace
{

template<typename T>
T* ConnectLibrary( T **library,
	CreateInterfaceFn *factory_list, intp index,
	const char *interface_name )
{
	if ( !*library )
	{
		return (*library = static_cast<T*>(factory_list[index]( interface_name, nullptr )));
	}

	return *library;
}

}

//-----------------------------------------------------------------------------
// Call this to connect to all tier 2 libraries.
// It's up to the caller to check the globals it cares about to see if ones are missing
//-----------------------------------------------------------------------------
void ConnectTier2Libraries( CreateInterfaceFn *pFactoryList, intp nFactoryCount )
{
	// Don't connect twice..
	//
	// The check below is evaluated on the first call only. Every app system
	// deriving from CTier2AppSystem calls this from its own Connect, and the
	// dedicated server has three of them -- CMaterialSystem::Connect,
	// CTier2AppSystem<IStudioRender>::Connect and
	// CTier2AppSystem<IDataCache>::Connect -- so the call re-enters by design
	// once the first system has connected, and a bare Assert() (or even
	// AssertOnce, which still evaluates the expression on every call and
	// reports the first false result) printed a bogus
	//   Assertion Failed: !g_pMaterialSystem && !materials && ...
	// for the second and subsequent callers. ConnectLibrary() early-returns on
	// an already-set pointer, so repeat calls are idempotent and need no guard.
	// Evaluating once still catches a global that was set before anything
	// connected, which is the mistake the assert was written for. Upstream never
	// trips any of this because Assert is a no-op unless _DEBUG is defined, and
	// this port builds with _DEBUG.
	//
	// g_pFullFileSystem is deliberately not part of the check at all:
	// main_posix preloads libfilesystem_stdio with RTLD_GLOBAL, and that
	// module's global g_FileSystem_Stdio implicitly default-constructs its
	// CBaseFileSystem base, whose constructor assigns g_pFullFileSystem (see
	// filesystem/basefilesystem.cpp), so it is already non-null on the very
	// first call.
	static bool s_bTier2LibrariesConnected = false;
	if (!s_bTier2LibrariesConnected)
	{
		s_bTier2LibrariesConnected = true;
		Assert(
		!g_pMaterialSystem && !materials &&
		!g_pInputSystem &&
		!g_pNetworkSystem && 
		!g_pMaterialSystemHardwareConfig &&
		!g_pMaterialSystemDebugTextureInfo && 
		!g_VBAllocTracker &&
		!p4 &&
		!colorcorrection && 
		!mdllib && 
		!g_pQueuedLoader );
	}

	for ( intp i = 0; i < nFactoryCount; ++i )
	{
		ConnectLibrary(&g_pFullFileSystem, pFactoryList, i, FILESYSTEM_INTERFACE_VERSION);
		materials = ConnectLibrary(&g_pMaterialSystem, pFactoryList, i, MATERIAL_SYSTEM_INTERFACE_VERSION);
		ConnectLibrary(&g_pInputSystem, pFactoryList, i, INPUTSYSTEM_INTERFACE_VERSION);
		ConnectLibrary(&g_pNetworkSystem, pFactoryList, i, NETWORKSYSTEM_INTERFACE_VERSION);
		ConnectLibrary(&g_pMaterialSystemHardwareConfig, pFactoryList, i, MATERIALSYSTEM_HARDWARECONFIG_INTERFACE_VERSION);
		ConnectLibrary(&g_pMaterialSystemDebugTextureInfo, pFactoryList, i, DEBUG_TEXTURE_INFO_VERSION);
		ConnectLibrary(&g_VBAllocTracker, pFactoryList, i, VB_ALLOC_TRACKER_INTERFACE_VERSION);
		// dimhotepus: No perforce
		// ConnectLibrary(&p4, pFactoryList, i, P4_INTERFACE_VERSION);
		ConnectLibrary(&colorcorrection, pFactoryList, i, COLORCORRECTION_INTERFACE_VERSION);
		ConnectLibrary(&mdllib, pFactoryList, i, MDLLIB_INTERFACE_VERSION);
		ConnectLibrary(&g_pQueuedLoader, pFactoryList, i, QUEUEDLOADER_INTERFACE_VERSION);
	}
}

void DisconnectTier2Libraries()
{
	// dimhotepus: In reverse order.
	g_pQueuedLoader = nullptr;
	mdllib = nullptr;
	colorcorrection = nullptr;
	p4 = nullptr;
	g_VBAllocTracker = nullptr;
	g_pMaterialSystemDebugTextureInfo = nullptr;
	g_pMaterialSystemHardwareConfig = nullptr;
	g_pNetworkSystem = nullptr;
	g_pInputSystem = nullptr;
	materials = g_pMaterialSystem = nullptr;
	g_pFullFileSystem = nullptr;
}


