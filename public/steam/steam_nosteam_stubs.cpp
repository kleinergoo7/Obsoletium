//========= Copyright Valve Corporation, All rights reserved. ============//
//
//	No-Steam stubs for the dedicated/offline server port.
//
//	The tree guards every real Steam call with #if !defined(NO_STEAM), but it
//	never stops referencing a handful of SteamWorks data symbols, so once all
//	the other undefined symbols were fixed the dedicated server still died
//	while loading the game DLL:
//
//	  [3.715][swds] failed to dlopen .../build/libserver_CStrike.so error=...
//	  undefined symbol: _ZTV28CSteamWorksGameStatsUploader
//
//	and recursed into the same set from
//
//	  LD_LIBRARY_PATH=$PWD ldd -r libserver_CStrike.so
//	  undefined symbol: g_pSteamClientGameServer
//	  undefined symbol: steamapicontext
//	  undefined symbol: SteamClient
//	  undefined symbol: steamgameserverapicontext
//	  undefined symbol: SteamGameServer_GetHSteamPipe
//	  undefined symbol: SteamGameServer_GetHSteamUser
//	  undefined symbol: _ZTV28CSteamWorksGameStatsUploader
//
//	The Win32 definitions of these live in the Steam SDK tools tree
//	(steam_api_flat.cpp / steam_api_public.cpp / gameserver_iso.cpp) that this
//	port intentionally does not ship, so they are provided here as null / zero.
//
//	This file is co-located (see the four game .vpc files) with
//	steamworks_gamestats.cpp on purpose: emitting the uploader vtable is only
//	legal in a DLL that ALSO compiles that file, because the vtable references
//	Init()/FireGameEvent()/LevelShutdown(). Registering it through the shared
//	POSIX skeleton leaked it into ~90 modules and a stale link failed at dlopen
//	with "undefined symbol: _ZTV28CSteamWorksGameStatsUploader" again.
//
//=============================================================================//

#include <ctime>

#include "tier1/strtools.h"

#include "steam/steam_api.h"
#include "steam/steam_gameserver.h"

// igamesystem.h:97-98 uses CBasePlayer / CUserCmd in pointer-only accessor
// declarations but does not declare them itself; cbase.h is what normally
// precedes it in this tree. Declared here BEFORE steamworks_gamestats.h (and
// its igamesystem.h) is parsed, or GCC rejects the header with:
//     igamesystem.h:98: Fehler: »CBasePlayer« bezeichnet keinen Typ
class CBasePlayer;
class CUserCmd;

#include "game/shared/steamworks_gamestats.h"

#if defined(NO_STEAM)

// steam_gameserver.h:147 already declared g_pSteamClientGameServer as S_API
// (extern "C"), so this redefinition simply inherits that linkage.
ISteamClient *g_pSteamClientGameServer = NULL;

// enginecallback.h declares these with C++ linkage, but the game DLLs
// reference the extern "C" versions (VERSION_SAFE_STEAM_API selects the
// steam_gameserver.h block above), so give both explicit C linkage.
extern "C" {
	CSteamAPIContext *steamapicontext = NULL;
	CSteamGameServerAPIContext *steamgameserverapicontext = NULL;
}

// Flat-C SteamWorks API: be a vacuum flask. The S_API declarations in the
// headers above already carry extern "C", so no linkage specifier is needed.
ISteamClient *SteamClient() { return NULL; }
HSteamPipe SteamGameServer_GetHSteamPipe() { return 0; }
HSteamUser SteamGameServer_GetHSteamUser() { return 0; }

// steamworks_gamestats.cpp only defines FrameUpdatePostEntityThink() inside a
// #ifndef NO_STEAM block (line 597), so the class's key function was never
// emitted and the uploader's vtable went missing. The body is a no-op; without
// Steam there is nothing to service.
void CSteamWorksGameStatsUploader::FrameUpdatePostEntityThink() {}

#endif // NO_STEAM