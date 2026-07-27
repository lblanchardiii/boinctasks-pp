// =============================================================================
// bt_compat.h - Linux compatibility layer for the BoincTasks port
//
// Maps the small set of Windows/MFC types and helpers used by the non-UI core
// onto POSIX + wxWidgets (wxBase) equivalents. Include via stdafx.h.
// =============================================================================
#pragma once

#ifndef _WIN32

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <wx/string.h>
#include <wx/thread.h>
#include <wx/datetime.h>
#include <wx/time.h>
#include <wx/utils.h>

// ---- basic Windows types ----------------------------------------------------
typedef uint8_t   BYTE;
typedef uint16_t  WORD;
typedef uint32_t  DWORD;
typedef int32_t   LONG;
typedef uint32_t  ULONG;
typedef int       BOOL;
typedef void*     HANDLE;
typedef void*     HWND;
typedef char      TCHAR;
typedef const char* LPCTSTR;
typedef char*     LPTSTR;
typedef uint64_t  ULONGLONG;
typedef int64_t   LONGLONG;

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#ifndef MAX_PATH
#define MAX_PATH 4096
#endif

// ---- minimal MFC object model ----------------------------------------------
class CObject { public: virtual ~CObject() {} };
class CWnd : public CObject {};   // opaque; only ever used as a pointer headless

// ---- app-level defines used by core files (from BoincTasks.h/DlgLogging.h) --
#define LOCALHOST_NAME     "localhost"
#define LOGGING_NORMAL     1
#define LOGGING_DEBUG      2
#define LOGGING_DEBUGFULL  3

#include "msvc_crt_shim.h"

// ---- strings ----------------------------------------------------------------
// wxString is API-compatible with CString for the operations the core uses
// (Format, Left, Right, Mid, Find, Trim, MakeLower, GetLength, IsEmpty, ...)
typedef wxString CString;
typedef wxString CStringA;
typedef wxString CStringW;

#ifndef _T
#define _T(x)    x
#endif
#ifndef _TEXT
#define _TEXT(x) x
#endif

// CString helpers that differ in name
inline int bt_atoi(const wxString& s)    { long v = 0; s.ToLong(&v); return (int)v; }
inline double bt_atof(const wxString& s) { double v = 0; s.ToDouble(&v); return v; }

// ---- timing -----------------------------------------------------------------
inline DWORD GetTickCount()
{
    return (DWORD)(wxGetLocalTimeMillis().GetValue() & 0xFFFFFFFFull);
}
inline void Sleep(DWORD ms) { wxMilliSleep(ms); }

// ---- debug ------------------------------------------------------------------
#ifndef ASSERT
#define ASSERT(x) ((void)0)
#endif
#ifndef VERIFY
#define VERIFY(x) ((void)(x))
#endif
#ifndef TRACE
#define TRACE(...) ((void)0)
#endif

#endif // !_WIN32
