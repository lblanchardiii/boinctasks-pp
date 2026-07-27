// =============================================================================
// bt_port_shim.h - minimal stand-ins for app-level objects (theApp, logging
// dialog) so unmodified core .cpp files compile headless on Linux.
// The real UI wiring replaces these as the wxWidgets port progresses.
// =============================================================================
#pragma once

#ifndef _WIN32

#include <cstdint>
#include <cstdio>
#include "bt_compat.h"

typedef uintptr_t WPARAM;
typedef long      LPARAM;

#ifndef UWM_MSG_LOGGING_TEXT
#define UWM_MSG_LOGGING_TEXT 0x8401
#endif

// stand-in for the MFC logging dialog: prints to stderr when debug enabled
class CDlgLogging : public CWnd
{
public:
    bool  m_bLogDebugTThrottleData = false;
    void* m_hWnd = nullptr;                 // never a real window headless
    void  SendMessage(unsigned, WPARAM, LPARAM) {}
};

// stand-in for the CWinApp-derived application object
class CBtAppShim
{
public:
    CDlgLogging* m_pDlgLogging;
    CBtAppShim();
};

extern CBtAppShim theApp;

// ::IsWindow() always false headless -> logging paths are skipped
inline bool IsWindow(void*) { return false; }

#endif // !_WIN32
