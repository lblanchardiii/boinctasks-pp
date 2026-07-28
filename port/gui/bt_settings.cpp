#include "bt_settings.h"
#include "bt_config.h"
#include <wx/fileconf.h>
#include <wx/utils.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>
#include <wx/clrpicker.h>
#include <wx/tokenzr.h>
#include <wx/listbox.h>
#include <wx/textctrl.h>
#include <wx/notebook.h>
#include <wx/statbox.h>
#include <wx/button.h>
#include "bt_rulesdlg.h"
#include <memory>

BtSettings gSettings;

// Defaults follow the Windows app: yellows for transfer/report, greens for
// running (brighter for GPU), grey abort, white idle states, red error.
void BtSettings::ResetColours()
{
    taskColour[BTS_UPLOAD_DOWNLOAD] = wxColour(255, 255, 190);
    taskColour[BTS_READY_TO_REPORT] = wxColour(255, 255, 120);
    taskColour[BTS_RUNNING]         = wxColour(150, 220, 150);
    taskColour[BTS_HIGH_PRIORITY]   = wxColour(110, 190, 110);
    taskColour[BTS_ABORT]           = wxColour(200, 200, 200);
    taskColour[BTS_WAITING_TO_RUN]  = wxColour(255, 255, 255);
    taskColour[BTS_READY_TO_START]  = wxColour(255, 255, 255);
    taskColour[BTS_ERROR]           = wxColour(255, 120, 120);
    taskColour[BTS_SUSPENDED]       = wxColour(255, 255, 255);

    for (int i = 0; i < BTS_COUNT; i++) taskColourGpu[i] = taskColour[i];
    taskColourGpu[BTS_RUNNING]       = wxColour(120, 255, 120);
    taskColourGpu[BTS_HIGH_PRIORITY] = wxColour( 80, 235,  80);
}

const char* BtSettings::StateName(int state)
{
    switch (state) {
        case BTS_UPLOAD_DOWNLOAD: return "Upload / Download";
        case BTS_READY_TO_REPORT: return "Ready to report";
        case BTS_RUNNING:         return "Running";
        case BTS_HIGH_PRIORITY:   return "High priority";
        case BTS_ABORT:           return "Abort";
        case BTS_WAITING_TO_RUN:  return "Waiting to run";
        case BTS_READY_TO_START:  return "Ready to start";
        case BTS_ERROR:           return "Error";
        case BTS_SUSPENDED:       return "Suspended";
    }
    return "";
}

std::vector<wxString> BtSettings::DefaultStatusOrder()
{
    // Running work first, then what is moving, then what is waiting, then what
    // is finished, and the failures last - the order you scan a farm in.
    return { "Running High P.", "Running", "Uploading", "Downloading",
             "Ready to report", "Waiting to run", "Ready to start", "New",
             "Suspended", "Project suspended", "Upload failed",
             "Computation error", "Aborted" };
}

int BtSettings::StatusRank(const wxString& status) const
{
    const auto& order = statusOrder.empty() ? DefaultStatusOrder() : statusOrder;
    for (size_t i = 0; i < order.size(); i++)
        if (order[i] == status) return (int)i;
    return (int)order.size();          // anything unlisted sorts to the end
}

static wxFileConfig* OpenConfig()
{
    return new wxFileConfig(BTPP_SHORT, "eFMer", BtConfigPath());
}

static wxColour ReadColour(wxFileConfig& cfg, const wxString& key, const wxColour& fallback)
{
    wxString s = cfg.Read(key, "");
    if (s.IsEmpty()) return fallback;
    wxColour c(s);
    return c.IsOk() ? c : fallback;
}

void BtSettings::Load()
{
    std::unique_ptr<wxFileConfig> cfg(OpenConfig());
    pollIntervalMs  = (int)cfg->ReadLong("/Settings/poll_interval_ms", 0);
    historyDays     = (int)cfg->ReadLong("/History/retention_days", 7);
    messageLimit    = (int)cfg->ReadLong("/Settings/message_limit", 2000);
    colourRows      = cfg->ReadBool("/Settings/colour_rows", true);
    treeSashPos     = (int)cfg->ReadLong("/Settings/tree_sash", 0);
    startClientWithApp = cfg->ReadBool("/General/start_client", false);
    stopClientOnExit   = cfg->ReadBool("/General/stop_client_exit", false);
    clientStartDelay   = (int)cfg->ReadLong("/General/client_delay", 5);
    hideAtStartup      = cfg->ReadBool("/General/hide_at_startup", false);
    projectSidebar     = cfg->ReadBool("/View/project_sidebar", false);
    alternatingStripes = cfg->ReadBool("/View/stripes", false);
    gridLinesH         = cfg->ReadBool("/View/grid_h", true);
    gridLinesV         = cfg->ReadBool("/View/grid_v", false);
    percentageRectangle= cfg->ReadBool("/View/pct_rect", true);
    timeFormatDays     = cfg->ReadBool("/View/time_days", true);
    thousandSeparator  = cfg->ReadBool("/View/thousands", true);
    userFriendlyName   = cfg->ReadBool("/Tasks/friendly_name", true);
    cpuDigits          = (int)cfg->ReadLong("/Tasks/cpu_digits", 2);
    progressDigits     = (int)cfg->ReadLong("/Tasks/progress_digits", 3);
    deadlineRemaining  = cfg->ReadBool("/Tasks/deadline_remaining", false);
    condenseUse        = cfg->ReadBool("/Tasks/condense_use", true);
    cpuLongAverage     = cfg->ReadBool("/Tasks/cpu_long_average", true);
    statusOrder.clear();
    {
        wxString packed = cfg->Read("/Tasks/status_order", "");
        wxStringTokenizer tk(packed, "|");
        while (tk.HasMoreTokens()) {
            wxString s = tk.GetNextToken();
            if (!s.IsEmpty()) statusOrder.push_back(s);
        }
    }
    historyLogging     = cfg->ReadBool("/History/logging", true);
    longTermAfterDays  = (int)cfg->ReadLong("/History/long_term_after", 0);
    historyBackup      = cfg->ReadBool("/History/backup", false);
    combineComputer    = cfg->ReadBool("/Combine/computer", true);
    combineProject     = cfg->ReadBool("/Combine/project", true);
    combineApplication = cfg->ReadBool("/Combine/application", true);
    combineStatus      = cfg->ReadBool("/Combine/status", true);
    warnDeadline      = cfg->ReadBool("/Warnings/deadline", false);
    warnDeadlineDays  = (int)cfg->ReadLong("/Warnings/deadline_days", 0);
    warnDeadlineHours = cfg->ReadDouble("/Warnings/deadline_hours", 12);
    for (int i = 0; i < 4; i++) {
        wxString k = wxString::Format("/Warnings/slot%d/", i);
        warnSlots[i].computer = cfg->Read(k + "computer", "");
        warnSlots[i].project  = cfg->Read(k + "project", "");
        warnSlots[i].cpuTasks = (int)cfg->ReadLong(k + "cpu", 0);
        warnSlots[i].gpuTasks = (int)cfg->ReadLong(k + "gpu", 0);
    }
    warnColour        = ReadColour(*cfg, "/Warnings/colour", wxColour(255, 80, 80));
    scanTimeoutMs     = (int)cfg->ReadLong("/Scan/timeout_ms", 700);
    scanSlowTimeoutMs = (int)cfg->ReadLong("/Scan/slow_timeout_ms", 4000);
    onlyActiveTasks = cfg->ReadBool("/Filter/only_active", false);
    showCpuTasks    = cfg->ReadBool("/Filter/cpu", true);
    showGpuTasks    = cfg->ReadBool("/Filter/gpu", true);
    showNciTasks    = cfg->ReadBool("/Filter/nci", true);
    winW            = (int)cfg->ReadLong("/Window/width", 0);
    winH            = (int)cfg->ReadLong("/Window/height", 0);
    winMaximized    = cfg->ReadBool("/Window/maximized", false);
    ResetColours();
    for (int i = 0; i < BTS_COUNT; i++) {
        taskColour[i]    = ReadColour(*cfg, wxString::Format("/Colours/cpu_%d", i),
                                      taskColour[i]);
        taskColourGpu[i] = ReadColour(*cfg, wxString::Format("/Colours/gpu_%d", i),
                                      taskColourGpu[i]);
    }
    noNewWorkColour = ReadColour(*cfg, "/Settings/colour_nonewwork", wxColour(255, 240, 180));
}

void BtSettings::Save() const
{
    std::unique_ptr<wxFileConfig> cfg(OpenConfig());
    cfg->Write("/Settings/poll_interval_ms", (long)pollIntervalMs);
    cfg->Write("/History/retention_days",    (long)historyDays);
    cfg->Write("/Settings/message_limit",    (long)messageLimit);
    cfg->Write("/Settings/colour_rows",      colourRows);
    cfg->Write("/Settings/tree_sash",        (long)treeSashPos);
    cfg->Write("/General/start_client",      startClientWithApp);
    cfg->Write("/General/stop_client_exit",  stopClientOnExit);
    cfg->Write("/General/client_delay",      (long)clientStartDelay);
    cfg->Write("/General/hide_at_startup",   hideAtStartup);
    cfg->Write("/View/project_sidebar",      projectSidebar);
    cfg->Write("/View/stripes",              alternatingStripes);
    cfg->Write("/View/grid_h",               gridLinesH);
    cfg->Write("/View/grid_v",               gridLinesV);
    cfg->Write("/View/pct_rect",             percentageRectangle);
    cfg->Write("/View/time_days",            timeFormatDays);
    cfg->Write("/View/thousands",            thousandSeparator);
    cfg->Write("/Tasks/friendly_name",       userFriendlyName);
    cfg->Write("/Tasks/cpu_digits",          (long)cpuDigits);
    cfg->Write("/Tasks/progress_digits",     (long)progressDigits);
    cfg->Write("/Tasks/deadline_remaining",  deadlineRemaining);
    cfg->Write("/Tasks/condense_use",        condenseUse);
    cfg->Write("/Tasks/cpu_long_average",    cpuLongAverage);
    {
        wxString packed;
        for (const auto& s : statusOrder) { if (!packed.IsEmpty()) packed += "|"; packed += s; }
        cfg->Write("/Tasks/status_order", packed);
    }
    cfg->Write("/History/logging",           historyLogging);
    cfg->Write("/History/long_term_after",   (long)longTermAfterDays);
    cfg->Write("/History/backup",            historyBackup);
    cfg->Write("/Combine/computer",          combineComputer);
    cfg->Write("/Combine/project",           combineProject);
    cfg->Write("/Combine/application",       combineApplication);
    cfg->Write("/Combine/status",            combineStatus);
    cfg->Write("/Warnings/deadline",         warnDeadline);
    cfg->Write("/Warnings/deadline_days",    (long)warnDeadlineDays);
    cfg->Write("/Warnings/deadline_hours",   warnDeadlineHours);
    for (int i = 0; i < 4; i++) {
        wxString k = wxString::Format("/Warnings/slot%d/", i);
        cfg->Write(k + "computer", warnSlots[i].computer);
        cfg->Write(k + "project",  warnSlots[i].project);
        cfg->Write(k + "cpu", (long)warnSlots[i].cpuTasks);
        cfg->Write(k + "gpu", (long)warnSlots[i].gpuTasks);
    }
    cfg->Write("/Warnings/colour",           warnColour.GetAsString(wxC2S_HTML_SYNTAX));
    cfg->Write("/Scan/timeout_ms",           (long)scanTimeoutMs);
    cfg->Write("/Scan/slow_timeout_ms",      (long)scanSlowTimeoutMs);
    cfg->Write("/Filter/only_active",        onlyActiveTasks);
    cfg->Write("/Filter/cpu",                showCpuTasks);
    cfg->Write("/Filter/gpu",                showGpuTasks);
    cfg->Write("/Filter/nci",                showNciTasks);
    cfg->Write("/Window/width",              (long)winW);
    cfg->Write("/Window/height",             (long)winH);
    cfg->Write("/Window/maximized",          winMaximized);
    for (int i = 0; i < BTS_COUNT; i++) {
        cfg->Write(wxString::Format("/Colours/cpu_%d", i),
                   taskColour[i].GetAsString(wxC2S_HTML_SYNTAX));
        cfg->Write(wxString::Format("/Colours/gpu_%d", i),
                   taskColourGpu[i].GetAsString(wxC2S_HTML_SYNTAX));
    }
    cfg->Write("/Settings/colour_nonewwork", noNewWorkColour.GetAsString(wxC2S_HTML_SYNTAX));
    cfg->Flush();
}

// ---------------------------------------------------------------------------
static const long kIntervals[] = { 0, 1000, 2000, 5000, 10000, 20000, 60000 };

const std::vector<BtRule>& BtSettingsDlg::Rules() const { return m_rulesPanel->Rules(); }

BtSettingsDlg::BtSettingsDlg(wxWindow* parent, const BtSettings& cur,
                             std::vector<BtRule> rules,
                             std::vector<BtColumnPage> columns)
    : wxDialog(parent, wxID_ANY, "BoincTasks Settings", wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_columns(std::move(columns))
    , m_friendlyName(nullptr), m_cpuDigits(nullptr), m_progressDigits(nullptr)
    , m_deadlineRemaining(nullptr), m_historyLogging(nullptr), m_longTermAfter(nullptr)
    , m_condenseUse(nullptr), m_cpuLongAvg(nullptr), m_historyBackup(nullptr)
    , m_statusOrder(nullptr), m_warnDeadline(nullptr), m_warnHours(nullptr)
    , m_warnColour(nullptr)
{
    // Same thirteen tabs, in the same order, as the Windows dialog. The four
    // that cover Windows-only integrations are present but empty, so the layout
    // is familiar and what is missing is obvious.
    auto* book = new wxNotebook(this, wxID_ANY);
    book->AddPage(BuildViewPage(book, cur), "View");

    m_columnBoxes.resize(m_columns.size());
    for (size_t i = 0; i < m_columns.size(); i++)
        book->AddPage(BuildColumnPage(book, i), m_columns[i].title);

    book->AddPage(BuildWarningsPage(book, cur), "Warnings");
    book->AddPage(BuildPlaceholder(book, "the desktop gadget"), "Gadget");
    m_rulesPanel = new BtRulesPanel(book, std::move(rules));
    book->AddPage(m_rulesPanel, "Rules");
    book->AddPage(BuildPlaceholder(book, "BoincTasks Cloud"), "Cloud");
    book->AddPage(BuildPlaceholder(book, "BoincTasks Mobile"), "Mobile");
    book->AddPage(BuildPlaceholder(book, "the expert options"), "Expert");

    auto* general = new wxPanel(book);

    auto* grid = new wxFlexGridSizer(2, 10, 10);
    grid->AddGrowableCol(1, 1);
    auto row = [&](const wxString& label, wxWindow* ctrl) {
        grid->Add(new wxStaticText(general, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    m_interval = new wxChoice(general, wxID_ANY);
    m_interval->Append("Automatic (scales with host count)");
    m_interval->Append("1 second");
    m_interval->Append("2 seconds");
    m_interval->Append("5 seconds");
    m_interval->Append("10 seconds");
    m_interval->Append("20 seconds");
    m_interval->Append("60 seconds");
    m_interval->SetSelection(0);
    for (unsigned i = 0; i < sizeof(kIntervals)/sizeof(kIntervals[0]); i++)
        if (kIntervals[i] == cur.pollIntervalMs) m_interval->SetSelection((int)i);

    m_history  = new wxSpinCtrl(general, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                wxSP_ARROW_KEYS, 0, 3650, cur.historyDays);
    m_messages = new wxSpinCtrl(general, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                wxSP_ARROW_KEYS, 100, 100000, cur.messageLimit);
    m_colour   = new wxCheckBox(general, wxID_ANY, "Colour rows by status");
    m_colour->SetValue(cur.colourRows);
    m_noWork   = new wxColourPickerCtrl(general, wxID_ANY, cur.noNewWorkColour);

    row("Refresh interval",        m_interval);
    row("Keep history (days)",     m_history);
    row("Messages per computer",   m_messages);
    row("",                        m_colour);
    row("Project: no new tasks",   m_noWork);

    // task colours: one row per status, CPU and GPU columns
    auto* colours = new wxFlexGridSizer(3, 6, 10);
    colours->Add(new wxStaticText(general, wxID_ANY, ""));
    colours->Add(new wxStaticText(general, wxID_ANY, "CPU"), 0, wxALIGN_CENTER);
    colours->Add(new wxStaticText(general, wxID_ANY, "GPU"), 0, wxALIGN_CENTER);
    for (int i = 0; i < BTS_COUNT; i++) {
        m_cpu[i] = new wxColourPickerCtrl(general, wxID_ANY, cur.taskColour[i]);
        m_gpu[i] = new wxColourPickerCtrl(general, wxID_ANY, cur.taskColourGpu[i]);
        colours->Add(new wxStaticText(general, wxID_ANY, BtSettings::StateName(i)),
                     0, wxALIGN_CENTER_VERTICAL);
        colours->Add(m_cpu[i]);
        colours->Add(m_gpu[i]);
    }

    auto* gtop = new wxBoxSizer(wxVERTICAL);
    gtop->Add(grid, 0, wxEXPAND | wxALL, 14);
    gtop->Add(new wxStaticText(general, wxID_ANY, "Task colours"), 0, wxLEFT | wxTOP, 14);
    gtop->Add(colours, 0, wxEXPAND | wxALL, 14);
    gtop->Add(new wxStaticText(general, wxID_ANY,
        "0 days keeps history forever. Automatic refresh slows down as more\n"
        "computers are added so a large farm stays responsive."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 14);
    {   // the General options the Windows tab carries, above the colours
        auto* box = new wxStaticBoxSizer(wxVERTICAL, general, "BOINC client");
        m_startClient = new wxCheckBox(box->GetStaticBox(), wxID_ANY,
            "Start BOINC client when BoincTasks starts");
        m_stopClient  = new wxCheckBox(box->GetStaticBox(), wxID_ANY,
            "Stop BOINC client on exit");
        m_hideAtStartup = new wxCheckBox(box->GetStaticBox(), wxID_ANY,
            "Hide BoincTasks at startup");
        m_clientDelay = new wxSpinCtrl(box->GetStaticBox(), wxID_ANY, "",
            wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 600,
            cur.clientStartDelay);
        m_startClient->SetValue(cur.startClientWithApp);
        m_stopClient->SetValue(cur.stopClientOnExit);
        m_hideAtStartup->SetValue(cur.hideAtStartup);
        box->Add(m_startClient, 0, wxALL, 6);
        auto* delayRow = new wxBoxSizer(wxHORIZONTAL);
        delayRow->Add(new wxStaticText(box->GetStaticBox(), wxID_ANY,
            "Delay starting BOINC client for"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        delayRow->Add(m_clientDelay, 0);
        delayRow->Add(new wxStaticText(box->GetStaticBox(), wxID_ANY, " second(s)"),
                      0, wxALIGN_CENTER_VERTICAL);
        box->Add(delayRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
        box->Add(m_stopClient, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
        box->Add(m_hideAtStartup, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
        gtop->Add(box, 0, wxEXPAND | wxALL, 14);
    }
    {   // Find computers
        auto* box = new wxStaticBoxSizer(wxVERTICAL, general, "Find computers");
        auto* grid = new wxFlexGridSizer(3, 6, 8);
        m_scanTimeout = new wxSpinCtrl(box->GetStaticBox(), wxID_ANY, "",
            wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 100, 30000,
            cur.scanTimeoutMs);
        m_scanSlowTimeout = new wxSpinCtrl(box->GetStaticBox(), wxID_ANY, "",
            wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 0, 60000,
            cur.scanSlowTimeoutMs);
        grid->Add(new wxStaticText(box->GetStaticBox(), wxID_ANY, "Wait up to"), 0,
                  wxALIGN_CENTER_VERTICAL);
        grid->Add(m_scanTimeout);
        grid->Add(new wxStaticText(box->GetStaticBox(), wxID_ANY,
                  "ms for a reply"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(new wxStaticText(box->GetStaticBox(), wxID_ANY, "Retry silent hosts at"),
                  0, wxALIGN_CENTER_VERTICAL);
        grid->Add(m_scanSlowTimeout);
        grid->Add(new wxStaticText(box->GetStaticBox(), wxID_ANY,
                  "ms (0 = no retry)"), 0, wxALIGN_CENTER_VERTICAL);
        box->Add(grid, 0, wxALL, 8);
        box->Add(new wxStaticText(box->GetStaticBox(), wxID_ANY,
            "The scan sweeps quickly, then goes back over whatever stayed silent\n"
            "with the longer timeout. Raise the retry if hosts on a slow or\n"
            "wireless link are being missed."),
            0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        gtop->Add(box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);
    }
    general->SetSizer(gtop);
    book->InsertPage(0, general, "General", true);

    auto* top = new wxBoxSizer(wxVERTICAL);
    top->Add(book, 1, wxEXPAND | wxALL, 8);
    top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 14);
    SetSizerAndFit(top);
}

BtSettings BtSettingsDlg::Result() const
{
    BtSettings s;
    int sel = m_interval->GetSelection();
    s.pollIntervalMs  = (sel >= 0 && sel < (int)(sizeof(kIntervals)/sizeof(kIntervals[0])))
                      ? (int)kIntervals[sel] : 0;
    s.historyDays     = m_history->GetValue();
    s.messageLimit    = m_messages->GetValue();
    s.colourRows      = m_colour->GetValue();
    for (int i = 0; i < BTS_COUNT; i++) {
        s.taskColour[i]    = m_cpu[i]->GetColour();
        s.taskColourGpu[i] = m_gpu[i]->GetColour();
    }
    s.noNewWorkColour = m_noWork->GetColour();

    s.startClientWithApp = m_startClient->GetValue();
    s.stopClientOnExit   = m_stopClient->GetValue();
    s.clientStartDelay   = m_clientDelay->GetValue();
    s.hideAtStartup      = m_hideAtStartup->GetValue();
    s.scanTimeoutMs      = m_scanTimeout->GetValue();
    s.scanSlowTimeoutMs  = m_scanSlowTimeout->GetValue();
    s.alternatingStripes = m_stripes->GetValue();
    s.gridLinesH         = m_gridH->GetValue();
    s.gridLinesV         = m_gridV->GetValue();
    s.percentageRectangle= m_pctRect->GetValue();
    s.timeFormatDays     = (m_timeFormat->GetSelection() == 0);
    s.thousandSeparator  = m_thousands->GetValue();
    if (m_friendlyName)      s.userFriendlyName  = m_friendlyName->GetValue();
    if (m_deadlineRemaining) s.deadlineRemaining = m_deadlineRemaining->GetValue();
    if (m_cpuDigits)         s.cpuDigits         = m_cpuDigits->GetValue();
    if (m_progressDigits)    s.progressDigits    = m_progressDigits->GetValue();
    if (m_statusOrder) {
        s.statusOrder.clear();
        for (unsigned i = 0; i < m_statusOrder->GetCount(); i++)
            s.statusOrder.push_back(m_statusOrder->GetString(i));
    }
    if (m_warnDeadline) {
        s.warnDeadline      = m_warnDeadline->GetValue();
        s.warnDeadlineDays  = m_warnDays->GetValue();
        s.warnDeadlineHours = m_warnHours->GetValue();
        s.warnColour        = m_warnColour->GetColour();
        for (int i = 0; i < 4; i++) {
            s.warnSlots[i].computer = m_slotComputer[i]->GetValue();
            s.warnSlots[i].project  = m_slotProject[i]->GetValue();
            s.warnSlots[i].cpuTasks = m_slotCpu[i]->GetValue();
            s.warnSlots[i].gpuTasks = m_slotGpu[i]->GetValue();
        }
    }
    if (m_condenseUse)       s.condenseUse       = m_condenseUse->GetValue();
    if (m_cpuLongAvg)        s.cpuLongAverage    = m_cpuLongAvg->GetValue();
    if (m_historyBackup)     s.historyBackup     = m_historyBackup->GetValue();
    if (m_historyLogging)    s.historyLogging    = m_historyLogging->GetValue();
    if (m_longTermAfter)     s.longTermAfterDays = m_longTermAfter->GetValue();

    // fold the column checkboxes back into the page definitions
    for (size_t p = 0; p < m_columns.size(); p++) {
        auto& shown = const_cast<std::vector<bool>&>(m_columns[p].shown);
        shown.resize(m_columnBoxes[p].size());
        for (size_t i = 0; i < m_columnBoxes[p].size(); i++)
            shown[i] = m_columnBoxes[p][i]->GetValue();
    }
    return s;
}

// ---------------------------------------------------------------------------
// the remaining pages
// ---------------------------------------------------------------------------
wxWindow* BtSettingsDlg::BuildViewPage(wxWindow* parent, const BtSettings& cur)
{
    auto* page = new wxPanel(parent);
    auto* top  = new wxBoxSizer(wxVERTICAL);

    m_stripes = new wxCheckBox(page, wxID_ANY, "Use alternating stripes");
    m_gridH   = new wxCheckBox(page, wxID_ANY, "Use horizontal grid lines");
    m_gridV   = new wxCheckBox(page, wxID_ANY, "Use vertical grid lines");
    m_pctRect = new wxCheckBox(page, wxID_ANY, "Show percentage rectangle");
    m_stripes->SetValue(cur.alternatingStripes);
    m_gridH->SetValue(cur.gridLinesH);
    m_gridV->SetValue(cur.gridLinesV);
    m_pctRect->SetValue(cur.percentageRectangle);

    auto* fmt = new wxBoxSizer(wxHORIZONTAL);
    m_timeFormat = new wxChoice(page, wxID_ANY);
    m_timeFormat->Append("d,HH:MM:SS");
    m_timeFormat->Append("HH:MM:SS (hours accumulate)");
    m_timeFormat->SetSelection(cur.timeFormatDays ? 0 : 1);
    fmt->Add(new wxStaticText(page, wxID_ANY, "Time format"), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    fmt->Add(m_timeFormat);
    top->Add(fmt, 0, wxLEFT | wxRIGHT | wxTOP, 12);

    m_thousands = new wxCheckBox(page, wxID_ANY, "1000 separator (1,000,000.9)");
    m_thousands->SetValue(cur.thousandSeparator);
    top->Add(m_thousands, 0, wxLEFT | wxRIGHT | wxTOP, 12);

    for (wxCheckBox* c : { m_stripes, m_gridH, m_gridV, m_pctRect })
        top->Add(c, 0, wxLEFT | wxRIGHT | wxTOP, 12);

    top->Add(new wxStaticText(page, wxID_ANY,
        "\nGrid lines and stripes apply to the list views. The percentage "
        "rectangle draws Progress % as a bar; turning it off shows the number "
        "alone.\n\nRefresh rate lives on the General tab, with the rest of the "
        "polling settings. Skin and language are not implemented."),
        0, wxALL, 12);
    page->SetSizer(top);
    return page;
}

wxWindow* BtSettingsDlg::BuildColumnPage(wxWindow* parent, size_t index, wxWindow**)
{
    auto* page = new wxPanel(parent);
    auto* top  = new wxBoxSizer(wxVERTICAL);
    const BtColumnPage& def = m_columns[index];

    auto* box = new wxStaticBoxSizer(wxVERTICAL, page, "Show column");
    auto* grid = new wxFlexGridSizer(3, 4, 10);
    for (size_t i = 0; i < def.names.size(); i++) {
        auto* c = new wxCheckBox(box->GetStaticBox(), wxID_ANY, def.names[i]);
        c->SetValue(i < def.shown.size() ? def.shown[i] : true);
        m_columnBoxes[index].push_back(c);
        grid->Add(c);
    }
    box->Add(grid, 0, wxALL, 8);
    top->Add(box, 0, wxEXPAND | wxALL, 12);

    // the per-view extras the Windows tabs carry, on the tabs that have them
    if (def.key == "tasks") {
        auto* extra = new wxStaticBoxSizer(wxVERTICAL, page, "Tasks");
        m_friendlyName = new wxCheckBox(extra->GetStaticBox(), wxID_ANY,
                                        "Use user friendly name");
        m_deadlineRemaining = new wxCheckBox(extra->GetStaticBox(), wxID_ANY,
                                        "Deadline show remaining time");
        m_friendlyName->SetValue(gSettings.userFriendlyName);
        m_deadlineRemaining->SetValue(gSettings.deadlineRemaining);
        m_condenseUse = new wxCheckBox(extra->GetStaticBox(), wxID_ANY,
                                       "Condense Use column");
        m_cpuLongAvg  = new wxCheckBox(extra->GetStaticBox(), wxID_ANY,
                                       "CPU % long time average");
        m_condenseUse->SetValue(gSettings.condenseUse);
        m_cpuLongAvg->SetValue(gSettings.cpuLongAverage);
        extra->Add(m_friendlyName, 0, wxALL, 6);
        extra->Add(m_deadlineRemaining, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
        extra->Add(m_condenseUse, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
        extra->Add(m_cpuLongAvg, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

        auto* dg = new wxFlexGridSizer(2, 6, 8);
        m_cpuDigits = new wxSpinCtrl(extra->GetStaticBox(), wxID_ANY, "",
            wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 6,
            gSettings.cpuDigits);
        m_progressDigits = new wxSpinCtrl(extra->GetStaticBox(), wxID_ANY, "",
            wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 6,
            gSettings.progressDigits);
        dg->Add(new wxStaticText(extra->GetStaticBox(), wxID_ANY, "CPU % digits"),
                0, wxALIGN_CENTER_VERTICAL);
        dg->Add(m_cpuDigits);
        dg->Add(new wxStaticText(extra->GetStaticBox(), wxID_ANY, "Progress % digits"),
                0, wxALIGN_CENTER_VERTICAL);
        dg->Add(m_progressDigits);
        extra->Add(dg, 0, wxALL, 6);
        top->Add(extra, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

        // Status column sorting: a rank list rather than alphabetical
        auto* sortBox = new wxStaticBoxSizer(wxVERTICAL, page, "Status column sorting");
        m_statusOrder = new wxListBox(sortBox->GetStaticBox(), wxID_ANY,
                                      wxDefaultPosition, wxSize(240, 150));
        const auto& order = gSettings.statusOrder.empty()
                          ? BtSettings::DefaultStatusOrder() : gSettings.statusOrder;
        for (const auto& s : order) m_statusOrder->Append(s);

        auto* btns = new wxBoxSizer(wxVERTICAL);
        auto* up   = new wxButton(sortBox->GetStaticBox(), wxID_ANY, "Up");
        auto* down = new wxButton(sortBox->GetStaticBox(), wxID_ANY, "Down");
        auto* dflt = new wxButton(sortBox->GetStaticBox(), wxID_ANY, "Default");
        btns->Add(up, 0, wxEXPAND | wxBOTTOM, 6);
        btns->Add(down, 0, wxEXPAND | wxBOTTOM, 6);
        btns->Add(dflt, 0, wxEXPAND | wxTOP, 12);

        auto move = [this](int delta) {
            int i = m_statusOrder->GetSelection();
            int j = i + delta;
            if (i == wxNOT_FOUND || j < 0 || j >= (int)m_statusOrder->GetCount()) return;
            wxString a = m_statusOrder->GetString(i);
            m_statusOrder->SetString(i, m_statusOrder->GetString(j));
            m_statusOrder->SetString(j, a);
            m_statusOrder->SetSelection(j);
        };
        up->Bind(wxEVT_BUTTON,   [move](wxCommandEvent&) { move(-1); });
        down->Bind(wxEVT_BUTTON, [move](wxCommandEvent&) { move(1); });
        dflt->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            m_statusOrder->Clear();
            for (const auto& s : BtSettings::DefaultStatusOrder())
                m_statusOrder->Append(s);
        });

        auto* sortRow = new wxBoxSizer(wxHORIZONTAL);
        sortRow->Add(m_statusOrder, 1, wxEXPAND | wxRIGHT, 10);
        sortRow->Add(btns, 0);
        sortBox->Add(sortRow, 0, wxEXPAND | wxALL, 8);
        sortBox->Add(new wxStaticText(sortBox->GetStaticBox(), wxID_ANY,
            "Sorting the Status column follows this order instead of the alphabet."),
            0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        top->Add(sortBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    } else if (def.key == "history") {
        auto* extra = new wxStaticBoxSizer(wxVERTICAL, page, "History");
        m_historyLogging = new wxCheckBox(extra->GetStaticBox(), wxID_ANY,
                                          "Enable history logging");
        m_historyLogging->SetValue(gSettings.historyLogging);
        extra->Add(m_historyLogging, 0, wxALL, 6);
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        m_longTermAfter = new wxSpinCtrl(extra->GetStaticBox(), wxID_ANY, "",
            wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 3650,
            gSettings.longTermAfterDays);
        row->Add(new wxStaticText(extra->GetStaticBox(), wxID_ANY,
            "Move to long term history after"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        row->Add(m_longTermAfter);
        row->Add(new wxStaticText(extra->GetStaticBox(), wxID_ANY,
            " day(s)   (0 = never move)"), 0, wxALIGN_CENTER_VERTICAL);
        extra->Add(row, 0, wxALL, 6);
        m_historyBackup = new wxCheckBox(extra->GetStaticBox(), wxID_ANY,
            "Make a backup of the history file at start-up");
        m_historyBackup->SetValue(gSettings.historyBackup);
        extra->Add(m_historyBackup, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
        extra->Add(new wxStaticText(extra->GetStaticBox(), wxID_ANY,
            "\"Remove history after\" is the retention setting on the General tab."),
            0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
        top->Add(extra, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    }

    page->SetSizer(top);
    return page;
}

wxWindow* BtSettingsDlg::BuildWarningsPage(wxWindow* parent, const BtSettings& cur)
{
    auto* page = new wxPanel(parent);
    auto* top  = new wxBoxSizer(wxVERTICAL);

    top->Add(new wxStaticText(page, wxID_ANY,
        "A warning highlights a task that wants attention and says why in the\n"
        "Status column, alongside whatever the task is already doing - so a row\n"
        "reads \"Ready to report, Deadline warning\"."),
        0, wxALL, 12);

    auto* box = new wxStaticBoxSizer(wxVERTICAL, page, "Deadline");
    m_warnDeadline = new wxCheckBox(box->GetStaticBox(), wxID_ANY,
                                    "Warn when a task is close to its deadline");
    m_warnDeadline->SetValue(cur.warnDeadline);
    box->Add(m_warnDeadline, 0, wxALL, 8);

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    m_warnDays = new wxSpinCtrl(box->GetStaticBox(), wxID_ANY, "", wxDefaultPosition,
                                wxSize(70, -1), wxSP_ARROW_KEYS, 0, 365,
                                cur.warnDeadlineDays);
    m_warnHours = new wxSpinCtrlDouble(box->GetStaticBox(), wxID_ANY, "",
                                       wxDefaultPosition, wxSize(80, -1),
                                       wxSP_ARROW_KEYS, 0, 23.5, cur.warnDeadlineHours, 0.5);
    m_warnHours->SetDigits(1);
    row->Add(new wxStaticText(box->GetStaticBox(), wxID_ANY, "Warn"), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    row->Add(m_warnDays, 0);
    row->Add(new wxStaticText(box->GetStaticBox(), wxID_ANY, " days and "), 0,
             wxALIGN_CENTER_VERTICAL);
    row->Add(m_warnHours, 0);
    row->Add(new wxStaticText(box->GetStaticBox(), wxID_ANY,
             " hours before the deadline"), 0, wxALIGN_CENTER_VERTICAL);
    box->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    top->Add(box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    auto* cbox = new wxStaticBoxSizer(wxVERTICAL, page, "Highlight");
    auto* crow = new wxBoxSizer(wxHORIZONTAL);
    m_warnColour = new wxColourPickerCtrl(cbox->GetStaticBox(), wxID_ANY, cur.warnColour);
    crow->Add(new wxStaticText(cbox->GetStaticBox(), wxID_ANY, "Warning colour"), 0,
              wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    crow->Add(m_warnColour, 0);
    cbox->Add(crow, 0, wxALL, 8);
    cbox->Add(new wxStaticText(cbox->GetStaticBox(), wxID_ANY,
        "The warning colour replaces the status colour on a warned row, and on a\n"
        "collapsed group if any task inside it is warning."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    top->Add(cbox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    // ---- run-dry warnings ----
    auto* dry = new wxStaticBoxSizer(wxVERTICAL, page, "Low on work");
    dry->Add(new wxStaticText(dry->GetStaticBox(), wxID_ANY,
        "Warn when a project on a computer drops below this many tasks left.\n"
        "Computer and Project match on part of the name, so \"epyc\" covers every\n"
        "epyc host; leave either blank to match everything, and 0 to disable."),
        0, wxALL, 8);

    auto* grid = new wxFlexGridSizer(4, 6, 6);
    for (const char* head : { "Computer", "Project", "CPU less than", "GPU less than" })
        grid->Add(new wxStaticText(dry->GetStaticBox(), wxID_ANY, head));
    for (int i = 0; i < 4; i++) {
        m_slotComputer[i] = new wxTextCtrl(dry->GetStaticBox(), wxID_ANY,
                                           cur.warnSlots[i].computer,
                                           wxDefaultPosition, wxSize(150, -1));
        m_slotProject[i]  = new wxTextCtrl(dry->GetStaticBox(), wxID_ANY,
                                           cur.warnSlots[i].project,
                                           wxDefaultPosition, wxSize(170, -1));
        m_slotCpu[i] = new wxSpinCtrl(dry->GetStaticBox(), wxID_ANY, "", wxDefaultPosition,
                                      wxSize(90, -1), wxSP_ARROW_KEYS, 0, 100000,
                                      cur.warnSlots[i].cpuTasks);
        m_slotGpu[i] = new wxSpinCtrl(dry->GetStaticBox(), wxID_ANY, "", wxDefaultPosition,
                                      wxSize(90, -1), wxSP_ARROW_KEYS, 0, 100000,
                                      cur.warnSlots[i].gpuTasks);
        grid->Add(m_slotComputer[i]);
        grid->Add(m_slotProject[i]);
        grid->Add(m_slotCpu[i]);
        grid->Add(m_slotGpu[i]);
    }
    dry->Add(grid, 0, wxALL, 8);
    dry->Add(new wxStaticText(dry->GetStaticBox(), wxID_ANY,
        "Matching rows highlight on the Projects view."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    top->Add(dry, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    page->SetSizer(top);
    return page;
}

wxWindow* BtSettingsDlg::BuildPlaceholder(wxWindow* parent, const wxString& what)
{
    auto* page = new wxPanel(parent);
    auto* top  = new wxBoxSizer(wxVERTICAL);
    top->Add(new wxStaticText(page, wxID_ANY,
        "Settings for " + what + " are not implemented in the Linux port.\n\n"
        "The tab is here so the layout matches the Windows application and so "
        "it is clear what is still missing rather than silently absent."),
        0, wxALL, 20);
    page->SetSizer(top);
    return page;
}

wxWindow* BtSettingsDlg::BuildGeneralPage(wxWindow*, const BtSettings&)
{
    return nullptr;   // General is the legacy panel, assembled in the constructor
}
