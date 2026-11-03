# SeVpc.cmake -- turns the modules emitted by tools/vpc2cmake.py into CMake
# targets.
#
# Each generated <name>.cmake calls:
#
#   se_vpc_module(<target>
#       SOURCES  "a.cpp" "b.cpp"          # engine-root relative
#       INCLUDES "public" "public/tier0"   # engine-root relative
#       DEFINES  "DEBUG" "POSIX"
#       LIBS     "tier1" "mathlib"         # other se_vpc_module targets
#       SYSTEM_LIBS "rt" "pthread"         # passed straight to the linker
#       DYNAMIC  "foo.so"                  # resolved at runtime
#       IS_LIB   0|1                       # 1 = library, 0 = executable
#       PCH     "stdafx.h"
#       CONFIG_TYPE "Dynamic Library (.dll)"
#   )
#
# VPC uses "$BASE" for "inherit what the including script already had"; the
# generator resolves that for us, so nothing here needs to know about it.

include_guard(GLOBAL)

set(SE_VPC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../obsoletium"
    CACHE PATH "Obsoletium source root")
set(SE_VPC_GENERATED "${CMAKE_CURRENT_LIST_DIR}/generated"
    CACHE PATH "Directory holding generated .cmake module files")

# Recorded so the top level can report real numbers instead of guessing.
set_property(GLOBAL PROPERTY SE_VPC_MODULE_COUNT 0)
set_property(GLOBAL PROPERTY SE_VPC_SOURCE_COUNT 0)

# --- helpers ----------------------------------------------------------------

# Everything the generator emits is engine-root relative, and both CMake and
# the compiler want absolute paths. Resolve once, at configure time.
function(_se_vpc_abs out_var)
  foreach(p IN LISTS ARGN)
    if(IS_ABSOLUTE "${p}")
      list(APPEND _abs "${p}")
    else()
      list(APPEND _abs "${SE_VPC_ROOT}/${p}")
    endif()
  endforeach()
  set(${out_var} "${_abs}" PARENT_SCOPE)
endfunction()

# VPC marks some includes as system includes (thirdparty, and anything under a
# "public" dir pulled in via AdditionalLibraryDirectories). Too noisy to
# reproduce exactly, so only silence the obvious thirdparty trees.
function(_se_vpc_system_includes out_var)
  set(_sys "")
  foreach(d IN LISTS ARGN)
    if(d MATCHES "/thirdparty/" OR d MATCHES "/lib/(public|common)/")
      list(APPEND _sys "${d}")
    endif()
  endforeach()
  set(${out_var} "${_sys}" PARENT_SCOPE)
endfunction()

# --- distro-tolerant system library names ---------------------------------

# Valve's Linux $SystemLibraries name the TLS-enabled curl "curl-gnutls", a
# Debian/Ubuntu convention. Distros that ship a single libcurl (openSUSE,
# Fedora, Arch) only provide "curl", so the name from the .vpc does not link.
#
# Rather than editing the .vpc files -- which would diverge from upstream and be
# undone by the next generator run -- resolve these against what the toolchain
# actually has. Unresolvable names are passed through unchanged so the link
# error still names what was asked for.
# Locate a library by bare name, returning its full path or "<name>-NOTFOUND".
# _se_vpc_resolve_system_lib above cannot be used for that: on success it
# returns the candidate name unchanged, which is indistinguishable from its
# "nothing found" return value.
function(_se_vpc_find_lib out_var lib)
  set(_candidates "${lib}")
  if(lib MATCHES "^(.+)-(gnutls|openssl|nss|ssl|wssl|mbedtls|wolfssl)$")
    list(APPEND _candidates "${CMAKE_MATCH_1}")
  endif()
  foreach(_cand IN LISTS _candidates)
    string(REGEX REPLACE "^lib(.+)\\.(so|a)(\\.\\d+)*$" "\\1" _stem "${_cand}")
    string(MAKE_C_IDENTIFIER "${_stem}" _var)
    find_library(_se_vpc_find_${_var} NAMES "${_cand}")
    if(_se_vpc_find_${_var})
      set(${out_var} "${_se_vpc_find_${_var}}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out_var} "${lib}-NOTFOUND" PARENT_SCOPE)
endfunction()

function(_se_vpc_resolve_system_lib out_var lib)
  set(_candidates "${lib}")
  if(lib MATCHES "^(.+)-(gnutls|openssl|nss|ssl|wssl|mbedtls|wolfssl)$")
    list(APPEND _candidates "${CMAKE_MATCH_1}")
  endif()

  foreach(_cand IN LISTS _candidates)
    string(REGEX REPLACE "^lib(.+)\\.(so|a)(\\.\\d+)*$" "\\1" _stem "${_cand}")
    string(MAKE_C_IDENTIFIER "${_stem}" _var)
    unset(_found CACHE)
    find_library(_se_vpc_lib_${_var} NAMES "${_cand}")
    if(_se_vpc_lib_${_var})
      if(NOT "${_cand}" STREQUAL "${lib}")
        message(STATUS "se_vpc: system lib '${lib}' -> '${_cand}'")
      endif()
      set(${out_var} "${_cand}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${out_var} "${lib}" PARENT_SCOPE)
endfunction()

# GCC lowers 16-byte atomics (__atomic_load_16/__atomic_store_16, emitted for
# 16-byte-aligned lock-free operations that x86-64 has no native encoding for)
# to libatomic calls rather than inline code. Every module that touches such an
# object therefore carries undefined __atomic_*_16 symbols that libstdc++ and
# libc do not provide, and the loader then stops with
#     undefined symbol: __atomic_load_16
# at the first dlopen. This is a compiler-runtime dependency, not a game
# dependency, so it is resolved here once and linked into each generated shared
# module instead of being added to every .vpc by hand.
#
# Like the libedit fallback above, prefer the -dev symlink but fall back to the
# versioned runtime, which is all that a runtime-only install ships.
if(NOT DEFINED SE_VPC_ATOMIC_LIBRARY)
  find_library(SE_VPC_ATOMIC_FIND NAMES atomic)
  if(SE_VPC_ATOMIC_FIND)
    set(SE_VPC_ATOMIC_LIBRARY "${SE_VPC_ATOMIC_FIND}")
  else()
    find_file(SE_VPC_ATOMIC_FIND
      NAMES libatomic.so.1 libatomic.so
      PATHS /usr/lib64 /usr/lib /usr/lib/x86_64-linux-gnu)
    set(SE_VPC_ATOMIC_LIBRARY "${SE_VPC_ATOMIC_FIND}")
  endif()
  if(SE_VPC_ATOMIC_LIBRARY)
    message(STATUS "se_vpc: libatomic: ${SE_VPC_ATOMIC_LIBRARY}")
  else()
    message(WARNING
      "se_vpc: libatomic not found; modules using 16-byte atomics will fail to "
      "load with \"undefined symbol: __atomic_load_16\". Install the libatomic "
      "runtime (it does not need -devel).")
  endif()
endif()

# --- the module entry point --------------------------------------------------

function(se_vpc_module TARGET)
  set(options "")
  # These four take a single value; without them cmake_parse_arguments dumps
  # "IS_LIB", "1", "PCH", ... into M_LIBS and tries to link them.
  set(oneValueArgs IS_LIB PCH CONFIG_TYPE OUTPUT)
  set(multiValueArgs SOURCES INCLUDES DEFINES LIBS SYSTEM_LIBS DYNAMIC)
  cmake_parse_arguments(M "${options}" "${oneValueArgs}"
                        "${multiValueArgs}" ${ARGN})

  # The .vpc [$SDL] include dirs point at the vendored thirdparty/SDL
  # snapshot, whose SDL_oldnames.h is broken (legacy names map to undefined
  # *_renamed_* tokens) and whose API is newer than this code. System SDL3
  # provides working legacy aliases, so drop any vendored-SDL include dir
  # up front; <SDL3/...> then resolves to /usr/include/SDL3. (The link side
  # already uses the system library.) Filtering M_INCLUDES covers both the
  # normal and the SYSTEM include steps below. Nothing else thirdparty is
  # filtered -- other vendored trees are still needed.
  if(M_INCLUDES)
    set(_se_no_sdl "")
    foreach(_se_d IN LISTS M_INCLUDES)
      if(_se_d MATCHES "thirdparty/SDL")
        continue()
      endif()
      list(APPEND _se_no_sdl "${_se_d}")
    endforeach()
    set(M_INCLUDES "${_se_no_sdl}")
  endif()

  # A .vpc may list sources that do not exist in this tree (Windows-only tools
  # such as hammer_dll/mxtoolkitwin32, ScriptBind language modules, and a few
  # $SRCDIR\public\*.cpp references that are not shipped). Drop them but keep
  # them visible: a genuine omission shows up as an undefined symbol at link
  # time, which is far easier to diagnose than a configure-time abort.
  # The generator emits engine-root relative paths, so they must be resolved
  # against SE_VPC_ROOT before testing: EXISTS() is relative to the current
  # source/binary dir, and would reject every perfectly good file.
  set(_present "")
  set(_missing "")
  foreach(s IN LISTS M_SOURCES)
    if(IS_ABSOLUTE "${s}")
      set(_abs "${s}")
    else()
      set(_abs "${SE_VPC_ROOT}/${s}")
    endif()
    if(EXISTS "${_abs}")
      list(APPEND _present "${_abs}")
    else()
      list(APPEND _missing "${s}")
    endif()
  endforeach()
  if(_missing)
    list(LENGTH _missing _nmiss)
    list(GET _missing 0 _first)
    message(STATUS "se_vpc: ${TARGET} drops ${_nmiss} absent source(s), "
                   "e.g. ${_first}")
    set_property(GLOBAL APPEND PROPERTY SE_VPC_MISSING_SOURCES
                 "${TARGET}:${_nmiss}")
  endif()

  # A target with no buildable sources cannot exist in CMake. This happens for
  # Windows-only tools whose entire file list is absent from the tree. Record
  # the name so the top level can report it instead of failing the configure.
  if(NOT _present)
    message(STATUS "se_vpc: ${TARGET} has no buildable sources; not created.")
    set_property(GLOBAL APPEND PROPERTY SE_VPC_EMPTY_TARGETS "${TARGET}")
    return()
  endif()

  if(NOT TARGET "${TARGET}")
    if(M_IS_LIB)
      add_library("${TARGET}" SHARED ${_present})
      set_target_properties("${TARGET}" PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET default)
    else()
      add_executable("${TARGET}" ${_present})
    endif()
  endif()

  target_include_directories("${TARGET}" PRIVATE
    ${SE_VPC_ROOT}
    ${M_INCLUDES})

  _se_vpc_system_includes(_sysinc ${M_INCLUDES})
  if(_sysinc)
    target_include_directories("${TARGET}" SYSTEM PRIVATE ${_sysinc})
  endif()

  if(M_DEFINES)
    target_compile_definitions("${TARGET}" PRIVATE ${M_DEFINES})
  endif()

  if(M_SYSTEM_LIBS)
    set(_se_vpc_sys_libs "")
    foreach(_lib IN LISTS M_SYSTEM_LIBS)
      _se_vpc_resolve_system_lib(_lib_out "${_lib}")
      list(APPEND _se_vpc_sys_libs "${_lib_out}")
    endforeach()
    target_link_libraries("${TARGET}" PRIVATE ${_se_vpc_sys_libs})
  endif()

  if(SE_VPC_ATOMIC_LIBRARY)
    target_link_libraries("${TARGET}" PRIVATE "${SE_VPC_ATOMIC_LIBRARY}")
  endif()

  # M_LIBS names other se_vpc_module targets. Link them but do not let CMake
  # guess the ordering from directory names; VPC's own order is authoritative
  # and mostly irrelevant for shared objects.
  foreach(lib IN LISTS M_LIBS)
    # A .vpc may name its own module in $Lib: an import library is a real build
    # product on Windows, so listing it is meaningful there, but linking a
    # CMake target to itself is an error. Skip it rather than fail configure.
    if("${lib}" STREQUAL "${TARGET}")
      message(STATUS "se_vpc: ${TARGET} lists itself in \$Lib; ignored")
      continue()
    endif()
    if(TARGET "${lib}")
      target_link_libraries("${TARGET}" PRIVATE "${lib}")
    else()
      # A $Lib that names another generated module cannot be resolved yet when
      # the referring module happens to be configured first: modules are
      # created in the order se_vpc_load() walks them, so material system's
      # "$Lib vtf" was checked before the vtf target existed, fell through to
      # the system-library lookup, found nothing on disk (libvtf.so is a build
      # product) and was dropped. libmaterialsystem.so then kept an undefined
      # DestroyVTFTexture(IVTFTexture*) and its dlopen failed.
      # Defer instead; se_vpc_resolve_pending_libs() retries once every module
      # target exists, which both finds the target and records the build
      # dependency so the library is linked before this module.
      set_property(GLOBAL APPEND PROPERTY
        SE_VPC_PENDING_LIBS "${TARGET}|${lib}")
    endif()
  endforeach()

  # PCH: VPC's $CreateUsePCHThroughFile names a header included *through*
  # stdafx.h, so the PCH source is always stdafx.h.
  if(M_PCH AND EXISTS "${SE_VPC_ROOT}/tier0/stdafx.h")
    target_precompile_headers("${TARGET}" PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:${SE_VPC_ROOT}/tier0/stdafx.h>")
  endif()

  if(M_OUTPUT)
    set_target_properties("${TARGET}" PROPERTIES OUTPUT_NAME "${M_OUTPUT}")
  endif()

  set_property(GLOBAL APPEND PROPERTY SE_VPC_MODULE_TARGETS "${TARGET}")
  get_property(_n GLOBAL PROPERTY SE_VPC_MODULE_COUNT)
  if(NOT _n)
    set(_n 0)
  endif()
  math(EXPR _n "${_n} + 1")
  set_property(GLOBAL PROPERTY SE_VPC_MODULE_COUNT ${_n})
endfunction()

# --- deferred $Lib resolution -------------------------------------------------

# Retry the $Lib entries se_vpc_module() had to defer, now that every generated
# module target exists. Names that still are not targets are real system or
# vendored shared libraries: engine/engine.vpc lists SDL3, snappy, bz2, zs and
# zip-utils that way, and a .vpc $Lib is a plain library name rather than a
# target, so resolve those the same way M_SYSTEM_LIBS is resolved. Previously
# these were skipped with "not built yet", which left libengine.so with
# unresolved SDL_* and snappy::* symbols and made dlopen("libengine.so",
# RTLD_NOW) fail.
function(se_vpc_resolve_pending_libs)
  get_property(_pending GLOBAL PROPERTY SE_VPC_PENDING_LIBS)
  if(NOT _pending)
    return()
  endif()
  set_property(GLOBAL PROPERTY SE_VPC_PENDING_LIBS "")
  foreach(_entry IN LISTS _pending)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _target)
    list(GET _parts 1 _lib)
    if(NOT TARGET "${_target}")
      continue()
    endif()
    if(TARGET "${_lib}")
      target_link_libraries("${_target}" PRIVATE "${_lib}")
      message(STATUS "se_vpc: ${_target} links module '${_lib}' (deferred)")
    else()
      _se_vpc_find_lib(_lib_out "${_lib}")
      if(NOT _lib_out MATCHES "-NOTFOUND$")
        target_link_libraries("${_target}" PRIVATE "${_lib_out}")
        message(STATUS "se_vpc: ${_target} links system '${_lib}'")
      else()
        message(STATUS
          "se_vpc: ${_target} wants '${_lib}' (no such module or library)")
      endif()
    endif()
  endforeach()
endfunction()

# --- loading the generated modules -------------------------------------------

# Load every generated module whose name is in MODULES. Callers pass the
# dependency-ordered list they actually need so that a half-finished project
# does not drag in targets that cannot compile yet.
function(se_vpc_load)
  foreach(m IN LISTS ARGN)
    set(_f "${SE_VPC_GENERATED}/${m}.cmake")
    if(NOT EXISTS "${_f}")
      message(FATAL_ERROR "se_vpc: missing generated module '${_f}'")
    endif()
    include("${_f}")
  endforeach()
endfunction()
