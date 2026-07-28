#include "bt_scan.h"
#include "bt_settings.h"
#include <wx/checklst.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include "gui_rpc_client.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <errno.h>

// The probe below is raw sockets, which is the one place the two platforms
// genuinely differ: Winsock needs its own headers, an explicit startup call,
// closesocket() rather than close(), and ioctlsocket() for non-blocking mode.
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #define BT_CLOSESOCKET closesocket
  using bt_socket_t = SOCKET;
  static const bt_socket_t BT_INVALID_SOCKET = INVALID_SOCKET;
  // Winsock reports errors through WSAGetLastError, not errno, and uses its
  // own constants. Reading errno here silently yields nothing, which makes
  // every probe look like a timeout and the whole scan come back empty.
  static inline int BtSockError()      { return WSAGetLastError(); }
  static const int BT_EINPROGRESS      = WSAEWOULDBLOCK;
  static const int BT_ECONNREFUSED     = WSAECONNREFUSED;
#else
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #define BT_CLOSESOCKET close
  using bt_socket_t = int;
  static const bt_socket_t BT_INVALID_SOCKET = -1;
  static inline int BtSockError()      { return errno; }
  static const int BT_EINPROGRESS      = EINPROGRESS;
  static const int BT_ECONNREFUSED     = ECONNREFUSED;
#endif

namespace {

// RPC_CLIENT::init() blocks on the OS connect timeout, which is ~2 minutes for
// a filtered port - unusable when sweeping a /24. Probe the port ourselves with
// a short deadline first and only speak RPC to hosts that actually answer.
enum ProbeResult { PROBE_OPEN, PROBE_REFUSED, PROBE_TIMEOUT };

// Distinguishing "refused" (host is up, nothing listening) from "timed out"
// (nothing there at all) is what makes a large port sweep affordable: a dead
// address costs one timeout, a live one answers every port immediately.
ProbeResult Probe(const char* addr, int port, int timeoutMs)
{
    bt_socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == BT_INVALID_SOCKET) return PROBE_TIMEOUT;

    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1) {
        BT_CLOSESOCKET(fd);
        return PROBE_TIMEOUT;
    }

#ifdef _WIN32
    u_long nonblocking = 1;
    ioctlsocket(fd, FIONBIO, &nonblocking);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif

    ProbeResult res = PROBE_TIMEOUT;
    if (connect(fd, (sockaddr*)&sa, sizeof(sa)) == 0) {
        res = PROBE_OPEN;
    } else if (BtSockError() == BT_EINPROGRESS) {
        fd_set wf, ef;
        FD_ZERO(&wf); FD_SET(fd, &wf);
        // Winsock signals a failed connect through the exception set rather
        // than the write set. Watching only writes would leave every refused
        // port waiting out the full timeout, which is what makes sweeping a
        // large port range affordable in the first place.
        FD_ZERO(&ef); FD_SET(fd, &ef);
        timeval tv;
        tv.tv_sec  = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        if (select((int)fd + 1, nullptr, &wf, &ef, &tv) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
            res = (err == 0) ? PROBE_OPEN
                : (err == BT_ECONNREFUSED) ? PROBE_REFUSED : PROBE_TIMEOUT;
        }
    } else if (BtSockError() == BT_ECONNREFUSED) {
        res = PROBE_REFUSED;
    }
    BT_CLOSESOCKET(fd);
    return res;
}

} // namespace

std::vector<BtScanResult> BtScanRange(const wxString& baseAddr,
                                      int hostFirst, int hostLast,
                                      long portFirst, long portLast,
                                      const wxString& password,
                                      std::function<bool(int, int)> onProgress)
{
    std::vector<BtScanResult> results;
    if (hostFirst > hostLast) std::swap(hostFirst, hostLast);
    if (portFirst > portLast) std::swap(portFirst, portLast);
    const int total = hostLast - hostFirst + 1;
    if (total <= 0) return results;
    // The bar covers both passes. Reporting the first pass as complete lets a
    // dialog with AUTO_HIDE vanish while the retry is still working, leaving an
    // app-modal parent disabled behind it - which looks exactly like a freeze.
    const int reportTotal = total * 2;
    std::atomic<bool> cancelled(false);

    std::mutex mutex;
    std::atomic<int> done(0);

    std::string pw(password.mb_str());
    std::string base(baseAddr.mb_str());

    const unsigned workers = 48;
    // The retry deliberately runs far fewer threads. 48 simultaneous probes
    // saturate a weak link - a wireless bridge, say - and the retry then times
    // out for the same reason the first pass did. Fewer, slower probes are
    // what actually reach a congested host.
    const unsigned slowWorkers = 6;

    // Addresses whose first port timed out in the fast pass. A host behind a
    // slow or flaky link - a wireless bridge, say - can easily take longer to
    // answer than a quick probe allows, and abandoning the address on the
    // first timeout loses every port on it. They get a second, patient pass.
    std::vector<int> slowCandidates;
    std::atomic<size_t> slowNext(0);
    std::atomic<int> nextHost(hostFirst);

    auto sweepHost = [&](bool patient) {
        if (cancelled.load()) return;
        const int firstPortTimeout = patient ? gSettings.scanSlowTimeoutMs
                                             : gSettings.scanTimeoutMs;
        // In the patient pass the whole address is slow, not just its first
        // port: giving the rest a short timeout finds two ports out of twelve
        // and silently drops the others.
        const int otherPortTimeout = patient ? gSettings.scanSlowTimeoutMs : 250;
        for (;;) {
            int n;
            if (patient) {
                size_t i = slowNext.fetch_add(1);
                if (i >= slowCandidates.size()) return;
                n = slowCandidates[i];
            } else {
                n = nextHost.fetch_add(1);
                if (n > hostLast) return;
            }

            if (cancelled.load()) return;

            char addr[64];
            snprintf(addr, sizeof(addr), "%s.%d", base.c_str(), n);

            bool hostAlive = false;
            for (long port = portFirst; port <= portLast; port++) {
                // Give the first port of an address a real timeout; once we
                // know the host is up, closed ports answer instantly, so the
                // remaining hundreds cost almost nothing.
                int timeoutMs = hostAlive ? otherPortTimeout : firstPortTimeout;
                ProbeResult pr = Probe(addr, (int)port, timeoutMs);

                if (pr == PROBE_TIMEOUT) {
                    if (!hostAlive) {
                        // nothing answered here yet; worth one patient retry
                        if (!patient) {
                            std::lock_guard<std::mutex> lock(mutex);
                            slowCandidates.push_back(n);
                        }
                        break;
                    }
                    continue;                   // filtered port on a live host
                }
                hostAlive = true;
                if (pr == PROBE_REFUSED) continue;

                RPC_CLIENT rpc;
                if (rpc.init(addr, (int)port) != 0) continue;
                BtScanResult r;
                r.host = wxString::FromUTF8(addr);
                r.port = port;
                // Plenty of clients run with no password at all; if the
                // supplied one is refused, an empty password often works.
                bool authed = (rpc.authorize(pw.c_str()) == 0);
                if (!authed && !pw.empty()) {
                    rpc.close();
                    if (rpc.init(addr, (int)port) == 0)
                        authed = (rpc.authorize("") == 0);
                }
                if (authed) {
                    r.authorized = true;
                    CC_STATE state;
                    if (rpc.get_state(state) == 0)
                        r.hostname = wxString::FromUTF8(state.host_info.domain_name);
                    VERSION_INFO vi;
                    if (rpc.exchange_versions(vi) == 0)
                        r.version = wxString::Format("%d.%d.%d",
                                        vi.major, vi.minor, vi.release);
                }
                rpc.close();
                std::lock_guard<std::mutex> lock(mutex);
                results.push_back(std::move(r));
            }
            done.fetch_add(1);
        }
    };

    // pass one: quick, over every address
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < workers; i++) pool.emplace_back(sweepHost, false);

    int lastReported = -1;
    while (done.load() < total) {
        int d = done.load();
        if (onProgress && d != lastReported) {
            if (!onProgress(d, reportTotal)) cancelled.store(true);
            lastReported = d;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (auto& t : pool) t.join();

    // pass two: patient, over only the addresses that never answered. Running
    // it threaded keeps the cost bounded - a quiet /24 is a few seconds even at
    // a multi-second timeout.
    if (!cancelled.load() && !slowCandidates.empty() &&
        gSettings.scanSlowTimeoutMs > gSettings.scanTimeoutMs) {
        std::sort(slowCandidates.begin(), slowCandidates.end());
        done.store(0);
        const int slowTotal = (int)slowCandidates.size();
        std::vector<std::thread> slowPool;
        for (unsigned i = 0; i < slowWorkers; i++) slowPool.emplace_back(sweepHost, true);
        lastReported = -1;
        while (done.load() < slowTotal) {
            int d = done.load();
            if (onProgress && d != lastReported) {
                // second half of the bar, so it keeps moving and never tops out
                int shown = total + (slowTotal ? (d * total) / slowTotal : total);
                if (!onProgress(shown, reportTotal)) cancelled.store(true);
                lastReported = d;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        for (auto& t : slowPool) t.join();
    }
    if (onProgress) onProgress(reportTotal, reportTotal);

    std::sort(results.begin(), results.end(),
              [](const BtScanResult& a, const BtScanResult& b) {
                  int c = a.host.CmpNoCase(b.host);
                  return c != 0 ? c < 0 : a.port < b.port;
              });
    return results;
}

// ---------------------------------------------------------------------------
// scan results picker
// ---------------------------------------------------------------------------
BtScanResultsDlg::BtScanResultsDlg(wxWindow* parent,
                                   const std::vector<BtScanResult>& results)
    : wxDialog(parent, wxID_ANY, "Find computers", wxDefaultPosition,
               wxSize(560, 520), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_results(results)
{
    int authorized = 0;
    wxArrayString labels;
    for (const auto& r : m_results) {
        if (r.authorized) authorized++;
        wxString who = r.authorized
                     ? (r.hostname.IsEmpty() ? wxString("authorized") : r.hostname)
                     : wxString("password refused");
        labels.Add(wxString::Format("%s:%ld   %s%s", r.host, r.port, who,
                   r.version.IsEmpty() ? "" : "   BOINC " + r.version));
    }

    auto* top = new wxBoxSizer(wxVERTICAL);
    top->Add(new wxStaticText(this, wxID_ANY,
        wxString::Format("%zu client(s) answered, %d authorized. Add which?",
                         m_results.size(), authorized)),
        0, wxALL, 12);

    m_list = new wxCheckListBox(this, wxID_ANY, wxDefaultPosition,
                                wxDefaultSize, labels);
    for (unsigned i = 0; i < m_list->GetCount(); i++) m_list->Check(i, true);
    top->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* all  = new wxButton(this, wxID_ANY, "Select &all");
    auto* none = new wxButton(this, wxID_ANY, "Select &none");
    auto* inv  = new wxButton(this, wxID_ANY, "&Invert");
    auto* auth = new wxButton(this, wxID_ANY, "Only a&uthorized");
    buttons->Add(all,  0, wxRIGHT, 6);
    buttons->Add(none, 0, wxRIGHT, 6);
    buttons->Add(inv,  0, wxRIGHT, 6);
    buttons->Add(auth, 0);
    top->Add(buttons, 0, wxALL, 12);

    all->Bind(wxEVT_BUTTON,  [this](wxCommandEvent&) { SetAll(true); });
    none->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SetAll(false); });
    inv->Bind(wxEVT_BUTTON,  [this](wxCommandEvent&) {
        for (unsigned i = 0; i < m_list->GetCount(); i++)
            m_list->Check(i, !m_list->IsChecked(i));
    });
    auth->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        for (unsigned i = 0; i < m_list->GetCount(); i++)
            m_list->Check(i, i < m_results.size() && m_results[i].authorized);
    });

    top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
    SetSizer(top);
}

void BtScanResultsDlg::SetAll(bool checked)
{
    for (unsigned i = 0; i < m_list->GetCount(); i++) m_list->Check(i, checked);
}

std::vector<BtScanResult> BtScanResultsDlg::Selected() const
{
    std::vector<BtScanResult> out;
    for (unsigned i = 0; i < m_list->GetCount(); i++)
        if (m_list->IsChecked(i) && i < m_results.size())
            out.push_back(m_results[i]);
    return out;
}
