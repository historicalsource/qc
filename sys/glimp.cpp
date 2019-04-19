/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifdef USE_SDL

#include "../idlib/precompiled.h"

#include <SDL_syswm.h>

#include "../framework/Licensee.h"

#include "../renderer/tr_local.h"

#if !defined(ID_GL_HARDLINK) && defined(__linux__)
#include "linux/local.h"
#endif

#ifdef __WINDOWS__
#include "win32/win_local.h"
#endif

idCVar in_nograb("in_nograb", "0", CVAR_SYSTEM | CVAR_NOCHEAT, "prevents input grabbing");
idCVar r_waylandcompat("r_waylandcompat", "0", CVAR_SYSTEM | CVAR_NOCHEAT | CVAR_ARCHIVE, "wayland compatible framebuffer");

bool g_inputGrabbed = false;

static SDL_Window *window = NULL;
static SDL_GLContext context = NULL;

//
// function declaration
//
#ifdef __WINDOWS__
bool QGL_Init( const char *dllname );
void QGL_Shutdown( void );
#endif

/*
===================
GLimp_Init
===================
*/
bool GLimp_Init(glimpParms_t parms) {
	common->Printf("Initializing OpenGL subsystem\n");

#if !defined(ID_GL_HARDLINK) && defined(__linux__)
	if ( !GLimp_dlopen() ) {
		return false;
	}
#endif

#ifdef __WINDOWS__
	const char	*driverName;
	// r_glDriver is only intended for using instrumented OpenGL
	// dlls.  Normal users should never have to use it, and it is
	// not archived.
	driverName = r_glDriver.GetString()[0] ? r_glDriver.GetString() : "opengl32";
	if ( !QGL_Init( driverName ) ) {
		common->Printf( "^3GLimp_Init() could not load r_glDriver \"%s\"^0\n", driverName );
		return false;
	}
#endif

	assert(SDL_WasInit(SDL_INIT_VIDEO));

	Uint32 flags = SDL_WINDOW_OPENGL;

#ifndef __WINDOWS__ // HACK: windows starts windowed then switches to fullscreen below.
	if (parms.fullScreen)
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif

	int colorbits = 24;
	int depthbits = 24;
	int stencilbits = 8;

	for (int i = 0; i < 16; i++) {
		// 0 - default
		// 1 - minus colorbits
		// 2 - minus depthbits
		// 3 - minus stencil
		if ((i % 4) == 0 && i) {
			// one pass, reduce
			switch (i / 4) {
			case 2 :
				if (colorbits == 24)
					colorbits = 16;
				break;
			case 1 :
				if (depthbits == 24)
					depthbits = 16;
				else if (depthbits == 16)
					depthbits = 8;
			case 3 :
				if (stencilbits == 24)
					stencilbits = 16;
				else if (stencilbits == 16)
					stencilbits = 8;
			}
		}

		int tcolorbits = colorbits;
		int tdepthbits = depthbits;
		int tstencilbits = stencilbits;

		if ((i % 4) == 3) {
			// reduce colorbits
			if (tcolorbits == 24)
				tcolorbits = 16;
		}

		if ((i % 4) == 2) {
			// reduce depthbits
			if (tdepthbits == 24)
				tdepthbits = 16;
			else if (tdepthbits == 16)
				tdepthbits = 8;
		}

		if ((i % 4) == 1) {
			// reduce stencilbits
			if (tstencilbits == 24)
				tstencilbits = 16;
			else if (tstencilbits == 16)
				tstencilbits = 8;
			else
				tstencilbits = 0;
		}

		int channelcolorbits = 4;
		if (tcolorbits == 24)
			channelcolorbits = 8;

		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, channelcolorbits);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, channelcolorbits);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, channelcolorbits);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, tdepthbits);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, tstencilbits);

		if (r_waylandcompat.GetBool())
			SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
		else
			SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, channelcolorbits);

		SDL_GL_SetAttribute(SDL_GL_STEREO, parms.stereo ? 1 : 0);

		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, parms.multiSamples ? 1 : 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, parms.multiSamples);

#ifdef __WINDOWS__ // flush out any outstanding messages.
		SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
		SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
#endif

		window = SDL_CreateWindow(GAME_NAME,
									SDL_WINDOWPOS_UNDEFINED,
									SDL_WINDOWPOS_UNDEFINED,
									parms.width, parms.height, flags);

#ifdef __WINDOWS__ // HACK: windows fullscreeen doesn't get focus properly, this is a work around.
		// Switch to fullscreen now if needed, this works around a weird focus bug in SDL2
		if (parms.fullScreen)
			SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);

		// flush out the event queue so we set the hWnd etc. for id
		win32.hWnd = NULL;
		while (Sys_GetEvent().evType != SE_NONE) {}
		SDL_EventState(SDL_SYSWMEVENT, SDL_DISABLE);
#endif

		context = SDL_GL_CreateContext(window);

		if (!window) {
			common->DPrintf("Couldn't set GL mode %d/%d/%d: %s",
							channelcolorbits, tdepthbits, tstencilbits, SDL_GetError());
			continue;
		}

		if (SDL_GL_SetSwapInterval(r_swapInterval.GetInteger()) < 0)
			common->Warning("SDL_GL_SWAP_CONTROL not supported");

#if 0 // TODO: make the engine render to the specified size but live in native res window.
        glConfig.vidWidth = parms.width;
        glConfig.vidHeight = parms.height;
#else
		SDL_GetWindowSize(window, &glConfig.vidWidth, &glConfig.vidHeight);
#endif

		glConfig.isFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == SDL_WINDOW_FULLSCREEN;

		common->Printf("Using %d color bits, %d depth, %d stencil display\n",
						channelcolorbits, tdepthbits, tstencilbits);

		glConfig.colorBits = tcolorbits;
		glConfig.depthBits = tdepthbits;
		glConfig.stencilBits = tstencilbits;

		glConfig.displayFrequency = 0;

		break;
	}

	if (!window) {
		common->Warning("No usable GL mode found: %s", SDL_GetError());
		return false;
	}

	return true;
}

/*
===================
GLimp_SetScreenParms
===================
*/
bool GLimp_SetScreenParms(glimpParms_t parms) {
	common->DPrintf("TODO: GLimp_ActivateContext\n");
	return true;
}

/*
===================
GLimp_Shutdown
===================
*/
void GLimp_Shutdown() {
	common->Printf("Shutting down OpenGL subsystem\n");

	if (context) {
		SDL_GL_DeleteContext(context);
		context = NULL;
	}

	if (window) {
		SDL_DestroyWindow(window);
		window = NULL;
	}

#ifdef __WINDOWS__
	// shutdown QGL subsystem
	QGL_Shutdown();
#endif
}

/*
===================
GLimp_SwapBuffers
===================
*/
void GLimp_SwapBuffers() {
	SDL_GL_SwapWindow(window);

}

/*
=================
GLimp_SetGamma
=================
*/
void GLimp_SetGamma(unsigned short red[256], unsigned short green[256], unsigned short blue[256]) {
	if (!window) {
		common->Warning("GLimp_SetGamma called without window");
		return;
	}

	if (SDL_SetWindowGammaRamp(window, red, green, blue))
		common->Warning("Couldn't set gamma ramp: %s", SDL_GetError());
}

/*
=================
GLimp_ActivateContext
=================
*/
void GLimp_ActivateContext() {
	common->DPrintf("TODO: GLimp_ActivateContext\n");
}

/*
=================
GLimp_DeactivateContext
=================
*/
void GLimp_DeactivateContext() {
	common->DPrintf("TODO: GLimp_DeactivateContext\n");
}

#ifndef __WINDOWS__
/*
=================
GLimp_EnableLogging
=================
*/
void GLimp_EnableLogging(bool stat) { }
#endif

/*
===================
GLimp_ExtensionPointer
===================
*/
GLExtension_t GLimp_ExtensionPointer(const char *name) {
	assert(SDL_WasInit(SDL_INIT_VIDEO));

	return (GLExtension_t)SDL_GL_GetProcAddress(name);
}

void GLimp_GrabInput(int flags) {
	if (!window) {
		common->Warning("GLimp_GrabInput called without window");
		return;
	}

	bool shouldGrab = true;
	bool hasFocus = SDL_GetKeyboardFocus() == window;

	if ( in_nograb.GetBool() ) {
		shouldGrab = false;
	}
	// if fullscreen, we always want the mouse
	const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
	if ( !fullscreen ) {
		if ( (flags & GRAB_ENABLE) != GRAB_ENABLE) {
			shouldGrab = false;
		}
		if ( !hasFocus ) {
			shouldGrab = false;
		}
	}

	if (shouldGrab && (flags & GRAB_REENABLE) )
		shouldGrab = false;

	g_inputGrabbed = SDL_GetWindowGrab(window) == SDL_TRUE;
	if (shouldGrab != g_inputGrabbed) {
		SDL_SetWindowGrab(window, shouldGrab ? SDL_TRUE : SDL_FALSE);

		g_inputGrabbed = SDL_GetWindowGrab(window) == SDL_TRUE;
	}

	const bool relativeMouseMode = SDL_GetRelativeMouseMode();
	if (shouldGrab != relativeMouseMode) {
		SDL_SetRelativeMouseMode(shouldGrab ? SDL_TRUE : SDL_FALSE);
	}

	const bool shown = SDL_ShowCursor(-1) == SDL_ENABLE;
	const bool shouldShow = !shouldGrab;
	if (shouldShow != shown) {
		SDL_ShowCursor(shouldShow ? SDL_ENABLE : SDL_DISABLE);
	}
}

#endif

