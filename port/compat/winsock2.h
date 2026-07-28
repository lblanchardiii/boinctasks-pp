// =============================================================================
// winsock2.h (compat shim) - maps Winsock API onto BSD sockets for the port.
// Sits on the include path before the system so `#include <winsock2.h>` in
// unmodified source resolves here on Linux.
// =============================================================================
#pragma once

#ifdef _WIN32
// On Windows this file must not shadow the real header. It does sit earlier on
// the include path than the system directories - a -I path always does - so
// hand off to the next winsock2.h along the search order rather than replacing
// it. Without this, Winsock 2 has no declarations at all and ws2tcpip.h fails.
#include_next <winsock2.h>
#else

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

typedef int SOCKET;
typedef struct sockaddr  SOCKADDR;
typedef struct sockaddr* LPSOCKADDR;
typedef int* PINT;

// shutdown() 'how' names
#define SD_RECEIVE SHUT_RD
#define SD_SEND    SHUT_WR
#define SD_BOTH    SHUT_RDWR

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR   (-1)
#endif

#define closesocket(s)          close(s)
#define ioctlsocket(s,c,a)      ioctl((s),(c),(a))

// error codes commonly checked by the RPC code
#define WSAEWOULDBLOCK  EWOULDBLOCK
#define WSAEINPROGRESS  EINPROGRESS
#define WSAECONNRESET   ECONNRESET
#define WSAETIMEDOUT    ETIMEDOUT
#define WSAECONNREFUSED ECONNREFUSED
#define WSAEADDRINUSE   EADDRINUSE
#define WSAHOST_NOT_FOUND HOST_NOT_FOUND

inline int WSAGetLastError() { return errno; }

// startup/teardown are no-ops on POSIX
typedef struct { int dummy; } WSADATA;
#define MAKEWORD(a,b) (((b) << 8) | (a))
inline int WSAStartup(unsigned short, WSADATA*) { return 0; }
inline int WSACleanup() { return 0; }

#endif // !_WIN32
