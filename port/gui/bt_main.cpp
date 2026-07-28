// =============================================================================
// bt_main.cpp - BoincTasks Linux port: application shell
//
// Layout mirrors the Windows app: a docked computer tree on the left selects
// which hosts are shown; the views on the right are combined across every
// selected computer and carry a trailing Computer column.
// =============================================================================
#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/treectrl.h>
#include <wx/splitter.h>
#include <wx/spinctrl.h>
#include <csignal>
#include <map>
#include <deque>
#include <thread>
#include <algorithm>
#include <fstream>
#include <memory>
#include <vector>
#include <set>
#include <functional>
#include <chrono>

#include "bt_types.h"
#include "bt_config.h"
#include "bt_poller.h"
#include "bt_views.h"
#include "bt_history.h"
#include "bt_scan.h"
#include "bt_graphs.h"
#include "bt_settings.h"
#include "bt_addproject.h"
#include "bt_prefs.h"
#include "bt_rules.h"
#include "bt_rulesdlg.h"
#include <wx/progdlg.h>
#include <wx/numdlg.h>
#include <wx/fileconf.h>
#include <wx/notifmsg.h>
#include "gui_rpc_client.h"

// Version, and where a build checks for a newer one. eFMer hosts the Windows
// application only - this port is not distributed from there - so until the
// Linux download site exists (boinctasks.free-dc.org is the plan) there is
// nothing to check against and Help > Update just says so. Point kUpdateUrl at
// the site to turn it into a real link.
static const char* kVersion   = "0.9.0";
static const char* kUpdateUrl = nullptr;

// ---------------------------------------------------------------------------
// tree item payload: which computer (empty = "All computers" / a group node)
// ---------------------------------------------------------------------------
class BtTreeData : public wxTreeItemData
{
public:
    BtTreeData(const wxString& computer, const wxString& group)
        : computer(computer), group(group) {}
    wxString computer;   // empty for group / root nodes
    wxString group;      // group label for group nodes
};

// ---------------------------------------------------------------------------
// Add computer dialog
// ---------------------------------------------------------------------------
class AddComputerDlg : public wxDialog
{
public:
    AddComputerDlg(wxWindow* parent, const BtComputer* existing = nullptr)
        : wxDialog(parent, wxID_ANY, existing ? "Edit computer" : "Add computer")
    {
        auto* grid = new wxFlexGridSizer(2, 8, 8);
        grid->AddGrowableCol(1, 1);
        auto addRow = [&](const wxString& label, wxWindow* ctrl) {
            grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
            grid->Add(ctrl, 1, wxEXPAND);
        };
        m_name  = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(260, -1));
        m_group = new wxTextCtrl(this, wxID_ANY);
        m_host  = new wxTextCtrl(this, wxID_ANY);
        m_port  = new wxSpinCtrl(this, wxID_ANY, "31416", wxDefaultPosition,
                                 wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, 31416);
        m_password = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition,
                                    wxDefaultSize, wxTE_PASSWORD);
        addRow("Name",     m_name);
        addRow("Group",    m_group);
        addRow("Host/IP",  m_host);
        addRow("Port",     m_port);
        addRow("Password", m_password);

        if (existing) {          // editing: prefill from the stored entry
            m_name->SetValue(existing->name);
            m_group->SetValue(existing->group);
            m_host->SetValue(existing->host);
            m_port->SetValue((int)existing->port);
            m_password->SetValue(existing->password);
        }

        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(grid, 1, wxEXPAND | wxALL, 12);
        top->Add(new wxStaticText(this, wxID_ANY,
            "Leave password empty on localhost to use the client's own auth file."),
            0, wxLEFT | wxRIGHT, 12);
        top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
        SetSizerAndFit(top);
    }

    BtComputer Result() const
    {
        BtComputer c;
        c.name     = m_name->GetValue().IsEmpty() ? m_host->GetValue() : m_name->GetValue();
        c.group    = m_group->GetValue();
        c.host     = m_host->GetValue();
        c.port     = m_port->GetValue();
        c.password = m_password->GetValue();
        return c;
    }

private:
    wxTextCtrl* m_name;
    wxTextCtrl* m_group;
    wxTextCtrl* m_host;
    wxSpinCtrl* m_port;
    wxTextCtrl* m_password;
};

// ---------------------------------------------------------------------------
// main frame
// ---------------------------------------------------------------------------
class MainFrame : public wxFrame
{
public:
    MainFrame() : wxFrame(nullptr, wxID_ANY, BTPP_NAME,
                          wxDefaultPosition, wxSize(1400, 760))
    {
        BtMigrateLegacyConfig();     // carry over a boinctasks-linux install
        gSettings.Load();
        if (gSettings.winW > 200 && gSettings.winH > 150)
            SetSize(gSettings.winW, gSettings.winH);
        if (gSettings.winMaximized) Maximize();
        if (gSettings.hideAtStartup) Iconize(true);
        BuildMenu();
        CreateStatusBar(3);
        int widths[] = { 220, -1, 190 };
        GetStatusBar()->SetStatusWidths(3, widths);
        SetStatusText("Connecting...", 0);
        SetStatusText(BTPP_NAME " - based on eFMer BoincTasks", 2);

        auto* split = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition,
                                           wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
        m_split = split;
        // The Windows app puts the computer tree under its own "Computers" tab,
        // so the tab strip runs the full width of the window just below the
        // menu. A one-page notebook on the left reproduces that.
        m_treeBook = new wxNotebook(split, wxID_ANY);
        m_tree = new wxTreeCtrl(m_treeBook, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_SINGLE);
        m_treeBook->AddPage(m_tree, "Computers", true);

        // Second sidebar: pick a project and the views show only that project.
        // It lives in its own splitter between the computer tree and the views,
        // and is hidden until View > Sidebar project selection turns it on.
        m_projSplit = new wxSplitterWindow(split, wxID_ANY, wxDefaultPosition,
                                           wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
        m_projBook = new wxNotebook(m_projSplit, wxID_ANY);
        m_projTree = new wxTreeCtrl(m_projBook, wxID_ANY, wxDefaultPosition,
                                    wxDefaultSize,
                                    wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_SINGLE);
        m_projBook->AddPage(m_projTree, "Projects", true);
        m_book = new wxNotebook(m_projSplit, wxID_ANY);

        m_tasks     = new TasksView(m_book);
        m_projects  = new ProjectsView(m_book);
        m_transfers = new TransfersView(m_book);
        m_messages  = new MessagesView(m_book);
        m_history   = new HistoryView(m_book);
        m_notices   = new NoticesView(m_book);
        m_computersView = new ComputersView(m_book);
        m_graphs    = new GraphsView(m_book);
        m_longHistory = new HistoryView(m_book);
        m_log       = new LogView(m_book);
        m_rulesLog  = new LogView(m_book);
        m_book->AddPage(m_tasks,     "Tasks", true);
        m_book->AddPage(m_projects,  "Projects");
        m_book->AddPage(m_transfers, "Transfers");
        m_book->AddPage(m_messages,  "Messages");
        m_book->AddPage(m_history,   "History");
        m_noticesPage = (int)m_book->GetPageCount();
        m_book->AddPage(m_notices,   "Notices");
        m_book->AddPage(m_computersView, "Computers");
        m_book->AddPage(m_graphs,    "Graphs");
        m_book->AddPage(m_longHistory, "Long term History");
        m_book->AddPage(m_log,       "Log");
        m_book->AddPage(m_rulesLog,  "Rules log");

        // Bigger tab labels, easier to pick out at a glance. Setting the font on
        // a notebook would push the same size onto its pages, so put the
        // original size back on each one - only the tabs should grow.
        {
            wxFont base = m_book->GetFont();
            wxFont tabs = base;
            tabs.SetPointSize(base.GetPointSize() + 2);
            for (wxNotebook* nb : { m_book, m_treeBook, m_projBook }) {
                nb->SetFont(tabs);
                for (size_t i = 0; i < nb->GetPageCount(); i++)
                    nb->GetPage(i)->SetFont(base);
            }
        }

        m_projSplit->SplitVertically(m_projBook, m_book, 200);
        m_projSplit->SetMinimumPaneSize(120);
        if (!gSettings.projectSidebar) m_projSplit->Unsplit(m_projBook);

        split->SplitVertically(m_treeBook, m_projSplit, 220);
        split->SetMinimumPaneSize(120);
        // A drag is the user overriding auto-fit; remember it across restarts.
        split->Bind(wxEVT_SPLITTER_SASH_POS_CHANGED, [this](wxSplitterEvent& ev) {
            gSettings.treeSashPos = ev.GetSashPosition();
            gSettings.Save();
            ev.Skip();
        });

        m_tasks->SetOpHandler([this](const std::vector<BtTaskRow>& t, const wxString& op) {
            OnTaskOp(t, op);
        });
        m_projects->SetOpHandler([this](const std::vector<BtProjectRow>& p, const wxString& op) {
            OnProjectOp(p, op);
        });
        m_transfers->SetOpHandler([this](const std::vector<BtTransferRow>& t,
                                        const wxString& op) {
            OnTransferOp(t, op);
        });
        m_computersView->SetOpHandler([this](const std::vector<wxString>& names,
                                             const wxString& op) {
            OnComputerOp(names, op);
        });
        m_computersView->SetEditHandler([this](const wxString& name, int col,
                                               const wxString& value) {
            OnComputerEdit(name, col, value);
        });

        LoadKnownProjects();
        m_pollIntervalMs = gSettings.pollIntervalMs;
        if (gSettings.startClientWithApp) {
            // General tab: bring the local client up with the app, after the
            // configured delay so it doesn't race BOINC's own autostart
            int delay = gSettings.clientStartDelay;
            std::thread([delay]() {
                std::this_thread::sleep_for(std::chrono::seconds(delay));
#ifdef _WIN32
                (void)std::system("net start boinc");
#else
                if (std::system("systemctl start boinc-client") != 0)
                    (void)std::system("boinc --daemon >/dev/null 2>&1 &");
#endif
            }).detach();
        }
        m_engine.SetRules(BtLoadRules());
        m_computers = BtLoadComputers();
        OpenHistory();
        RebuildTree();
        StartPollers();

        m_tree->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent&) { Rebuild(); });
        m_projTree->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent& ev) {
            wxTreeItemId sel = ev.GetItem();
            m_selectedProject = (sel.IsOk() && sel != m_allProjects)
                              ? m_projTree->GetItemText(sel) : wxString();
            Rebuild();
        });
        m_rebuildTimer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
            m_rebuildPending = false;
            Rebuild();
        });
        // Rules are checked every 30 seconds, the same cadence as the Windows
        // app, independent of how often the views redraw.
        m_ruleTimer.SetOwner(this, ID_RULE_TIMER);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { RunRules(); }, ID_RULE_TIMER);
        m_ruleTimer.Start(30000);

        // Archive on the hour; a machine left running for weeks would otherwise
        // only move rows at startup.
        m_archiveTimer.SetOwner(this, ID_ARCHIVE_TIMER);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
            if (gSettings.longTermAfterDays <= 0) return;
            int moved = m_history_db.MoveToLongTerm(gSettings.longTermAfterDays);
            if (moved > 0) {
                if (m_log) m_log->Append("", wxString::Format(
                    "history: moved %d row(s) to long term", moved));
                m_historyKey.Clear();
            }
        }, ID_ARCHIVE_TIMER);
        m_archiveTimer.Start(3600000);

        // Rebuild the moment the window comes back
        Bind(wxEVT_ICONIZE, [this](wxIconizeEvent& ev) {
            ev.Skip();
            if (!ev.IsIconized() && m_rebuildSkipped) {
                m_rebuildSkipped = false;
                Rebuild();
            }
        });

        m_book->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& ev) {
            ev.Skip();
            if (ev.GetEventObject() == m_book) Rebuild();   // the new page is stale
        });

        Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    }

private:
    enum { ID_ADD_COMPUTER = wxID_HIGHEST + 1, ID_REMOVE_COMPUTER,
           ID_HISTORY_RETENTION, ID_SETTINGS, ID_ADD_PROJECT,
           ID_FIND_COMPUTERS, ID_REFRESH_NOW, ID_FIT_TREE,
           ID_REPORT_ALL, ID_ACCOUNT_MGR, ID_BOINC_PREFS,
           ID_CLIENT_START, ID_CLIENT_STOP, ID_COLOURS_READ, ID_COLOURS_WRITE,
           ID_SIDEBAR, ID_STATUSBAR, ID_COLOURS_FONTS, ID_PROXY,
           ID_ONLY_ACTIVE, ID_SHOW_CPU, ID_SHOW_GPU, ID_SHOW_NCI,
           ID_BENCHMARKS, ID_EDIT_CC, ID_EDIT_APP, ID_READ_CONFIG,
           ID_HELP_MANUAL, ID_HELP_FORUM, ID_HELP_BOINC, ID_HELP_UPDATE,
           // three modes each for network / cpu / gpu, kept contiguous
           ID_NET_MODE, ID_RUN_MODE = ID_NET_MODE + 3, ID_GPU_MODE = ID_NET_MODE + 6,
           ID_MODE_END = ID_NET_MODE + 9,
           ID_COMBINE_COMPUTER, ID_COMBINE_PROJECT, ID_COMBINE_APP, ID_COMBINE_STATUS,
           ID_PROJECT_SIDEBAR };

    // A run mode submenu: Always / Based on preferences / Never, the three the
    // client accepts for each of network, CPU and GPU.
    wxMenu* ModeMenu(int baseId)
    {
        auto* m = new wxMenu;
        m->Append(baseId + 0, "&Always");
        m->Append(baseId + 1, "&Based on preferences");
        m->Append(baseId + 2, "&Never");
        return m;
    }

    void BuildMenu()
    {
        // Menu bar follows the Windows app item for item: File, View, Computer,
        // Show, Projects, Extra, Help. Entries that need something this port
        // does not have yet are present but disabled, so the layout still
        // matches what a Windows user is looking for.
        auto* menuFile = new wxMenu;
        m_clientStart = menuFile->Append(ID_CLIENT_START, "Start BOINC Client (localhost)");
        m_clientStop  = menuFile->Append(ID_CLIENT_STOP,  "Stop BOINC Client (localhost)");
        menuFile->AppendSeparator();
        menuFile->Append(ID_COLOURS_READ,  "Read color settings...");
        menuFile->Append(ID_COLOURS_WRITE, "Write color settings...");
        menuFile->AppendSeparator();
        menuFile->Append(wxID_EXIT, "E&xit\tCtrl-Q");

        auto* menuView = new wxMenu;
        menuView->Append(wxID_ANY, "Toolbar graphic")->Enable(false);
        menuView->Append(wxID_ANY, "Toolbar operation")->Enable(false);
        menuView->Append(wxID_ANY, "Toolbar allow")->Enable(false);
        menuView->Append(wxID_ANY, "Selection bar")->Enable(false);
        menuView->AppendCheckItem(ID_SIDEBAR, "Sidebar computer selection");
        menuView->Check(ID_SIDEBAR, true);
        menuView->AppendCheckItem(ID_PROJECT_SIDEBAR, "Sidebar project selection");
        menuView->Check(ID_PROJECT_SIDEBAR, gSettings.projectSidebar);
        menuView->AppendCheckItem(ID_STATUSBAR, "Status bar");
        menuView->Check(ID_STATUSBAR, true);
        menuView->AppendSeparator();
        menuView->Append(ID_REFRESH_NOW, "&Refresh now\tF5");
        menuView->Append(ID_FIT_TREE, "&Fit computer list to names");

        auto* menuComputer = new wxMenu;
        menuComputer->Append(ID_FIND_COMPUTERS, "&Find computers...");
        menuComputer->Append(ID_ADD_COMPUTER, "&Add computer...\tCtrl-N");
        menuComputer->Append(ID_REMOVE_COMPUTER, "&Remove computer");

        auto* menuShow = new wxMenu;
        menuShow->Append(ID_SHOW_COMPUTERS, "Computers\tCtrl+Shift+C");
        menuShow->Append(ID_SHOW_PROJECTS,  "Projects\tCtrl+Shift+P");
        menuShow->Append(ID_SHOW_TASKS,     "Tasks\tCtrl+Shift+T");
        menuShow->Append(ID_SHOW_TRANSFERS, "Transfers\tCtrl+Shift+X");
        menuShow->Append(ID_SHOW_MESSAGES,  "Messages\tCtrl+Shift+M");
        menuShow->Append(ID_SHOW_HISTORY,   "History\tCtrl+Shift+H");
        menuShow->Append(ID_SHOW_LONGHIST,  "Long term History\tCtrl+Shift+L");
        menuShow->Append(ID_SHOW_NOTICES,   "Notices\tCtrl+Shift+N");
        menuShow->AppendSeparator();
        menuShow->Append(ID_GRAPH_STATS,    "Statistics graph\tCtrl+Shift+S");
        menuShow->Append(ID_GRAPH_TASKS,    "Tasks graph");
        // temperature comes from TThrottle, which is Windows-only
        menuShow->Append(ID_GRAPH_TEMP,     "Temperature graph")->Enable(false);
        menuShow->Append(ID_GRAPH_XFER,     "Data transfer graph");
        menuShow->Append(ID_GRAPH_DEADLINE, "Deadline graph");
        menuShow->AppendSeparator();
        menuShow->Append(wxID_ANY, "TThrottle")->Enable(false);
        menuShow->AppendSeparator();
        menuShow->Append(ID_SHOW_LOG, "Log");
        menuShow->Append(ID_SHOW_RULESLOG, "Rules log");

        auto* menuProjects = new wxMenu;
        menuProjects->Append(ID_ADD_PROJECT, "Add a new project...");
        menuProjects->Append(ID_ACCOUNT_MGR, "Account manager...");
        menuProjects->Append(wxID_ANY, "Synchronize with")->Enable(false);
        menuProjects->Append(wxID_ANY, "Set debt")->Enable(false);
        menuProjects->AppendSeparator();
        // label carries the count, like "Report all completed tasks 330"
        m_reportItem = menuProjects->Append(ID_REPORT_ALL, "Report all completed tasks");
        m_reportItem->Enable(false);

        auto* menuExtra = new wxMenu;
        menuExtra->Append(ID_SETTINGS, "BoincTasks settings...");
        menuExtra->Append(ID_COLOURS_FONTS, "BoincTasks colors and fonts...");
        menuExtra->AppendSeparator();
        menuExtra->Append(ID_BOINC_PREFS, "BOINC preference...");
        menuExtra->Append(ID_PROXY, "BOINC proxy settings...");
        menuExtra->AppendSeparator();
        auto* menuCombine = new wxMenu;
        menuCombine->AppendCheckItem(ID_COMBINE_COMPUTER, "Computer");
        menuCombine->AppendCheckItem(ID_COMBINE_PROJECT,  "Project");
        menuCombine->AppendCheckItem(ID_COMBINE_APP,      "Application");
        menuCombine->AppendCheckItem(ID_COMBINE_STATUS,   "Status");
        menuCombine->Check(ID_COMBINE_COMPUTER, gSettings.combineComputer);
        menuCombine->Check(ID_COMBINE_PROJECT,  gSettings.combineProject);
        menuCombine->Check(ID_COMBINE_APP,      gSettings.combineApplication);
        menuCombine->Check(ID_COMBINE_STATUS,   gSettings.combineStatus);
        menuExtra->AppendSubMenu(menuCombine, "Filter (combine) tasks on");
        menuExtra->AppendCheckItem(ID_ONLY_ACTIVE, "Show only active tasks");
        menuExtra->AppendCheckItem(ID_SHOW_CPU,    "Show CPU tasks");
        menuExtra->AppendCheckItem(ID_SHOW_GPU,    "Show GPU tasks");
        menuExtra->AppendCheckItem(ID_SHOW_NCI,    "Show non CPU intensive tasks");
        menuExtra->Check(ID_ONLY_ACTIVE, gSettings.onlyActiveTasks);
        menuExtra->Check(ID_SHOW_CPU,    gSettings.showCpuTasks);
        menuExtra->Check(ID_SHOW_GPU,    gSettings.showGpuTasks);
        menuExtra->Check(ID_SHOW_NCI,    gSettings.showNciTasks);
        menuExtra->AppendSeparator();
        menuExtra->Append(ID_BENCHMARKS, "Run CPU benchmarks");
        menuExtra->AppendSeparator();
        menuExtra->Append(ID_EDIT_CC, "Edit config file (cc_config.xml)...");
        menuExtra->Append(ID_EDIT_APP, "Edit config file (app_config.xml)")->Enable(false);
        menuExtra->Append(ID_READ_CONFIG, "Read config files (cc/app_config.xml)");
        menuExtra->AppendSeparator();
        menuExtra->AppendSubMenu(ModeMenu(ID_NET_MODE), "Allow network communication");
        menuExtra->AppendSubMenu(ModeMenu(ID_RUN_MODE), "Allow to run");
        menuExtra->AppendSubMenu(ModeMenu(ID_GPU_MODE), "Allow to run GPU");

        auto* menuHelp = new wxMenu;
        menuHelp->Append(ID_HELP_MANUAL, "BoincTasks manual");
        menuHelp->Append(ID_HELP_FORUM,  "BoincTasks forum");
        menuHelp->Append(ID_HELP_BOINC,  "BOINC website");
        menuHelp->Append(wxID_ABOUT,     "About BoincTasks");
        menuHelp->AppendSeparator();
        menuHelp->Append(ID_HELP_UPDATE, "Update");

        auto* bar = new wxMenuBar;
        bar->Append(menuFile,     "&File");
        bar->Append(menuView,     "&View");
        bar->Append(menuComputer, "&Computer");
        bar->Append(menuShow,     "&Show");
        bar->Append(menuProjects, "&Projects");
        bar->Append(menuExtra,    "&Extra");
        bar->Append(menuHelp,     "&Help");
        SetMenuBar(bar);

        // ---- bindings ----
        Bind(wxEVT_MENU, &MainFrame::OnAddComputer, this, ID_ADD_COMPUTER);
        Bind(wxEVT_MENU, &MainFrame::OnRemoveComputer, this, ID_REMOVE_COMPUTER);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnFindComputers(); }, ID_FIND_COMPUTERS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Rebuild(); }, ID_REFRESH_NOW);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            gSettings.treeSashPos = 0;          // back to automatic
            gSettings.Save();
            FitTreePane(true);
        }, ID_FIT_TREE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnReportAll(); }, ID_REPORT_ALL);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnAccountManager(); }, ID_ACCOUNT_MGR);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnBoincSettings(); }, ID_BOINC_PREFS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(true); }, wxID_EXIT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnAbout(); }, wxID_ABOUT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SetRetention(); }, ID_HISTORY_RETENTION);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnSettings(); }, ID_SETTINGS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnSettings(); }, ID_COLOURS_FONTS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnAddProject(); }, ID_ADD_PROJECT);

        // File: local client control and colour import/export
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnClientPower(true); },  ID_CLIENT_START);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnClientPower(false); }, ID_CLIENT_STOP);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnColourFile(false); }, ID_COLOURS_READ);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnColourFile(true); },  ID_COLOURS_WRITE);

        // View toggles
        Bind(wxEVT_MENU, [this](wxCommandEvent& ev) {
            if (!ev.IsChecked()) { m_split->Unsplit(m_treeBook); return; }
            // 0 would make wx centre the sash; size it to the names instead
            m_split->SplitVertically(m_treeBook, m_book,
                                     gSettings.treeSashPos > 0 ? gSettings.treeSashPos : 220);
            FitTreePane();
        }, ID_SIDEBAR);
        Bind(wxEVT_MENU, [this](wxCommandEvent& ev) {
            GetStatusBar()->Show(ev.IsChecked());
            SendSizeEvent();
        }, ID_STATUSBAR);
        Bind(wxEVT_MENU, [this](wxCommandEvent& ev) {
            gSettings.projectSidebar = ev.IsChecked();
            gSettings.Save();
            if (gSettings.projectSidebar) {
                m_projSplit->SplitVertically(m_projBook, m_book, 200);
                m_projectNames.clear();          // force a fill
            } else {
                m_projSplit->Unsplit(m_projBook);
                m_selectedProject.Clear();       // filter off with the sidebar
            }
            Rebuild();
        }, ID_PROJECT_SIDEBAR);

        // Show: pages, then the five graphs
        const int pages[] = { PAGE_COMPUTERS, PAGE_PROJECTS, PAGE_TASKS, PAGE_TRANSFERS,
                              PAGE_MESSAGES, PAGE_HISTORY, PAGE_LONGHIST, PAGE_NOTICES,
                              PAGE_LOG, PAGE_RULESLOG };
        const int ids[]   = { ID_SHOW_COMPUTERS, ID_SHOW_PROJECTS, ID_SHOW_TASKS,
                              ID_SHOW_TRANSFERS, ID_SHOW_MESSAGES, ID_SHOW_HISTORY,
                              ID_SHOW_LONGHIST, ID_SHOW_NOTICES, ID_SHOW_LOG,
                              ID_SHOW_RULESLOG };
        for (unsigned i = 0; i < sizeof(ids)/sizeof(ids[0]); i++) {
            int page = pages[i];
            Bind(wxEVT_MENU, [this, page](wxCommandEvent&) { m_book->SetSelection(page); },
                 ids[i]);
        }
        struct { int id; BtGraphPanel::Kind kind; } graphs[] = {
            { ID_GRAPH_STATS,    BtGraphPanel::GRAPH_STATISTICS },
            { ID_GRAPH_TASKS,    BtGraphPanel::GRAPH_TASKS },
            { ID_GRAPH_XFER,     BtGraphPanel::GRAPH_TRANSFER },
            { ID_GRAPH_DEADLINE, BtGraphPanel::GRAPH_DEADLINE },
        };
        for (const auto& g : graphs) {
            BtGraphPanel::Kind k = g.kind;
            Bind(wxEVT_MENU, [this, k](wxCommandEvent&) {
                m_graphs->SetKind(k);
                m_book->SetSelection(PAGE_GRAPHS);
            }, g.id);
        }

        // Extra: task filters
        auto filter = [this](bool BtSettings::*field, int id) {
            Bind(wxEVT_MENU, [this, field](wxCommandEvent& ev) {
                gSettings.*field = ev.IsChecked();
                gSettings.Save();
                Rebuild();
            }, id);
        };
        filter(&BtSettings::combineComputer,    ID_COMBINE_COMPUTER);
        filter(&BtSettings::combineProject,     ID_COMBINE_PROJECT);
        filter(&BtSettings::combineApplication, ID_COMBINE_APP);
        filter(&BtSettings::combineStatus,      ID_COMBINE_STATUS);
        filter(&BtSettings::onlyActiveTasks, ID_ONLY_ACTIVE);
        filter(&BtSettings::showCpuTasks,    ID_SHOW_CPU);
        filter(&BtSettings::showGpuTasks,    ID_SHOW_GPU);
        filter(&BtSettings::showNciTasks,    ID_SHOW_NCI);

        // Extra: client operations
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnProxySettings(); }, ID_PROXY);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnBenchmarks(); },    ID_BENCHMARKS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnEditCcConfig(); },  ID_EDIT_CC);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnReadConfig(); },    ID_READ_CONFIG);
        Bind(wxEVT_MENU, [this](wxCommandEvent& ev) { OnModeChange(ev.GetId()); },
             ID_NET_MODE, ID_MODE_END);

        // Help
        Bind(wxEVT_MENU, [](wxCommandEvent&) {
            wxLaunchDefaultBrowser("https://efmer.com/boinctasks-manual/"); }, ID_HELP_MANUAL);
        Bind(wxEVT_MENU, [](wxCommandEvent&) {
            wxLaunchDefaultBrowser("https://efmer.com/forum/"); }, ID_HELP_FORUM);
        Bind(wxEVT_MENU, [](wxCommandEvent&) {
            wxLaunchDefaultBrowser("https://boinc.berkeley.edu/"); }, ID_HELP_BOINC);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            if (kUpdateUrl) { wxLaunchDefaultBrowser(kUpdateUrl); return; }
            wxMessageBox(wxString::Format(
                "%s %s\n\n"
                "This is not distributed from efmer.com - that site carries "
                "the Windows application - so there is nothing to check against "
                "yet. A download site for the Linux build is planned; until then "
                "update from wherever you got this package.", BTPP_NAME, kVersion),
                "Update", wxOK | wxICON_INFORMATION, this);
        }, ID_HELP_UPDATE);
    }

    // Page order in the notebook; the Show menu maps onto these.
    enum { PAGE_TASKS = 0, PAGE_PROJECTS, PAGE_TRANSFERS, PAGE_MESSAGES, PAGE_HISTORY,
           PAGE_NOTICES, PAGE_COMPUTERS, PAGE_GRAPHS, PAGE_LONGHIST, PAGE_LOG,
           PAGE_RULESLOG };
    enum { ID_RULE_TIMER = wxID_HIGHEST + 200, ID_ARCHIVE_TIMER };

    enum { ID_SHOW_TASKS = wxID_HIGHEST + 100, ID_SHOW_PROJECTS,
           ID_SHOW_TRANSFERS, ID_SHOW_MESSAGES, ID_SHOW_HISTORY, ID_SHOW_NOTICES,
           ID_SHOW_COMPUTERS, ID_SHOW_LONGHIST, ID_SHOW_LOG,
           ID_GRAPH_STATS, ID_GRAPH_TASKS, ID_GRAPH_TEMP, ID_GRAPH_XFER,
           ID_GRAPH_DEADLINE, ID_SHOW_RULESLOG };

    // The master project list is static; fetch it once in the background so the
    // Add-project dialog can offer names instead of demanding raw URLs.
    void LoadKnownProjects()
    {
        MainFrame* self = this;
        std::thread([self]() {
            RPC_CLIENT rpc;
            std::ifstream f((BtBoincDataDir() + wxFILE_SEP_PATH +
                             "gui_rpc_auth.cfg").mb_str());
            std::string pw;
            std::getline(f, pw);
            while (!pw.empty() && (pw.back() == '\n' || pw.back() == '\r')) pw.pop_back();
            if (rpc.init("127.0.0.1") != 0 || rpc.authorize(pw.c_str()) != 0) return;

            ALL_PROJECTS_LIST list;
            if (rpc.get_all_projects_list(list) != 0) return;

            std::vector<BtProjectChoice> choices;
            for (auto* p : list.projects) {
                BtProjectChoice c;
                c.name = wxString::FromUTF8(p->name.c_str());
                c.url  = wxString::FromUTF8(p->url.c_str());
                if (!c.name.IsEmpty() && !c.url.IsEmpty()) choices.push_back(std::move(c));
            }
            std::sort(choices.begin(), choices.end(),
                      [](const BtProjectChoice& a, const BtProjectChoice& b) {
                          return a.name.CmpNoCase(b.name) < 0;
                      });
            wxTheApp->CallAfter([self, choices]() { self->m_knownProjects = choices; });
        }).detach();
    }

    // ---- history ---------------------------------------------------------
    void OpenHistory()
    {
        wxString path = BtHistoryPath();
        if (!m_history_db.Open(path)) {
            SetStatusText("history: cannot open " + path, 1);
            return;
        }
        {   // retention (days); 0 keeps everything
            wxFileConfig cfg(BTPP_SHORT, "eFMer", BtConfigPath());
            m_retentionDays = gSettings.historyDays;
            (void)cfg;
        }
        if (gSettings.historyBackup) {
            // Copy before archiving or pruning, so the backup is the state as
            // it was found rather than what is left afterwards.
            wxString bak = path + ".bak";
            if (wxCopyFile(path, bak, true) && m_log)
                m_log->Append("", "history: backed up to " + bak);
        }

        // Archive first: moving to long term is how a row survives retention,
        // so it has to happen before the prune that would delete it.
        if (gSettings.longTermAfterDays > 0) {
            int moved = m_history_db.MoveToLongTerm(gSettings.longTermAfterDays);
            if (moved > 0 && m_log)
                m_log->Append("", wxString::Format(
                    "history: moved %d row(s) to long term", moved));
        }
        m_history_db.Prune(m_retentionDays);

        // first run: seed the local host from the client's own job_log files
        if (m_history_db.Count() == 0) {
            for (const auto& c : m_computers) {
                if (c.host != "127.0.0.1" && !c.host.Lower().Contains("localhost"))
                    continue;
                auto rows = BtReadJobLogs(c.name, BtBoincDataDir());
                if (!rows.empty()) m_history_db.Insert(rows);
            }
            // job_log files reach back months, so archive and prune what they
            // brought in rather than leaving it to the next start-up
            if (gSettings.longTermAfterDays > 0)
                m_history_db.MoveToLongTerm(gSettings.longTermAfterDays);
            m_history_db.Prune(m_retentionDays);
        }
    }

    void OnSettings()
    {
        // Hand each list view's columns to the dialog so its per-view tabs edit
        // the same state the header right-click menu does.
        std::vector<BtColumnPage> pages;
        auto addPage = [&pages](const wxString& title, BtListView* v) {
            BtColumnPage p;
            p.title = title;
            p.key   = v->ViewKey();
            p.names = v->ColumnNames();
            p.shown = v->ColumnsShown();
            pages.push_back(std::move(p));
        };
        addPage("Projects",  m_projects);
        {   // Tasks is a wxDataViewCtrl rather than a BtListView, so it can't go
            // through addPage, but it exposes the same column interface.
            BtColumnPage p;
            p.title = "Tasks";
            p.key   = m_tasks->ViewKey();
            p.names = m_tasks->ColumnNames();
            p.shown = m_tasks->ColumnsShown();
            pages.push_back(std::move(p));
        }
        addPage("Transfers", m_transfers);
        addPage("Messages",  m_messages);
        addPage("History",   m_history);
        addPage("Notices",   m_notices);

        BtSettingsDlg dlg(this, gSettings, m_engine.Rules(), pages);
        if (dlg.ShowModal() != wxID_OK) return;

        BtSaveRules(dlg.Rules());
        m_engine.SetRules(dlg.Rules());

        // Result() is what folds the checkbox state back into the column pages,
        // so it has to run before Columns() is read.
        BtSettings updated = dlg.Result();

        {   // push the edited column visibility back into the views
            const auto& edited = dlg.Columns();
            std::map<wxString, BtListView*> byKey = {
                { m_projects->ViewKey(),  m_projects },
                { m_transfers->ViewKey(), m_transfers },
                { m_messages->ViewKey(),  m_messages },
                { m_history->ViewKey(),   m_history },
                { m_notices->ViewKey(),   m_notices },
            };
            for (const auto& p : edited) {
                if (p.key == m_tasks->ViewKey()) { m_tasks->SetColumnsShown(p.shown); continue; }
                auto it = byKey.find(p.key);
                if (it != byKey.end()) it->second->SetColumnsShown(p.shown);
            }
        }

        bool pollChanged = updated.pollIntervalMs != gSettings.pollIntervalMs;
        bool histChanged = updated.historyDays    != gSettings.historyDays;
        gSettings = updated;
        gSettings.Save();

        for (BtListView* v : { (BtListView*)m_projects, (BtListView*)m_transfers,
                               (BtListView*)m_messages, (BtListView*)m_history,
                               (BtListView*)m_notices, (BtListView*)m_computersView,
                               (BtListView*)m_longHistory, (BtListView*)m_log,
                               (BtListView*)m_rulesLog })
            v->ApplyViewStyle();

        m_pollIntervalMs = gSettings.pollIntervalMs;
        m_retentionDays  = gSettings.historyDays;
        if (gSettings.longTermAfterDays > 0)
            m_history_db.MoveToLongTerm(gSettings.longTermAfterDays);
        if (histChanged) m_history_db.Prune(m_retentionDays);   // archive first
        m_historyKey.Clear();          // force the views to re-read
        if (pollChanged) {          // restart pollers at the new cadence
            StopPollers();
            StartPollers();
        }
        Rebuild();
    }

    void SetRetention()
    {
        long days = wxGetNumberFromUser(
            "Keep completed tasks for how many days?\n"
            "(0 = keep everything)", "Days", "History retention",
            m_retentionDays, 0, 3650, this);
        if (days < 0) return;                       // cancelled
        m_retentionDays = (int)days;
        wxFileConfig cfg(BTPP_SHORT, "eFMer", BtConfigPath());
        cfg.Write("/History/retention_days", (long)m_retentionDays);
        cfg.Flush();
        int removed = m_history_db.Prune(m_retentionDays);
        SetStatusText(wxString::Format("history: kept %d days, removed %d row(s)",
                                       m_retentionDays, removed), 1);
        Rebuild();
    }

    // ---- computer tree ---------------------------------------------------
    void RebuildTree()
    {
        m_tree->DeleteAllItems();
        wxTreeItemId root = m_tree->AddRoot("root");
        m_allItem = m_tree->AppendItem(root, "All computers", -1, -1,
                                       new BtTreeData("", ""));
        std::map<wxString, wxTreeItemId> groups;
        for (const auto& c : m_computers) {
            if (!c.enabled) continue;          // unticked on the Computers tab
            wxTreeItemId parent = m_allItem;
            if (!c.group.IsEmpty()) {
                auto it = groups.find(c.group);
                if (it == groups.end()) {
                    parent = m_tree->AppendItem(m_allItem, c.group, -1, -1,
                                                new BtTreeData("", c.group));
                    groups[c.group] = parent;
                } else {
                    parent = it->second;
                }
            }
            m_tree->AppendItem(parent, c.name, -1, -1, new BtTreeData(c.name, c.group));
        }
        m_tree->ExpandAll();
        m_tree->SelectItem(m_allItem);
        FitTreePane();
    }

    // Widen the tree pane to the longest visible name so hosts like
    // "garageepycy2:31423" aren't cut off. A sash the user dragged themselves
    // wins - gSettings.treeSashPos is 0 until they do.
    void FitTreePane(bool force = false)
    {
        if (!m_split || !m_split->IsSplit()) return;
        if (gSettings.treeSashPos > 0 && !force) {
            m_split->SetSashPosition(gSettings.treeSashPos);
            return;
        }

        int widest = 0;
        std::function<void(const wxTreeItemId&)> walk = [&](const wxTreeItemId& id) {
            wxRect r;
            if (m_tree->GetBoundingRect(id, r, true))       // text only, incl. indent
                widest = std::max(widest, r.GetRight());
            wxTreeItemIdValue cookie;
            for (wxTreeItemId c = m_tree->GetFirstChild(id, cookie); c.IsOk();
                 c = m_tree->GetNextChild(id, cookie))
                walk(c);
        };
        wxTreeItemId root = m_tree->GetRootItem();
        if (root.IsOk()) walk(root);
        if (widest <= 0) return;

        // room for the scrollbar and a little breathing space, but never so wide
        // that the pane crowds out the view next to it
        int want = widest + 40;
        int cap  = std::max(200, GetClientSize().x / 2);
        m_split->SetSashPosition(std::min(std::max(want, 160), cap));
    }

    // The single computer highlighted in the tree, empty for "All computers"
    // or a group node. Messages and History are per-computer views in the
    // Windows app, so they key off this rather than the whole selection.
    wxString TreeComputer() const
    {
        wxTreeItemId sel = m_tree->GetSelection();
        if (!sel.IsOk()) return wxString();
        auto* data = (BtTreeData*)m_tree->GetItemData(sel);
        return data ? data->computer : wxString();
    }

    // Rebuild the project sidebar only when the set of projects changes, so a
    // selection isn't lost every refresh.
    void RefreshProjectTree(const std::vector<BtProjectRow>& projects,
                            const std::vector<BtTaskRow>& tasks)
    {
        std::set<wxString> names;
        for (const auto& p : projects) if (!p.project.IsEmpty()) names.insert(p.project);
        for (const auto& t : tasks)    if (!t.project.IsEmpty()) names.insert(t.project);

        std::vector<wxString> sorted(names.begin(), names.end());
        if (sorted == m_projectNames) return;
        m_projectNames = sorted;

        m_projTree->DeleteAllItems();
        wxTreeItemId root = m_projTree->AddRoot("root");
        m_allProjects = m_projTree->AppendItem(root, "All projects");
        wxTreeItemId reselect;
        for (const auto& n : sorted) {
            wxTreeItemId id = m_projTree->AppendItem(m_allProjects, n);
            if (n == m_selectedProject) reselect = id;
        }
        m_projTree->ExpandAll();
        if (reselect.IsOk()) m_projTree->SelectItem(reselect);
        else {
            m_selectedProject.Clear();
            m_projTree->SelectItem(m_allProjects);
        }
    }

    // the project sidebar's filter; empty selection means everything
    bool IncludesProject(const wxString& project) const
    {
        return m_selectedProject.IsEmpty() || project == m_selectedProject;
    }

    // which computers the current tree selection covers
    bool Includes(const wxString& computer) const
    {
        wxTreeItemId sel = m_tree->GetSelection();
        if (!sel.IsOk()) return true;
        auto* data = (BtTreeData*)m_tree->GetItemData(sel);
        if (!data) return true;
        if (!data->computer.IsEmpty()) return computer == data->computer;
        if (!data->group.IsEmpty()) {                 // a group node
            for (const auto& c : m_computers)
                if (c.name == computer) return c.group == data->group;
            return false;
        }
        return true;                                  // "All computers"
    }

    // ---- pollers ---------------------------------------------------------
    void StartPollers()
    {
        // A few clients can be polled hard; a farm of hundreds cannot - each
        // poll pulls that client's whole task list. Scale the interval with the
        // number of clients and stagger their start so load stays even.
        int n = 0;
        for (const auto& c : m_computers) if (c.enabled) n++;
        int interval = m_pollIntervalMs;
        if (interval <= 0) {                       // 0 = automatic
            interval = 2000;
            if (n > 16)  interval = 5000;
            if (n > 64)  interval = 10000;
            if (n > 256) interval = 20000;
        }
        int idx = 0;
        for (const auto& c : m_computers) {
            if (!c.enabled) continue;          // not polled while unticked
            MainFrame* self = this;
            wxString name = c.name;
            int stagger = (n > 1) ? (int)((long long)idx * interval / n) : 0;
            m_pollers.push_back(std::make_unique<BtPoller>(c,
                [self, name](std::shared_ptr<BtSnapshot> snap) {
                    wxTheApp->CallAfter([self, name, snap]() {
                        auto prev = self->m_snapshots.find(name);
                        bool was = (prev != self->m_snapshots.end()) &&
                                   prev->second->connected;
                        if (self->m_log && was != snap->connected)
                            self->m_log->Append(name, snap->connected
                                ? "connected" : "lost connection: " + snap->error);
                        self->m_snapshots[name] = snap;
                        self->ScheduleRebuild();
                    });
                }, interval, stagger));
            m_pollerByName[c.name] = m_pollers.back().get();
            idx++;
        }
    }

    void StopPollers() { m_pollers.clear(); m_pollerByName.clear(); }

    // ---- operations ------------------------------------------------------
    // Commands run on the owning computer's poller thread: that thread owns the
    // RPC connection, so the GUI must never touch it directly.
    void OnTaskOp(const std::vector<BtTaskRow>& tasks, const wxString& op)
    {
        std::string sop(op.mb_str());
        int sent = 0;
        for (const auto& t : tasks) {
            auto it = m_pollerByName.find(t.computer);
            if (it == m_pollerByName.end()) continue;
            std::string name(t.name.mb_str()), url(t.projectUrl.mb_str());
            it->second->Post([name, url, sop](RPC_CLIENT& rpc) {
                RESULT r;
                r.name        = name;
                r.project_url = url;
                rpc.result_op(r, sop.c_str());
            });
            sent++;
        }
        SetStatusText(wxString::Format("%s: %d task(s)", op, sent), 1);
        if (m_log) m_log->Append("", wxString::Format("task %s: %d task(s)", op, sent));
    }

    void OnProjectOp(const std::vector<BtProjectRow>& projects, const wxString& op)
    {
        std::string sop(op.mb_str());
        int sent = 0;
        for (const auto& p : projects) {
            auto it = m_pollerByName.find(p.computer);
            if (it == m_pollerByName.end()) continue;
            std::string url(p.masterUrl.mb_str());
            it->second->Post([url, sop](RPC_CLIENT& rpc) {
                PROJECT pr;
                pr.master_url = url;
                rpc.project_op(pr, sop.c_str());
            });
            sent++;
        }
        SetStatusText(wxString::Format("%s: %d project(s)", op, sent), 1);
        if (m_log) m_log->Append("", wxString::Format("project %s: %d project(s)", op, sent));
    }

    // With many clients, snapshots stream in continuously; rebuilding the views
    // on each one would peg the UI thread. Coalesce into at most ~2 redraws/sec.
    void ScheduleRebuild()
    {
        if (m_rebuildPending) return;
        m_rebuildPending = true;
        CallAfter([this]() {
            if (!m_rebuildTimer.IsRunning()) m_rebuildTimer.StartOnce(500);
        });
    }

    // ---- merge every snapshot into the combined views --------------------
    void Rebuild()
    {
        // Most people leave this running minimised all day. Merging thousands of
        // rows into views nobody can see is pure waste, so skip it entirely
        // while iconised or hidden - pollers and rules keep running, and the
        // restore handler rebuilds immediately so nothing looks stale.
        if (IsIconized() || !IsShown()) { m_rebuildSkipped = true; return; }

        std::vector<BtTaskRow>     tasks;
        std::vector<BtProjectRow>  projects;
        std::vector<BtTransferRow> transfers;
        std::vector<BtMessageRow>  messages;
        std::vector<BtNoticeRow>   notices;
        std::vector<BtStatSeries>  stats;
        int connected = 0, total = 0;
        const int shownPage  = m_book ? m_book->GetSelection() : PAGE_TASKS;
        // Messages follow one computer at a time, like the Windows app, so at
        // most 2000 rows are merged instead of that many per host.
        const wxString oneComputer = TreeComputer();
        const bool wantMessages = (shownPage == PAGE_MESSAGES) && !oneComputer.IsEmpty();
        const bool wantStats    = (shownPage == PAGE_GRAPHS);

        for (const auto& kv : m_snapshots) {
            const auto& snap = kv.second;
            if (!Includes(kv.first)) continue;
            total++;
            if (snap->connected) connected++;
            if (!snap->completed.empty()) {
                m_history_db.Insert(snap->completed);
                const_cast<BtSnapshot*>(snap.get())->completed.clear();
            }
            tasks.insert(tasks.end(), snap->tasks.begin(), snap->tasks.end());
            for (const auto& pr : snap->projects) {
                wxString host = pr.masterUrl;
                host.Replace("https://", ""); host.Replace("http://", "");
                host = host.BeforeFirst('/');
                if (!host.IsEmpty() && !pr.project.IsEmpty())
                    m_projectNameByHost[host] = pr.project;
            }
            projects.insert(projects.end(), snap->projects.begin(), snap->projects.end());
            transfers.insert(transfers.end(), snap->transfers.begin(), snap->transfers.end());
            // Messages are the bulk of what a snapshot holds - up to 2000 per
            // computer - so only merge them when that page is showing.
            if (wantMessages && kv.first == oneComputer)
                messages.insert(messages.end(), snap->messages.begin(), snap->messages.end());
            notices.insert(notices.end(), snap->notices.begin(), snap->notices.end());
            if (wantStats)
                stats.insert(stats.end(), snap->stats.begin(), snap->stats.end());
        }

        {   // sample farm-wide counts for the tasks-over-time graph
            BtTaskSample sample;
            sample.time  = (double)wxDateTime::Now().GetTicks();
            sample.total = (int)tasks.size();
            for (const auto& t : tasks) {
                if (t.running) sample.running++;
                else if (t.status == "Ready to start") sample.ready++;
            }
            for (const auto& tr : transfers) {
                if (tr.speed <= 0) continue;
                if (tr.status.StartsWith("Upload")) sample.upBytesSec += tr.speed;
                else                                sample.downBytesSec += tr.speed;
            }
            if (m_samples.empty() || sample.time - m_samples.back().time >= 10) {
                m_samples.push_back(sample);
                if (m_samples.size() > 720) m_samples.pop_front();   // ~2h at 10s
            }
        }

        if (gSettings.projectSidebar) {
            RefreshProjectTree(projects, tasks);
            if (!m_selectedProject.IsEmpty()) {
                auto keepTasks = std::vector<BtTaskRow>();
                for (auto& t : tasks)
                    if (IncludesProject(t.project)) keepTasks.push_back(std::move(t));
                tasks.swap(keepTasks);

                auto keepProjects = std::vector<BtProjectRow>();
                for (auto& p : projects)
                    if (IncludesProject(p.project)) keepProjects.push_back(std::move(p));
                projects.swap(keepProjects);

                auto keepTransfers = std::vector<BtTransferRow>();
                for (auto& tr : transfers)
                    if (IncludesProject(tr.project)) keepTransfers.push_back(std::move(tr));
                transfers.swap(keepTransfers);

                // Messages and notices follow too. Client-level lines carry no
                // project - "Starting BOINC client", benchmarks and so on - and
                // they drop out here, which is the point of a project filter.
                auto keepMessages = std::vector<BtMessageRow>();
                for (auto& m : messages)
                    if (IncludesProject(m.project)) keepMessages.push_back(std::move(m));
                messages.swap(keepMessages);

                auto keepNotices = std::vector<BtNoticeRow>();
                for (auto& n : notices)
                    if (IncludesProject(n.project)) keepNotices.push_back(std::move(n));
                notices.swap(keepNotices);
            }
        }

        {   // Extra menu task filters. "Active" is the Windows sense: running,
            // or moving data, or waiting to be reported - not idle work sitting
            // in the queue.
            const auto& g = gSettings;
            if (g.onlyActiveTasks || !g.showCpuTasks || !g.showGpuTasks || !g.showNciTasks) {
                std::vector<BtTaskRow> keep;
                keep.reserve(tasks.size());
                for (auto& t : tasks) {
                    if (t.nonCpuIntensive) { if (!g.showNciTasks) continue; }
                    else if (t.isGpu)      { if (!g.showGpuTasks) continue; }
                    else                   { if (!g.showCpuTasks) continue; }
                    if (g.onlyActiveTasks &&
                        !(t.running || t.state == BTS_UPLOAD_DOWNLOAD ||
                          t.state == BTS_READY_TO_REPORT))
                        continue;
                    keep.push_back(std::move(t));
                }
                tasks.swap(keep);
            }
        }

        {   // Warnings: flag a project on a computer that has run low on work.
            // Windows colours the task-count cell; wxListCtrl colours by row,
            // so the whole row highlights instead.
            auto contains = [](const wxString& hay, const wxString& needle) {
                return needle.IsEmpty() || hay.Lower().Find(needle.Lower()) != wxNOT_FOUND;
            };
            for (auto& p : projects) {
                p.warning = false;
                for (const auto& slot : gSettings.warnSlots) {
                    if (!slot.active()) continue;
                    if (!contains(p.computer, slot.computer)) continue;
                    if (!contains(p.project,  slot.project))  continue;
                    if (slot.cpuTasks > 0 && p.cpuTasks < slot.cpuTasks) { p.warning = true; break; }
                    if (slot.gpuTasks > 0 && p.gpuTasks < slot.gpuTasks) { p.warning = true; break; }
                }
            }
        }

        {   // Projects > "Report all completed tasks N" - the count, and the
            // projects holding those tasks, both come from the current view
            m_reportTargets.clear();
            int ready = 0;
            for (const auto& t : tasks) {
                if (t.state != BTS_READY_TO_REPORT) continue;
                ready++;
                m_reportTargets.insert({ t.computer, t.projectUrl });
            }
            if (m_reportItem) {
                wxString label = ready > 0
                    ? wxString::Format("Report all completed tasks %d", ready)
                    : wxString("Report all completed tasks");
                if (m_reportItem->GetItemLabel() != label)
                    m_reportItem->SetItemLabel(label);
                m_reportItem->Enable(ready > 0);
            }
        }

        size_t taskCount = tasks.size();
        // Only the page on screen needs rebuilding. Feeding all ten of them
        // twice a second is most of this program's CPU time - Messages alone
        // merges thousands of rows, and the history pages hit SQLite. Switching
        // tabs forces a rebuild, so a hidden page is never stale when shown.
        const int page = shownPage;

        if (page == PAGE_TASKS) m_tasks->SetRows(std::move(tasks));
        if (page == PAGE_PROJECTS) {
            // Tasks a day / a week: no project reports these, so they come from
            // the completions this app has recorded. Recount every 60s rather
            // than on each redraw - two more GROUP BY queries twice a second
            // would be pure waste.
            double nowSec = (double)wxDateTime::Now().GetTicks();
            if (nowSec - m_rateCountedAt >= 60) {
                m_rateCountedAt = nowSec;
                m_perDay  = m_history_db.CountSince(nowSec - 86400);
                m_perWeek = m_history_db.CountSince(nowSec - 7 * 86400);
            }
            for (auto& p : projects) {
                // history stores the project host; the live rows carry the name
                wxString host = p.masterUrl;
                host.Replace("https://", ""); host.Replace("http://", "");
                host = host.BeforeFirst('/');
                for (const auto& key : { p.computer + "\x1f" + p.project,
                                         p.computer + "\x1f" + host }) {
                    auto d = m_perDay.find(key);
                    if (d != m_perDay.end()) p.perDay = d->second;
                    auto w = m_perWeek.find(key);
                    if (w != m_perWeek.end()) p.perWeek = w->second;
                }
            }
            m_projects->SetRows(projects);
        }
        if (page == PAGE_TRANSFERS) m_transfers->SetRows(transfers);
        if (page == PAGE_MESSAGES) {
            if (oneComputer.IsEmpty()) m_messages->ShowHint(
                "Select only one computer, or use the right mouse button, "
                "to select a computer");
            else m_messages->SetRows(messages);
        }
        if (page == PAGE_NOTICES)   m_notices->SetRows(notices);
        if (page == PAGE_GRAPHS)    m_graphs->SetTasks(tasks);

        if (page == PAGE_GRAPHS) {   // graphs: merge per-computer series, then feed the panel
            std::map<wxString, BtStatSeries> merged;
            for (const auto& sr : stats) {
                // get_statistics reports the master URL; use the friendly name
                wxString name = sr.project;
                wxString host = name;
                host.Replace("https://", ""); host.Replace("http://", "");
                host = host.BeforeFirst('/');
                auto it = m_projectNameByHost.find(host);
                if (it != m_projectNameByHost.end()) name = it->second;

                auto& m = merged[name];
                m.project = name;
                for (const auto& pt : sr.points) m.points.push_back(pt);
            }
            std::vector<BtStatSeries> series;
            for (auto& kv : merged) series.push_back(std::move(kv.second));
            m_graphs->SetStatistics(series);
            m_graphs->SetSamples(m_samples);
        }
        {   // the Windows app shows the notice count in the tab label
            wxString label = notices.empty() ? "Notices"
                           : wxString::Format("%zu Notices", notices.size());
            if (m_noticesPage >= 0 && m_book->GetPageText(m_noticesPage) != label)
                m_book->SetPageText(m_noticesPage, label);
        }

        if (page == PAGE_COMPUTERS) {   // configured hosts with live status
            std::map<wxString, BtSnapshot*> status;
            for (auto& kv : m_snapshots) status[kv.first] = kv.second.get();
            m_computersView->SetRows(m_computers, status);
        }

        if (page == PAGE_HISTORY || page == PAGE_LONGHIST) {
            // Also one computer at a time, and only while the page is in front:
            // the long-term store can hold a lot of rows.
            // Unlike Messages, History combines every computer the selection
            // covers - that is what the Windows app does. It also refreshes on
            // a slow cadence there, so throttle rather than query SQLite twice
            // a second; a change of page or selection re-queries at once.
            HistoryView* view = (page == PAGE_LONGHIST) ? m_longHistory : m_history;
            std::vector<wxString> shown;
            for (const auto& c : m_computers)
                if (Includes(c.name)) shown.push_back(c.name);

            // Re-query only when something could have changed: a different
            // page or selection, or new rows in the store. The row count is a
            // cheap proxy - without it this re-reads and re-formats the whole
            // window every 10 seconds forever, which starts to hitch once the
            // history runs to hundreds of thousands of rows.
            long long rows = m_history_db.Count();
            wxString sel = wxString::Format("%d|%s|", page, m_selectedProject);
            for (const auto& n : shown) sel += n + ";";
            wxString key = sel + wxString::Format("%lld", rows);
            double nowSec = (double)wxDateTime::Now().GetTicks();

            // A new page or selection re-queries at once; new rows arriving wait
            // for the throttle so a busy farm can't drive it continuously.
            bool selChanged = (sel != m_historySel);
            bool dataChanged = (key != m_historyKey) &&
                               (nowSec - m_historyQueriedAt >= 10);
            if (selChanged || dataChanged) {
                m_historySel = sel;
                m_historyKey = key;
                m_historyQueriedAt = nowSec;
                // With a project filter on, pull a wider window: the row limit
                // applies before filtering, so a narrow one would leave almost
                // nothing for a project that finishes rarely.
                bool filtered = gSettings.projectSidebar && !m_selectedProject.IsEmpty();
                int limit = (page == PAGE_LONGHIST || filtered) ? 50000 : 5000;
                auto hist = m_history_db.Query(shown, limit, page == PAGE_LONGHIST);
                for (auto& h : hist) {
                    auto it = m_projectNameByHost.find(h.project);
                    if (it != m_projectNameByHost.end()) h.project = it->second;
                }
                if (filtered) {
                    // filter on the display name, after the host -> name mapping
                    std::vector<BtHistoryRow> keep;
                    for (auto& h : hist)
                        if (IncludesProject(h.project)) keep.push_back(std::move(h));
                    hist.swap(keep);
                }
                view->SetRows(hist);
            }
        }

        if (m_clientStart && m_clientStop) {
            // Start/Stop only make sense for a localhost entry, and only in the
            // direction the client isn't already in.
            bool haveLocal = false, localUp = false;
            for (const auto& c : m_computers) {
                if (c.host != "localhost" && c.host != "127.0.0.1") continue;
                haveLocal = true;
                auto it = m_snapshots.find(c.name);
                if (it != m_snapshots.end() && it->second->connected) localUp = true;
            }
            m_clientStart->Enable(haveLocal && !localUp);
            m_clientStop->Enable(haveLocal && localUp);
        }

        SetStatusText(wxString::Format("%d of %d computers connected", connected, total), 0);
        SetStatusText(wxString::Format("%zu Tasks, %zu Projects, %zu Transfers",
                                       taskCount, projects.size(), transfers.size()), 1);
    }

    // ---- menu handlers ---------------------------------------------------
    // "Report all completed tasks". The client has no report-now RPC: a project
    // update forces a scheduler request, and that request carries the finished
    // results. So update every project that is currently holding one.
    void OnReportAll()
    {
        if (m_reportTargets.empty()) return;
        int sent = 0;
        for (const auto& target : m_reportTargets) {
            auto it = m_pollerByName.find(target.first);
            if (it == m_pollerByName.end()) continue;
            std::string url(target.second.mb_str());
            it->second->Post([url](RPC_CLIENT& rpc) {
                PROJECT pr;
                pr.master_url = url;
                rpc.project_op(pr, "update");
            });
            sent++;
        }
        SetStatusText(wxString::Format("reporting completed tasks: %d project(s)", sent), 1);
        if (m_log)
            m_log->Append("", wxString::Format(
                "report all completed tasks: updating %d project(s)", sent));
    }

    // ---- rules -----------------------------------------------------------
    void RunRules()
    {
        for (const auto& kv : m_snapshots)
            if (kv.second->connected) m_everConnected[kv.first] = true;

        // Build the engine's view here rather than caching one every redraw:
        // rules run every 30 seconds, redraws happen twice a second.
        m_lastTasks.clear();
        m_lastProjects.clear();
        for (const auto& kv : m_snapshots) {
            const auto& snap = kv.second;
            m_lastTasks.insert(m_lastTasks.end(), snap->tasks.begin(), snap->tasks.end());
            m_lastProjects.insert(m_lastProjects.end(),
                                  snap->projects.begin(), snap->projects.end());
        }

        BtRuleEngine::World world;
        world.now      = (double)wxDateTime::Now().GetTicks();
        world.tasks    = &m_lastTasks;
        world.projects = &m_lastProjects;
        for (const auto& kv : m_snapshots) world.connected[kv.first] = kv.second->connected;
        world.everConnected = m_everConnected;

        for (const auto& act : m_engine.Evaluate(world)) DispatchRule(act);
    }

    void DispatchRule(const BtRuleAction& act)
    {
        wxString what = wxString(BtRuleEventName(act.event));
        wxString line = act.ruleName + ": " + what + " - " + act.text;
        if (m_rulesLog) m_rulesLog->Append(act.computer, line);

        if (act.show == BTSHOW_NOTICE) {
            wxNotificationMessage note("BoincTasks rule: " + act.ruleName, line, this);
            note.Show(10);
        }

        if (act.event == BTE_RUN_PROGRAM) {
            if (!act.program.IsEmpty()) wxExecute(act.program, wxEXEC_ASYNC);
            return;
        }

        // Everything else is a client operation on the computers the rule
        // matched - one named host, or all of them when the rule left it blank.
        int sent = 0;
        for (const auto& c : m_computers) {
            if (!act.computer.IsEmpty() && c.name != act.computer) continue;
            auto it = m_pollerByName.find(c.name);
            if (it == m_pollerByName.end()) continue;

            int    event   = act.event;
            double snooze  = act.snoozeMinutes * 60.0;
            std::string url(act.projectUrl.mb_str());
            std::string task(act.taskName.mb_str());
            it->second->Post([event, snooze, url, task](RPC_CLIENT& rpc) {
                PROJECT pr;
                pr.master_url = url;
                switch (event) {
                    case BTE_SUSPEND_PROJECT: rpc.project_op(pr, "suspend"); break;
                    case BTE_RESUME_PROJECT:  rpc.project_op(pr, "resume"); break;
                    case BTE_NO_NEW_WORK:     rpc.project_op(pr, "nomorework"); break;
                    case BTE_ALLOW_NEW_WORK:  rpc.project_op(pr, "allowmorework"); break;
                    case BTE_SUSPEND_TASK: {
                        RESULT r;
                        r.name        = task;
                        r.project_url = url;
                        rpc.result_op(r, "suspend");
                        break;
                    }
                    // Snooze is the client's own idea of a timed suspend: run
                    // mode "never" with a duration, which expires by itself.
                    case BTE_SNOOZE:            rpc.set_run_mode(3, snooze); break;
                    case BTE_CANCEL_SNOOZE:     rpc.set_run_mode(2, 0); break;
                    case BTE_SNOOZE_GPU:        rpc.set_gpu_mode(3, snooze); break;
                    case BTE_CANCEL_SNOOZE_GPU: rpc.set_gpu_mode(2, 0); break;
                    case BTE_SUSPEND_NETWORK:   rpc.set_network_mode(3, snooze); break;
                    case BTE_RESUME_NETWORK:    rpc.set_network_mode(2, 0); break;
                }
            });
            sent++;
        }
        SetStatusText(wxString::Format("rule \"%s\": %s on %d computer(s)",
                                       act.ruleName, what, sent), 1);
        if (m_log) m_log->Append(act.computer, "rule " + act.ruleName + ": " + what);
    }

    // A field edited directly in the Computers list. Anything that changes how
    // we reach the client restarts its poller; a rename carries the snapshot
    // and history key across.
    void OnComputerEdit(const wxString& name, int col, const wxString& value)
    {
        BtComputer* target = nullptr;
        for (auto& c : m_computers) if (c.name == name) target = &c;
        if (!target) return;

        bool reconnect = false;
        switch (col) {
            case ComputersView::COL_GROUP: target->group = value; break;
            case ComputersView::COL_NAME: {
                if (value.IsEmpty() || value == name) return;
                for (const auto& c : m_computers)
                    if (c.name == value) {
                        wxMessageBox("A computer called \"" + value + "\" already exists.",
                                     "Computers", wxOK | wxICON_WARNING, this);
                        return;
                    }
                auto snap = m_snapshots.find(name);
                if (snap != m_snapshots.end()) {
                    m_snapshots[value] = snap->second;
                    m_snapshots.erase(snap);
                }
                target->name = value;
                reconnect = true;
                break;
            }
            case ComputersView::COL_HOST: target->host = value; reconnect = true; break;
            case ComputersView::COL_PORT: {
                long p = 0;
                if (!value.ToLong(&p) || p < 1 || p > 65535) return;
                target->port = p; reconnect = true;
                break;
            }
            case ComputersView::COL_PASSWORD:
                // blank means "leave it alone" - the cell only ever shows a mask
                if (value.IsEmpty()) return;
                target->password = value; reconnect = true;
                break;
            default: return;
        }

        BtSaveComputers(m_computers);
        if (reconnect) { StopPollers(); StartPollers(); }
        RebuildTree();
        Rebuild();
    }

    void OnAbout()
    {
        wxMessageBox(wxString::Format(
                     "%s %s\n\n"
                     "Created by Skillz.\n\n"
                     "Based on BoincTasks by eFMer (Fred), the native Windows\n"
                     "application for managing BOINC across many computers.\n\n"
                     "Released under the GPLv3.", BTPP_NAME, kVersion),
                     "About " BTPP_NAME, wxOK | wxICON_INFORMATION, this);
    }

    // File > Start / Stop BOINC Client (localhost). Stopping goes through the
    // client's own quit RPC so it shuts down cleanly; starting has to come from
    // the service manager because there is no client to talk to yet.
    void OnClientPower(bool start)
    {
        if (start) {
#ifdef _WIN32
            wxString cmd = "net start boinc";        // the BOINC service
#else
            wxString cmd = "systemctl start boinc-client";
#endif
            if (wxExecute(cmd, wxEXEC_SYNC) != 0) {
                wxMessageBox("Could not start the local client.\n\n"
                             "Tried: " + cmd + "\n"
#ifdef _WIN32
                             "This usually needs an elevated prompt, or BOINC may "
                             "be installed to run as an application rather than a "
                             "service.",
#else
                             "This usually needs root, so run it yourself or give "
                             "your user permission for that unit.",
#endif
                             "Start BOINC Client", wxOK | wxICON_WARNING, this);
                return;
            }
            SetStatusText("local client: start requested", 1);
            return;
        }

        if (wxMessageBox("Stop the BOINC client on localhost?\n\n"
                         "Running tasks are checkpointed and suspended.",
                         "Stop BOINC Client", wxYES_NO | wxICON_EXCLAMATION, this) != wxYES)
            return;
        for (const auto& c : m_computers) {
            if (c.host != "localhost" && c.host != "127.0.0.1") continue;
            auto it = m_pollerByName.find(c.name);
            if (it == m_pollerByName.end()) continue;
            it->second->Post([](RPC_CLIENT& rpc) { rpc.quit(); });
            SetStatusText("local client: stop requested", 1);
            return;
        }
        wxMessageBox("No computer in the list points at localhost.",
                     "Stop BOINC Client", wxOK | wxICON_INFORMATION, this);
    }

    // File > Read / Write color settings - the palette only, so a scheme can be
    // shared between machines without carrying the rest of the settings.
    void OnColourFile(bool write)
    {
        wxFileDialog dlg(this, write ? "Write color settings" : "Read color settings",
                         wxGetHomeDir(), "boinctasks-colors.conf",
                         "Color settings (*.conf)|*.conf|All files (*)|*",
                         write ? (wxFD_SAVE | wxFD_OVERWRITE_PROMPT) : wxFD_OPEN);
        if (dlg.ShowModal() != wxID_OK) return;
        wxString path = dlg.GetPath();

        wxFileConfig cfg(BTPP_SHORT, "eFMer", path, "", wxCONFIG_USE_LOCAL_FILE);
        if (write) {
            for (int i = 0; i < BTS_COUNT; i++) {
                cfg.Write(wxString::Format("/Colours/cpu_%d", i),
                          gSettings.taskColour[i].GetAsString(wxC2S_HTML_SYNTAX));
                cfg.Write(wxString::Format("/Colours/gpu_%d", i),
                          gSettings.taskColourGpu[i].GetAsString(wxC2S_HTML_SYNTAX));
            }
            cfg.Write("/Colours/nonewwork",
                      gSettings.noNewWorkColour.GetAsString(wxC2S_HTML_SYNTAX));
            cfg.Flush();
            SetStatusText("colors written to " + path, 1);
            return;
        }

        auto read = [&](const wxString& key, wxColour& dest) {
            wxString v = cfg.Read(key, "");
            if (v.IsEmpty()) return;
            wxColour c(v);
            if (c.IsOk()) dest = c;
        };
        for (int i = 0; i < BTS_COUNT; i++) {
            read(wxString::Format("/Colours/cpu_%d", i), gSettings.taskColour[i]);
            read(wxString::Format("/Colours/gpu_%d", i), gSettings.taskColourGpu[i]);
        }
        read("/Colours/nonewwork", gSettings.noNewWorkColour);
        gSettings.Save();
        Rebuild();
        SetStatusText("colors read from " + path, 1);
    }

    // Extra > Allow network communication / Allow to run / Allow to run GPU.
    // Each is Always / Based on preferences / Never, applied to every computer
    // the tree selection covers.
    void OnModeChange(int id)
    {
        int offset = id - ID_NET_MODE;
        int which  = offset / 3;                 // 0 net, 1 cpu, 2 gpu
        int mode   = (offset % 3) + 1;           // RUN_MODE_ALWAYS/AUTO/NEVER
        const char* what = which == 0 ? "network" : which == 1 ? "run" : "GPU";

        int sent = 0;
        for (const auto& c : m_computers) {
            if (!Includes(c.name)) continue;
            auto it = m_pollerByName.find(c.name);
            if (it == m_pollerByName.end()) continue;
            it->second->Post([which, mode](RPC_CLIENT& rpc) {
                if (which == 0)      rpc.set_network_mode(mode, 0);
                else if (which == 1) rpc.set_run_mode(mode, 0);
                else                 rpc.set_gpu_mode(mode, 0);
            });
            sent++;
        }
        const char* modeName = mode == 1 ? "always" : mode == 2 ? "preferences" : "never";
        wxString msg = wxString::Format("%s mode -> %s on %d computer(s)",
                                        what, modeName, sent);
        SetStatusText(msg, 1);
        if (m_log) m_log->Append("", msg);
    }

    void OnBenchmarks()
    {
        int sent = 0;
        for (const auto& c : m_computers) {
            if (!Includes(c.name)) continue;
            auto it = m_pollerByName.find(c.name);
            if (it == m_pollerByName.end()) continue;
            it->second->Post([](RPC_CLIENT& rpc) { rpc.run_benchmarks(); });
            sent++;
        }
        SetStatusText(wxString::Format("benchmarks started on %d computer(s)", sent), 1);
    }

    void OnReadConfig()
    {
        int sent = 0;
        for (const auto& c : m_computers) {
            if (!Includes(c.name)) continue;
            auto it = m_pollerByName.find(c.name);
            if (it == m_pollerByName.end()) continue;
            it->second->Post([](RPC_CLIENT& rpc) { rpc.read_cc_config(); });
            sent++;
        }
        SetStatusText(wxString::Format("re-read config on %d computer(s)", sent), 1);
    }

    // Extra > Edit config file (cc_config.xml). The client hands over the file
    // as text and takes it back the same way, so this works on remote hosts too
    // - no file access needed.
    void OnEditCcConfig()
    {
        wxString name = TargetComputer();
        if (name.IsEmpty()) {
            wxMessageBox("Select a computer first.", "cc_config.xml",
                         wxOK | wxICON_INFORMATION, this);
            return;
        }
        auto it = m_pollerByName.find(name);
        if (it == m_pollerByName.end()) return;

        MainFrame* self = this;
        BtPoller* poller = it->second;
        poller->Post([self, poller, name](RPC_CLIENT& rpc) {
            CC_CONFIG cfg; LOG_FLAGS flags;
            auto text = std::make_shared<std::string>();
            int rc = rpc.get_cc_config_raw(cfg, flags, *text);
            wxTheApp->CallAfter([self, poller, name, text, rc]() {
                if (rc != 0) {
                    wxMessageBox("Could not read cc_config.xml from " + name + ".",
                                 "cc_config.xml", wxOK | wxICON_ERROR, self);
                    return;
                }
                self->ShowConfigEditor(poller, name, wxString(text->c_str(), wxConvUTF8));
            });
        });
    }

    void ShowConfigEditor(BtPoller* poller, const wxString& name, const wxString& text)
    {
        wxDialog dlg(this, wxID_ANY, "cc_config.xml - " + name, wxDefaultPosition,
                     wxSize(760, 600), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
        auto* edit = new wxTextCtrl(&dlg, wxID_ANY, text, wxDefaultPosition,
                                    wxDefaultSize, wxTE_MULTILINE | wxTE_DONTWRAP);
        edit->SetFont(wxFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE)));
        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(edit, 1, wxEXPAND | wxALL, 10);
        top->Add(new wxStaticText(&dlg, wxID_ANY,
            "Saving writes the file on that computer and tells the client to re-read it."),
            0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
        top->Add(dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);
        dlg.SetSizer(top);
        if (dlg.ShowModal() != wxID_OK) return;
        if (edit->GetValue() == text) return;

        auto body = std::make_shared<std::string>(edit->GetValue().mb_str());
        MainFrame* self = this;
        poller->Post([self, name, body](RPC_CLIENT& rpc) {
            bool ok = rpc.set_cc_config_raw(body.get()) == 0 && rpc.read_cc_config() == 0;
            wxString msg = ok ? "cc_config.xml saved" : "cc_config.xml: write failed";
            wxTheApp->CallAfter([self, name, msg]() {
                self->SetStatusText(name + ": " + msg, 1);
                if (self->m_log) self->m_log->Append(name, msg);
            });
        });
    }

    void OnProxySettings()
    {
        wxString name = TargetComputer();
        if (name.IsEmpty()) {
            wxMessageBox("Select a computer first.", "BOINC proxy settings",
                         wxOK | wxICON_INFORMATION, this);
            return;
        }
        auto it = m_pollerByName.find(name);
        if (it == m_pollerByName.end()) return;

        MainFrame* self = this;
        BtPoller* poller = it->second;
        poller->Post([self, poller, name](RPC_CLIENT& rpc) {
            auto info = std::make_shared<GR_PROXY_INFO>();
            int rc = rpc.get_proxy_settings(*info);
            wxTheApp->CallAfter([self, poller, name, info, rc]() {
                if (rc != 0) {
                    wxMessageBox("Could not read proxy settings from " + name + ".",
                                 "BOINC proxy settings", wxOK | wxICON_ERROR, self);
                    return;
                }
                self->ShowProxyDialog(poller, name, *info);
            });
        });
    }

    void ShowProxyDialog(BtPoller* poller, const wxString& name, const GR_PROXY_INFO& cur)
    {
        wxDialog dlg(this, wxID_ANY, "BOINC proxy settings - " + name);
        auto* grid = new wxFlexGridSizer(2, 8, 8);
        grid->AddGrowableCol(1, 1);
        auto field = [&](const wxString& label, const wxString& value, long style = 0) {
            grid->Add(new wxStaticText(&dlg, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
            auto* c = new wxTextCtrl(&dlg, wxID_ANY, value, wxDefaultPosition,
                                     wxSize(280, -1), style);
            grid->Add(c, 1, wxEXPAND);
            return c;
        };
        auto* useHttp = new wxCheckBox(&dlg, wxID_ANY, "Use an HTTP proxy");
        useHttp->SetValue(cur.use_http_proxy);
        auto* httpHost = field("HTTP server",  wxString(cur.http_server_name.c_str(), wxConvUTF8));
        auto* httpPort = field("HTTP port",    wxString::Format("%d", cur.http_server_port));
        auto* httpUser = field("HTTP user",    wxString(cur.http_user_name.c_str(), wxConvUTF8));
        auto* httpPass = field("HTTP password",wxString(cur.http_user_passwd.c_str(), wxConvUTF8),
                               wxTE_PASSWORD);
        auto* useSocks = new wxCheckBox(&dlg, wxID_ANY, "Use a SOCKS proxy");
        useSocks->SetValue(cur.use_socks_proxy);
        auto* sockHost = field("SOCKS server",  wxString(cur.socks_server_name.c_str(), wxConvUTF8));
        auto* sockPort = field("SOCKS port",    wxString::Format("%d", cur.socks_server_port));
        auto* sockUser = field("SOCKS user",    wxString(cur.socks5_user_name.c_str(), wxConvUTF8));
        auto* sockPass = field("SOCKS password",wxString(cur.socks5_user_passwd.c_str(), wxConvUTF8),
                               wxTE_PASSWORD);

        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(useHttp, 0, wxLEFT | wxTOP, 14);
        top->Add(grid, 0, wxEXPAND | wxALL, 14);
        top->Add(useSocks, 0, wxLEFT, 14);
        top->Add(dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 14);
        dlg.SetSizerAndFit(top);
        if (dlg.ShowModal() != wxID_OK) return;

        GR_PROXY_INFO info = cur;
        long port = 0;
        info.use_http_proxy     = useHttp->GetValue();
        info.http_server_name   = std::string(httpHost->GetValue().mb_str());
        httpPort->GetValue().ToLong(&port); info.http_server_port = (int)port;
        info.http_user_name     = std::string(httpUser->GetValue().mb_str());
        info.http_user_passwd   = std::string(httpPass->GetValue().mb_str());
        info.use_socks_proxy    = useSocks->GetValue();
        info.socks_server_name  = std::string(sockHost->GetValue().mb_str());
        sockPort->GetValue().ToLong(&port); info.socks_server_port = (int)port;
        info.socks5_user_name   = std::string(sockUser->GetValue().mb_str());
        info.socks5_user_passwd = std::string(sockPass->GetValue().mb_str());

        MainFrame* self = this;
        poller->Post([self, name, info](RPC_CLIENT& rpc) mutable {
            wxString msg = rpc.set_proxy_settings(info) == 0
                         ? "proxy settings applied" : "proxy settings: write failed";
            wxTheApp->CallAfter([self, name, msg]() {
                self->SetStatusText(name + ": " + msg, 1);
                if (self->m_log) self->m_log->Append(name, msg);
            });
        });
    }

    // Which computer a per-host command applies to: the tree selection when it
    // names one, otherwise the row selected on the Computers tab.
    wxString TargetComputer()
    {
        wxTreeItemId sel = m_tree->GetSelection();
        if (sel.IsOk()) {
            auto* data = (BtTreeData*)m_tree->GetItemData(sel);
            if (data && !data->computer.IsEmpty()) return data->computer;
        }
        return m_computersView ? m_computersView->SelectedName() : wxString();
    }

    // Client preferences. The fetch and the write both run on the poller thread
    // that owns the connection; the dialog itself runs on the GUI thread in
    // between.
    void OnBoincSettings()
    {
        wxString name = TargetComputer();
        if (name.IsEmpty()) {
            wxMessageBox("Select a computer first - either in the tree on the left "
                         "or on the Computers tab.",
                         "BOINC Settings", wxOK | wxICON_INFORMATION, this);
            return;
        }
        auto it = m_pollerByName.find(name);
        if (it == m_pollerByName.end()) return;

        wxString host = name;
        for (const auto& c : m_computers)
            if (c.name == name) host = wxString::Format("%s:%ld", c.host, c.port);

        MainFrame* self = this;
        BtPoller* poller = it->second;
        poller->Post([self, poller, name, host](RPC_CLIENT& rpc) {
            auto prefs = std::make_shared<GLOBAL_PREFS>();
            auto mask  = std::make_shared<GLOBAL_PREFS_MASK>();
            auto apps  = std::make_shared<std::vector<wxString>>();
            // Working prefs are what the user should see - project web prefs
            // included. The override's own mask says which fields this computer
            // already pins locally; writing without it would drop them.
            int rc = rpc.get_global_prefs_working_struct(*prefs, *mask);
            auto existing = std::make_shared<GLOBAL_PREFS_MASK>();
            GLOBAL_PREFS overridePrefs;
            rpc.get_global_prefs_override_struct(overridePrefs, *existing);

            CC_CONFIG cfg; LOG_FLAGS flags;
            if (rpc.get_cc_config(cfg, flags) == 0)
                for (const auto& a : cfg.exclusive_apps) apps->push_back(wxString(a.c_str()));

            wxTheApp->CallAfter([self, poller, name, host, prefs, apps, existing, rc]() {
                if (rc != 0) {
                    self->SetStatusText(name + ": cannot read preferences", 1);
                    wxMessageBox("Could not read preferences from " + name + ".",
                                 "BOINC Settings", wxOK | wxICON_ERROR, self);
                    return;
                }
                self->ShowPrefsDialog(poller, name, host, *prefs, *apps, *existing);
            });
        });
        SetStatusText(name + ": reading preferences...", 1);
    }

    void ShowPrefsDialog(BtPoller* poller, const wxString& name, const wxString& host,
                         const GLOBAL_PREFS& current,
                         const std::vector<wxString>& apps,
                         const GLOBAL_PREFS_MASK& alreadyOverridden)
    {
        BtPrefsDlg dlg(this, name, host, current, apps);
        if (dlg.ShowModal() != wxID_OK) return;

        GLOBAL_PREFS edited = current;
        // Start from what this computer already overrides. set_global_prefs_-
        // override_struct rewrites the whole file from the mask, so anything
        // left out of it would be silently dropped.
        GLOBAL_PREFS_MASK mask = alreadyOverridden;
        bool prefsChanged = dlg.Apply(edited, mask);
        bool appsChanged  = dlg.ExclusiveAppsChanged();
        if (!prefsChanged && !appsChanged) {
            SetStatusText(name + ": no changes", 1);
            return;
        }

        std::vector<std::string> newApps;
        for (const auto& a : dlg.ExclusiveApps()) newApps.push_back(std::string(a.mb_str()));

        MainFrame* self = this;
        poller->Post([self, name, edited, mask, prefsChanged, appsChanged,
                      newApps](RPC_CLIENT& rpc) mutable {
            wxString result;
            if (prefsChanged) {
                if (rpc.set_global_prefs_override_struct(edited, mask) == 0 &&
                    rpc.read_global_prefs_override() == 0)
                    result = "preferences applied";
                else
                    result = "preferences: write failed";
            }
            if (appsChanged) {
                // read-modify-write: only the exclusive app list is replaced,
                // the rest of cc_config goes back as it came
                CC_CONFIG cfg; LOG_FLAGS flags;
                if (rpc.get_cc_config(cfg, flags) == 0) {
                    cfg.exclusive_apps = newApps;
                    bool ok = rpc.set_cc_config(cfg, flags, 0) == 0 &&
                              rpc.read_cc_config() == 0;
                    result += result.IsEmpty() ? "" : "; ";
                    result += ok ? "exclusive applications applied"
                                 : "exclusive applications: write failed";
                }
            }
            wxTheApp->CallAfter([self, name, result]() {
                self->SetStatusText(name + ": " + result, 1);
                if (self->m_log) self->m_log->Append(name, result);
            });
        });
    }

    void OnAccountManager()
    {
        wxDialog dlg(this, wxID_ANY, "Account manager");
        auto* grid = new wxFlexGridSizer(2, 8, 8);
        grid->AddGrowableCol(1, 1);
        auto field = [&](const wxString& label, long style = 0) {
            grid->Add(new wxStaticText(&dlg, wxID_ANY, label), 0,
                      wxALIGN_CENTER_VERTICAL);
            auto* c = new wxTextCtrl(&dlg, wxID_ANY, "", wxDefaultPosition,
                                     wxSize(320, -1), style);
            grid->Add(c, 1, wxEXPAND);
            return c;
        };
        auto* url  = field("URL");
        auto* user = field("Account / e-mail");
        auto* pass = field("Password", wxTE_PASSWORD);

        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(grid, 0, wxEXPAND | wxALL, 14);
        top->Add(new wxStaticText(&dlg, wxID_ANY,
            "Attaches the computers in the current selection to the account\n"
            "manager (BAM!, GridRepublic, ...). Leave all three blank to detach."),
            0, wxLEFT | wxRIGHT | wxBOTTOM, 14);
        top->Add(dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 14);
        dlg.SetSizerAndFit(top);
        if (dlg.ShowModal() != wxID_OK) return;

        std::string u(url->GetValue().mb_str()), n(user->GetValue().mb_str()),
                    p(pass->GetValue().mb_str());
        MainFrame* self = this;
        int sent = 0;
        for (const auto& c : m_computers) {
            if (!Includes(c.name)) continue;
            auto it = m_pollerByName.find(c.name);
            if (it == m_pollerByName.end()) continue;
            wxString name = c.name;
            it->second->Post([self, name, u, n, p](RPC_CLIENT& rpc) {
                wxString msg;
                if (rpc.acct_mgr_rpc(u.c_str(), n.c_str(), p.c_str()) != 0) {
                    msg = "account manager: request failed";
                } else {
                    // the attach runs asynchronously in the client; poll it out
                    ACCT_MGR_RPC_REPLY reply;
                    for (int i = 0; i < 30; i++) {
                        if (rpc.acct_mgr_rpc_poll(reply) != 0) break;
                        if (reply.error_num != -204 /* in progress */) break;
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    msg = reply.error_num == 0
                        ? "account manager: attached"
                        : wxString::Format("account manager: error %d", reply.error_num);
                    for (const auto& m : reply.messages) msg += "; " + wxString(m.c_str());
                }
                wxTheApp->CallAfter([self, name, msg]() {
                    if (self->m_log) self->m_log->Append(name, msg);
                    self->SetStatusText(name + ": " + msg, 1);
                });
            });
            sent++;
        }
        SetStatusText(wxString::Format("account manager: %d computer(s)", sent), 1);
    }

    void OnAddComputer(wxCommandEvent&)
    {
        AddComputerDlg dlg(this);
        if (dlg.ShowModal() != wxID_OK) return;
        BtComputer c = dlg.Result();
        if (c.host.IsEmpty()) return;
        StopPollers();
        m_computers.push_back(c);
        BtSaveComputers(m_computers);
        RebuildTree();
        StartPollers();
    }

    void OnRemoveComputer(wxCommandEvent&)
    {
        wxTreeItemId sel = m_tree->GetSelection();
        if (!sel.IsOk()) return;
        auto* data = (BtTreeData*)m_tree->GetItemData(sel);
        if (!data || data->computer.IsEmpty()) return;
        if (wxMessageBox(wxString::Format("Remove computer '%s'?", data->computer),
                "Remove computer", wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
        wxString name = data->computer;
        StopPollers();
        m_computers.erase(std::remove_if(m_computers.begin(), m_computers.end(),
            [&](const BtComputer& c) { return c.name == name; }), m_computers.end());
        m_snapshots.erase(name);
        BtSaveComputers(m_computers);
        RebuildTree();
        StartPollers();
        Rebuild();
    }

    void OnAddProject()
    {
        std::vector<wxString> names;
        for (const auto& c : m_computers) names.push_back(c.name);
        if (names.empty()) return;

        BtAddProjectDlg dlg(this, names, m_knownProjects);
        if (dlg.ShowModal() != wxID_OK) return;

        auto requests = dlg.Result();
        if (requests.empty()) {
            wxMessageBox("No project URL given.", "Add project",
                         wxOK | wxICON_INFORMATION, this);
            return;
        }

        MainFrame* self = this;
        for (const auto& r : requests) {
            auto it = m_pollerByName.find(r.computer);
            if (it == m_pollerByName.end()) continue;

            std::string url(r.url.mb_str()), name(r.projectName.mb_str());
            std::string email(r.email.mb_str()), pass(r.password.mb_str());
            std::string key(r.accountKey.mb_str());
            wxString computer = r.computer;

            it->second->Post([self, computer, url, name, email, pass, key](RPC_CLIENT& rpc) {
                wxString msg;
                std::string auth = key;

                if (auth.empty()) {
                    // exchange credentials for an authenticator, then attach
                    ACCOUNT_IN in;
                    in.url        = url;
                    in.email_addr = email;
                    in.passwd     = pass;
                    if (rpc.lookup_account(in) == 0) {
                        ACCOUNT_OUT out;
                        for (int i = 0; i < 60; i++) {      // ~30s of polling
                            int retval = rpc.lookup_account_poll(out);
                            if (retval == 0 && out.error_num != ERR_IN_PROGRESS) break;
                            wxMilliSleep(500);
                        }
                        if (out.error_num == 0 && !out.authenticator.empty())
                            auth = out.authenticator;
                        else
                            msg = wxString::Format("Account lookup failed: %s",
                                    out.error_msg.empty()
                                        ? wxString::Format("error %d", out.error_num)
                                        : wxString(out.error_msg));
                    } else {
                        msg = "Could not start account lookup.";
                    }
                }

                if (msg.IsEmpty() && !auth.empty()) {
                    if (rpc.project_attach(url.c_str(), auth.c_str(), name.c_str()) == 0) {
                        PROJECT_ATTACH_REPLY reply;
                        for (int i = 0; i < 60; i++) {
                            if (rpc.project_attach_poll(reply) == 0 &&
                                reply.error_num != ERR_IN_PROGRESS) break;
                            wxMilliSleep(500);
                        }
                        msg = reply.error_num == 0
                            ? wxString("attached")
                            : wxString::Format("attach failed (error %d)", reply.error_num);
                    } else {
                        msg = "Could not start attach.";
                    }
                } else if (msg.IsEmpty()) {
                    msg = "No account key and no credentials given.";
                }

                wxTheApp->CallAfter([self, computer, msg]() {
                    self->SetStatusText(computer + ": " + msg, 1);
                    if (msg != "attached")
                        wxMessageBox(computer + ": " + msg, "Add project",
                                     wxOK | wxICON_WARNING, self);
                });
            });
        }
        SetStatusText(wxString::Format("attaching on %zu computer(s)...",
                                       requests.size()), 1);
    }

    void OnTransferOp(const std::vector<BtTransferRow>& transfers, const wxString& op)
    {
        std::string sop(op.mb_str());
        int sent = 0;
        for (const auto& t : transfers) {
            auto it = m_pollerByName.find(t.computer);
            if (it == m_pollerByName.end()) continue;
            std::string name(t.file.mb_str()), url(t.projectUrl.mb_str());
            it->second->Post([name, url, sop](RPC_CLIENT& rpc) {
                FILE_TRANSFER ft;
                ft.name        = name;
                ft.project_url = url;
                rpc.file_transfer_op(ft, sop.c_str());
            });
            sent++;
        }
        SetStatusText(wxString::Format("%s: %d transfer(s)", op, sent), 1);
        if (m_log) m_log->Append("", wxString::Format("transfer %s: %d", op, sent));
    }

    void OnComputerOp(const std::vector<wxString>& names, const wxString& op)
    {
        if (op == "add") { wxCommandEvent e; OnAddComputer(e); return; }

        if (op == "remove") {
            StopPollers();
            for (const auto& n : names) {
                m_computers.erase(std::remove_if(m_computers.begin(), m_computers.end(),
                    [&](const BtComputer& c) { return c.name == n; }), m_computers.end());
                m_snapshots.erase(n);
            }
            BtSaveComputers(m_computers);
            RebuildTree();
            StartPollers();
            Rebuild();
            return;
        }

        if (op == "enable" || op == "disable") {
            bool on = (op == "enable");
            for (auto& c : m_computers)
                for (const auto& n : names)
                    if (c.name == n) c.enabled = on;
            BtSaveComputers(m_computers);
            if (!on)                              // drop its data straight away
                for (const auto& n : names) m_snapshots.erase(n);
            StopPollers();
            StartPollers();
            RebuildTree();
            Rebuild();
            return;
        }
        if (op == "scan") { OnFindComputers(); return; }

        if (op == "edit" && !names.empty()) {
            auto it = std::find_if(m_computers.begin(), m_computers.end(),
                [&](const BtComputer& c) { return c.name == names[0]; });
            if (it == m_computers.end()) return;

            AddComputerDlg dlg(this, &(*it));
            if (dlg.ShowModal() != wxID_OK) return;
            BtComputer updated = dlg.Result();
            if (updated.host.IsEmpty()) return;

            wxString oldName = it->name;
            StopPollers();
            *it = updated;
            if (oldName != updated.name) {          // keep snapshots keyed correctly
                auto snap = m_snapshots.find(oldName);
                if (snap != m_snapshots.end()) {
                    m_snapshots[updated.name] = snap->second;
                    m_snapshots.erase(snap);
                }
            }
            BtSaveComputers(m_computers);
            RebuildTree();
            StartPollers();
            Rebuild();
        }
    }

    void OnFindComputers()
    {
        wxTextEntryDialog rangeDlg(this,
            "Scan an address range for BOINC clients.\n"
            "Enter the first and last address, e.g. 192.168.1.1-254",
            "Find computers", "192.168.1.1-254");
        if (rangeDlg.ShowModal() != wxID_OK) return;

        wxString spec = rangeDlg.GetValue();
        wxString basePart = spec.BeforeLast('.');
        wxString tail     = spec.AfterLast('.');
        long first = 1, last = 254;
        if (tail.Contains("-")) {
            tail.BeforeFirst('-').ToLong(&first);
            tail.AfterFirst('-').ToLong(&last);
        } else {
            tail.ToLong(&first);
            last = first;
        }
        if (basePart.IsEmpty()) return;

        wxTextEntryDialog portDlg(this,
            "Port range to sweep on each address.\n"
            "Many farms run several instances per machine on consecutive ports,\n"
            "e.g. 31416-31430 or 31417-31800.",
            "Find computers", "31416-31430");
        if (portDlg.ShowModal() != wxID_OK) return;
        wxString portSpec = portDlg.GetValue();
        long portFirst = 31416, portLast = 31416;
        if (portSpec.Contains("-")) {
            portSpec.BeforeFirst('-').Trim().Trim(false).ToLong(&portFirst);
            portSpec.AfterFirst('-').Trim().Trim(false).ToLong(&portLast);
        } else {
            portSpec.Trim().Trim(false).ToLong(&portFirst);
            portLast = portFirst;
        }

        wxTextEntryDialog pwDlg(this,
            "GUI RPC password for the discovered clients\n(leave empty if none):",
            "Find computers", "");
        if (pwDlg.ShowModal() != wxID_OK) return;
        wxString password = pwDlg.GetValue();

        wxProgressDialog progress("Find computers",
            wxString::Format("Scanning %s.%ld-%ld ports %ld-%ld ...",
                             basePart, first, last, portFirst, portLast),
            (int)(last - first + 1), this,
            wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME);

        auto found = BtScanRange(basePart, (int)first, (int)last,
            portFirst, portLast, password,
            [&](int done, int total) {
                progress.Update(done, wxString::Format("Scanned %d of %d", done, total));
                wxTheApp->Yield(true);
            });
        progress.Update((int)(last - first + 1));

        if (found.empty()) {
            wxMessageBox("No BOINC clients answered on that range.",
                         "Find computers", wxOK | wxICON_INFORMATION, this);
            return;
        }

        // let the user pick which of the discovered hosts to add
        BtScanResultsDlg picker(this, found);
        if (picker.ShowModal() != wxID_OK) return;
        auto chosen = picker.Selected();
        if (chosen.empty()) return;

        StopPollers();
        int added = 0;
        for (const auto& f : chosen) {
            BtComputer c;
            // one machine can host hundreds of instances: name them uniquely
            wxString label = f.hostname.IsEmpty() ? f.host : f.hostname;
            c.name     = (portFirst == portLast)
                       ? label : wxString::Format("%s:%ld", label, f.port);
            c.host     = f.host;
            c.port     = f.port;
            c.password = password;
            // no group: discovered hosts sit directly under "All computers",
            // the same as the Windows version
            bool dup = false;
            for (const auto& e : m_computers)
                if (e.host == c.host && e.port == c.port) dup = true;
            if (!dup) { m_computers.push_back(c); added++; }
        }
        BtSaveComputers(m_computers);
        RebuildTree();
        StartPollers();
        Rebuild();
        SetStatusText(wxString::Format("added %d computer(s)", added), 1);
        if (m_log) m_log->Append("", wxString::Format("added %d computer(s) from scan", added));
    }

    void OnClose(wxCloseEvent& ev)
    {
        // Remember the window for next launch. Save the restored size, not the
        // maximized one, so un-maximizing lands back where the user had it.
        gSettings.winMaximized = IsMaximized();
        if (!IsMaximized()) {
            wxSize sz = GetSize();
            gSettings.winW = sz.x;
            gSettings.winH = sz.y;
        }
        gSettings.Save();
        if (gSettings.stopClientOnExit) {
            for (const auto& c : m_computers) {
                if (c.host != "localhost" && c.host != "127.0.0.1") continue;
                auto it = m_pollerByName.find(c.name);
                if (it != m_pollerByName.end())
                    it->second->Post([](RPC_CLIENT& rpc) { rpc.quit(); });
                break;
            }
            wxMilliSleep(300);        // let the command reach the poller thread
        }
        StopPollers();
        ev.Skip();
    }

    wxTreeCtrl*    m_tree;
    wxTreeItemId   m_allItem;
    wxSplitterWindow* m_split = nullptr;
    wxNotebook*    m_treeBook = nullptr;   // holds the tree under a "Computers" tab
    wxSplitterWindow* m_projSplit = nullptr;
    wxNotebook*    m_projBook = nullptr;
    wxTreeCtrl*    m_projTree = nullptr;
    wxTreeItemId   m_allProjects;
    wxString       m_selectedProject;      // empty = all projects
    std::vector<wxString> m_projectNames;  // what the sidebar currently lists
    wxNotebook*    m_book;
    wxMenuItem*    m_reportItem  = nullptr;
    LogView*       m_rulesLog    = nullptr;
    BtRuleEngine   m_engine;
    wxTimer        m_ruleTimer;
    wxTimer        m_archiveTimer;
    std::map<wxString, bool> m_everConnected;
    wxString       m_historyKey;          // page + computers + row count
    wxString       m_historySel;          // page + computers only
    double         m_historyQueriedAt = 0;
    std::map<wxString, int> m_perDay, m_perWeek;
    double         m_rateCountedAt = 0;
    bool           m_rebuildSkipped = false;
    std::vector<BtTaskRow>    m_lastTasks;
    std::vector<BtProjectRow> m_lastProjects;
    wxMenuItem*    m_clientStart = nullptr;
    wxMenuItem*    m_clientStop  = nullptr;
    // (computer, project url) pairs currently holding a ready-to-report task
    std::set<std::pair<wxString, wxString>> m_reportTargets;
    TasksView*     m_tasks;
    ProjectsView*  m_projects;
    TransfersView* m_transfers;
    MessagesView*  m_messages;
    HistoryView*   m_history;
    NoticesView*   m_notices;
    ComputersView* m_computersView;
    GraphsView*    m_graphs;
    HistoryView*   m_longHistory;
    LogView*       m_log;
    std::deque<BtTaskSample> m_samples;
    int            m_noticesPage = -1;
    int            m_pollIntervalMs = 0;   // 0 = scale automatically
    bool           m_rebuildPending = false;
    wxTimer        m_rebuildTimer;
    BtHistory      m_history_db;
    int            m_retentionDays = 7;

    std::vector<BtComputer>                          m_computers;
    std::vector<std::unique_ptr<BtPoller>>           m_pollers;
    std::map<wxString, BtPoller*>                    m_pollerByName;
    std::map<wxString, wxString>                     m_projectNameByHost;
    std::vector<BtProjectChoice>                     m_knownProjects;
    std::map<wxString, std::shared_ptr<BtSnapshot>>  m_snapshots;
};

class BtApp : public wxApp
{
public:
    bool OnInit() override
    {
#ifndef _WIN32
        // A client dropping mid-RPC would otherwise kill the process. Windows
        // has no SIGPIPE; failed sends just return an error there.
        signal(SIGPIPE, SIG_IGN);
#endif
        (new MainFrame())->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(BtApp);
