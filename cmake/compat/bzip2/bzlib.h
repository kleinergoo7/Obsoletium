//========= Copyright, Port maintainers, All rights reserved. ============//
//
// Purpose:
//   Portability shim for the bzip2 development header.
//
//   The engine sources include bzip2 as "bzip2/bzlib.h", which is the layout
//   used by the Windows/Debian-style bzip2 development packages. Distribution
//   headers installed directly into the system include directory (openSUSE,
//   Arch, Fedora) expose "bzlib.h" at the top level instead, and the matching
//   development symlink may not be present at all.
//
//   Rather than editing every engine translation unit that pulls in bzip2, this
//   forwarding header reproduces the "bzip2/" prefix the sources expect and
//   defers to whatever the platform actually provides.
//
//===========================================================================//

#ifndef COMPAT_BZIP2_BZLIB_H
#define COMPAT_BZIP2_BZLIB_H

#if defined( __has_include ) && __has_include( <bzlib.h> )
#  include <bzlib.h>
#else
#  error "bzip2 headers not found: install the bzip2 development package (bzip2-devel)."
#endif

#endif // COMPAT_BZIP2_BZLIB_H
