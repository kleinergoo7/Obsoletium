# Copyright (c) 2024 The Source Authors.  All rights reserved.

cmake_minimum_required(VERSION 3.15 FATAL_ERROR)

# Compiler id for Apple Clang is now AppleClang.
if (POLICY CMP0025)
  cmake_policy(SET CMP0025 NEW)
endif()

# Include file check macros honor CMAKE_REQUIRED_LIBRARIES.
if (POLICY CMP0075)
  cmake_policy(SET CMP0075 NEW)
endif()

# option() honor variables.
if (POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
endif()

# Setup root directory of the Obsoletium project.
set(SE_ROOT_DIR "${CMAKE_SOURCE_DIR}" CACHE STRING
  "Directory root of the Obsoletium project.")

## Package information.
set(PACKAGE_NAME   "Obsoletium")

# Git is required to build software version via Git.
find_package(Git)

if (NOT DEFINED PACKAGE_VERSION)
  if (NOT Git_FOUND)
    message(FATAL "[common]: Unable to extract Git software version - no Git.")
  endif()

  message(STATUS "[common]: Extracting Git software version.")

  if ("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Windows")
    execute_process(COMMAND cmd /c "git rev-list --branches HEAD | find /v " " /c" OUTPUT_VARIABLE SE_GIT_VERSION WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  else()
    execute_process(COMMAND bash "-c" "git rev-list --branches HEAD | wc -l | tr -d ' ' | tr -d '\n'" OUTPUT_VARIABLE SE_GIT_VERSION WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  set(PACKAGE_VER_PATCH 2)
  math(EXPR PACKAGE_VER_TWEAK ${SE_GIT_VERSION})
  message(STATUS
    "[common]: Build version from Git (patch.tweak): ${PACKAGE_VER_PATCH}.${PACKAGE_VER_TWEAK}.")

  set(PACKAGE_VER_MAJOR 1)
  set(PACKAGE_VER_MINOR 3)

  set(PACKAGE_VERSION
    "${PACKAGE_VER_MAJOR}.${PACKAGE_VER_MINOR}.${PACKAGE_VER_PATCH}.${PACKAGE_VER_TWEAK}")
endif()
set(PACKAGE_STRING     "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME    "${PACKAGE_NAME}-${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT  "https://github.com/Source-Authors/obsolete-source-engine/issues")

project(${PACKAGE_NAME}
  VERSION ${PACKAGE_VER_MAJOR}.${PACKAGE_VER_MINOR}.${PACKAGE_VER_PATCH}.${PACKAGE_VER_TWEAK}
  DESCRIPTION "Obsoletium"
  # C as well as CXX: the .vpc file lists include genuine C sources that have to
  # be compiled as C. thirdparty/7zip/C/LzmaDec.c is the one that matters here --
  # it provides LzmaDec_Init/Allocate/Free/DecodeToBuf and LzmaDecode, which
  # tier1/lzmaDecoder.cpp and the VPK/zip readers call, so with C disabled CMake
  # silently emitted no rule for it and tier1 left those symbols undefined.
  LANGUAGES C CXX)

message(STATUS "[common]: CMake version: ${CMAKE_VERSION}.")
message(STATUS "[common]: ${PACKAGE_NAME} version: ${PACKAGE_VERSION}.")

## Common variables.

set(SE_BINARY_DIR "${CMAKE_BINARY_DIR}" CACHE STRING
  "Directory of the Obsoletium binaries output.")
set(SE_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}" CACHE STRING
  "Name of the Obsoletium build system.")

if (${SE_SYSTEM_NAME} STREQUAL "Darwin")
  set(SE_OS_MACOS ON CACHE BOOL "MacOS is build system.")
else()
  set(SE_OS_MACOS OFF CACHE BOOL "MacOS is NOT build system.")
endif()

if (${SE_SYSTEM_NAME} STREQUAL "Linux")
  set(SE_OS_LINUX ON CACHE BOOL "Linux distro is build system.")
else()
  set(SE_OS_LINUX OFF CACHE BOOL "Linux distro is NOT build system.")
endif()

if (${SE_SYSTEM_NAME} STREQUAL "Windows")
  set(SE_OS_WIN ON CACHE BOOL "Windows is build system.")
else()
  set(SE_OS_WIN OFF CACHE BOOL "Windows is NOT build system.")
endif()

if (SE_OS_MACOS OR SE_OS_LINUX)
  set(SE_OS_POSIX ON CACHE BOOL "POSIX compatible os is build system.")
else()
  set(SE_OS_POSIX OFF CACHE BOOL "POSIX compatible os is NOT build system.")
endif()

set(SE_PRODUCT_NAME "Obsoletium" CACHE STRING
  "Name of the Obsoletium product.")

# Minimum standard level is C++17.
set(CMAKE_CXX_STANDARD 17)
# On g++ this ensures: -std=c++17 and not -std=gnu++17
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING
  "CMake build type used to build ${PACKAGE_NAME}.")
set(SE_CMAKE_GENERATOR "${CMAKE_GENERATOR}" CACHE STRING
  "CMake generator used to generate build tree." FORCE)
set(SE_CXX_COMPILER "${CMAKE_CXX_COMPILER}" CACHE STRING
  "Compiler used to build ${PACKAGE_NAME}.")
set(SE_CXX_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}" CACHE STRING
  "Compiler id used to build ${PACKAGE_NAME}.")
set(SE_CXX_COMPILER_FRONTEND_VARIANT "${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}"
  CACHE STRING "Compiler frontend used to build ${PACKAGE_NAME}.")
set(SE_EXPORT_COMPILE_COMMANDS ${CMAKE_EXPORT_COMPILE_COMMANDS} CACHE BOOL
  "Enable/Disable output of compile commands during generation.")

if (SE_CXX_COMPILER_ID MATCHES "Clang")
  set(SE_COMPILER_CLANG ON CACHE BOOL "Compiler is Clang.")
else()
  set(SE_COMPILER_CLANG OFF CACHE BOOL "Compiler is NOT Clang.")
endif()

if (SE_CXX_COMPILER_ID STREQUAL "GNU")
  set(SE_COMPILER_GCC ON CACHE BOOL "Compiler is GCC.")
else()
  set(SE_COMPILER_GCC OFF CACHE BOOL "Compiler is NOT GCC.")
endif()

if (SE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(SE_COMPILER_MSVC ON CACHE BOOL "Compiler is MSVC.")
else()
  set(SE_COMPILER_MSVC OFF CACHE BOOL "Compiler is NOT MSVC.")
endif()

## Common dependencies.

if (Git_FOUND)
  message(STATUS
    "[common]: Git version ${GIT_VERSION_STRING} found at '${GIT_EXECUTABLE}'.")

  # Get the current working branch.
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${SE_ROOT_DIR}
    OUTPUT_VARIABLE SE_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  # Get the latest commit hash.
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY ${SE_ROOT_DIR}
    OUTPUT_VARIABLE SE_GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  # Get the subject of the commit.
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" log -1 --format=%s
    WORKING_DIRECTORY "${SE_ROOT_DIR}"
    OUTPUT_VARIABLE SE_GIT_COMMIT_SUBJECT
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

  message(STATUS
    "[common]: Git using branch '${SE_GIT_BRANCH}', "
    "commit ${SE_GIT_COMMIT_HASH} | ${SE_GIT_COMMIT_SUBJECT}.")
endif()

message(STATUS
  "[common]: ${PACKAGE_NAME} using CMake generator: ${SE_CMAKE_GENERATOR}.")

# Select correct threading library.
if (NOT SE_OS_WIN)
  set(CMAKE_THREAD_PREFER_PTHREAD ON)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
endif()
find_package(Threads REQUIRED)
list(APPEND CMAKE_REQUIRED_LIBRARIES Threads::Threads)
if (NOT CMAKE_THREAD_LIBS_INIT)
  set(SE_THREADS_LIBRARY "system default" CACHE STRING
    "Threads library used to build ${PACKAGE_NAME}.")
else()
  set(SE_THREADS_LIBRARY "${CMAKE_THREAD_LIBS_INIT}" CACHE STRING
    "Threads library used to build ${PACKAGE_NAME}.")
endif()
message(STATUS
  "[common]: ${PACKAGE_NAME} using threading library: ${SE_THREADS_LIBRARY}.")

if (XCODE_VERSION)
  message(STATUS
    "[common]: ${PACKAGE_NAME} using XCode version: ${XCODE_VERSION}.")
endif()

message(STATUS "[common]: ${PACKAGE_NAME} cxx compiler: ${SE_CXX_COMPILER}.")
message(STATUS "[common]: ${PACKAGE_NAME} build type: ${SE_BUILD_TYPE}.")

if (SE_OS_WIN)
  # Generate common product version info.
  configure_file(
    ${SE_ROOT_DIR}/build/product_version_config.h.cmake.in
    ${SE_BINARY_DIR}/build/product_version_config.h
  )

  # Generate app version info.
  configure_file(
    ${SE_ROOT_DIR}/build/app_version_config.h.cmake.in
    ${SE_BINARY_DIR}/build/app_version_config.h
  )

  # Generate visual styles manifest info.
  configure_file(
    ${SE_ROOT_DIR}/build/win/manifests/enable-visual-styles.manifest.cmake.in
    ${SE_BINARY_DIR}/manifests/enable-visual-styles.manifest
  )
endif()

# ===========================================================================
# Native Linux port: generated VPC modules.
#
# Upstream Obsoletium ships this file as a configuration skeleton with no
# add_subdirectory() and no targets. Rather than hand-maintain ~230
# CMakeLists.txt, cmake/SeVpc.cmake turns the modules emitted by
# tools/vpc2cmake.py into real targets.
#
# Regenerate with:
#   python3 tools/vpc2cmake.py --root . --out cmake/generated
# ===========================================================================

set(SE_PORT_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake" CACHE PATH
  "Directory holding SeVpc.cmake and the generated modules.")
list(APPEND CMAKE_MODULE_PATH "${SE_PORT_DIR}")

if(NOT EXISTS "${SE_PORT_DIR}/SeVpc.cmake")
  message(FATAL_ERROR
    "[port]: ${SE_PORT_DIR}/SeVpc.cmake missing. Is this Obsoletium "
    "checkout inside the css-port-experiment tree?")
endif()
if(NOT EXISTS "${SE_PORT_DIR}/generated")
  message(FATAL_ERROR
    "[port]: ${SE_PORT_DIR}/generated missing. Run: "
    "python3 tools/vpc2cmake.py --root . --out cmake/generated")
endif()

set(SE_VPC_ROOT "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH
  "Engine source root that generated module paths are relative to.")
set(SE_VPC_GENERATED "${SE_PORT_DIR}/generated" CACHE PATH
  "Directory holding generated .cmake module files.")

include(SeVpc)

# --- global compile settings ------------------------------------------------
# Linux/GCC specific. The defines themselves come from the .vpc files and
# arrive per-target via se_vpc_module(DEFINES ...).
add_compile_options(
  -Wall
  -Wno-unused-variable
  -Wno-unused-but-set-variable
  -Wno-unused-function
  -Wno-sign-compare
  -Wno-parentheses
  -Wno-missing-field-initializers
  -Wno-class-memaccess
  -Wno-implicit-fallthrough
  -Wno-format-truncation
  -Wno-stringop-truncation
  -Wno-deprecated-declarations
  -fno-strict-aliasing
  -fPIC
  # VPC defines _XM_SSE3_INTRINSICS_/_XM_SSE4_INTRINSICS_ for every module
  # (source_dll_base.vpc); DirectXMath's always_inline intrinsics then require
  # the matching target ISA or compilation fails on the first rebuild.
  -msse3
  -msse4.1
)

# The 16-byte atomic CAS in tier0/tslist.h is not inlined by gcc without
# -mcx16, so it calls libatomic's __atomic_compare_exchange_16; current
# openSUSE no longer loads libatomic transitively, so link it explicitly.
# Without it the loader fails on "undefined symbol:
# __atomic_compare_exchange_16".
add_link_options(-latomic)

# Headers are found with -I<root>/<dir>, exactly as the .vpc include paths are
# written ("tier0/dbg.h", not "obsoletium/tier0/dbg.h").
include_directories("${CMAKE_CURRENT_LIST_DIR}")

# glibc removed the malloc/realloc/memalign/free interposition hooks
# (__malloc_hook and friends) in glibc 2.34, so the hook-based allocator
# override cannot be built on any current system -- this host runs glibc 2.40.
# The tree already provides exactly this escape hatch (see
# public/tier0/memalloc.h, tier0/memdbg.cpp and tier0/platform_posix.cpp, all of
# which test NO_HOOK_MALLOC), so define it rather than patching those files.
# The modern dlsym/RTLD_NEXT path in tier0/memoverride.cpp is unaffected.
add_compile_definitions(NO_HOOK_MALLOC)

# DirectXMath is a header-only INTERFACE library here (thirdparty/DirectXMath's
# own CMakeLists adds an INTERFACE target, and Inc/DirectXMath.h carries no
# _WIN32 guards, so it is portable). But only materialsystem/shader_api_dx9|dx11
# add its include directory, while public/mathlib/vector.h, vmatrix.h,
# vector2d.h, vector4d.h, mathlib.h, math_pfns.h, ssemath.h and halton.h all do
# a bare <DirectXMath.h>. tier0 reaches vector.h through
#   stdafx.h -> dbg.h -> platform.h -> byteswap.h -> datamap.h -> mathlib/vector.h
# so tier0 cannot compile without this directory on the search path.
include_directories("${CMAKE_CURRENT_LIST_DIR}/thirdparty/DirectXMath/Inc")

# sal.h shim for the vendored DirectXMath, which includes it unconditionally.
# See cmake/compat/sal.h for why each annotation expands to nothing.
include_directories("${SE_PORT_DIR}/compat")

# SDL3: use the system headers (/usr/include/SDL3), not the vendored
# thirdparty/SDL snapshot. The snapshot's SDL_oldnames.h maps every legacy
# name (SDL_NUM_SCANCODES, SDL_SYSTEM_CURSOR_ARROW, ...) to a
# *_renamed_* token that its own headers never define, so any TU using a
# legacy name fails; and its API (float mouse state etc.) is newer than what
# this code was written against. System SDL3 3.2.18 still provides the legacy
# aliases and matches. The .vpc "thirdparty/SDL" include dirs do not resolve
# <SDL3/...> on their own, so nothing else needs to change; the link side
# already resolves SDL3 from the system (see se_vpc_resolve_pending_libs).
# If <SDL3/SDL.h> is not found, install the distro's SDL3-devel package.

# Phase 2 native Vulkan backend (PROMPT 2.x): port-owned module, no upstream
# .vpc (it replaces shaderapidx9/shaderapiempty/toGL at runtime). Sources are
# variant-independent; S2.2 brings instance/device/queue, later steps extend.
set(_se_vk_dir "${CMAKE_CURRENT_LIST_DIR}/materialsystem/shaderapivulkan")
if(EXISTS "${_se_vk_dir}/shaderapivulkan.cpp")
  find_library(SE_VULKAN_LIBRARY NAMES vulkan)
  add_library(shaderapivulkan SHARED
    ${_se_vk_dir}/shaderapivulkan.cpp
    ${_se_vk_dir}/se_vk_bind.cpp
    ${_se_vk_dir}/se_vk_init.cpp
    ${_se_vk_dir}/se_vk_matrix.cpp
    ${_se_vk_dir}/se_vk_mesh.cpp
    ${_se_vk_dir}/se_vk_swapchain.cpp
    ${_se_vk_dir}/se_vk_texcache.cpp
    ${_se_vk_dir}/se_vk_pipeline.cpp
    ${_se_vk_dir}/se_vk_texture.cpp
    ${_se_vk_dir}/se_vk_shaderpack.cpp
    ${_se_vk_dir}/api_stubs.cpp
    ${_se_vk_dir}/device_stubs.cpp
    ${_se_vk_dir}/devmgr_stubs.cpp
    ${_se_vk_dir}/shadow_stubs.cpp
    # Every module carries its own CreateInterface export (the documented
    # tier1/interface.cpp design: Sys_GetFactoryThis must resolve to the
    # module-specific symbol, not libtier1's process-global one).
    "${CMAKE_CURRENT_LIST_DIR}/tier1/interface.cpp"
  )
  set_target_properties(shaderapivulkan PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default)
  target_include_directories(shaderapivulkan PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/public"
    "${CMAKE_CURRENT_LIST_DIR}/public/tier0"
    "${CMAKE_CURRENT_LIST_DIR}/public/tier1"
    "${CMAKE_CURRENT_LIST_DIR}/public/appframework"
    "${CMAKE_CURRENT_LIST_DIR}/public/shaderapi"
    "${CMAKE_CURRENT_LIST_DIR}/public/materialsystem"
    "${CMAKE_CURRENT_LIST_DIR}/materialsystem"
    "${CMAKE_CURRENT_LIST_DIR}/common"
    "${_se_vk_dir}")
  # Same platform/ABI defines every generated module gets from its .vpc;
  # platform.h refuses to configure a platform without them.
  target_compile_definitions(shaderapivulkan PRIVATE
    BINK_VIDEO COMPILER_GCC DEBUG DEV_BUILD FRAME_POINTER_OMISSION_DISABLED
    GNUC LINUX NO_MEMOVERRIDE_NEW_DELETE NO_STEAM PLATFORM_64BITS POSIX
    USE_SDL VPC _DEBUG _DLL_EXT=.so _LINUX _POSIX
    _XM_SSE3_INTRINSICS_ _XM_SSE4_INTRINSICS_)
  target_link_libraries(shaderapivulkan PRIVATE vstdlib)
  find_library(SE_SDL3_LIBRARY NAMES SDL3)
  if(SE_SDL3_LIBRARY)
    target_link_libraries(shaderapivulkan PRIVATE "${SE_SDL3_LIBRARY}")
  else()
    message(WARNING "[port]: SDL3 lib not found; shaderapivulkan WSI needs it.")
  endif()
  if(SE_VULKAN_LIBRARY)
    target_link_libraries(shaderapivulkan PRIVATE "${SE_VULKAN_LIBRARY}")
    message(STATUS "[port]: shaderapivulkan links ${SE_VULKAN_LIBRARY}")
  else()
    message(WARNING "[port]: libvulkan not found; shaderapivulkan will not link.")
  endif()
  # S2.4 hardcoded triangle shaders: GLSL sources in spv/ are compiled
  # ahead of time with glslangValidator (a build tool, not a runtime layer),
  # validated with spirv-val (build fails on invalid modules), and embedded
  # as uint32_t arrays. setup-env.sh mandates both tools.
  find_program(SE_GLSLANG_VALIDATOR NAMES glslangValidator REQUIRED)
  find_program(SE_SPIRV_VAL NAMES spirv-val REQUIRED)
  set(_se_spv_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/vkspv")
  file(MAKE_DIRECTORY "${_se_spv_dir}")
  add_custom_command(
    OUTPUT "${_se_spv_dir}/tri.vert.spv" "${_se_spv_dir}/tri.frag.spv"
    COMMAND "${SE_GLSLANG_VALIDATOR}" -V "${_se_vk_dir}/spv/tri.vert"
      -o "${_se_spv_dir}/tri.vert.spv"
    COMMAND "${SE_GLSLANG_VALIDATOR}" -V "${_se_vk_dir}/spv/tri.frag"
      -o "${_se_spv_dir}/tri.frag.spv"
    COMMAND "${SE_SPIRV_VAL}" "${_se_spv_dir}/tri.vert.spv"
    COMMAND "${SE_SPIRV_VAL}" "${_se_spv_dir}/tri.frag.spv"
    DEPENDS "${_se_vk_dir}/spv/tri.vert" "${_se_vk_dir}/spv/tri.frag"
    COMMENT "[port]: glslang+spirv-val S2.4 triangle shaders")
  add_custom_command(
    OUTPUT "${_se_spv_dir}/tri_tex.vert.spv" "${_se_spv_dir}/tri_tex.frag.spv"
    COMMAND "${SE_GLSLANG_VALIDATOR}" -V "${_se_vk_dir}/spv/tri_tex.vert"
      -o "${_se_spv_dir}/tri_tex.vert.spv"
    COMMAND "${SE_GLSLANG_VALIDATOR}" -V "${_se_vk_dir}/spv/tri_tex.frag"
      -o "${_se_spv_dir}/tri_tex.frag.spv"
    COMMAND "${SE_SPIRV_VAL}" "${_se_spv_dir}/tri_tex.vert.spv"
    COMMAND "${SE_SPIRV_VAL}" "${_se_spv_dir}/tri_tex.frag.spv"
    DEPENDS "${_se_vk_dir}/spv/tri_tex.vert" "${_se_vk_dir}/spv/tri_tex.frag"
    COMMENT "[port]: glslang+spirv-val S2.5 textured shaders")
  add_custom_command(
    OUTPUT "${_se_spv_dir}/se_tri_spv.h"
    COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${SE_PORT_DIR}/.."
      python3 "${SE_PORT_DIR}/../tools/spv_embed.py"
      "${_se_spv_dir}/se_tri_spv.h"
      kTriVertSpv "${_se_spv_dir}/tri.vert.spv"
      kTriFragSpv "${_se_spv_dir}/tri.frag.spv"
      kTriTexVertSpv "${_se_spv_dir}/tri_tex.vert.spv"
      kTriTexFragSpv "${_se_spv_dir}/tri_tex.frag.spv"
    DEPENDS "${_se_spv_dir}/tri.vert.spv" "${_se_spv_dir}/tri.frag.spv"
      "${_se_spv_dir}/tri_tex.vert.spv" "${_se_spv_dir}/tri_tex.frag.spv"
    COMMENT "[port]: embedding S2.4/S2.5 SPIR-V")
  add_custom_target(se_tri_spv DEPENDS "${_se_spv_dir}/se_tri_spv.h")
  add_dependencies(shaderapivulkan se_tri_spv)
  target_include_directories(shaderapivulkan PRIVATE "${_se_spv_dir}")
  # Alias under the exact name the -shaderapi flag takes (cf. shaderapiempty).
  add_custom_command(TARGET shaderapivulkan POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "$<TARGET_FILE_NAME:shaderapivulkan>" "shaderapivulkan.so"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:shaderapivulkan>")

  # S2.3 gate executable: standalone swapchain/clear proof outside the engine.
  add_executable(vk_present_test EXCLUDE_FROM_ALL
    ${_se_vk_dir}/vk_present_test.cpp
    ${_se_vk_dir}/se_vk_init.cpp
    ${_se_vk_dir}/se_vk_swapchain.cpp
    ${_se_vk_dir}/se_vk_pipeline.cpp
    ${_se_vk_dir}/se_vk_texture.cpp
    ${_se_vk_dir}/se_vk_shaderpack.cpp
  )
  add_dependencies(vk_present_test se_tri_spv)
  target_include_directories(vk_present_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/public"
    "${CMAKE_CURRENT_LIST_DIR}/public/tier0"
    "${CMAKE_CURRENT_LIST_DIR}/public/tier1"
    "${_se_vk_dir}"
    "${_se_spv_dir}")
  target_compile_definitions(vk_present_test PRIVATE
    COMPILER_GCC DEBUG DEV_BUILD GNUC LINUX NO_STEAM PLATFORM_64BITS POSIX
    USE_SDL VPC _DEBUG _LINUX _POSIX)
  target_link_libraries(vk_present_test PRIVATE vstdlib "${SE_VULKAN_LIBRARY}")
  if(SE_SDL3_LIBRARY)
    target_link_libraries(vk_present_test PRIVATE "${SE_SDL3_LIBRARY}")
  endif()

  # S2.8-matrix gate: the constant upload convention (slot = column). Pure CPU,
  # no Vulkan device; run by hand: obsoletium/build-client/vk_matrix_test.
  add_executable(vk_matrix_test EXCLUDE_FROM_ALL
    ${_se_vk_dir}/vk_matrix_test.cpp)
  target_include_directories(vk_matrix_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/public"
    "${CMAKE_CURRENT_LIST_DIR}/public/tier0"
    "${CMAKE_CURRENT_LIST_DIR}/public/tier1"
    "${_se_vk_dir}")
  target_compile_definitions(vk_matrix_test PRIVATE
    COMPILER_GCC DEBUG DEV_BUILD GNUC LINUX NO_STEAM PLATFORM_64BITS POSIX
    USE_SDL VPC _DEBUG _LINUX _POSIX)
  target_link_libraries(vk_matrix_test PRIVATE shaderapivulkan vstdlib mathlib)
endif()

# thirdparty/snappy ships snappy-stubs-public.h.in and expects the build system
# to generate snappy-stubs-public.h from it at configure time (its own
# CMakeLists.txt does exactly that). Nothing in the port configured it, so
# thirdparty/snappy/snappy.h:47 fails to find the header. On POSIX the
# generated file only carries the version macros: the iovec fallback struct is
# compiled out because sys/uio.h exists. The substitutions are done explicitly
# rather than via configure_file() so the engine's own PROJECT_VERSION_* is not
# clobbered -- snappy's version is its own (1.2.2).
set(_se_snappy_stubs_dir "${SE_BINARY_DIR}/generated/snappy")
file(READ "${CMAKE_CURRENT_LIST_DIR}/thirdparty/snappy/snappy-stubs-public.h.in"
     _se_snappy_stubs_in)
# The \$ escaping matters: an unescaped "${...}" would be expanded by CMake
# itself (to nothing, for an undefined name) and string(REPLACE) would then
# match the empty string everywhere in the file.
string(REPLACE "\${HAVE_SYS_UIO_H_01}" "1" _se_snappy_stubs_in "${_se_snappy_stubs_in}")
string(REPLACE "\${PROJECT_VERSION_MAJOR}" "1" _se_snappy_stubs_in "${_se_snappy_stubs_in}")
string(REPLACE "\${PROJECT_VERSION_MINOR}" "2" _se_snappy_stubs_in "${_se_snappy_stubs_in}")
string(REPLACE "\${PROJECT_VERSION_PATCH}" "2" _se_snappy_stubs_in "${_se_snappy_stubs_in}")
file(WRITE "${_se_snappy_stubs_dir}/snappy-stubs-public.h" "${_se_snappy_stubs_in}")
include_directories("${_se_snappy_stubs_dir}")

# thirdparty/mpaheaderinfo is the one engine dependency with no .vpc, so
# tools/vpc2cmake.py cannot describe it -- yet engine.cmake still links
# "mpaheaderinfo" (engine/audio/private/mp3duration_helper.cpp uses CMPAFile /
# MPAFileSystem to read MP3 durations).
#
# It does ship its own CMakeLists.txt, but that file is a standalone project:
# it calls project(), find_package(Git), derives its version from `git rev-list`
# and forces CMAKE_BUILD_TYPE to Release, so add_subdirectory() would mutate the
# engine's cache. Define the target here instead, mirroring the
# SA_MPA_BUILD_WITHOUT_ATL=ON source list of that CMakeLists (the ATL/MFC
# sources it excludes are Windows-only and already excluded by that option).
# engine/engine.vpc lists snappy as $LibExternal "$SNAPPYOUTDIRRELEASE/snappy"
# [!$WINDOWS], i.e. upstream links a prebuilt that a Linux checkout does not
# have, so the name never resolved and libengine.so kept four undefined
# snappy::* symbols (RawCompress, RawUncompress, MaxCompressedLength,
# GetUncompressedLength; engine/common.cpp uses them for the VPK fast path),
# which is fatal under the RTLD_NOW that Sys_LoadModule uses.
#
# thirdparty/snappy ships a CMakeLists.txt, but it is a standalone project that
# calls project() and configures its own options, so add_subdirectory() would
# mutate the engine's cache. Define the target from the same source list that
# file's non-test build uses, and the stubs header generated just above is
# already on the include path.
set(_se_snappy_dir "${CMAKE_CURRENT_LIST_DIR}/thirdparty/snappy")
add_library(snappy STATIC
  ${_se_snappy_dir}/snappy.cc
  ${_se_snappy_dir}/snappy-c.cc
  ${_se_snappy_dir}/snappy-sinksource.cc
  ${_se_snappy_dir}/snappy-stubs-internal.cc
)
target_include_directories(snappy PUBLIC "${_se_snappy_dir}")

# thirdparty/zip-utils (XUnzip/XZip, used by engine for zip handling) is the
# same story: standalone CMake project, so define the target from its two
# sources rather than add_subdirectory(). engine.vpc names it as $Lib
# "zip-utils", which never resolves to a target, leaving OpenZip/CloseZip/
# UnzipItem et al. undefined and libengine.so unloadable.
set(_se_ziputils_dir "${CMAKE_CURRENT_LIST_DIR}/thirdparty/zip-utils")
add_library(zip-utils STATIC
  ${_se_ziputils_dir}/XUnzip.cpp
  ${_se_ziputils_dir}/XZip.cpp
)
target_include_directories(zip-utils PUBLIC "${_se_ziputils_dir}")
# Upstream's portable (non-Windows) path, enabled by its ZU_ENABLE_STD_ONLY
# option; without it the sources pull windows.h/tchar.h.
target_compile_definitions(zip-utils PRIVATE ZIP_STD=1)
# XZip.h uses uint32_t two lines before its own <cstdint> include (harmless
# when windows.h comes first). Force the include instead of patching upstream.
target_compile_options(zip-utils PRIVATE -include cstdint)
if(TARGET engine)
  target_link_libraries(engine PRIVATE zip-utils)
endif()

set(_se_mpa_dir "${CMAKE_CURRENT_LIST_DIR}/thirdparty/mpaheaderinfo")
add_library(mpaheaderinfo STATIC
  ${_se_mpa_dir}/APETag.cpp
  ${_se_mpa_dir}/ID3V1Tag.cpp
  ${_se_mpa_dir}/ID3V2Tag.cpp
  ${_se_mpa_dir}/LameTag.cpp
  ${_se_mpa_dir}/Lyrics3Tag.cpp
  ${_se_mpa_dir}/MPAEndOfFileException.cpp
  ${_se_mpa_dir}/MPAException.cpp
  ${_se_mpa_dir}/MPAFile.cpp
  ${_se_mpa_dir}/MPAFileStream.cpp
  ${_se_mpa_dir}/MPAFileSystem.cpp
  ${_se_mpa_dir}/StdioMPAFileSystem.cpp
  ${_se_mpa_dir}/MPAFrame.cpp
  ${_se_mpa_dir}/MPAHeader.cpp
  ${_se_mpa_dir}/MPAStream.cpp
  ${_se_mpa_dir}/MusicMatchTag.cpp
  ${_se_mpa_dir}/Platform.cpp
  ${_se_mpa_dir}/stdafx.cpp
  ${_se_mpa_dir}/Tag.cpp
  ${_se_mpa_dir}/Tags.cpp
  ${_se_mpa_dir}/VBRHeader.cpp
  ${_se_mpa_dir}/VBRIHeader.cpp
  ${_se_mpa_dir}/XINGHeader.cpp
)
target_compile_definitions(mpaheaderinfo PRIVATE SA_MPA_BUILD_WITHOUT_ATL=1)
target_include_directories(mpaheaderinfo PUBLIC "${_se_mpa_dir}")

# engine's sources spell this dependency "mpaheaderinfo/stdafx.h" and
# "mpaheaderinfo/MPAFile.h", i.e. they expect `thirdparty` itself to be an
# include root. The generated INCLUDES only carry
# thirdparty/mpaheaderinfo/out/Debug, which holds no headers.
include_directories("${CMAKE_CURRENT_LIST_DIR}/thirdparty")

# The engine targets itself with -m32 nowhere; make that explicit and fatal if
# a toolchain file or inherited CXXFLAGS ever tries.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR
    "[port]: refusing to configure a 32-bit build (CMAKE_SIZEOF_VOID_P="
    "${CMAKE_SIZEOF_VOID_P}). PROMPT.md forbids -m32.")
endif()

# --- load generated modules -------------------------------------------------
# SE_VPC_MODULES lets a caller load a subset (faster configure while bringing
# the tree up stage by stage). Empty means "everything the generator produced".
set(SE_VPC_MODULES "" CACHE STRING
  "Generated module target names to load. Empty = all of them.")

if(SE_VPC_MODULES)
  se_vpc_load(${SE_VPC_MODULES})
else()
  file(GLOB _se_vpc_all "${SE_VPC_GENERATED}/*.cmake")
  list(SORT _se_vpc_all)
  foreach(_f IN LISTS _se_vpc_all)
    include("${_f}")
  endforeach()
  list(LENGTH _se_vpc_all _se_vpc_n)
  message(STATUS "[port]: loaded ${_se_vpc_n} generated VPC modules.")
endif()

# No generated module declares the tier libraries it imports. tier1.vpc and
# vstdlib.vpc have no $Dependencies and no $Libs section at all, and the other
# modules only ever list their own subsystem peers (engine.vpc names appframework,
# mathlib, dmxloader, ... but not tier0/vstdlib/tier1), so every shared object
# ends up with a DT_NEEDED that covers none of its tier imports.
#
# That is invisible at link time -- a shared object may leave symbols undefined --
# and only surfaces at dlopen, one library at a time:
#   libappframework.so: undefined symbol: ...
#   libengine.so:       undefined symbol: ...
#   libdedicated.so:    undefined symbol: g_pFullFileSystem
#
# Linking vstdlib covers the whole tier stack, because the chain above gives it a
# DT_NEEDED on tier1 and tier1 has one on tier0. tier0, vstdlib and tier1 are
# skipped: they are the chain itself, and a self-edge would be a cycle.
#
# Linking vstdlib covers the whole tier stack, because the chain above gives it a
# DT_NEEDED on tier1 and tier1 has one on tier0. tier0, vstdlib and tier1 are
# skipped: they are the chain itself, and a self-edge would be a cycle.
get_property(_se_targets GLOBAL PROPERTY SE_VPC_MODULE_TARGETS)
if(_se_targets AND TARGET vstdlib)
  set(_se_tier_roots tier0 vstdlib tier1)
  set(_se_linked 0)
  foreach(_se_t IN LISTS _se_targets)
    if(TARGET ${_se_t} AND NOT ${_se_t} IN_LIST _se_tier_roots)
      get_target_property(_se_type ${_se_t} TYPE)
      if(_se_type STREQUAL "SHARED_LIBRARY")
        target_link_libraries(${_se_t} PRIVATE vstdlib)
        math(EXPR _se_linked "${_se_linked} + 1")
      endif()
    endif()
  endforeach()
  message(STATUS
    "[port]: linked vstdlib into ${_se_linked} generated shared modules")
endif()

# The same disease extends beyond the tier: no .vpc names mathlib, and several
# subsystem modules omit peers they use (dmserializers.json LIBS is literally
# []), so ldd -r leaves e.g. MatrixBuildRotateZ undefined and the first
# dlopen(RTLD_NOW) of the chain dies -- launcher.so via libdmserializers.so.
# Audited 2026-10-09 (ldd -r over build-client, undefineds intersected with
# sibling providers). Every edge below was checked acyclic against the
# vpc-declared LIBS (a target-level cycle fails configure -- shared-shared
# cycles are rejected, unlike the vstdlib<->tier1 one worked around by path);
# the multi-provider tuples (same symbol in client+server+engine+...) stay on
# the runtime global scope, as on the dedicated server.
set(_se_link_edges
  "tier2|mathlib"
  "tier3|tier2;mathlib;choreoobjects"
  "datamodel|mathlib"
  "dmserializers|datamodel;mathlib"
  "datacache|choreoobjects;mathlib"
  "choreoobjects|mathlib"
  "bitmap|tier2"
  "vtf|bitmap;mathlib"
  "dmxloader|mathlib"
  "particles|dmxloader;tier2;mathlib"
  "vgui_controls|tier2;tier3;mathlib"
  "matsys_controls|vgui_controls;bitmap;dmxloader;choreoobjects;tier2;tier3;mathlib"
  "gameui|dmxloader;choreoobjects;vgui_controls"
  "studiorender|choreoobjects"
  "vguimatsurface|choreoobjects;vgui_controls"
  "engine|choreoobjects;vgui_controls"
  "launcher|choreoobjects;mathlib"
  "filesystem_stdio|mathlib"
  "inputsystem|mathlib"
  "Networksystem|mathlib"
  "scriplib|mathlib"
  "shaderlib|mathlib"
  "replay_common|tier3")
foreach(_se_edge IN LISTS _se_link_edges)
  string(REPLACE "|" ";" _se_parts "${_se_edge}")
  list(GET _se_parts 0 _se_consumer)
  list(REMOVE_AT _se_parts 0)
  if(TARGET ${_se_consumer})
    get_target_property(_se_ctype ${_se_consumer} TYPE)
    if(_se_ctype STREQUAL "SHARED_LIBRARY")
      foreach(_se_prov IN LISTS _se_parts)
        if(TARGET ${_se_prov})
          target_link_libraries(${_se_consumer} PRIVATE ${_se_prov})
        endif()
      endforeach()
    endif()
  endif()
endforeach()

# datamodel->tier2 and vpklib->filesystem_stdio are genuine mutual needs too
# (datamodel wants tier2's CUtlStreamBuffer/GenerateFullPath while tier2's vpc
# LIBS name datamodel+dmserializers back; vpklib wants filesystem_stdio's
# CCrypto RSA helpers while filesystem_stdio's vpc LIBS name vpklib back), but
# they must stay edge-free on the build side: Ninja maps the .so path back to
# the producing target, so even a path link closes a file-level cycle
# (libtier2.so -> libdmserializers.so -> libdatamodel.so -> libtier2.so) and
# the generate step fails. At runtime glibc resolves the mutual NEEDED pair
# through the global scope -- exactly how the dedicated server (same vpc
# pairs, no extra edges) already loads. If a run ever faults on one of these
# symbols, revisit then with evidence, not speculation.

# filesystem_stdio owns g_pFullFileSystem (filesystem/basefilesystem.cpp), which
# dedicated's ISystem layer calls, but dedicated.vpc does not name it. Without
# this the dedicated server dies at
#   open libdedicated.so failed: ... undefined symbol: g_pFullFileSystem
if(TARGET dedicated AND TARGET filesystem_stdio)
  target_link_libraries(dedicated PRIVATE filesystem_stdio)
endif()

# engine/sys_getmodes.cpp writes JPEG screenshots via jpeglib.h; no .vpc names
# the library (Windows links its own), so link the system one. libjpeg62-devel
# is a declared setup-env.sh dependency.
if(TARGET engine)
  find_library(SE_JPEG_LIBRARY NAMES jpeg)
  if(SE_JPEG_LIBRARY)
    target_link_libraries(engine PRIVATE "${SE_JPEG_LIBRARY}")
  else()
    message(WARNING "[port]: libjpeg not found; engine keeps undefined "
                    "jpeg_* and dlopen(libengine.so) will fail.")
  endif()
endif()

# engine/audio/private/mp3duration_helper.cpp is in engine.vpc unconditionally and
# uses CMPAFile / IMPAFile from thirdparty/mpaheaderinfo, but the library itself is
# named only as
#   $Libexternal "$MPAHEADERINFOOUTDIRRELEASE/mpaheaderinfo" [!$WINDOWS&&!$DEDICATED]
# so on a dedicated build the generator drops the name and the target above ends
# up unreferenced: libengine.so then has three undefined mpaheaderinfo symbols
# (_ZN8CMPAFileC1EPKcP14IMPAFileSystem, _ZN8CMPAFileD1Ev, _ZN8IMPAFileD2Ev).
# The link has to be added here; the .vpc cannot express it.
if(TARGET engine AND TARGET mpaheaderinfo)
  target_link_libraries(engine PRIVATE mpaheaderinfo)
endif()

# engine/sv_uploadgamestats.cpp includes <uuid/uuid.h> and calls uuid_generate,
# uuid_parse and uuid_unparse_lower to build the game-server session id. No .vpc
# names a uuid library on any platform -- the Windows build gets it from another
# dependency -- so on Linux the three symbols stay undefined. libuuid is a
# system library, and util-linux-libs ships it.
if(TARGET engine)
  find_library(SE_UUID_LIBRARY NAMES uuid)
  if(SE_UUID_LIBRARY)
    target_link_libraries(engine PRIVATE "${SE_UUID_LIBRARY}")
  else()
    message(WARNING "[port]: libuuid not found; engine will keep undefined "
                    "uuid_generate/uuid_parse/uuid_unparse_lower and "
                    "dlopen(libengine.so) will fail.")
  endif()
endif()

get_property(_se_vpc_n GLOBAL PROPERTY SE_VPC_MODULE_COUNT)
message(STATUS "[port]: ${_se_vpc_n} CMake targets created.")
get_property(_se_missing GLOBAL PROPERTY SE_VPC_MISSING_SOURCES)
if(_se_missing)
  list(LENGTH _se_missing _se_missing_n)
  message(STATUS
    "[port]: ${_se_missing_n} target(s) had absent sources dropped "
    "(Windows-only tools / unbundled ScriptBind modules).")
endif()

# thirdparty/cryptopp/*.cpp uses always_inline AES/PCLMUL/SHA/SIMD intrinsics
# guarded by CRYPTOPP_BOOL_ALIGN16 / CRYPTOPP_DISABLE_ASM-less paths. Building
# the vendored sources with the project's default flags fails with
#   "Fehler: Inlining beim Aufruf von ... always_inline fehlgeschlagen:
#    target specific option mismatch"
# because __AES_DECL__ and friends are no-ops unless the matching -m<isa> is
# enabled, so the intrinsic and the always_inline body disagree on the target.
# cryptopp.vpc already documents this, and it collects them in
# $Compiler $GCC_ExtraCompilerFlags -- but tools/vpc2cmake.py has no FLAGS
# keyword on se_vpc_module(), so Settings.flags is recorded in the .json audit
# and never reaches the compiler. Attach them to the one target that needs
# them: cryptopp is self-contained and no other module compiles its sources.
if(TARGET cryptopp)
  target_compile_options(cryptopp PRIVATE
    -maes -mpclmul -msha -msse4.1 -mssse3 -mavx2 -mbmi2)
  message(STATUS "[port]: cryptopp compiled with AES/PCLMUL/SHA/SSE/AVX2/BMI2")
endif()

# dedicated/console/TextConsoleUnix.cpp includes the vendored libedit header
# directly ("../thirdparty/libedit-3.1/src/histedit.h") and uses the full
# el_* / history_* API. dedicated.vpc links it as
#   $LibExternal "$SRCDIR/thirdparty/libedit-3.1/src/.libs/libedit" [$LINUXALL]
# which expects a prebuilt in-tree library we do not have (and must not ship).
# The header is the only thing needed to compile, and the distro runtime library
# exports the identical NetBSD libedit ABI, so link that instead. -ledit needs
# libedit-devel for the libedit.so symlink, so fall back to the versioned
# runtime library when that is all the host has.
if(TARGET dedicated)
  find_library(SE_EDIT_LIBRARY NAMES edit)
  if(NOT SE_EDIT_LIBRARY)
    find_file(SE_EDIT_LIBRARY
      NAMES libedit.so.0 libedit.so
      PATHS /usr/lib64 /usr/lib /usr/lib/x86_64-linux-gnu)
  endif()
  if(SE_EDIT_LIBRARY)
    message(STATUS "[port]: dedicated links libedit: ${SE_EDIT_LIBRARY}")
    target_link_libraries(dedicated PRIVATE "${SE_EDIT_LIBRARY}")
  else()
    message(WARNING
      "[port]: no libedit found; the dedicated console will not build. "
      "Install the libedit runtime (it does not need -devel).")
  endif()
endif()

# No .vpc anywhere names freetype, but the Linux font path needs it:
# common/vgui_surfacelib/linuxfont.h and FontManager.h do
# `#include <ft2build.h>` + `FT_FREETYPE_H` (used by vgui_surfacelib,
# vgui2/src/Surface.cpp and vguimatsurface/MatSystemSurface.cpp). Upstream
# Windows must get these headers another way; on Linux the system freetype2
# (a declared setup-env.sh dependency, BUILD.md-pinned) is the provider.
# FindFreetype ships with CMake, so no extra find-module is needed.
find_package(Freetype QUIET)
if(NOT FREETYPE_FOUND)
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(FREETYPE QUIET freetype2)
  endif()
endif()
if(FREETYPE_FOUND OR FREETYPE_LIBRARIES)
  message(STATUS "[port]: freetype: ${FREETYPE_INCLUDE_DIRS}")
  foreach(_se_ft_t IN ITEMS vgui_surfacelib vgui2 vguimatsurface)
    if(TARGET ${_se_ft_t})
      target_include_directories(${_se_ft_t} PRIVATE ${FREETYPE_INCLUDE_DIRS})
      target_link_libraries(${_se_ft_t} PRIVATE ${FREETYPE_LIBRARIES})
    endif()
  endforeach()
else()
  message(WARNING
    "[port]: freetype2 not found; vgui_surfacelib/vgui2/vguimatsurface "
    "will fail on ft2build.h. Install freetype2 devel files.")
endif()

# The tier libraries import each other heavily, but none of tier0/vstdlib/tier1
# declare their dependencies: tier1.vpc and vstdlib.vpc have no $Dependencies
# and no $Libs section at all, and the generated modules therefore carry an empty
# LIBS list. Valve's build infers the tier order from the directory layout; a
# generated graph cannot, so the chain has to be spelled out.
#
# This is not optional tidiness. Because no DT_NEEDED edges exist between the
# tiers, every cross-tier import is resolved from the global scope, and
# RTLD_LAZY cannot rescue them either: at least one reference is a data
# relocation, which glibc must bind eagerly.
#     R_X86_64_64  _ZNK10ConCommand9IsCommandEv + 0     (in libvstdlib.so)
# so opening libvstdlib.so fails outright with
#     undefined symbol: _ZNK10ConCommand9IsCommandEv
# and, because libdedicated.so has a DT_NEEDED for appframework only, its 113
# tier1 / 43 tier0 imports fail the same way.
#
# vstdlib and tier1 reference one another by design -- vstdlib calls
# ConnectTier1Libraries() plus the ConCommand/ConVar methods once tier1 is up,
# and tier1 calls back into vstdlib -- so both edges are declared. glibc handles
# a DT_NEEDED cycle between shared objects: it maps each member, relocates
# dependencies first, and resolves from the symbol tables.
if(TARGET tier0 AND TARGET vstdlib AND TARGET tier1)
  target_link_libraries(vstdlib PUBLIC tier0)
  target_link_libraries(tier1 PUBLIC tier0)
  # The vstdlib -> tier1 edge is added by path rather than by target name.
  # CMake refuses a vstdlib <-> tier1 cycle outright ("cyclic dependencies are
  # allowed only among static libraries") and Ninja then rejects the
  # file-level cycle "libvstdlib.so -> libtier1.so -> libvstdlib.so", so the
  # opposite edge is deliberately not declared: tier1 links with its vstdlib
  # imports unresolved, which a shared object is allowed to do.
  #
  # The runtime direction is what matters, and a DT_NEEDED cycle between two
  # shared objects is fine for glibc. Opening libvstdlib.so maps it, pulls in
  # libtier1.so through DT_NEEDED, relocates the dependency first, and resolves
  # each member's imports from the symbol tables of both. --no-as-needed stops
  # the linker dropping the edge as unreferenced.
  get_target_property(_se_tier1_dir tier1 LIBRARY_OUTPUT_DIRECTORY)
  if(NOT _se_tier1_dir)
    set(_se_tier1_dir "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
  target_link_options(vstdlib PRIVATE "LINKER:--no-as-needed")
  target_link_libraries(vstdlib PRIVATE "${_se_tier1_dir}/libtier1${CMAKE_SHARED_MODULE_SUFFIX}")
  message(STATUS "[port]: tier link chain vstdlib -> tier1 -> tier0 declared")
endif()

# dedicated.vpc lists only "SDL3 appframework edit mathlib tier2 tier3" and
# relies on the tier2/tier3 entries to pull tier1/vstdlib/tier0 in transitively.
# tier2 and tier3 are static libraries, so nothing is propagated at runtime and
# libdedicated.so ends up with no DT_NEEDED on any tier. Name them explicitly so
# the loader can satisfy the imports itself instead of depending on whatever the
# launcher happened to have in the global scope.
if(TARGET dedicated)
  foreach(se_tier tier0 vstdlib tier1)
    if(TARGET ${se_tier})
      target_link_libraries(dedicated PRIVATE ${se_tier})
    endif()
  endforeach()
endif()

# tier1/pathmatch.cpp normalises UTF-8 paths by interposing on the libc file
# APIs: every WRAP(fn, ...) in that file defines __wrap_fn, and the linker's
# -Wl,--wrap=fn is what turns the declared `__real_fn` into a reference to the
# genuine libc `fn` and redirects this link unit's calls to `fn` at __wrap_fn.
#
# Without those flags the wrappers cannot resolve, and both libtier0.so and
# libtier1.so fail to load at startup with "undefined symbol: __real_utime"
# (then __real_fopen, __real_stat, ... one at a time, since the loader reports
# the first missing symbol only).
#
# The list is scraped from the source instead of hardcoded, so adding a WRAP()
# to pathmatch.cpp cannot silently need a matching edit here. pathmatch.cpp is
# compiled into both tier0 and tier1, and __wrap_fn has to be in the same link
# unit as the __real_fn references for the redirect to resolve, so both targets
# need the flags.
set(_se_pathmatch "${CMAKE_CURRENT_LIST_DIR}/tier1/pathmatch.cpp")
set(_se_wrap_libs "")
if(EXISTS "${_se_pathmatch}")
  # Most wrappers are macro-generated, so the source text is "WRAP(fn, ...)".
  # A few (__wrap_access) are written out by hand, so pick up explicit
  # "__wrap_fn(" definitions too. The macro body itself is
  # "__wrap_##fn(...)", whose "##" cannot start the [A-Za-z_] class, so the
  # expansion of WRAP is never mistaken for a hand-written wrapper.
  # Each pattern is a full-line REPLACE pattern capturing the symbol name:
  #   1. macro-generated wrappers, anchored at the start of the line
  #   2. hand-written wrappers, with a leading ".*" so the return type
  #      ("int __wrap_access(...)") is consumed along with the tail
  # The leading/trailing ".*" are required because REGEX REPLACE substitutes
  # only the matched span. The macro body itself is "__wrap_##fn(...)", whose
  # "##" cannot start the [A-Za-z_] class, so a WRAP() expansion is never
  # mistaken for a hand-written wrapper.
  foreach(_se_re_pat
      "^[ \t]*WRAP\\(([A-Za-z_][A-Za-z0-9_]*).*$"
      ".*__wrap_([A-Za-z_][A-Za-z0-9_]*).*$")
    file(STRINGS "${_se_pathmatch}" _se_wrap_lines REGEX "${_se_re_pat}")
    foreach(_se_wrap_line IN LISTS _se_wrap_lines)
      string(REGEX REPLACE "${_se_re_pat}" "\\1" _se_wrap_fn "${_se_wrap_line}")
      list(APPEND _se_wrap_libs "${_se_wrap_fn}")
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES _se_wrap_libs)
  list(SORT _se_wrap_libs)
endif()

if(_se_wrap_libs)
  set(_se_wrap_opts "")
  foreach(_se_wrap_fn IN LISTS _se_wrap_libs)
    list(APPEND _se_wrap_opts "-Wl,--wrap=${_se_wrap_fn}")
  endforeach()
  set(_se_wrap_targets "")
  foreach(_t IN ITEMS tier0 tier1)
    if(TARGET "${_t}")
      target_link_options("${_t}" PRIVATE ${_se_wrap_opts})
      list(APPEND _se_wrap_targets "${_t}")
    endif()
  endforeach()
  list(LENGTH _se_wrap_libs _se_wrap_n)
  message(STATUS
    "[port]: libc --wrap interposition (${_se_wrap_n} fns) on: ${_se_wrap_targets}")
else()
  message(WARNING
    "[port]: could not scrape WRAP() list from tier1/pathmatch.cpp; "
    "libtier0/libtier1 will not load at runtime.")
endif()

# launcher_main (main_posix.cpp) dlopens bin/<plat>/launcher.so by exact
# path via source::ScopedDll, with no lib-prefix retry, so publish the alias
# next to the binary like the engine's client/server aliases above.
if(TARGET launcher)
  add_custom_command(TARGET launcher POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "$<TARGET_FILE_NAME:launcher>" "launcher.so"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:launcher>"
    COMMENT "[port]: launcher.so -> $<TARGET_FILE_NAME:launcher> "
            "(launcher_main dlopens \"launcher\"${_DLL_EXT} by path)")
endif()

# Two more exact-name loads with no prefix retry: FileSystem_GetFileSystemDLLName
# (public/filesystem_init.cpp) stats "<exeDir>/filesystem_stdio.so" before any
# filesystem exists, and the materialsystem Sys_LoadModule("shaderapiempty")
# behind -shaderapi takes the name verbatim. Publish both aliases.
foreach(_se_alias_pair "filesystem_stdio|filesystem_stdio" "shaderapiempty|shaderapiempty")
  string(REPLACE "|" ";" _se_parts "${_se_alias_pair}")
  list(GET _se_parts 0 _se_tgt)
  list(GET _se_parts 1 _se_alias)
  if(TARGET ${_se_tgt})
    add_custom_command(TARGET ${_se_tgt} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E create_symlink
              "$<TARGET_FILE_NAME:${_se_tgt}>" "${_se_alias}.so"
      WORKING_DIRECTORY "$<TARGET_FILE_DIR:${_se_tgt}>")
  endif()
endforeach()

# Must stay last: se_vpc_resolve_pending_libs() can only tell a generated module
# target from a system library once every module has been created.
se_vpc_resolve_pending_libs()

# Port-owned: game DLLs (client_*/server_*) must not export their globals.
# game/shared sources (usermessages, igamesystem, ConVars, CBGSDriver,
# per-DLL `engine` pointers, ...) compile into both DLLs. Windows DLLs have
# isolated namespaces so each keeps private copies; on Linux every module
# loads RTLD_GLOBAL (load-bearing for engine<->materialsystem globals), so
# without this the game DLLs' globals interpose across client/server. The
# observed failures: duplicate "Geiger" user-message Error (modal-dialog
# stall), ConCommand double-registration asserts, cross-DLL game-system
# execution (server running the client's GameStats ResetData -> null call
# crash), "already added" warning floods. Hiding visibility restores the
# Windows isolation: CreateInterface stays exported (explicit DLL_EXPORT),
# game->engine imports still resolve via the global namespace, and only the
# factory function remains visible.
foreach(_gametgt IN ITEMS client_CStrike server_CStrike
    client_HL2 server_HL1 client_HL1MP
    client_DOD server_DOD client_Episodic server_Episodic)
  if(TARGET ${_gametgt})
    set_target_properties(${_gametgt} PROPERTIES
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON)
  endif()
endforeach()

# The engine loads the game DLL by a fixed name rather than by project name:
# engine/sys_dll.cpp calls LoadThisDll( "server" DLL_EXT_STRING, ... ), so at
# runtime it looks for server.so in the GAMEBIN search path and then, via the
# executable-directory fallback plus the lib prefix retry added to
# CBaseFileSystem::LoadModule, for libserver.so next to the binary. The per-mod
# projects are named after the mod, so the CS:S module builds as
# libserver_CStrike.so and startup died with
#   Could not load server.so library. Try restarting. If that doesn't work,
#   verify the cache.
# Publish the module under the name the loader actually asks for. A symlink
# rather than an OUTPUT_NAME override keeps the descriptive artifact name (and
# keeps the other per-mod projects from colliding on the generic one).
if(TARGET server_CStrike)
  add_custom_command(TARGET server_CStrike POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "$<TARGET_FILE_NAME:server_CStrike>" "libserver.so"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:server_CStrike>"
    COMMENT "[port]: libserver.so -> $<TARGET_FILE_NAME:server_CStrike> "
            "(engine loads \"server\"${_DLL_EXT})")
endif()

# Same fixed-name lookup on the client side: engine/cdll_engine_int.cpp calls
# LoadModule( "client" DLL_EXT_STRING, "GAMEBIN", ... ), so publish
# libclient_CStrike.so as libclient.so next to the binary.
if(TARGET client_CStrike)
  add_custom_command(TARGET client_CStrike POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "$<TARGET_FILE_NAME:client_CStrike>" "libclient.so"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:client_CStrike>"
    COMMENT "[port]: libclient.so -> $<TARGET_FILE_NAME:client_CStrike> "
            "(engine loads \"client\"${_DLL_EXT})")
endif()
