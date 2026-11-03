// Copyright Valve Corporation, All rights reserved.
//
// Purpose: A redirection tool that allows the DLLs to reside elsewhere.

#include <unistd.h>

#include <cstdio>
#include <cstddef>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cerrno>

#include "tier0/basetypes.h"
#include "tier0/platform.h"
#include "scoped_dll.h"

namespace {

#ifdef LINUX

#include <fcntl.h>

bool IsDebuggerPresent(int time) {
  // Need to get around the __wrap_open() stuff. Just find the open symbol
  // directly and use it...
  using open_func_t = int (*)(const char *pathname, int flags, mode_t mode);
  open_func_t open_func = (open_func_t)dlsym(RTLD_NEXT, "open");

  if (open_func) {
    for (int i = 0; i < time; i++) {
      int tracerpid = -1;

      int fd = (*open_func)("/proc/self/status", O_RDONLY, S_IRUSR);
      if (fd >= 0) {
        char buf[4096];
        static const char tracerpid_str[] = "TracerPid:";

        const int len = read(fd, buf, sizeof(buf) - 1);
        if (len > 0) {
          buf[len] = 0;

          const char *str = strstr(buf, tracerpid_str);
          tracerpid = str ? atoi(str + sizeof(tracerpid_str)) : -1;
        }

        close(fd);
      }

      if (tracerpid > 0) return true;

      sleep(1);
    }
  }

  return false;
}

void WaitForDebuggerConnect(int argc, char *argv[], int time) {
  for (int i = 1; i < argc; i++) {
    if (strstr(argv[i], "-wait_for_debugger")) {
      printf("\nArg -wait_for_debugger found.\nWaiting %dsec for debugger...\n",
             time);
      printf("  pid = %d\n", getpid());

      if (IsDebuggerPresent(time)) printf("Debugger connected...\n\n");

      break;
    }
  }
}

#endif  // LINUX

}  // namespace

int main(int argc, char *argv[]) {
  // Must add 'bin' to the path....
  const char *path_env{::getenv("LD_LIBRARY_PATH")};

  char cwd[PATH_MAX];
  if (!::getcwd(cwd, sizeof(cwd))) {
    const auto rc = errno;
    fprintf(stderr, "getcwd failed: %s", strerror(rc));
    return rc;
  }

  char new_path_env[4096];
  snprintf(new_path_env, sizeof(new_path_env) - 1,
           "LD_LIBRARY_PATH=%s/" PLATFORM_BIN_DIR ":%s", cwd, path_env);
  if (putenv(new_path_env)) {
    const auto rc = errno;
    fprintf(stderr, "putenv (%s) failed: %s\n", new_path_env, strerror(rc));
    return rc;
  }

  // The generated VPC link graph is incomplete in the same way for every
  // module: none of them declare the tier libraries they import, so a
  // shared object ends up with a DT_NEEDED covering only its subsystem peers.
  // libdedicated.so, for instance, needs appframework and libedit, yet also
  // imports 113 symbols from tier1, 43 from tier0 and 3 from tier2.
  //
  // RTLD_GLOBAL is therefore mandatory, not optional: with the default
  // RTLD_LOCAL the loader has nowhere to look and stops at
  //   undefined symbol: ConCommand::IsCommand
  //
  // RTLD_LAZY rather than RTLD_NOW is equally required. vstdlib and tier1
  // reference each other by design -- vstdlib calls ConnectTier1Libraries()
  // and the ConCommand/ConVar methods once tier1 is mapped, and tier1 calls
  // back into vstdlib -- so neither can be fully bound before the other is
  // loaded. Every such cross-tier reference is a function, so deferring the
  // bind to the first call is correct.
  //
  // The order is load-bearing, because RTLD_LAZY still resolves *data*
  // relocations eagerly. libfilesystem_stdio.so reads the g_pFullFileSystem
  // global, which tier2/tier2.cpp defines, so tier2 has to be in the global
  // scope before filesystem_stdio is opened. Each library is therefore listed
  // after everything it takes data symbols from.
  constexpr int kTierLoadFlags = RTLD_LAZY | RTLD_GLOBAL;
  static constexpr const char *const kTierLibraries[] = {
      "libtier0" DLL_EXT_STRING,
      "libvstdlib" DLL_EXT_STRING,
      "libtier1" DLL_EXT_STRING,
      "libmathlib" DLL_EXT_STRING,
      "libdatamodel" DLL_EXT_STRING,
      "libdmserializers" DLL_EXT_STRING,
      "libtier2" DLL_EXT_STRING,
      "libtier3" DLL_EXT_STRING,
      "libfilesystem_stdio" DLL_EXT_STRING,
      "libappframework" DLL_EXT_STRING,
      "libsoundemittersystem" DLL_EXT_STRING,
      "libNetworksystem" DLL_EXT_STRING,
      "libdmxloader" DLL_EXT_STRING,
      "libbitmap" DLL_EXT_STRING,
  };

  // ScopedDll is move-only, so the handles are kept in a vector; it holds the
  // dlopen() results for the lifetime of main() and closes them on exit.
  std::vector<source::ScopedDll> tier_libs;
  tier_libs.reserve(std::size(kTierLibraries) + 1);
  for (const char *const name : kTierLibraries) {
    tier_libs.emplace_back(name, kTierLoadFlags);
    if (!tier_libs.back()) {
      fprintf(stderr, "open %s failed: %s\n", name, ::dlerror());
      return 1;
    }
  }

  constexpr char dedicated_name[]{"libdedicated" DLL_EXT_STRING};
  tier_libs.emplace_back(dedicated_name, kTierLoadFlags);
  const source::ScopedDll &dedicated = tier_libs.back();
  if (!dedicated) {
    fprintf(stderr, "open %s failed: %s\n", dedicated_name, ::dlerror());
    return 2;
  }

  using DedicatedMainFunction = int (*)(int argc, char *argv[]);
  const auto [dedicated_main, dedicated_main_rc] =
      dedicated.GetFunction<DedicatedMainFunction>("DedicatedMain");
  if (dedicated_main_rc) {
    fprintf(stderr, "Failed to find dedicated server entry point: %s\n",
            dedicated_main_rc.message().c_str());
    return 4;
  }

#ifdef LINUX
  WaitForDebuggerConnect(argc, argv, 30);
#endif

  const int rc{dedicated_main(argc, argv)};

  // Prevent tail call optimization and incorrect stack traces.
  exit(rc);
}
