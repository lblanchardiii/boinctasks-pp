#include "bt_ccconfig.h"

int BtGetCcConfigRaw(RPC_CLIENT& rpc, std::string& out)
{
    SET_LOCALE sl;
    RPC r(&rpc);
    int retval = r.do_rpc("<get_cc_config/>");
    if (retval) return retval;
    if (!r.mbuf) return -1;

    // The reply arrives wrapped in the RPC envelope; hand back what is inside
    // it, which is the cc_config document itself.
    const std::string reply(r.mbuf);
    const std::string open = "<boinc_gui_rpc_reply>";
    const std::string close = "</boinc_gui_rpc_reply>";

    size_t b = reply.find(open);
    size_t e = reply.rfind(close);
    if (b == std::string::npos || e == std::string::npos || e < b) return -1;

    b += open.length();
    out = reply.substr(b, e - b);

    // trim the newlines the envelope leaves behind
    while (!out.empty() && (out.front() == '\n' || out.front() == '\r')) out.erase(out.begin());
    while (!out.empty() && (out.back()  == '\n' || out.back()  == '\r')) out.pop_back();
    return 0;
}

int BtSetCcConfigRaw(RPC_CLIENT& rpc, const std::string& body)
{
    SET_LOCALE sl;
    RPC r(&rpc);
    const std::string req = "<set_cc_config>\n" + body + "\n</set_cc_config>\n";
    return r.do_rpc(req.c_str());
}
