// =============================================================================
// bt_win_stubs.cpp - Windows-only stand-ins for BOINC diagnostics
//
// On MinGW, lib/diagnostics.h maps BOINCTRACE onto boinc_trace(), so the
// gui-rpc sources reference it. The real implementation lives in
// diagnostics.cpp, which pulls in diagnostics_win.cpp and with it BOINC's
// crash-handling machinery - unhandled-exception monitors, thread lists,
// dbghelp symbol lookup. None of that belongs in this application, which has
// its own logging and its own Log tab.
//
// These are the trace hooks and nothing else, so they do nothing.
// =============================================================================
#ifdef _WIN32
#include <cstdarg>

// diagnostics.h declares these inside an extern "C" block, so the definitions
// have to match or the linker never finds them.
extern "C" {
void boinc_trace(const char*, ...) { }
void boinc_info(const char*, ...)  { }
}
#endif
