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

// One entry of BOINC's master project list (all_projects_list.xml, which the
// client refreshes from boinc.berkeley.edu). Everything past name/url is what
// makes the picker readable: Classic groups by area and shows the blurb.
struct BtProjectChoice
{
    wxString name;
    wxString url;
    wxString area;          // general_area, e.g. "Biology and Medicine"
    wxString specificArea;  // specific_area, e.g. "Molecular biology"
    wxString description;
    wxString home;          // sponsoring organisation
    wxString platforms;     // comma separated, for the "will it run here" hint
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
    // `preselect` is the computer highlighted in the sidebar, or empty when the
    // selection is "All computers" or a group. Only that one is ticked: making
    // "attach to every machine" the default turns the destructive case into the
    // easy one.
    BtAddProjectDlg(wxWindow* parent,
                    const std::vector<wxString>& computers,
                    const std::vector<BtProjectChoice>& knownProjects,
                    const wxString& preselect = wxString());

    // One request per selected computer.
    std::vector<BtAttachRequest> Result() const;

private:
    // Rebuilds the tree, keeping only projects matching `filter` (empty = all).
    void BuildTree(const wxString& filter);
    void OnTreeSelect();

    class wxCheckListBox*  m_computers;
    class wxTextCtrl*      m_filter;
    class wxTreeCtrl*      m_tree;
    class wxTextCtrl*      m_desc;
    class wxTextCtrl*      m_url;
    class wxTextCtrl*      m_email;
    class wxTextCtrl*      m_password;
    class wxTextCtrl*      m_key;
    std::vector<BtProjectChoice> m_known;
    int m_selected = -1;        // index into m_known, -1 when nothing picked
};
