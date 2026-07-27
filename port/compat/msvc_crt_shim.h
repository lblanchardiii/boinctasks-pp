// MSVC "secure CRT" mappings for POSIX builds (shared by bt_compat.h and config.h)
#pragma once
#if !defined(_WIN32) && defined(__cplusplus)
#include <cstdio>
#include <cstring>
#include <strings.h>
#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif
#ifndef BT_CRT_SHIM
#define BT_CRT_SHIM
inline int strcpy_s(char* d, size_t n, const char* s) { snprintf(d, n, "%s", s); return 0; }
template<size_t N> inline int strcpy_s(char (&d)[N], const char* s) { snprintf(d, N, "%s", s); return 0; }
inline int strncpy_s(char* d, size_t dn, const char* s, size_t count)
{
    if (count == _TRUNCATE) { snprintf(d, dn, "%s", s); return 0; }
    size_t n = (count < dn - 1) ? count : dn - 1;
    memcpy(d, s, n); d[n] = '\0'; return 0;
}
template<size_t N> inline int strncpy_s(char (&d)[N], const char* s, size_t count) { return strncpy_s(d, N, s, count); }
inline int strcat_s(char* d, size_t n, const char* s) { size_t l = strlen(d); if (l < n) snprintf(d + l, n - l, "%s", s); return 0; }
template<size_t N> inline int strcat_s(char (&d)[N], const char* s) { return strcat_s(d, N, s); }
#define _snprintf_s(buf, size, trunc, ...) snprintf(buf, size, __VA_ARGS__)
#define sprintf_s snprintf
#define sscanf_s  sscanf
#define _stricmp  strcasecmp
#define _strnicmp strncasecmp
#endif // BT_CRT_SHIM
#endif // !_WIN32 && __cplusplus
