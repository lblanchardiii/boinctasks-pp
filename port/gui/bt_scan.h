// =============================================================================
// bt_scan.h - find BOINC clients on an address range
//
// Probes each address on the BOINC GUI RPC port and reports the ones that
// answer, noting whether the supplied password authenticates. Runs on worker
// threads so the UI stays responsive; results are delivered to the caller.
// =============================================================================
#pragma once
#include <wx/string.h>
#include <wx/dialog.h>
#include <functional>
#include <vector>

struct BtScanResult
{
    wxString host;
    long     port = 31416;
    wxString hostname;      // domain_name if we could authenticate
    wxString version;       // client version, when known
    bool     authorized = false;
};

// Picker for scan results: everything is checked by default and there are
// bulk actions, because a farm sweep can return dozens of instances and
// ticking them one at a time is unusable.
class BtScanResultsDlg : public wxDialog
{
public:
    BtScanResultsDlg(wxWindow* parent, const std::vector<BtScanResult>& results);
    std::vector<BtScanResult> Selected() const;

private:
    void SetAll(bool checked);

    class wxCheckListBox* m_list;
    std::vector<BtScanResult> m_results;
};

// Scan hostFirst..hostLast on the given /24-style base (e.g. "192.168.1"),
// sweeping portFirst..portLast on each address. Many BOINC farms run dozens or
// hundreds of instances per machine on consecutive ports, so the port sweep is
// the common case, not the exception.
std::vector<BtScanResult> BtScanRange(const wxString& baseAddr,
                                      int hostFirst, int hostLast,
                                      long portFirst, long portLast,
                                      const wxString& password,
                                      std::function<bool(int done, int total)> onProgress);
// onProgress returns false to cancel the scan; the workers stop at their next
// address and whatever was found so far is returned.
