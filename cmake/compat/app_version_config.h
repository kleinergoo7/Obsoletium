// Copyright (c) 2024 The Source Authors.  All rights reserved.
//
// POSIX stand-in for the Windows-generated app_version_config.h.
//
// The upstream tree only generates this header inside `if (SE_OS_WIN)` in
// CMakeLists.txt, from a build/app_version_config.h.cmake.in template that is
// not shipped in this source drop. dedicated/sys_ded.cpp still includes it
// unconditionally, because it prints a version banner on startup:
//
//   Msg("%s v.%s build with %s on glibc ...", SE_PRODUCT_FILE_DESCRIPTION_STRING,
//       SE_PRODUCT_FILE_VERSION_INFO_STRING, ...);
//
// This header lives under cmake/compat/, which is already on the include path
// of every target, so no per-target include needs to be added. The version
// numbers mirror the values CMake derives from the git history (PACKAGE_VER_*)
// so the banner agrees with project(... VERSION ...). Bump them together.

#ifndef SE_COMPAT_APP_VERSION_CONFIG_H_
#define SE_COMPAT_APP_VERSION_CONFIG_H_

#define SE_STRINGIFY_HELPER(x) #x
#define SE_STRINGIFY(x) SE_STRINGIFY_HELPER(x)

// App long product name.
#define SE_PRODUCT_NAME_STRING "Source Dedicated Server"

// App file version header / info, as the dotted strings the banner expects.
// (The Windows-generated header builds these from numeric components via
// SE_STRINGIFY, but a version list is variadic, so keep them as literals.)
#define SE_APP_VER_FILE_VERSION_HEADER "4.1.1.1"
#define SE_APP_VER_FILE_VERSION_INFO "4.1.1.1"

// App file version as string.
#define SE_PRODUCT_FILE_VERSION_INFO_STRING SE_APP_VER_FILE_VERSION_INFO

// App file description.
#ifndef SE_PRODUCT_FILE_DESCRIPTION_STRING
#define SE_PRODUCT_FILE_DESCRIPTION_STRING SE_PRODUCT_NAME_STRING
#endif

// Product version as string. engine/console.cpp paints it into the console
// background banner ("Source Engine %i (build %d [v.%S])"). Upstream defines
// this in external/vpc/utils/vpc/product_version_config.h (1.3.2.920); the
// value here mirrors that file so both banners agree.
#define SE_PRODUCT_VERSION_INFO_STRING "1.3.2.920"

#endif  // !SE_COMPAT_APP_VERSION_CONFIG_H_
