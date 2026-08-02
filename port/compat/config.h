// Minimal stand-in for BOINC's autoconf-generated config.h.
//
// third_party/boinc is a source submodule, not a configured build tree, so
// nothing generates this. Only the handful of feature macros the gui-rpc
// library actually tests are defined; the rest default to 0, which is correct
// on every platform this port targets.
//
// The BOINC version is not written here. The library expects config.h to carry
// it on non-Windows (autoconf normally generates both together), so this pulls
// in the submodule's own version.h instead of restating the numbers - they
// cannot drift from the sources being built. The file this replaced claimed
// 7.24.1 while compiling 6.13-era code.
#pragma once

// Windows does not use this file - BOINC reaches for boinc_win.h there. Left
// defined, HAVE_SYS_SOCKET_H and friends convince lib/network.h that it must
// declare sockaddr_in and socklen_t itself, which then collide with the real
// declarations in MinGW's ws2tcpip.h.
#ifndef _WIN32

#define HAVE_UNISTD_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_UN_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_RESOURCE_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STATVFS_H 1      // free-space query in filesys.cpp
#define HAVE_NETINET_IN_H 1
#define HAVE_NETINET_TCP_H 1
#define HAVE_ARPA_INET_H 1
#define HAVE_NETDB_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_STRINGS_H 1
#define HAVE_FCNTL_H 1
#define HAVE_ALLOCA_H 1
#define HAVE_STRDUP 1
#define HAVE_STRCASECMP 1
#define HAVE_SETENV 1
#define HAVE_DAEMON 1
#define HAVE_RES_INIT 0

#endif  // !_WIN32

#include "version.h"        // BOINC_MAJOR_VERSION etc, from the submodule
#include "msvc_crt_shim.h"
