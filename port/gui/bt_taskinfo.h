// =============================================================================
// bt_taskinfo.h - task properties window ("view raw task info")
//
// Shows everything the client reports about one result, fetched fresh so the
// window reflects the task at the moment it was opened.
// =============================================================================
#pragma once
#include "bt_types.h"
#include <wx/dialog.h>

class BtTaskInfoDlg : public wxDialog
{
public:
    BtTaskInfoDlg(wxWindow* parent, const BtTaskRow& task);
    void SetDetail(const wxString& text);

private:
    class wxTextCtrl* m_text;
};

// Render the fields we already hold for a task; the poller can append the raw
// client fields when they arrive.
wxString BtFormatTaskInfo(const BtTaskRow& t);
