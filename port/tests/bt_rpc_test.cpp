// Headless smoke test: drive the ported CRpcClient against a live BOINC client.
#include "stdafx.h"
#include "bt_port_shim.h"
#include "RpcClient.h"
extern "C" {
#include "Boinc/md5.h"
}
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>

static std::string md5hex(const std::string& in)
{
    md5_state_t st; md5_byte_t dig[16]; char hex[33];
    md5_init(&st);
    md5_append(&st, (const md5_byte_t*)in.data(), (int)in.size());
    md5_finish(&st, dig);
    for (int i = 0; i < 16; i++) sprintf(hex + i*2, "%02x", dig[i]);
    return std::string(hex, 32);
}

static std::string extract(const std::string& xml, const char* tag)
{
    std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
    size_t b = xml.find(open), e = xml.find(close);
    if (b == std::string::npos || e == std::string::npos) return "";
    b += open.size();
    return xml.substr(b, e - b);
}

int main(int argc, char** argv)
{
    const char* host = (argc > 1) ? argv[1] : (char*)"127.0.0.1";
    std::ifstream pwf("/var/lib/boinc-client/gui_rpc_auth.cfg");
    std::string pw; std::getline(pwf, pw);
    while (!pw.empty() && (pw.back() == '\n' || pw.back() == '\r')) pw.pop_back();

    CRpcClient rpc;
    if (!rpc.Initialize()) { printf("FAIL: Initialize\n"); return 1; }
    if (!rpc.Connect((char*)host, (char*)"31416", 10, false))
    { printf("FAIL: Connect\n"); return 1; }
    printf("OK: connected to %s:31416\n", host);

    char req1[] = "<boinc_gui_rpc_request>\n<auth1/>\n</boinc_gui_rpc_request>\n\003";
    if (!rpc.SendReceive(req1)) { printf("FAIL: auth1\n"); return 1; }
    std::string nonce = extract(rpc.m_pcReceiveBuffer, "nonce");
    printf("OK: auth1 nonce=%s\n", nonce.c_str());

    std::string hash = md5hex(nonce + pw);
    char req2[512];
    snprintf(req2, sizeof(req2),
        "<boinc_gui_rpc_request>\n<auth2>\n<nonce_hash>%s</nonce_hash>\n</auth2>\n</boinc_gui_rpc_request>\n\003",
        hash.c_str());
    if (!rpc.SendReceive(req2)) { printf("FAIL: auth2\n"); return 1; }
    if (!strstr(rpc.m_pcReceiveBuffer, "authorized")) { printf("FAIL: not authorized\n"); return 1; }
    printf("OK: authorized\n");

    char req3[] = "<boinc_gui_rpc_request>\n<get_cc_status/>\n</boinc_gui_rpc_request>\n\003";
    if (!rpc.SendReceive(req3)) { printf("FAIL: get_cc_status\n"); return 1; }
    printf("OK: get_cc_status task_mode=%s (%zu bytes)\n",
           extract(rpc.m_pcReceiveBuffer, "task_mode").c_str(),
           strlen(rpc.m_pcReceiveBuffer));

    rpc.Disconnect();
    printf("PASS: ported CRpcClient end-to-end\n");
    return 0;
}
