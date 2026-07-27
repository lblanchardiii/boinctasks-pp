// =============================================================================
// bt_rulesdlg.h - the rule editor and the rule list
//
// Mirrors the Windows "Rule editor": match fields at the top, three condition
// rows, a weekly grid for wall-clock rules, and the event to fire.
// =============================================================================
#pragma once
#include "bt_rules.h"
#include <wx/dialog.h>
#include <wx/panel.h>

class wxTextCtrl;
class wxChoice;
class wxCheckBox;
class wxSpinCtrl;
class wxColourPickerCtrl;
class wxCheckListBox;

// 7 x 24 on/off grid. Rules can hold any number of intervals, so unlike the
// preferences grid every hour is independently settable; runs of set hours are
// merged back into intervals on the way out.
class BtRuleWeekGrid : public wxPanel
{
public:
    BtRuleWeekGrid(wxWindow* parent, const std::vector<BtRuleInterval>& intervals);
    std::vector<BtRuleInterval> Intervals() const;

private:
    void OnPaint(wxPaintEvent&);
    void OnMouse(wxMouseEvent&);
    bool HitTest(const wxPoint& p, int& day, int& hour) const;

    bool m_on[7][24] = {};
    bool m_dragging  = false;
    bool m_dragValue = true;
};

class BtRuleDlg : public wxDialog
{
public:
    BtRuleDlg(wxWindow* parent, const BtRule& rule);
    BtRule Result() const;

private:
    void SyncEnabledState();

    BtRule            m_orig;
    wxTextCtrl*       m_name;
    wxTextCtrl*       m_computer;
    wxTextCtrl*       m_project;
    wxTextCtrl*       m_application;
    wxCheckBox*       m_appPartial;
    wxChoice*         m_type[3];
    wxChoice*         m_op[3];
    wxTextCtrl*       m_value[3];
    BtRuleWeekGrid*   m_week;
    wxChoice*         m_event;
    wxTextCtrl*       m_program;
    wxSpinCtrl*       m_snooze;
    wxChoice*         m_show;
    wxColourPickerCtrl* m_colour;
    wxCheckBox*       m_enabled;
};

// The Rules page of the settings dialog: list, add / edit / copy / delete.
class BtRulesPanel : public wxPanel
{
public:
    BtRulesPanel(wxWindow* parent, std::vector<BtRule> rules);
    const std::vector<BtRule>& Rules() const { return m_rules; }

private:
    void Refill();
    void OnAdd(wxCommandEvent&);
    void OnEdit(wxCommandEvent&);
    void OnCopy(wxCommandEvent&);
    void OnDelete(wxCommandEvent&);
    void OnToggle(wxCommandEvent&);

    std::vector<BtRule> m_rules;
    wxCheckListBox*     m_list;
};
