// =============================================================================
// bt_addproject.h - attach a computer to a BOINC project
//
// The attach sequence is inherently slow (two polled RPC round-trips against
// the project server), so it runs on the owning computer's poller thread and
// reports back to the GUI when it finishes.
// =============================================================================
#pragma once
#include "bt_types.h"
#include <wx/dialog.h>
#include <functional>
#include <vector>

struct BtProjectChoice
{
    wxString name;
    wxString url;
};

struct BtAttachRequest
{
    wxString computer;
    wxString url;
    wxString projectName;
    wxString email;
    wxString password;
    wxString accountKey;    // used instead of email/password when non-empty
};

class BtAddProjectDlg : public wxDialog
{
public:
    BtAddProjectDlg(wxWindow* parent,
                    const std::vector<wxString>& computers,
                    const std::vector<BtProjectChoice>& knownProjects);

    // One request per selected computer.
    std::vector<BtAttachRequest> Result() const;

private:
    void SyncUrlFromChoice();

    class wxCheckListBox*  m_computers;
    class wxChoice*        m_project;
    class wxTextCtrl*      m_url;
    class wxTextCtrl*      m_email;
    class wxTextCtrl*      m_password;
    class wxTextCtrl*      m_key;
    std::vector<BtProjectChoice> m_known;
};
