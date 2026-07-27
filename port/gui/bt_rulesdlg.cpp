#include "bt_rulesdlg.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/checklst.h>
#include <wx/spinctrl.h>
#include <wx/clrpicker.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/dcbuffer.h>
#include <wx/settings.h>
#include <wx/msgdlg.h>
#include <wx/filedlg.h>

// ---------------------------------------------------------------------------
// weekly grid
// ---------------------------------------------------------------------------
static const char* kDay[7] =
    { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const int kLabelW = 78, kCellW = 22, kCellH = 17, kTopH = 15;

BtRuleWeekGrid::BtRuleWeekGrid(wxWindow* parent,
                               const std::vector<BtRuleInterval>& intervals)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition,
              wxSize(kLabelW + 24 * kCellW + 2, kTopH + 7 * kCellH + 2))
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    for (const auto& iv : intervals) {
        if (iv.invers) continue;                  // drawn as "off"
        for (int s = iv.startSec; s < iv.stopSec; s += 3600) {
            int day = (s / 86400) % 7;
            int hour = (s % 86400) / 3600;
            if (day >= 0 && day < 7 && hour >= 0 && hour < 24) m_on[day][hour] = true;
        }
    }
    Bind(wxEVT_PAINT, &BtRuleWeekGrid::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &BtRuleWeekGrid::OnMouse, this);
    Bind(wxEVT_LEFT_UP,   &BtRuleWeekGrid::OnMouse, this);
    Bind(wxEVT_MOTION,    &BtRuleWeekGrid::OnMouse, this);
}

bool BtRuleWeekGrid::HitTest(const wxPoint& p, int& day, int& hour) const
{
    if (p.x < kLabelW || p.y < kTopH) return false;
    hour = (p.x - kLabelW) / kCellW;
    day  = (p.y - kTopH) / kCellH;
    return hour >= 0 && hour < 24 && day >= 0 && day < 7;
}

void BtRuleWeekGrid::OnMouse(wxMouseEvent& ev)
{
    int d, h;
    if (ev.LeftDown()) {
        if (!HitTest(ev.GetPosition(), d, h)) return;
        m_dragValue = !m_on[d][h];        // dragging paints the opposite of the first cell
        m_on[d][h] = m_dragValue;
        m_dragging = true;
        Refresh();
        return;
    }
    if (ev.LeftUp()) { m_dragging = false; return; }
    if (!m_dragging || !ev.LeftIsDown()) return;
    if (!HitTest(ev.GetPosition(), d, h)) return;
    if (m_on[d][h] == m_dragValue) return;
    m_on[d][h] = m_dragValue;
    Refresh();
}

void BtRuleWeekGrid::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    wxFont small = GetFont();
    small.SetPointSize(std::max(6, small.GetPointSize() - 2));
    dc.SetFont(small);
    dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    for (int h = 0; h < 24; h += 2)
        dc.DrawText(wxString::Format("%d", h), kLabelW + h * kCellW + 2, 1);

    for (int d = 0; d < 7; d++) {
        int y = kTopH + d * kCellH;
        dc.DrawText(kDay[d], 2, y + 2);
        for (int h = 0; h < 24; h++) {
            dc.SetBrush(wxBrush(m_on[d][h] ? wxColour(60, 200, 60)
                                           : wxColour(245, 245, 245)));
            dc.SetPen(wxPen(wxColour(160, 160, 160)));
            dc.DrawRectangle(kLabelW + h * kCellW, y, kCellW, kCellH);
        }
    }
}

std::vector<BtRuleInterval> BtRuleWeekGrid::Intervals() const
{
    // merge runs of set hours into as few intervals as possible
    std::vector<BtRuleInterval> out;
    for (int d = 0; d < 7; d++) {
        int h = 0;
        while (h < 24) {
            if (!m_on[d][h]) { h++; continue; }
            int start = h;
            while (h < 24 && m_on[d][h]) h++;
            BtRuleInterval iv;
            iv.startSec = d * 86400 + start * 3600;
            iv.stopSec  = d * 86400 + h * 3600;
            out.push_back(iv);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// rule editor
// ---------------------------------------------------------------------------
namespace {

// "1d,02:03:04" / "02:03:04" / "3600" all parse to seconds.
double ParseTimeValue(const wxString& text)
{
    wxString s = text;
    double days = 0;
    int comma = s.Find(',');
    if (comma != wxNOT_FOUND) {
        wxString d = s.Left(comma);
        d.Replace("d", "");
        d.ToDouble(&days);
        s = s.Mid(comma + 1);
    }
    double total = days * 86400;
    if (s.Find(':') == wxNOT_FOUND) {
        double v = 0;
        s.ToDouble(&v);
        return total + v;
    }
    long h = 0, m = 0, sec = 0;
    wxString part = s.BeforeFirst(':');   part.ToLong(&h);
    wxString rest = s.AfterFirst(':');
    part = rest.BeforeFirst(':');         part.ToLong(&m);
    part = rest.AfterFirst(':');          part.ToLong(&sec);
    return total + h * 3600 + m * 60 + sec;
}

} // namespace

BtRuleDlg::BtRuleDlg(wxWindow* parent, const BtRule& rule)
    : wxDialog(parent, wxID_ANY, "Rule editor", wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_orig(rule)
{
    auto* top = new wxBoxSizer(wxVERTICAL);

    // --- match fields
    auto* match = new wxFlexGridSizer(2, 6, 8);
    match->AddGrowableCol(1, 1);
    auto row = [&](const wxString& label, wxWindow* ctrl) {
        match->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        match->Add(ctrl, 1, wxEXPAND);
    };
    m_name        = new wxTextCtrl(this, wxID_ANY, rule.name, wxDefaultPosition,
                                   wxSize(340, -1));
    m_computer    = new wxTextCtrl(this, wxID_ANY, rule.computer);
    m_project     = new wxTextCtrl(this, wxID_ANY, rule.project);
    m_application = new wxTextCtrl(this, wxID_ANY, rule.application);
    m_appPartial  = new wxCheckBox(this, wxID_ANY, "Application is a partial match");
    m_appPartial->SetValue(rule.applicationPartial);
    row("Rule name:",   m_name);
    row("Computer:",    m_computer);
    row("Project:",     m_project);
    row("Application:", m_application);
    row("",             m_appPartial);
    top->Add(match, 0, wxEXPAND | wxALL, 12);
    top->Add(new wxStaticText(this, wxID_ANY,
        "Leave a field empty to match everything. Computer matches partially, so "
        "\"epyc\" covers epyc1 and epyc2; project and application must match exactly."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    // --- conditions
    auto* condBox = new wxStaticBoxSizer(wxVERTICAL, this, "Conditions (all must hold)");
    auto* grid = new wxFlexGridSizer(3, 6, 8);
    grid->Add(new wxStaticText(condBox->GetStaticBox(), wxID_ANY, "Type"));
    grid->Add(new wxStaticText(condBox->GetStaticBox(), wxID_ANY, "Operator"));
    grid->Add(new wxStaticText(condBox->GetStaticBox(), wxID_ANY, "Value"));
    for (int i = 0; i < 3; i++) {
        m_type[i] = new wxChoice(condBox->GetStaticBox(), wxID_ANY);
        m_type[i]->Append("(none)");
        for (int t = 1; t < BTR_TYPE_COUNT; t++) m_type[i]->Append(BtRuleTypeName(t));
        m_type[i]->SetSelection(rule.cond[i].type);

        m_op[i] = new wxChoice(condBox->GetStaticBox(), wxID_ANY);
        m_op[i]->Append("");
        for (int o = BTOP_IS; o <= BTOP_NOLONGEREQUAL; o++) m_op[i]->Append(BtRuleOpName(o));
        m_op[i]->SetSelection(rule.cond[i].op);

        int vc = BtRuleValueClassOf(rule.cond[i].type);
        m_value[i] = new wxTextCtrl(condBox->GetStaticBox(), wxID_ANY,
                                    rule.cond[i].type == BTR_NONE ? ""
                                      : BtFormatRuleValue(vc, rule.cond[i].value,
                                                          rule.cond[i].value2),
                                    wxDefaultPosition, wxSize(140, -1));
        grid->Add(m_type[i]);
        grid->Add(m_op[i]);
        grid->Add(m_value[i]);
        m_type[i]->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SyncEnabledState(); });
    }
    condBox->Add(grid, 0, wxALL, 8);
    condBox->Add(new wxStaticText(condBox->GetStaticBox(), wxID_ANY,
        "Times accept 01:30:00 or 1d,02:00:00. Temperature needs TThrottle and "
        "never matches on Linux. Status takes \"state,count\"."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    top->Add(condBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    // --- wall-clock grid
    auto* weekBox = new wxStaticBoxSizer(wxVERTICAL, this,
                                         "Wall-clock Time - hours the rule is active");
    m_week = new BtRuleWeekGrid(weekBox->GetStaticBox(), rule.intervals);
    weekBox->Add(m_week, 0, wxALL, 8);
    weekBox->Add(new wxStaticText(weekBox->GetStaticBox(), wxID_ANY,
        "Only used when a condition above is set to Wall-clock Time. Drag to paint."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    top->Add(weekBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    // --- event
    auto* evBox = new wxStaticBoxSizer(wxVERTICAL, this, "Event");
    auto* eg = new wxFlexGridSizer(2, 6, 8);
    eg->AddGrowableCol(1, 1);
    auto erow = [&](const wxString& label, wxWindow* ctrl) {
        eg->Add(new wxStaticText(evBox->GetStaticBox(), wxID_ANY, label), 0,
                wxALIGN_CENTER_VERTICAL);
        eg->Add(ctrl, 1, wxEXPAND);
    };
    m_event = new wxChoice(evBox->GetStaticBox(), wxID_ANY);
    m_event->Append("(none)");
    for (int e = 1; e < BTE_EVENT_COUNT; e++) m_event->Append(BtRuleEventName(e));
    m_event->SetSelection(rule.event);
    m_event->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SyncEnabledState(); });

    m_program = new wxTextCtrl(evBox->GetStaticBox(), wxID_ANY, rule.program);
    m_snooze  = new wxSpinCtrl(evBox->GetStaticBox(), wxID_ANY, "", wxDefaultPosition,
                               wxDefaultSize, wxSP_ARROW_KEYS, 0, 10080,
                               rule.snoozeMinutes);
    m_show = new wxChoice(evBox->GetStaticBox(), wxID_ANY);
    m_show->Append("Show nothing");
    m_show->Append("Show in Notices");
    m_show->Append("Show logging dialog");
    m_show->SetSelection(rule.show);
    m_colour  = new wxColourPickerCtrl(evBox->GetStaticBox(), wxID_ANY, rule.colour);
    m_enabled = new wxCheckBox(evBox->GetStaticBox(), wxID_ANY, "Rule is active");
    m_enabled->SetValue(rule.enabled);

    erow("Event:",              m_event);
    erow("Program:",            m_program);
    erow("Snooze / re-arm (minutes):", m_snooze);
    erow("Reporting:",          m_show);
    erow("Colour:",             m_colour);
    erow("",                    m_enabled);
    evBox->Add(eg, 0, wxEXPAND | wxALL, 8);
    top->Add(evBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
    SetSizerAndFit(top);
    SyncEnabledState();
}

void BtRuleDlg::SyncEnabledState()
{
    bool wantsProgram = m_event->GetSelection() == BTE_RUN_PROGRAM;
    m_program->Enable(wantsProgram);
    bool wallclock = false;
    for (int i = 0; i < 3; i++)
        if (m_type[i]->GetSelection() == BTR_WALLCLOCK) wallclock = true;
    m_week->Enable(wallclock);
    for (int i = 0; i < 3; i++) {
        int t = m_type[i]->GetSelection();
        bool needsValue = t != BTR_NONE && t != BTR_WALLCLOCK && t != BTR_CONNECTION;
        m_value[i]->Enable(needsValue);
        m_op[i]->Enable(t != BTR_NONE && t != BTR_WALLCLOCK);
    }
}

BtRule BtRuleDlg::Result() const
{
    BtRule r = m_orig;
    r.name        = m_name->GetValue();
    r.computer    = m_computer->GetValue();
    r.project     = m_project->GetValue();
    r.application = m_application->GetValue();
    r.applicationPartial = m_appPartial->GetValue();

    for (int i = 0; i < 3; i++) {
        r.cond[i] = BtRuleCondition();
        r.cond[i].type = m_type[i]->GetSelection();
        r.cond[i].op   = m_op[i]->GetSelection();
        wxString v = m_value[i]->GetValue();
        switch (BtRuleValueClassOf(r.cond[i].type)) {
            case BTVC_TIME:
                r.cond[i].value = ParseTimeValue(v);
                break;
            case BTVC_STATUS: {
                double a = 0, b = 0;
                v.BeforeFirst(',').ToDouble(&a);
                v.AfterFirst(',').ToDouble(&b);
                r.cond[i].value = a;
                r.cond[i].value2 = b;
                break;
            }
            default: {
                double d = 0;
                v.BeforeFirst(' ').ToDouble(&d);
                r.cond[i].value = d;
                break;
            }
        }
    }
    r.intervals     = m_week->Intervals();
    r.event         = m_event->GetSelection();
    r.program       = m_program->GetValue();
    r.snoozeMinutes = m_snooze->GetValue();
    r.show          = m_show->GetSelection();
    r.colour        = m_colour->GetColour();
    r.enabled       = m_enabled->GetValue();
    r.backoffUntil  = 0;
    return r;
}

// ---------------------------------------------------------------------------
// rules list
// ---------------------------------------------------------------------------
BtRulesPanel::BtRulesPanel(wxWindow* parent, std::vector<BtRule> rules)
    : wxPanel(parent), m_rules(std::move(rules))
{
    m_list = new wxCheckListBox(this, wxID_ANY, wxDefaultPosition, wxSize(620, 240));
    m_list->Bind(wxEVT_CHECKLISTBOX, &BtRulesPanel::OnToggle, this);
    m_list->Bind(wxEVT_LISTBOX_DCLICK, &BtRulesPanel::OnEdit, this);

    auto* buttons = new wxBoxSizer(wxVERTICAL);
    auto add  = new wxButton(this, wxID_ANY, "Add...");
    auto edit = new wxButton(this, wxID_ANY, "Edit...");
    auto copy = new wxButton(this, wxID_ANY, "Copy");
    auto del  = new wxButton(this, wxID_ANY, "Delete");
    for (auto* b : { add, edit, copy, del }) buttons->Add(b, 0, wxEXPAND | wxBOTTOM, 6);
    add->Bind(wxEVT_BUTTON,  &BtRulesPanel::OnAdd, this);
    edit->Bind(wxEVT_BUTTON, &BtRulesPanel::OnEdit, this);
    copy->Bind(wxEVT_BUTTON, &BtRulesPanel::OnCopy, this);
    del->Bind(wxEVT_BUTTON,  &BtRulesPanel::OnDelete, this);

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(m_list, 1, wxEXPAND | wxRIGHT, 10);
    row->Add(buttons, 0);

    auto* top = new wxBoxSizer(wxVERTICAL);
    top->Add(new wxStaticText(this, wxID_ANY,
        "Rules are checked every 30 seconds. Tick a rule to make it active - a new "
        "rule starts inactive. Show -> Rules log records every trigger."),
        0, wxALL, 10);
    top->Add(row, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    SetSizer(top);
    Refill();
}

void BtRulesPanel::Refill()
{
    int sel = m_list->GetSelection();
    m_list->Clear();
    for (const auto& r : m_rules) {
        m_list->Append(r.name + "   -   " + BtRuleDescribe(r));
        m_list->Check(m_list->GetCount() - 1, r.enabled);
    }
    if (sel != wxNOT_FOUND && sel < (int)m_list->GetCount()) m_list->SetSelection(sel);
}

void BtRulesPanel::OnToggle(wxCommandEvent& ev)
{
    int i = ev.GetInt();
    if (i >= 0 && i < (int)m_rules.size()) m_rules[i].enabled = m_list->IsChecked(i);
}

void BtRulesPanel::OnAdd(wxCommandEvent&)
{
    BtRule r;
    r.name = wxString::Format("Rule %zu", m_rules.size() + 1);
    BtRuleDlg dlg(this, r);
    if (dlg.ShowModal() != wxID_OK) return;
    BtRule made = dlg.Result();
    if (made.name.IsEmpty()) {
        wxMessageBox("Rule name is empty.", "Rules", wxOK | wxICON_WARNING, this);
        return;
    }
    for (const auto& e : m_rules)
        if (e.name == made.name) {
            wxMessageBox("Rule name is already in use.", "Rules",
                         wxOK | wxICON_WARNING, this);
            return;
        }
    m_rules.push_back(made);
    Refill();
}

void BtRulesPanel::OnEdit(wxCommandEvent&)
{
    int i = m_list->GetSelection();
    if (i == wxNOT_FOUND || i >= (int)m_rules.size()) return;
    BtRuleDlg dlg(this, m_rules[i]);
    if (dlg.ShowModal() != wxID_OK) return;
    m_rules[i] = dlg.Result();
    Refill();
}

void BtRulesPanel::OnCopy(wxCommandEvent&)
{
    int i = m_list->GetSelection();
    if (i == wxNOT_FOUND || i >= (int)m_rules.size()) return;
    BtRule r = m_rules[i];
    r.name += " (copy)";
    r.enabled = false;
    m_rules.push_back(r);
    Refill();
}

void BtRulesPanel::OnDelete(wxCommandEvent&)
{
    int i = m_list->GetSelection();
    if (i == wxNOT_FOUND || i >= (int)m_rules.size()) return;
    if (wxMessageBox("Delete rule \"" + m_rules[i].name + "\"?", "Rules",
                     wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
    m_rules.erase(m_rules.begin() + i);
    Refill();
}
