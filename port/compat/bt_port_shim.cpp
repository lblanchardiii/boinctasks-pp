#ifndef _WIN32
#include "bt_port_shim.h"
#include <csignal>

// Windows has no SIGPIPE: writing to a closed socket returns an error instead.
// Match that behavior process-wide so ported code sees send() == -1 / EPIPE.
static struct BtIgnoreSigpipe
{
    BtIgnoreSigpipe() { signal(SIGPIPE, SIG_IGN); }
} s_ignoreSigpipe;

static CDlgLogging s_logging;
CBtAppShim::CBtAppShim() { m_pDlgLogging = &s_logging; }
CBtAppShim theApp;
#endif
