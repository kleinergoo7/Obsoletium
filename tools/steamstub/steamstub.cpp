// steamstub.cpp -- offline SteamAPI stub for headless/offline testing.
//
// The content's libsteam_api.so attempts IPC with a running Steam client and
// stalls init when Steam is absent. These three symbols are the only Steam
// imports of libclient_CStrike.so (see nm -D); stubbing them lets the game
// load offline with instant no-ops instead of IPC timeouts.
//
// Build: g++ -shared -fPIC -o steamstub.so steamstub.cpp
// Use: LD_PRELOAD=<path>/steamstub.so (NOT for any Steam-connected purpose).
#include <cstdint>

extern "C" {

// Opaque handles; the game never dereferences them without a live backend.
using HSteamPipe = int32_t;

__attribute__((visibility("default"))) HSteamPipe GetHSteamPipe() {
  return 0;
}

struct CCallbackBase {
  int m_nCallbackFlags = 0;
  int m_iCallback = 0;
};

// Instant no-op: with no backend there is nothing to dispatch to.
__attribute__((visibility("default"))) void SteamAPI_RegisterCallResult(
    CCallbackBase *pCallback, uint64_t /*hApiCall*/) {
  if (pCallback) {
    pCallback->m_nCallbackFlags = 0;
  }
}

__attribute__((visibility("default"))) void SteamAPI_UnregisterCallResult(
    CCallbackBase *pCallback, uint64_t /*hApiCall*/) {
  if (pCallback) {
    pCallback->m_nCallbackFlags = 0;
  }
}
}
