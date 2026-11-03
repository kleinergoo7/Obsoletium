// forkspy.cpp -- LD_PRELOAD interposer logging fork/clone call sites.
//
// Autonomous stall diagnosis: something forks a parked child ~24s into
// client init. This prints a backtrace at every fork/clone, then chains
// to the real call. No ptrace needed.
//
// Build: g++ -shared -fPIC -o forkspy.so forkspy.cpp -ldl
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <sched.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

static void log_bt(const char *what) {
  void *frames[24];
  int n = backtrace(frames, 24);
  char **syms = backtrace_symbols(frames, n);
  fprintf(stderr, "[forkspy] %s() from pid %d:\n", what, getpid());
  for (int i = 0; i < n; ++i) {
    fprintf(stderr, "[forkspy]   #%d %s\n", i, syms ? syms[i] : "?");
  }
  fflush(stderr);
  // backtrace_symbols allocates; intentionally leaked (fork path, rare).
}

extern "C" {

__attribute__((visibility("default"))) pid_t fork(void) {
  log_bt("fork");
  static pid_t (*real_fork)(void) = nullptr;
  if (!real_fork) {
    real_fork = (pid_t (*)(void))dlsym(RTLD_NEXT, "fork");
  }
  return real_fork();
}

__attribute__((visibility("default"))) int clone(
    int (*fn)(void *), void *stack, int flags, void *arg, ...) {
  log_bt("clone");
  static int (*real_clone)(int (*)(void *), void *, int, void *) = nullptr;
  if (!real_clone) {
    real_clone =
        (int (*)(int (*)(void *), void *, int, void *))dlsym(RTLD_NEXT,
                                                             "clone");
  }
  return real_clone(fn, stack, flags, arg);
}

}  // extern "C"
