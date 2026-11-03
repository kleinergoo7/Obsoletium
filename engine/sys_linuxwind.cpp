//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Linux support for the IGame interface
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//
#include "iengine.h"
#include <stdlib.h>

#include "engine_launcher_api.h"
#include "basetypes.h"
#include "ivideomode.h"
#include "igame.h"

#define UINT unsigned int
#define WPARAM int
#define LPARAM int

#include "profile.h"
#include "server.h"
#include "cdll_int.h"

// DisplaySystemVersion is the only definition behind sys_dll.cpp's
// CON_COMMAND( star_os ), and it lives in engine/bugreporter.cpp, which
// engine.vpc lists as [!$DEDICATED] because that file includes client.h and
// vgui_controls. The function is not Windows-only -- it has an
// "#elif defined(LINUX)" branch that reports /proc/version_signature -- but
// that branch never reaches a dedicated build, so the shared sys_dll.cpp left
// libengine.so with an undefined DisplaySystemVersion(char*, int) and
// Sys_LoadModule's RTLD_NOW could not bind it.
//
// Provide the dedicated-only equivalent, keeping the upstream output format.
// The client build keeps the canonical definition in bugreporter.cpp, so this
// is guarded by DEDICATED to guarantee the two can never coexist.
#if defined( DEDICATED ) && !defined( _WIN32 )

#include <sys/utsname.h>
#include <unistd.h>

#include "strtools.h"
#include "tier0/platform.h"

void DisplaySystemVersion( char *osversion, int maxlen )
{
	struct utsname utsBuf;
	if ( uname( &utsBuf ) != 0 )
	{
		Q_strncat( osversion, "Linux N/A", maxlen, COPY_ALL_CHARACTERS );
		return;
	}

	Q_strncat( osversion, "Linux ", maxlen, COPY_ALL_CHARACTERS );
	Q_strncat( osversion, utsBuf.release, maxlen, COPY_ALL_CHARACTERS );

	Q_strncat( osversion,
#if defined( PLATFORM_X86 ) && ( PLATFORM_X86 == 64 )
		" x86-64",
#elif defined( PLATFORM_ARM_64 )
		" AArch64",
#elif defined( PLATFORM_ARM_32 )
		" ARM32",
#elif defined( PLATFORM_X86 ) && ( PLATFORM_X86 == 32 )
		" x86",
#else
		" N/A",
#endif
		maxlen, COPY_ALL_CHARACTERS );

	Q_strncat( osversion, " (super user: ", maxlen, COPY_ALL_CHARACTERS );
	Q_strncat( osversion, getuid() == 0 ? "yes)" : "no)", maxlen, COPY_ALL_CHARACTERS );

	char *szlf = Q_strrchr( osversion, '\n' );
	if ( szlf )
		*szlf = '\0';
}

#endif // DEDICATED && !_WIN32

#ifdef SWDS
void ForceReloadProfile( void );

void ClearIOStates( void );
//-----------------------------------------------------------------------------
// Purpose: Main game interface, including message pump and window creation
//-----------------------------------------------------------------------------
class CGame : public IGame
{
public:
					CGame( void );
	virtual			~CGame( void );

	bool			Init( void *pvInstance );
	bool			Shutdown( void );

	bool			CreateGameWindow( void );
	virtual void	DestroyGameWindow( void );
	virtual void	SetGameWindow( void *hWnd );

	virtual bool	InputAttachToGameWindow();
	virtual void	InputDetachFromGameWindow();

	void*			GetMainWindow( void );
	void*			GetMainDeviceWindow( void );
	void**			GetMainWindowAddress( void );

	void			SetWindowXY( int x, int y );
	void			SetWindowSize( int w, int h );
	void			GetWindowRect( int *x, int *y, int *w, int *h );

	bool			IsActiveApp( void );
	virtual void		DispatchAllStoredGameMessages();
	virtual void		PlayStartupVideos() {}
	virtual void		GetDesktopInfo( int &width, int &height, int &refreshRate );
private:
	void			SetActiveApp( bool fActive );

private:
	bool m_bActiveApp;
	static const char CLASSNAME[];

};

static CGame g_Game;
IGame *game = ( IGame * )&g_Game;

const char CGame::CLASSNAME[] = "Valve001";

// In VCR playback mode, it sleeps this amount each frame.
int g_iVCRPlaybackSleepInterval = 0;

// During VCR playback, if this is true, then it'll pause at the end of each frame.
bool g_bVCRSingleStep = false;

void VCR_EnterPausedState()
{
        // Turn this off in case they're in single-step mode.
        g_bVCRSingleStep = false;

        // This is cheesy, but GetAsyncKeyState is blocked (in protected_things. h)
        // from being accidentally used, so we get it through it by getting its pointer directly.

         // In this mode, we enter a wait state where we only pay attention to R and Q.
 /*        while ( 1 )
         {
                 if ( pfn( 'R' ) & 0x8000 )
                        break;

                if ( pfn( 'Q' ) & 0x8000 )
                      kill( getpid(), SIGKILL );

                if ( pfn( 'S' ) & 0x8000 )
                {
                        // Do a single step.
                        g_bVCRSingleStep = true;
                        break;
                }

                Sleep( 2 );
        }
*/
}


bool CGame::CreateGameWindow( void )
{
	return true;
}

void CGame::DestroyGameWindow( void )
{
}


// This is used in edit mode to override the default wnd proc associated w/
bool CGame::InputAttachToGameWindow()
{
	return true;
}

void CGame::InputDetachFromGameWindow()
{
}

void CGame::SetGameWindow( void *hWnd )
{
	return;
}

CGame::CGame( void )
{
	m_bActiveApp = true;
}

CGame::~CGame( void )
{
}

bool CGame::Init( void *pvInstance )
{
	return true;
}

bool CGame::Shutdown( void )
{
	return true;
}

void *CGame::GetMainWindow( void )
{
	return 0;
}

void *CGame::GetMainDeviceWindow( void )
{
	return 0;
}

void **CGame::GetMainWindowAddress( void )
{
	return NULL;
}

void CGame::SetWindowXY( int x, int y )
{
}

void CGame::SetWindowSize( int w, int h )
{
}

void CGame::GetWindowRect( int *x, int *y, int *w, int *h )
{
	if ( x )
	{
		*x = 0;
	}
	if ( y )
	{
		*y = 0;
	}
	if ( w )
	{
		*w = 0;
	}
	if ( h )
	{
		*h = 0;
	}
}

bool CGame::IsActiveApp( void )
{
	// The Linux port does not wire window-activation events into
	// SetActiveApp, so m_bActiveApp stays at its initial value and the
	// engine falls into the SleepUntilInput path on the first frame,
	// which never wakes up under lavapipe/Xwayland. Treat the game as
	// always active; the SDL event loop already pumps input.
	return true;
}

void CGame::SetActiveApp( bool active )
{
	m_bActiveApp = active;
}

void CGame::DispatchAllStoredGameMessages()
{
}

void CGame::GetDesktopInfo( int &width, int &height, int &refreshRate )
{
    width = 0;
    height = 0;
	refreshRate = 0;
}

#endif
