/*
Copyright (c) 2003-2005 Alberto Demichelis

This software is provided 'as-is', without any express or implied warranty. In
no event will the authors be held liable for any damages arising from the use of
this software.

Permission is granted to anyone to use this software for any purpose, including
commercial applications, and to alter it and redistribute it freely, subject to
the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim
that you wrote the original software. If you use this software in a product, an
acknowledgment in the product documentation would be appreciated but is
not required.

2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

3. This notice may not be removed or altered from any source distribution.
*/

// Altered version.

#ifndef SQ_INCLUDE_SQRDBG_H_
#define SQ_INCLUDE_SQRDBG_H_

#include <cstdlib>  // std::size_t

#ifdef _WIN32
#pragma comment(lib, "WSOCK32.LIB")
#endif

#if (defined(_WIN64) || defined(_LP64))
#ifndef _SQ64
#define _SQ64
#endif
#endif

// SQInteger, SQRESULT and SQBool belong to sqconfig.h, which picks their width
// from _SQ64 -- so it must be included after the _SQ64 fixup above, not before.
// This header used to re-declare all three itself. On GCC that is a hard
// conflicting-declaration error rather than a tolerated duplicate, and the two
// spellings did not even agree: SQBool was std::size_t here but
// SQUnsignedInteger in sqconfig.h.
#include <sqconfig.h>

using HSQUIRRELVM = struct SQVM*;

namespace sq::dbg {

struct SQDbgServer;
using HSQREMOTEDBG = SQDbgServer*;

HSQREMOTEDBG sq_rdbg_init(HSQUIRRELVM v, unsigned short port,
                          SQBool autoupdate);
SQRESULT sq_rdbg_shutdown(HSQREMOTEDBG* rdbg);

SQRESULT sq_rdbg_waitforconnections(HSQREMOTEDBG rdbg);
SQRESULT sq_rdbg_update(HSQREMOTEDBG rdbg);

}  // namespace sq::dbg

#endif  // !SQ_INCLUDE_SQRDBG_H_
