// Smoke test: BOINC's RPC_CLIENT (as used by BoincTasks) against a live client.
#include "gui_rpc_client.h"
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv)
{
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    std::ifstream pwf("/var/lib/boinc-client/gui_rpc_auth.cfg");
    std::string pw; std::getline(pwf, pw);
    while (!pw.empty() && (pw.back() == '\n' || pw.back() == '\r')) pw.pop_back();

    RPC_CLIENT rpc;
    if (rpc.init(host)) { printf("FAIL: init/connect\n"); return 1; }
    printf("OK: connected to %s\n", host);
    if (rpc.authorize(pw.c_str())) { printf("FAIL: authorize\n"); return 1; }
    printf("OK: authorized\n");

    CC_STATUS status;
    if (rpc.get_cc_status(status)) { printf("FAIL: get_cc_status\n"); return 1; }
    printf("OK: cc_status task_mode=%d\n", status.task_mode);

    RESULTS results;
    if (rpc.get_results(results)) { printf("FAIL: get_results\n"); return 1; }
    printf("OK: get_results -> %zu tasks\n", results.results.size());

    CC_STATE state;
    if (rpc.get_state(state)) { printf("FAIL: get_state\n"); return 1; }
    printf("OK: get_state -> %zu projects, host: %s\n",
           state.projects.size(), state.host_info.domain_name);

    printf("PASS: BOINC RPC_CLIENT end-to-end on Linux\n");
    return 0;
}
