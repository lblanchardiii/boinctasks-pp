// =============================================================================
// bt_settings.h - user preferences, persisted to the app config
// =============================================================================
#pragma once
#include <wx/colour.h>
#include <wx/dialog.h>
#include <wx/string.h>
#include <wx/checkbox.h>
#include <vector>
#include "bt_types.h"

struct BtSettings
{
    int      pollIntervalMs   = 0;      // 0 = scale automatically with host count
    int      historyDays      = 7;      // 0 = keep everything
    int      messageLimit     = 2000;   // messages kept per computer
    bool     colourRows       = true;   // status colouring in the task/history lists
    int      treeSashPos      = 0;      // 0 = size the computer pane to its names
    // General tab
    bool     startClientWithApp = false;   // start the local client on launch
    bool     stopClientOnExit   = false;
    int      clientStartDelay   = 5;       // seconds
    bool     hideAtStartup      = false;

    // View tab
    bool     projectSidebar     = false;   // second sidebar, off like Windows
    bool     alternatingStripes = false;
    bool     gridLinesH         = true;
    bool     gridLinesV         = false;
    bool     percentageRectangle = true;   // progress bar vs plain number
    bool     timeFormatDays     = true;    // "02d,03:04:05" vs hours accumulating
    bool     thousandSeparator  = true;    // group digits in credit columns

    // Tasks tab
    bool     userFriendlyName   = true;
    int      cpuDigits          = 2;
    int      progressDigits     = 3;
    bool     deadlineRemaining  = false;
    bool     condenseUse        = true;    // "4C" vs "4.00 CPU"
    bool     cpuLongAverage     = true;    // whole-task average vs last interval

    // Order the Status column sorts in. Alphabetical is useless here - it puts
    // Aborted first and scatters the running states - so this is a rank list.
    std::vector<wxString> statusOrder;
    static std::vector<wxString> DefaultStatusOrder();
    int      StatusRank(const wxString& status) const;

    // History tab
    bool     historyLogging     = true;
    int      longTermAfterDays  = 0;       // 0 = never move
    bool     historyBackup      = false;   // copy the store at start-up

    // Extra > "Filter (combine) tasks on": which fields have to match for
    // tasks to collapse into one group row. All four on is the Windows default.
    bool     combineComputer    = true;
    bool     combineProject     = true;
    bool     combineApplication = true;
    bool     combineStatus      = true;

    // Warnings: highlight tasks that need attention and say why in the Status
    // column, alongside whatever the task is already doing.
    bool     warnDeadline      = false;
    int      warnDeadlineDays  = 0;       // warn this long before the deadline
    double   warnDeadlineHours = 12;
    wxColour warnColour        = wxColour(255, 80, 80);

    // "CPU less than / GPU less than": warn when a project on a computer drops
    // below a floor of remaining tasks - the run-dry alarm. Four independent
    // slots, computer and project matched as case-insensitive substrings so
    // "epyc" covers every epyc host. Blank matches everything, 0 disables.
    struct WarnSlot {
        wxString computer, project;
        int      cpuTasks = 0;
        int      gpuTasks = 0;
        bool active() const { return cpuTasks > 0 || gpuTasks > 0; }
    };
    WarnSlot warnSlots[4];

    // Extra menu task filters, matching the Windows app
    bool     onlyActiveTasks  = false;
    bool     showCpuTasks     = true;
    bool     showGpuTasks     = true;
    bool     showNciTasks     = true;   // non CPU intensive
    int      winW = 0, winH = 0;        // 0 = never saved, use the default size
    bool     winMaximized     = false;

    // Per-status row colours, CPU and GPU variants - the Windows palette.
    wxColour taskColour[BTS_COUNT];
    wxColour taskColourGpu[BTS_COUNT];
    wxColour noNewWorkColour  = wxColour(255, 240, 180);

    BtSettings() { ResetColours(); }
    void ResetColours();
    static const char* StateName(int state);

    // convenience for code that just wants "ok" / "failed" shades
    wxColour runningColour() const { return taskColour[BTS_RUNNING]; }
    wxColour errorColour()   const { return taskColour[BTS_ERROR]; }

    void Load();
    void Save() const;
};

extern BtSettings gSettings;

#include "bt_rules.h"

// One per-view column page: the "Show column" grid the Windows tabs are built
// around. The frame hands over each view's column names and current state and
// gets the edited state back.
struct BtColumnPage
{
    wxString              title;      // tab label
    wxString              key;        // view key, for the caller to match on
    std::vector<wxString> names;
    std::vector<bool>     shown;
};

class BtSettingsDlg : public wxDialog
{
public:
    BtSettingsDlg(wxWindow* parent, const BtSettings& current,
                  std::vector<BtRule> rules,
                  std::vector<BtColumnPage> columns);
    BtSettings Result() const;
    const std::vector<BtRule>& Rules() const;
    const std::vector<BtColumnPage>& Columns() const { return m_columns; }

private:
    wxWindow* BuildGeneralPage(wxWindow* parent, const BtSettings& cur);
    wxWindow* BuildViewPage(wxWindow* parent, const BtSettings& cur);
    wxWindow* BuildColumnPage(wxWindow* parent, size_t index,
                              wxWindow** extra = nullptr);
    wxWindow* BuildPlaceholder(wxWindow* parent, const wxString& what);
    wxWindow* BuildWarningsPage(wxWindow* parent, const BtSettings& cur);

    class BtRulesPanel* m_rulesPanel;
    std::vector<BtColumnPage>            m_columns;
    std::vector<std::vector<wxCheckBox*>> m_columnBoxes;

    // General
    class wxCheckBox* m_startClient;
    class wxCheckBox* m_stopClient;
    class wxSpinCtrl* m_clientDelay;
    class wxCheckBox* m_hideAtStartup;
    // View
    class wxCheckBox* m_stripes;
    class wxCheckBox* m_gridH;
    class wxCheckBox* m_gridV;
    class wxCheckBox* m_pctRect;
    class wxChoice*   m_timeFormat;
    class wxCheckBox* m_thousands;
    // Tasks
    class wxCheckBox* m_friendlyName;
    class wxSpinCtrl* m_cpuDigits;
    class wxSpinCtrl* m_progressDigits;
    class wxCheckBox* m_deadlineRemaining;
    class wxCheckBox* m_condenseUse;
    class wxCheckBox* m_cpuLongAvg;
    class wxListBox*  m_statusOrder;
    // History
    class wxCheckBox* m_historyLogging;
    class wxSpinCtrl* m_longTermAfter;
    class wxCheckBox* m_historyBackup;
    class wxCheckBox*         m_warnDeadline;
    class wxSpinCtrlDouble*   m_warnHours;
    class wxColourPickerCtrl* m_warnColour;
    class wxSpinCtrl*  m_warnDays;
    class wxTextCtrl*  m_slotComputer[4];
    class wxTextCtrl*  m_slotProject[4];
    class wxSpinCtrl*  m_slotCpu[4];
    class wxSpinCtrl*  m_slotGpu[4];
    class wxChoice*      m_interval;
    class wxSpinCtrl*    m_history;
    class wxSpinCtrl*    m_messages;
    class wxCheckBox*    m_colour;
    class wxColourPickerCtrl* m_cpu[BTS_COUNT];
    class wxColourPickerCtrl* m_gpu[BTS_COUNT];
    class wxColourPickerCtrl* m_noWork;
};
