#include "bt_prefs.h"
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>
#include <wx/listbox.h>
#include <wx/button.h>
#include <wx/dcbuffer.h>
#include <wx/textdlg.h>
#include <wx/statbox.h>
#include <wx/settings.h>
#include <cmath>

// ---------------------------------------------------------------------------
// day-of-week grid
// ---------------------------------------------------------------------------
static const char* kDayName[7] =
    { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

static const int kLabelW = 78;    // room for "Wednesday"
static const int kCellW  = 22;
static const int kCellH  = 17;
static const int kTopH   = 15;    // hour numbers

BtDayGrid::BtDayGrid(wxWindow* parent, const WEEK_PREFS& week)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition,
              wxSize(kLabelW + 24 * kCellW + 2, kTopH + 7 * kCellH + 2))
    , m_week(week)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT,      &BtDayGrid::OnPaint,     this);
    Bind(wxEVT_LEFT_DOWN,  &BtDayGrid::OnLeftDown,  this);
    Bind(wxEVT_MOTION,     &BtDayGrid::OnMotion,    this);
    Bind(wxEVT_LEFT_UP,    &BtDayGrid::OnLeftUp,    this);
    Bind(wxEVT_RIGHT_DOWN, &BtDayGrid::OnRightDown, this);
}

bool BtDayGrid::HitTest(const wxPoint& p, int& day, int& hour) const
{
    if (p.x < kLabelW || p.y < kTopH) return false;
    hour = (p.x - kLabelW) / kCellW;
    day  = (p.y - kTopH)  / kCellH;
    if (hour < 0 || hour > 23 || day < 0 || day > 6) return false;
    return true;
}

void BtDayGrid::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    wxFont small = GetFont();
    small.SetPointSize(std::max(6, small.GetPointSize() - 2));
    dc.SetFont(small);
    dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));

    for (int h = 0; h < 24; h += 2) {
        wxString t = wxString::Format("%d", h);
        dc.DrawText(t, kLabelW + h * kCellW + 2, 1);
    }

    const wxColour on(60, 200, 60), off(245, 245, 245), none(215, 215, 215);
    for (int d = 0; d < 7; d++) {
        int y = kTopH + d * kCellH;
        dc.DrawText(kDayName[d], 2, y + 2);

        const TIME_SPAN& span = m_week.days[d];
        bool overridden = span.present;
        double s = span.start_hour, e = span.end_hour;
        if (d == m_dragDay) {                       // live feedback while dragging
            overridden = true;
            s = std::min(m_dragFrom, m_dragTo);
            e = std::max(m_dragFrom, m_dragTo) + 1;
        }

        for (int h = 0; h < 24; h++) {
            bool active = false;
            if (overridden) {
                if (s == e)      active = false;               // never
                else if (s < e)  active = (h >= s && h < e);
                else             active = (h >= s || h < e);   // span wraps midnight
            }
            dc.SetBrush(wxBrush(!overridden ? none : (active ? on : off)));
            dc.SetPen(wxPen(wxColour(160, 160, 160)));
            dc.DrawRectangle(kLabelW + h * kCellW, y, kCellW, kCellH);
        }
    }
}

void BtDayGrid::OnLeftDown(wxMouseEvent& ev)
{
    int d, h;
    if (!HitTest(ev.GetPosition(), d, h)) return;
    m_dragDay = d;
    m_dragFrom = m_dragTo = h;
    CaptureMouse();
    Refresh();
}

void BtDayGrid::OnMotion(wxMouseEvent& ev)
{
    if (m_dragDay < 0 || !ev.LeftIsDown()) return;
    int d, h;
    if (!HitTest(ev.GetPosition(), d, h)) return;
    if (h == m_dragTo) return;
    m_dragTo = h;
    Refresh();
}

void BtDayGrid::OnLeftUp(wxMouseEvent&)
{
    if (m_dragDay < 0) return;
    if (HasCapture()) ReleaseMouse();
    Commit();
    m_dragDay = -1;
    Refresh();
}

void BtDayGrid::Commit()
{
    int lo = std::min(m_dragFrom, m_dragTo);
    int hi = std::max(m_dragFrom, m_dragTo) + 1;
    if (hi > 24) hi = 24;
    m_week.set(m_dragDay, (double)lo, (double)(hi % 24));
    m_changed = true;
}

void BtDayGrid::OnRightDown(wxMouseEvent& ev)
{
    int d, h;
    if (!HitTest(ev.GetPosition(), d, h)) return;
    m_week.unset(d);                 // back to the general "every day" setting
    m_changed = true;
    Refresh();
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
namespace {

wxSpinCtrlDouble* Num(wxWindow* parent, double value, double lo, double hi,
                      double inc = 1.0, int digits = 2)
{
    auto* c = new wxSpinCtrlDouble(parent, wxID_ANY, "", wxDefaultPosition,
                                   wxSize(96, -1), wxSP_ARROW_KEYS, lo, hi, value, inc);
    c->SetDigits(digits);
    return c;
}

// label / control / trailing-text row
void Row(wxFlexGridSizer* grid, wxWindow* parent, const wxString& label,
         wxWindow* ctrl, const wxString& suffix = "")
{
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0,
              wxALIGN_CENTER_VERTICAL);
    grid->Add(ctrl, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(parent, wxID_ANY, suffix), 0,
              wxALIGN_CENTER_VERTICAL);
}

wxStaticBoxSizer* Group(wxWindow* parent, const wxString& title)
{
    return new wxStaticBoxSizer(wxVERTICAL, parent, title);
}

bool Differs(double a, double b) { return std::fabs(a - b) > 1e-9; }

// A week is "changed" if any day's presence or span moved.
bool WeekDiffers(const WEEK_PREFS& a, const WEEK_PREFS& b)
{
    for (int d = 0; d < 7; d++) {
        if (a.days[d].present != b.days[d].present) return true;
        if (!a.days[d].present) continue;
        if (Differs(a.days[d].start_hour, b.days[d].start_hour)) return true;
        if (Differs(a.days[d].end_hour,   b.days[d].end_hour))   return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
BtPrefsDlg::BtPrefsDlg(wxWindow* parent, const wxString& computer,
                       const wxString& host, const GLOBAL_PREFS& prefs,
                       const std::vector<wxString>& exclusiveApps)
    : wxDialog(parent, wxID_ANY, "BOINC Settings: " + host + ", " + computer,
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_orig(prefs)
    , m_origApps(exclusiveApps)
{
    auto* book = new wxNotebook(this, wxID_ANY);
    book->AddPage(BuildProcessorPage(book), "Processor", true);
    book->AddPage(BuildNetworkPage(book),   "Network");
    book->AddPage(BuildDiskPage(book),      "Disk");
    book->AddPage(BuildExclusivePage(book), "Exclusive applications");

    auto* top = new wxBoxSizer(wxVERTICAL);
    top->Add(book, 1, wxEXPAND | wxALL, 10);
    top->Add(new wxStaticText(this, wxID_ANY,
        "Only the values you change are written to this computer's local override; "
        "everything else keeps following the project's web preferences."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
    top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);
    SetSizerAndFit(top);
}

wxWindow* BtPrefsDlg::BuildProcessorPage(wxWindow* parent)
{
    auto* page = new wxPanel(parent);
    auto* top  = new wxBoxSizer(wxVERTICAL);

    auto* allowed = Group(page, "Computing allowed");
    m_batteries = new wxCheckBox(allowed->GetStaticBox(), wxID_ANY,
                                 "While computer is on batteries");
    m_inUse     = new wxCheckBox(allowed->GetStaticBox(), wxID_ANY,
                                 "While computer is in use");
    m_gpuInUse  = new wxCheckBox(allowed->GetStaticBox(), wxID_ANY,
                                 "Use GPU while computer is in use");
    m_batteries->SetValue(m_orig.run_on_batteries);
    m_inUse->SetValue(m_orig.run_if_user_active);
    m_gpuInUse->SetValue(m_orig.run_gpu_if_user_active);

    auto* checks = new wxBoxSizer(wxHORIZONTAL);
    auto* left   = new wxBoxSizer(wxVERTICAL);
    left->Add(m_batteries, 0, wxBOTTOM, 4);
    left->Add(m_gpuInUse);
    checks->Add(left, 1);
    checks->Add(m_inUse, 1);
    allowed->Add(checks, 0, wxEXPAND | wxALL, 8);

    auto* grid = new wxFlexGridSizer(3, 6, 10);
    m_idleMinutes     = Num(allowed->GetStaticBox(), m_orig.idle_time_to_run, 0, 9999);
    m_suspendCpuUsage = Num(allowed->GetStaticBox(), m_orig.suspend_cpu_usage, 0, 100);
    m_cpuStart        = Num(allowed->GetStaticBox(), m_orig.cpu_times.start_hour, 0, 24, 0.5);
    m_cpuEnd          = Num(allowed->GetStaticBox(), m_orig.cpu_times.end_hour, 0, 24, 0.5);
    Row(grid, allowed->GetStaticBox(), "Only after computer has been idle for",
        m_idleMinutes, "minutes");
    Row(grid, allowed->GetStaticBox(), "While processor usage is less than",
        m_suspendCpuUsage, "% (0 means no restrictions)");

    auto* hours = new wxBoxSizer(wxHORIZONTAL);
    hours->Add(m_cpuStart, 0, wxALIGN_CENTER_VERTICAL);
    hours->Add(new wxStaticText(allowed->GetStaticBox(), wxID_ANY, " and "), 0,
               wxALIGN_CENTER_VERTICAL);
    hours->Add(m_cpuEnd, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(allowed->GetStaticBox(), wxID_ANY,
              "Every day between the hours of"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(hours, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(allowed->GetStaticBox(), wxID_ANY,
              "(no restrictions if equal)"), 0, wxALIGN_CENTER_VERTICAL);
    allowed->Add(grid, 0, wxALL, 8);
    top->Add(allowed, 0, wxEXPAND | wxALL, 8);

    auto* week = Group(page, "Day-of-week override");
    m_cpuWeek = new BtDayGrid(week->GetStaticBox(), m_orig.cpu_times.week);
    week->Add(m_cpuWeek, 0, wxALL, 8);
    week->Add(new wxStaticText(week->GetStaticBox(), wxID_ANY,
        "Drag across a row to set that day's hours. Right-click a row to clear it "
        "and follow the setting above. Grey means no override."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    top->Add(week, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* other = Group(page, "Other options");
    auto* og = new wxFlexGridSizer(3, 6, 10);
    m_switchEvery   = Num(other->GetStaticBox(), m_orig.cpu_scheduling_period_minutes, 1, 9999);
    m_maxCpusPct    = Num(other->GetStaticBox(), m_orig.max_ncpus_pct, 0, 100);
    m_cpuUsageLimit = Num(other->GetStaticBox(), m_orig.cpu_usage_limit, 0, 100);
    Row(og, other->GetStaticBox(), "Switch between applications every",
        m_switchEvery, "minutes");
    Row(og, other->GetStaticBox(), "On multiprocessor systems, use at most",
        m_maxCpusPct, "% of the processors");
    Row(og, other->GetStaticBox(), "Use at most", m_cpuUsageLimit, "% CPU time");
    other->Add(og, 0, wxALL, 8);
    top->Add(other, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    page->SetSizerAndFit(top);
    return page;
}

wxWindow* BtPrefsDlg::BuildNetworkPage(wxWindow* parent)
{
    auto* page = new wxPanel(parent);
    auto* top  = new wxBoxSizer(wxVERTICAL);

    auto* usage = Group(page, "Network usage");
    auto* grid  = new wxFlexGridSizer(3, 6, 10);
    // the client stores bytes/sec; the dialog works in KB/s like the Windows one
    m_downKbps     = Num(usage->GetStaticBox(), m_orig.max_bytes_sec_down / 1024.0, 0, 1e6);
    m_upKbps       = Num(usage->GetStaticBox(), m_orig.max_bytes_sec_up / 1024.0, 0, 1e6);
    m_bufMin       = Num(usage->GetStaticBox(), m_orig.work_buf_min_days, 0, 30, 0.1);
    m_bufAdd       = Num(usage->GetStaticBox(), m_orig.work_buf_additional_days, 0, 30, 0.1);
    m_dailyLimitMb = Num(usage->GetStaticBox(), m_orig.daily_xfer_limit_mb, 0, 1e7);
    m_dailyPeriod  = Num(usage->GetStaticBox(), (double)m_orig.daily_xfer_period_days, 0, 365, 1, 0);
    Row(grid, usage->GetStaticBox(), "Maximum download rate", m_downKbps, "KB/s (0 = no limit)");
    Row(grid, usage->GetStaticBox(), "Maximum upload rate",   m_upKbps,   "KB/s (0 = no limit)");
    Row(grid, usage->GetStaticBox(), "Minimum work buffer",   m_bufMin,   "days");
    Row(grid, usage->GetStaticBox(), "Additional work buffer",m_bufAdd,   "days");
    Row(grid, usage->GetStaticBox(), "Transfer at most",      m_dailyLimitMb, "MB");
    Row(grid, usage->GetStaticBox(), "every",                 m_dailyPeriod,  "days (0 = no limit)");
    usage->Add(grid, 0, wxALL, 8);
    top->Add(usage, 0, wxEXPAND | wxALL, 8);

    auto* when = Group(page, "Transfer allowed");
    auto* wg   = new wxFlexGridSizer(3, 6, 10);
    m_netStart = Num(when->GetStaticBox(), m_orig.net_times.start_hour, 0, 24, 0.5);
    m_netEnd   = Num(when->GetStaticBox(), m_orig.net_times.end_hour, 0, 24, 0.5);
    auto* hours = new wxBoxSizer(wxHORIZONTAL);
    hours->Add(m_netStart, 0, wxALIGN_CENTER_VERTICAL);
    hours->Add(new wxStaticText(when->GetStaticBox(), wxID_ANY, " and "), 0,
               wxALIGN_CENTER_VERTICAL);
    hours->Add(m_netEnd, 0, wxALIGN_CENTER_VERTICAL);
    wg->Add(new wxStaticText(when->GetStaticBox(), wxID_ANY,
            "Every day between the hours of"), 0, wxALIGN_CENTER_VERTICAL);
    wg->Add(hours, 0, wxALIGN_CENTER_VERTICAL);
    wg->Add(new wxStaticText(when->GetStaticBox(), wxID_ANY,
            "(no restrictions if equal)"), 0, wxALIGN_CENTER_VERTICAL);
    when->Add(wg, 0, wxALL, 8);
    m_netWeek = new BtDayGrid(when->GetStaticBox(), m_orig.net_times.week);
    when->Add(m_netWeek, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    top->Add(when, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* opts = Group(page, "Other options");
    m_skipVerify     = new wxCheckBox(opts->GetStaticBox(), wxID_ANY,
                                      "Skip image file verification");
    m_confirmConnect = new wxCheckBox(opts->GetStaticBox(), wxID_ANY,
                                      "Confirm before connecting to Internet");
    m_hangup         = new wxCheckBox(opts->GetStaticBox(), wxID_ANY,
                                      "Disconnect when done");
    m_skipVerify->SetValue(m_orig.dont_verify_images);
    m_confirmConnect->SetValue(m_orig.confirm_before_connecting);
    m_hangup->SetValue(m_orig.hangup_if_dialed);
    opts->Add(m_skipVerify, 0, wxALL, 6);
    opts->Add(m_confirmConnect, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
    opts->Add(m_hangup, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
    top->Add(opts, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    page->SetSizerAndFit(top);
    return page;
}

wxWindow* BtPrefsDlg::BuildDiskPage(wxWindow* parent)
{
    auto* page = new wxPanel(parent);
    auto* top  = new wxBoxSizer(wxVERTICAL);

    auto* disk = Group(page, "Disk usage");
    auto* dg   = new wxFlexGridSizer(3, 6, 10);
    m_diskMaxGb     = Num(disk->GetStaticBox(), m_orig.disk_max_used_gb, 0, 1e6);
    m_diskMinFreeGb = Num(disk->GetStaticBox(), m_orig.disk_min_free_gb, 0, 1e6);
    m_diskMaxPct    = Num(disk->GetStaticBox(), m_orig.disk_max_used_pct, 0, 100);
    m_diskInterval  = Num(disk->GetStaticBox(), m_orig.disk_interval, 0, 100000, 1, 0);
    m_swapPct       = Num(disk->GetStaticBox(), m_orig.vm_max_used_frac * 100.0, 0, 100);
    Row(dg, disk->GetStaticBox(), "Use at most",   m_diskMaxGb,     "Gigabyte disk space (0 = no limit)");
    Row(dg, disk->GetStaticBox(), "Leave at least",m_diskMinFreeGb, "Gigabyte disk space free");
    Row(dg, disk->GetStaticBox(), "Use at most",   m_diskMaxPct,    "% of total disk space");
    Row(dg, disk->GetStaticBox(), "Write to disk at most every", m_diskInterval, "seconds");
    Row(dg, disk->GetStaticBox(), "Use at most",   m_swapPct,       "% of page file (swap space)");
    disk->Add(dg, 0, wxALL, 8);
    top->Add(disk, 0, wxEXPAND | wxALL, 8);

    auto* mem = Group(page, "Memory usage");
    auto* mg  = new wxFlexGridSizer(3, 6, 10);
    m_ramBusyPct = Num(mem->GetStaticBox(), m_orig.ram_max_used_busy_frac * 100.0, 0, 100);
    m_ramIdlePct = Num(mem->GetStaticBox(), m_orig.ram_max_used_idle_frac * 100.0, 0, 100);
    Row(mg, mem->GetStaticBox(), "Use at most", m_ramBusyPct, "% when computer is in use");
    Row(mg, mem->GetStaticBox(), "Use at most", m_ramIdlePct, "% when computer is idle");
    mem->Add(mg, 0, wxALL, 8);
    m_leaveInMemory = new wxCheckBox(mem->GetStaticBox(), wxID_ANY,
                                     "Leave application in memory while suspended");
    m_leaveInMemory->SetValue(m_orig.leave_apps_in_memory);
    mem->Add(m_leaveInMemory, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    top->Add(mem, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    page->SetSizerAndFit(top);
    return page;
}

wxWindow* BtPrefsDlg::BuildExclusivePage(wxWindow* parent)
{
    auto* page = new wxPanel(parent);
    auto* top  = new wxBoxSizer(wxVERTICAL);

    top->Add(new wxStaticText(page, wxID_ANY,
        "While one of these programs is running, the client suspends computing.\n"
        "Names must match the executable, e.g. \"blender\" or \"vlc\"."),
        0, wxALL, 10);

    m_apps = new wxListBox(page, wxID_ANY, wxDefaultPosition, wxSize(360, 190));
    for (const auto& a : m_origApps) m_apps->Append(a);

    auto* buttons = new wxBoxSizer(wxVERTICAL);
    auto* add = new wxButton(page, wxID_ANY, "Add...");
    auto* del = new wxButton(page, wxID_ANY, "Remove");
    buttons->Add(add, 0, wxEXPAND | wxBOTTOM, 6);
    buttons->Add(del, 0, wxEXPAND);
    add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        wxString name = wxGetTextFromUser("Executable name", "Exclusive application",
                                          "", this);
        if (!name.IsEmpty()) m_apps->Append(name);
    });
    del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        int sel = m_apps->GetSelection();
        if (sel != wxNOT_FOUND) m_apps->Delete(sel);
    });

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(m_apps, 1, wxEXPAND | wxRIGHT, 10);
    row->Add(buttons, 0);
    top->Add(row, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    page->SetSizerAndFit(top);
    return page;
}

// ---------------------------------------------------------------------------
std::vector<wxString> BtPrefsDlg::ExclusiveApps() const
{
    std::vector<wxString> out;
    for (unsigned i = 0; i < m_apps->GetCount(); i++) out.push_back(m_apps->GetString(i));
    return out;
}

bool BtPrefsDlg::ExclusiveAppsChanged() const
{
    return ExclusiveApps() != m_origApps;
}

bool BtPrefsDlg::Apply(GLOBAL_PREFS& prefs, GLOBAL_PREFS_MASK& mask) const
{
    bool any = false;
    // Every field follows the same shape: if the control differs from what the
    // client gave us, copy it over and raise its mask bit so it lands in the
    // override file.
    auto boolField = [&](wxCheckBox* c, bool orig, bool& dest, bool& maskBit) {
        if (c->GetValue() == orig) return;
        dest = c->GetValue(); maskBit = true; any = true;
    };
    // Compare at the precision the control displays. The client can report
    // 0.1 GB where a 2-digit control shows "0.10", and a raw comparison against
    // the stored double would call that an edit and pin it in the override.
    auto numField = [&](wxSpinCtrlDouble* c, double orig, double& dest, bool& maskBit,
                        double scale = 1.0) {
        double shown     = c->GetValue();
        double origShown = orig / scale;
        double epsilon   = 0.5 * std::pow(10.0, -(double)c->GetDigits());
        if (std::fabs(shown - origShown) < epsilon) return;
        dest = shown * scale; maskBit = true; any = true;
    };

    // --- processor
    boolField(m_batteries, m_orig.run_on_batteries, prefs.run_on_batteries,
              mask.run_on_batteries);
    boolField(m_inUse, m_orig.run_if_user_active, prefs.run_if_user_active,
              mask.run_if_user_active);
    boolField(m_gpuInUse, m_orig.run_gpu_if_user_active, prefs.run_gpu_if_user_active,
              mask.run_gpu_if_user_active);
    numField(m_idleMinutes, m_orig.idle_time_to_run, prefs.idle_time_to_run,
             mask.idle_time_to_run);
    numField(m_suspendCpuUsage, m_orig.suspend_cpu_usage, prefs.suspend_cpu_usage,
             mask.suspend_cpu_usage);
    numField(m_cpuStart, m_orig.cpu_times.start_hour, prefs.cpu_times.start_hour,
             mask.start_hour);
    numField(m_cpuEnd, m_orig.cpu_times.end_hour, prefs.cpu_times.end_hour,
             mask.end_hour);
    numField(m_switchEvery, m_orig.cpu_scheduling_period_minutes,
             prefs.cpu_scheduling_period_minutes, mask.cpu_scheduling_period_minutes);
    numField(m_maxCpusPct, m_orig.max_ncpus_pct, prefs.max_ncpus_pct, mask.max_ncpus_pct);
    numField(m_cpuUsageLimit, m_orig.cpu_usage_limit, prefs.cpu_usage_limit,
             mask.cpu_usage_limit);

    // --- network
    numField(m_downKbps, m_orig.max_bytes_sec_down, prefs.max_bytes_sec_down,
             mask.max_bytes_sec_down, 1024.0);
    numField(m_upKbps, m_orig.max_bytes_sec_up, prefs.max_bytes_sec_up,
             mask.max_bytes_sec_up, 1024.0);
    numField(m_bufMin, m_orig.work_buf_min_days, prefs.work_buf_min_days,
             mask.work_buf_min_days);
    numField(m_bufAdd, m_orig.work_buf_additional_days, prefs.work_buf_additional_days,
             mask.work_buf_additional_days);
    numField(m_dailyLimitMb, m_orig.daily_xfer_limit_mb, prefs.daily_xfer_limit_mb,
             mask.daily_xfer_limit_mb);
    if ((int)m_dailyPeriod->GetValue() != m_orig.daily_xfer_period_days) {
        prefs.daily_xfer_period_days = (int)m_dailyPeriod->GetValue();
        mask.daily_xfer_period_days = true;
        any = true;
    }
    numField(m_netStart, m_orig.net_times.start_hour, prefs.net_times.start_hour,
             mask.net_start_hour);
    numField(m_netEnd, m_orig.net_times.end_hour, prefs.net_times.end_hour,
             mask.net_end_hour);
    boolField(m_skipVerify, m_orig.dont_verify_images, prefs.dont_verify_images,
              mask.dont_verify_images);
    boolField(m_confirmConnect, m_orig.confirm_before_connecting,
              prefs.confirm_before_connecting, mask.confirm_before_connecting);
    boolField(m_hangup, m_orig.hangup_if_dialed, prefs.hangup_if_dialed,
              mask.hangup_if_dialed);

    // --- disk / memory
    numField(m_diskMaxGb, m_orig.disk_max_used_gb, prefs.disk_max_used_gb,
             mask.disk_max_used_gb);
    numField(m_diskMinFreeGb, m_orig.disk_min_free_gb, prefs.disk_min_free_gb,
             mask.disk_min_free_gb);
    numField(m_diskMaxPct, m_orig.disk_max_used_pct, prefs.disk_max_used_pct,
             mask.disk_max_used_pct);
    numField(m_diskInterval, m_orig.disk_interval, prefs.disk_interval,
             mask.disk_interval);
    numField(m_swapPct, m_orig.vm_max_used_frac, prefs.vm_max_used_frac,
             mask.vm_max_used_frac, 0.01);
    numField(m_ramBusyPct, m_orig.ram_max_used_busy_frac, prefs.ram_max_used_busy_frac,
             mask.ram_max_used_busy_frac, 0.01);
    numField(m_ramIdlePct, m_orig.ram_max_used_idle_frac, prefs.ram_max_used_idle_frac,
             mask.ram_max_used_idle_frac, 0.01);
    boolField(m_leaveInMemory, m_orig.leave_apps_in_memory, prefs.leave_apps_in_memory,
              mask.leave_apps_in_memory);

    // --- day-of-week grids. There is no mask bit for these: the client takes
    // whatever day_prefs the override carries, so the hours mask bit rides along
    // to make sure the block is written.
    if (m_cpuWeek->Changed() && WeekDiffers(m_cpuWeek->Week(), m_orig.cpu_times.week)) {
        prefs.cpu_times.week = m_cpuWeek->Week();
        mask.start_hour = mask.end_hour = true;
        any = true;
    }
    if (m_netWeek->Changed() && WeekDiffers(m_netWeek->Week(), m_orig.net_times.week)) {
        prefs.net_times.week = m_netWeek->Week();
        mask.net_start_hour = mask.net_end_hour = true;
        any = true;
    }

    return any;
}
