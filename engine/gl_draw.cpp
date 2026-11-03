//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#include "render_pch.h"
#include "draw.h"
#include "decal.h"
#include "gl_cvars.h"
#include "view.h"
#include "screen.h"
#include "gl_matsysiface.h"
#include "cdll_int.h"
#include "materialsystem/imesh.h"
#include "materialsystem/imaterial.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

Vector g_CurrentViewOrigin(0, 0, 0), g_CurrentViewForward(1, 0, 0), g_CurrentViewRight(0, -1, 0), g_CurrentViewUp(0, 0, 1);
Vector g_MainViewOrigin(0, 0, 0), g_MainViewForward(1, 0, 0), g_MainViewRight(0, -1, 0), g_MainViewUp(0, 0, 1);


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pMaterial - 
//-----------------------------------------------------------------------------
void GL_UnloadMaterial( IMaterial *pMaterial )
{
	// Dedicated/headless shutdown: the material system may already have been
	// disconnected (tier2 DisconnectTier2Libraries() nulls `materials`
	// [tier2.cpp:139]) before Host_Shutdown -> ShutdownDebugMaterials tries
	// to release the debug materials.
	// CMaterial_QueueFriendly::DecrementReferenceCount virtual-dispatches
	// through `materials` to get the render call-queue; with that global NULL
	// it dereferences address 0x0 and SIGSEGVs:
	//
	//   CRASH CMaterial_QueueFriendly::DecrementReferenceCount
	//   [engine(+0x2ea288)] ShutdownMaterialSystem -> GL_UnloadMaterial
	//   [libmaterialsystem 0x1b6e5a] mov (%rax),%rax  rax=0  fault addr 0x0
	//
	// Also guard against uninitialized material stubs (non-NULL object whose
	// first word / vtable is zero). A null `materials` global means the
	// system is gone and its materials are already being torn down, so there
	// is nothing left to decrement; a real, live material always carries a
	// vtable pointer, so that check cannot skip a legitimate release.
	if ( pMaterial && *( const void ** )pMaterial && materials )
	{
		pMaterial->DecrementReferenceCount();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pName - 
// Output : IMaterial
//-----------------------------------------------------------------------------
static IMaterial *GL_LoadMaterialNoRef( const char *pName, const char *pTextureGroupName )
{
	IMaterial *material = NULL;

	if( mat_loadtextures.GetInt() )
	{
		material = materials->FindMaterial( pName, pTextureGroupName );
	}
	else
	{
		material = g_materialEmpty;
	}
	
	return material;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pName - 
// Output : IMaterial
//-----------------------------------------------------------------------------
IMaterial *GL_LoadMaterial( const char *pName, const char *pTextureGroupName )
{
	IMaterial *material;
	
	material = GL_LoadMaterialNoRef( pName, pTextureGroupName );
	if( material )
	{
		material->IncrementReferenceCount();
	}
	return material;
}

