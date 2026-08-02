// =============================================================================
// bt_ccconfig.h - read and write a client's cc_config.xml as raw text
//
// BOINC's own library offers only a structured CC_CONFIG get/set, which
// round-trips through its parser and would silently drop comments and any
// element it does not recognise. The cc_config editor shows people the actual
// file, so it needs the text.
//
// eFMer added get_cc_config_raw/set_cc_config_raw to his copy of the library.
// Since the library is now a pinned BOINC submodule rather than a copy we
// maintain, the equivalent lives here instead - RPC_CLIENT and its RPC helper
// are both public, so nothing upstream has to be patched.
// =============================================================================
#pragma once
#include "gui_rpc_client.h"
#include <string>

// Fetches the <cc_config> section as text. Returns 0 on success.
int BtGetCcConfigRaw(RPC_CLIENT& rpc, std::string& out);

// Sends `body` (the contents of a cc_config.xml) back to the client. The caller
// still has to invoke read_cc_config() for it to take effect.
int BtSetCcConfigRaw(RPC_CLIENT& rpc, const std::string& body);
