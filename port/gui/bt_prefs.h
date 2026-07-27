// =============================================================================
// bt_prefs.h - BOINC client preferences editor ("BOINC Settings")
//
// Mirrors the Windows dialog: Processor / Network / Disk / Exclusive
// applications, with the day-of-week override grid.
//
// The client is asked for its *working* preferences - what is actually in
// effect, project or web prefs included. On OK only the values the user
// actually changed are written to the local override, so anything they left
// alone keeps coming from the project's web preferences.
// =============================================================================
#pragma once
#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/string.h>
#include <vector>
#include "prefs.h"

class wxCheckBox;
class wxSpinCtrlDouble;
class wxListBox;

// -----------------------------------------------------------------------------
// The 7x24 grid. BOINC stores one contiguous span per day, so a drag across a
// row sets that day's span and a right-click drops the day back to the general
// setting - the grid can't express anything the client couldn't store.
// -----------------------------------------------------------------------------
class BtDayGrid : public wxPanel
{
public:
    BtDayGrid(wxWindow* parent, const WEEK_PREFS& week);

    const WEEK_PREFS& Week() const { return m_week; }
    bool Changed() const { return m_changed; }

private:
    void OnPaint(wxPaintEvent&);
    void OnLeftDown(wxMouseEvent&);
    void OnMotion(wxMouseEvent&);
    void OnLeftUp(wxMouseEvent&);
    void OnRightDown(wxMouseEvent&);
    bool HitTest(const wxPoint& p, int& day, int& hour) const;
    void Commit();

    WEEK_PREFS m_week;
    bool m_changed   = false;
    int  m_dragDay   = -1;
    int  m_dragFrom  = 0;
    int  m_dragTo    = 0;
};

class BtPrefsDlg : public wxDialog
{
public:
    BtPrefsDlg(wxWindow* parent, const wxString& computer, const wxString& host,
               const GLOBAL_PREFS& prefs,
               const std::vector<wxString>& exclusiveApps);

    // Fold the edits into `prefs`, flagging only what changed in `mask`.
    // Returns false when nothing was touched.
    bool Apply(GLOBAL_PREFS& prefs, GLOBAL_PREFS_MASK& mask) const;

    // Exclusive-application list, and whether it differs from what we loaded.
    std::vector<wxString> ExclusiveApps() const;
    bool ExclusiveAppsChanged() const;

private:
    wxWindow* BuildProcessorPage(wxWindow* parent);
    wxWindow* BuildNetworkPage(wxWindow* parent);
    wxWindow* BuildDiskPage(wxWindow* parent);
    wxWindow* BuildExclusivePage(wxWindow* parent);

    GLOBAL_PREFS m_orig;
    std::vector<wxString> m_origApps;

    // processor
    wxCheckBox*       m_batteries;
    wxCheckBox*       m_inUse;
    wxCheckBox*       m_gpuInUse;
    wxSpinCtrlDouble* m_idleMinutes;
    wxSpinCtrlDouble* m_suspendCpuUsage;
    wxSpinCtrlDouble* m_cpuStart;
    wxSpinCtrlDouble* m_cpuEnd;
    wxSpinCtrlDouble* m_switchEvery;
    wxSpinCtrlDouble* m_maxCpusPct;
    wxSpinCtrlDouble* m_cpuUsageLimit;
    BtDayGrid*        m_cpuWeek;

    // network
    wxSpinCtrlDouble* m_downKbps;
    wxSpinCtrlDouble* m_upKbps;
    wxSpinCtrlDouble* m_bufMin;
    wxSpinCtrlDouble* m_bufAdd;
    wxSpinCtrlDouble* m_dailyLimitMb;
    wxSpinCtrlDouble* m_dailyPeriod;
    wxSpinCtrlDouble* m_netStart;
    wxSpinCtrlDouble* m_netEnd;
    wxCheckBox*       m_skipVerify;
    wxCheckBox*       m_confirmConnect;
    wxCheckBox*       m_hangup;
    BtDayGrid*        m_netWeek;

    // disk / memory
    wxSpinCtrlDouble* m_diskMaxGb;
    wxSpinCtrlDouble* m_diskMinFreeGb;
    wxSpinCtrlDouble* m_diskMaxPct;
    wxSpinCtrlDouble* m_diskInterval;
    wxSpinCtrlDouble* m_swapPct;
    wxSpinCtrlDouble* m_ramBusyPct;
    wxSpinCtrlDouble* m_ramIdlePct;
    wxCheckBox*       m_leaveInMemory;

    // exclusive applications
    wxListBox*        m_apps;
};
