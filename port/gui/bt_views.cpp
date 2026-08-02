#include "bt_views.h"
#include "bt_config.h"
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/datetime.h>
#include <algorithm>
#include "bt_settings.h"
#include "bt_taskinfo.h"
#include "bt_freedc.h"

#include <wx/fileconf.h>
#include <wx/utils.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/settings.h>

bool BtOpenUrl(wxWindow* parent, const wxString& url)
{
    if (url.IsEmpty()) return false;
    if (wxLaunchDefaultBrowser(url)) return true;

    // Nothing opened. Put the address somewhere usable rather than leaving the
    // click looking like it did nothing at all.
    wxString extra;
    if (wxTheClipboard && wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(url));
        wxTheClipboard->Close();
        extra = "\n\nIt has been copied to the clipboard.";
    }
    wxMessageBox("No web browser could be opened.\n\n" + url + extra,
                 "Open in browser", wxOK | wxICON_INFORMATION, parent);
    return false;
}

// ---- column visibility (shared by every list view) -------------------------
void BtListView::EnableColumnMenu(const wxString& viewKey)
{
    m_viewKey = viewKey;
    int n = GetColumnCount();
    m_colWidths.resize(n);
    m_colShown.assign(n, true);
    for (int i = 0; i < n; i++) m_colWidths[i] = GetColumnWidth(i);

    wxFileConfig cfg(BTPP_SHORT, "eFMer", BtConfigPath());
    for (int i = 0; i < n; i++)
        m_colShown[i] = cfg.ReadBool(wxString::Format("/Columns/%s/%d", viewKey, i), true);
    ApplyColumnVisibility();

    Bind(wxEVT_LIST_COL_RIGHT_CLICK, &BtListView::OnHeaderMenu, this);
}

void BtListView::ApplyColumnVisibility()
{
    for (size_t i = 0; i < m_colShown.size(); i++)
        SetColumnWidth((int)i, m_colShown[i] ? m_colWidths[i] : 0);
}

void BtListView::SetColumnsShown(const std::vector<bool>& shown)
{
    if (shown.size() != m_colShown.size()) return;
    m_colShown = shown;
    ApplyColumnVisibility();
    if (m_viewKey.IsEmpty()) return;
    wxFileConfig cfg(BTPP_SHORT, "eFMer", BtConfigPath());
    for (size_t i = 0; i < m_colShown.size(); i++)
        cfg.Write(wxString::Format("/Columns/%s/%zu", m_viewKey, i), (bool)m_colShown[i]);
    cfg.Flush();
}

void BtListView::OnHeaderMenu(wxListEvent&)
{
    if (m_viewKey.IsEmpty()) return;
    wxMenu menu;
    const int base = wxID_HIGHEST + 900;
    for (size_t i = 0; i < m_colShown.size(); i++) {
        wxListItem col;
        col.SetMask(wxLIST_MASK_TEXT);
        GetColumn((int)i, col);
        menu.AppendCheckItem(base + (int)i, col.GetText());
        menu.Check(base + (int)i, m_colShown[i]);
    }
    menu.Bind(wxEVT_MENU, [this, base](wxCommandEvent& e) {
        int idx = e.GetId() - base;
        if (idx < 0 || idx >= (int)m_colShown.size()) return;
        m_colShown[idx] = !m_colShown[idx];
        ApplyColumnVisibility();
        wxFileConfig cfg(BTPP_SHORT, "eFMer", BtConfigPath());
        cfg.Write(wxString::Format("/Columns/%s/%d", m_viewKey, idx),
                  (bool)m_colShown[idx]);
        cfg.Flush();
    });
    PopupMenu(&menu);
}

static wxString fmtSize(double bytes)
{
    if (bytes >= 1e9) return wxString::Format("%.2f GB", bytes / 1e9);
    if (bytes >= 1e6) return wxString::Format("%.2f MB", bytes / 1e6);
    if (bytes >= 1e3) return wxString::Format("%.2f KB", bytes / 1e3);
    return wxString::Format("%.0f B", bytes);
}

// ---- TasksView --------------------------------------------------------------
TasksView::TasksView(wxWindow* parent) : wxPanel(parent)
{
    m_dv = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                              wxDV_ROW_LINES | wxDV_VERT_RULES | wxDV_MULTIPLE);
    m_model = new BtTaskModel;
    m_dv->AssociateModel(m_model.get());

    m_dv->AppendTextColumn("Project",      BtTaskModel::COL_PROJECT,  wxDATAVIEW_CELL_INERT, 150);
    m_dv->AppendTextColumn("Application",  BtTaskModel::COL_APP,      wxDATAVIEW_CELL_INERT, 200);
    m_dv->AppendTextColumn("Name",         BtTaskModel::COL_NAME,     wxDATAVIEW_CELL_INERT, 290);
    m_dv->AppendTextColumn("CPU %",        BtTaskModel::COL_CPUPCT,   wxDATAVIEW_CELL_INERT, 70,
                           wxALIGN_RIGHT);
    m_dv->AppendTextColumn("Elapsed Time", BtTaskModel::COL_ELAPSED,  wxDATAVIEW_CELL_INERT, 175,
                           wxALIGN_RIGHT);
    m_dv->AppendTextColumn("Time Left",    BtTaskModel::COL_TIMELEFT, wxDATAVIEW_CELL_INERT, 85,
                           wxALIGN_RIGHT);
    m_dv->AppendProgressColumn("Progress %", BtTaskModel::COL_PROGRESS,
                               wxDATAVIEW_CELL_INERT, 95);
    m_dv->AppendTextColumn("Deadline",     BtTaskModel::COL_DEADLINE, wxDATAVIEW_CELL_INERT, 160);
    m_dv->AppendTextColumn("Use",          BtTaskModel::COL_USE,      wxDATAVIEW_CELL_INERT, 55);
    m_dv->AppendTextColumn("Status",       BtTaskModel::COL_STATUS,   wxDATAVIEW_CELL_INERT, 120);
    m_dv->AppendTextColumn("Computer",     BtTaskModel::COL_COMPUTER, wxDATAVIEW_CELL_INERT, 130);
    // the rest of the Windows column set; hidden by default so the view opens
    // looking the same as before
    m_dv->AppendTextColumn("Account",      BtTaskModel::COL_ACCOUNT,    wxDATAVIEW_CELL_INERT, 120);
    m_dv->AppendTextColumn("Checkpoint",   BtTaskModel::COL_CHECKPOINT, wxDATAVIEW_CELL_INERT, 110);
    m_dv->AppendTextColumn("Received",     BtTaskModel::COL_RECEIVED,   wxDATAVIEW_CELL_INERT, 170);
    m_dv->AppendTextColumn("Debt",         BtTaskModel::COL_DEBT,       wxDATAVIEW_CELL_INERT, 70);
    m_dv->AppendTextColumn("Virtual memory", BtTaskModel::COL_VIRTMEM,  wxDATAVIEW_CELL_INERT, 110);
    m_dv->AppendTextColumn("Memory",       BtTaskModel::COL_MEMORY,     wxDATAVIEW_CELL_INERT, 100);
    LoadColumnVisibility();
    m_dv->Bind(wxEVT_DATAVIEW_COLUMN_HEADER_RIGHT_CLICK,
               &TasksView::OnHeaderMenu, this);

    // "double click to expand" - we own expansion, the control sees a flat list
    m_dv->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](wxDataViewEvent& ev) {
        wxDataViewItem item = ev.GetItem();
        if (item.IsOk()) m_model->ToggleRow(m_model->GetRow(item));
        ev.Skip();
    });

    m_dv->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &TasksView::OnContextMenu, this);
    m_dv->Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, [this](wxDataViewEvent& ev) {
        wxDataViewColumn* c = ev.GetDataViewColumn();
        if (c) m_model->SortBy((int)c->GetModelColumn());
    });

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_dv, 1, wxEXPAND);
    SetSizer(sizer);
}

std::vector<BtTaskRow> TasksView::Selected() const
{
    wxDataViewItemArray items;
    m_dv->GetSelections(items);
    std::vector<unsigned> rows;
    for (const auto& it : items) rows.push_back(m_model->GetRow(it));
    return m_model->TasksForRows(rows);
}

void TasksView::OnContextMenu(wxDataViewEvent& ev)
{
    std::vector<BtTaskRow> sel = Selected();
    if (sel.empty() || !m_onOp) { ev.Skip(); return; }

    enum { ID_SUSPEND = wxID_HIGHEST + 200, ID_RESUME, ID_ABORT, ID_PROPS };
    wxMenu menu;
    menu.Append(ID_SUSPEND, wxString::Format("&Suspend (%zu tasks)", sel.size()));
    menu.Append(ID_RESUME,  wxString::Format("&Resume (%zu tasks)", sel.size()));
    menu.AppendSeparator();
    menu.Append(ID_ABORT,   wxString::Format("&Abort (%zu tasks)", sel.size()));
    menu.AppendSeparator();
    menu.Append(ID_PROPS,   "&Properties");

    menu.Bind(wxEVT_MENU, [this, sel](wxCommandEvent& e) {
        wxString op;
        switch (e.GetId()) {
            case ID_PROPS: {
                BtTaskInfoDlg dlg(this, sel.front());
                dlg.ShowModal();
                return;
            }
            case ID_SUSPEND: op = "suspend"; break;
            case ID_RESUME:  op = "resume";  break;
            case ID_ABORT:
                if (wxMessageBox(wxString::Format(
                        "Abort %zu task(s)?\n\nAborted work is lost and cannot be resumed.",
                        sel.size()), "Abort tasks",
                        wxYES_NO | wxICON_EXCLAMATION, this) != wxYES) return;
                op = "abort";
                break;
            default: return;
        }
        m_onOp(sel, op);
    });
    PopupMenu(&menu);
}

// ---- ProjectsView -----------------------------------------------------------
ProjectsView::ProjectsView(wxWindow* parent) : BtListView(parent)
{
    AppendColumn("Project",    wxLIST_FORMAT_LEFT, 200);
    AppendColumn("Account",    wxLIST_FORMAT_LEFT, 130);
    AppendColumn("Team",       wxLIST_FORMAT_LEFT, 140);
    AppendColumn("Credit",     wxLIST_FORMAT_RIGHT, 120);
    AppendColumn("Avg credit", wxLIST_FORMAT_RIGHT, 110);
    AppendColumn("Share",      wxLIST_FORMAT_RIGHT, 65);
    // this computer's own contribution, which the combined view otherwise hides
    AppendColumn("Credit host",     wxLIST_FORMAT_RIGHT, 120);
    AppendColumn("Avg credit host", wxLIST_FORMAT_RIGHT, 120);
    AppendColumn("Tasks",      wxLIST_FORMAT_RIGHT, 70);
    AppendColumn("Time Left",  wxLIST_FORMAT_RIGHT, 110);
    AppendColumn("Tasks a day",  wxLIST_FORMAT_RIGHT, 95);
    AppendColumn("Tasks a week", wxLIST_FORMAT_RIGHT, 100);
    AppendColumn("Venue",      wxLIST_FORMAT_LEFT, 90);
    AppendColumn("Status",     wxLIST_FORMAT_LEFT, 230);
    AppendColumn("Computer",   wxLIST_FORMAT_LEFT, 130);
    AppendColumn("Free-DC Host ID", wxLIST_FORMAT_RIGHT, 110);
    SetNumericColumns({3, 4, 5, 6, 7, 8, 9, 10, 11, COL_FREEDC});
    EnableSorting();
    EnableColumnMenu("projects");
    Bind(wxEVT_CONTEXT_MENU, &ProjectsView::OnContextMenu, this);
    // The generic wxListCtrl used on GTK delivers mouse events to an internal
    // child window, not to the control itself, so bind there as well - binding
    // only to the control means the handler never runs and the cell looks like
    // a link that does nothing.
    Bind(wxEVT_LEFT_DOWN, &ProjectsView::OnLeftDown, this);
    Bind(wxEVT_MOTION, &ProjectsView::OnMotion, this);
    for (wxWindow* child : GetChildren()) {
        child->Bind(wxEVT_LEFT_DOWN, &ProjectsView::OnLeftDown, this);
        child->Bind(wxEVT_MOTION,    &ProjectsView::OnMotion,   this);
    }

    m_linkFont = GetFont();
    m_linkFont.SetUnderlined(true);
}

// Only the Free-DC cell gets link styling, and only when there is somewhere to
// go: a project Free-DC does not carry leaves the cell blank rather than
// offering a link that would 404.
wxListItemAttr* ProjectsView::OnGetItemColumnAttr(long item, long col) const
{
    // Every column gets an explicit answer, never a null one. Returning null
    // for a sub-item leaves the previous sub-item's colour and font in place
    // under MSW's custom draw, so the link styling bled into every column drawn
    // after it. That was invisible while this was the last column and obvious
    // the moment the column was dragged left.
    long i = SrcIndex(item);

    bool isLink = false;
    if (col == COL_FREEDC && i >= 0 && i < (long)m_rowData.size()) {
        const auto& r = m_rowData[i];
        isLink = !BtFreeDcHostIdUrl(r.masterUrl, r.project, r.hostId).IsEmpty();
    }

    // Start from whatever the row itself asked for, so status colours and the
    // alternating stripe survive.
    m_cellAttr = wxListItemAttr();
    if (wxListItemAttr* rowAttr = BtListView::OnGetItemAttr(item)) {
        if (rowAttr->HasBackgroundColour())
            m_cellAttr.SetBackgroundColour(rowAttr->GetBackgroundColour());
        if (rowAttr->HasTextColour())
            m_cellAttr.SetTextColour(rowAttr->GetTextColour());
    }

    if (isLink) {
        m_cellAttr.SetTextColour(wxColour(0, 0, 208));
        m_cellAttr.SetFont(m_linkFont);
    } else {
        // spell out the ordinary appearance rather than leaving it unset
        if (!m_cellAttr.HasTextColour())
            m_cellAttr.SetTextColour(
                wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
        m_cellAttr.SetFont(GetFont());
    }
    return &m_cellAttr;
}

// Which row and column a mouse position refers to. Two coordinate spaces are
// in play and they are not the same: on GTK the event arrives on the list's
// internal child window, whose origin is below the header, while GetSubItemRect
// reports rectangles relative to the control, header included. HitTest wants
// the former, the rectangles are in the latter, so both are kept.
//
// Asking each column's rectangle whether it contains the point avoids doing any
// scroll or column-order arithmetic by hand - both of which this got wrong
// before, and neither of which is the same on the two backends.
bool ProjectsView::CellAt(const wxMouseEvent& ev, long& row, int& col) const
{
    ProjectsView* self = const_cast<ProjectsView*>(this);
    const wxPoint praw = ev.GetPosition();
    wxPoint pctl = praw;
    if (wxWindow* src = wxDynamicCast(ev.GetEventObject(), wxWindow))
        if (src != this) pctl = self->ScreenToClient(src->ClientToScreen(praw));

    int flags = 0;
    row = self->HitTest(praw, flags);
    if (row == wxNOT_FOUND) {
        row = self->HitTest(pctl, flags);        // in case they coincide
        if (row == wxNOT_FOUND) return false;
    }

    col = -1;
    for (int c = 0; c < GetColumnCount(); c++) {
        wxRect rc;
        if (!self->GetSubItemRect(row, c, rc)) continue;
        if (rc.width <= 0) continue;             // hidden column
        if (rc.Contains(pctl) || rc.Contains(praw)) { col = c; break; }
    }
    return col >= 0;
}

wxString ProjectsView::LinkAt(long row) const
{
    long i = SrcIndex(row);
    if (i < 0 || i >= (long)m_rowData.size()) return wxEmptyString;
    const auto& r = m_rowData[i];
    return BtFreeDcHostIdUrl(r.masterUrl, r.project, r.hostId);
}

void ProjectsView::OnLeftDown(wxMouseEvent& ev)
{
    ev.Skip();                       // selection still behaves normally
    long row = -1; int col = -1;
    if (!CellAt(ev, row, col)) return;
    if (col != COL_FREEDC) return;
    wxString url = LinkAt(row);
    if (!url.IsEmpty()) BtOpenUrl(this, url);
}

// GTK's generic list control never calls OnGetItemColumnAttr, so the cell
// cannot be drawn blue and underlined there the way it is on Windows. A hand
// cursor over the cell is the affordance that does work on both.
//
// The cursor is set before ev.Skip(), not after. Skipping first lets the
// control's own motion handling run and put the cursor back, which is why this
// did nothing on Linux the first time.
void ProjectsView::OnMotion(wxMouseEvent& ev)
{
    wxWindow* src = wxDynamicCast(ev.GetEventObject(), wxWindow);
    if (!src) { ev.Skip(); return; }

    long row = -1; int col = -1;
    bool overLink = CellAt(ev, row, col) && col == COL_FREEDC && !LinkAt(row).IsEmpty();
    if (overLink != m_overLink) {                // only touch it when it changes
        m_overLink = overLink;
        if (overLink) src->SetCursor(wxCursor(wxCURSOR_HAND));
        else          src->SetCursor(wxNullCursor);
    }
    // Over the link the default handling would reset the cursor, so it is not
    // skipped there; everywhere else the list behaves exactly as before.
    if (!overLink) ev.Skip();
}

void ProjectsView::OnContextMenu(wxContextMenuEvent& ev)
{
    std::vector<BtProjectRow> sel;
    long item = -1;
    while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1)
        { long i = SrcIndex(item);
          if (i >= 0 && i < (long)m_rowData.size()) sel.push_back(m_rowData[i]); }
    if (sel.empty() || !m_onOp) { ev.Skip(); return; }

    enum { ID_UPDATE = wxID_HIGHEST + 300, ID_SUSPEND, ID_RESUME,
           ID_NOMORE, ID_ALLOWMORE, ID_RESET, ID_DETACH,
           ID_FREEDC_CPID, ID_FREEDC_HOST };
    wxMenu menu;
    menu.Append(ID_UPDATE,    "&Update project");
    menu.AppendSeparator();
    menu.Append(ID_SUSPEND,   "&Suspend");
    menu.Append(ID_RESUME,    "&Resume");
    menu.AppendSeparator();
    menu.Append(ID_NOMORE,    "&No new tasks");
    menu.Append(ID_ALLOWMORE, "&Allow new tasks");
    menu.AppendSeparator();
    menu.Append(ID_RESET,     "Rese&t project");
    menu.Append(ID_DETACH,    "&Detach project");

    // ---- Free-DC ---------------------------------------------------------
    // The CPID page is per computer, so several rows of the same computer are
    // still unambiguous. The host page is per computer *and* project, so it
    // needs exactly one row, and Free-DC's short code for that project.
    menu.AppendSeparator();

    bool oneComputer = true;
    for (const auto& r : sel)
        if (r.hostCpid != sel[0].hostCpid) { oneComputer = false; break; }
    const wxString cpidUrl =
        oneComputer ? BtFreeDcHostCpidUrl(sel[0].hostCpid) : wxString();
    menu.Append(ID_FREEDC_CPID, "Free-DC Host CPID");
    menu.Enable(ID_FREEDC_CPID, !cpidUrl.IsEmpty());

    const wxString hostUrl = (sel.size() == 1)
        ? BtFreeDcHostIdUrl(sel[0].masterUrl, sel[0].project, sel[0].hostId)
        : wxString();
    wxString hostLabel = "Free-DC Host ID";
    if (sel.size() == 1 && hostUrl.IsEmpty() &&
        BtFreeDcShortCode(sel[0].masterUrl, sel[0].project).IsEmpty())
        hostLabel += "  (project not mapped)";
    menu.Append(ID_FREEDC_HOST, hostLabel);
    menu.Enable(ID_FREEDC_HOST, !hostUrl.IsEmpty());

    menu.Bind(wxEVT_MENU, [this, sel, cpidUrl, hostUrl](wxCommandEvent& e) {
        if (e.GetId() == ID_FREEDC_CPID) {
            if (!cpidUrl.IsEmpty()) BtOpenUrl(this, cpidUrl);
            return;
        }
        if (e.GetId() == ID_FREEDC_HOST) {
            if (!hostUrl.IsEmpty()) BtOpenUrl(this, hostUrl);
            return;
        }
        wxString op, warn;
        switch (e.GetId()) {
            case ID_UPDATE:    op = "update"; break;
            case ID_SUSPEND:   op = "suspend"; break;
            case ID_RESUME:    op = "resume"; break;
            case ID_NOMORE:    op = "nomorework"; break;
            case ID_ALLOWMORE: op = "allowmorework"; break;
            case ID_RESET:     op = "reset";
                warn = "Reset will delete all tasks in progress for the selected project(s).";
                break;
            case ID_DETACH:    op = "detach";
                warn = "Detach will remove the project and delete all of its tasks.";
                break;
            default: return;
        }
        if (!warn.empty() &&
            wxMessageBox(warn + wxString::Format("\n\nContinue for %zu project(s)?", sel.size()),
                         "Confirm", wxYES_NO | wxICON_EXCLAMATION, this) != wxYES) return;
        m_onOp(sel, op);
    });
    PopupMenu(&menu);
}

// View tab: 1,000,000.9 rather than 1000000.9
static wxString fmtNumber(double value, int digits)
{
    wxString s = wxString::Format("%.*f", digits, value);
    if (!gSettings.thousandSeparator) return s;
    wxString whole = s.BeforeFirst('.'), frac = s.AfterFirst('.');
    bool neg = whole.StartsWith("-");
    if (neg) whole = whole.Mid(1);
    wxString out;
    int count = 0;
    for (int i = (int)whole.length() - 1; i >= 0; i--) {
        out.Prepend(whole[i]);
        if (++count % 3 == 0 && i > 0) out.Prepend(",");
    }
    if (neg) out.Prepend("-");
    return digits > 0 ? out + "." + frac : out;
}

void ProjectsView::SetRows(const std::vector<BtProjectRow>& rows)
{
    m_rowData = rows;
    std::vector<std::vector<wxString>> t;
    std::vector<wxColour> c;
    t.reserve(rows.size());
    for (const auto& r : rows) {
        auto hms = [](double secs) {
            if (secs <= 0) return wxString("-");
            long s = (long)secs, d = s / 86400;
            s %= 86400;
            wxString t2 = wxString::Format("%02ld:%02ld:%02ld", s/3600, (s%3600)/60, s%60);
            return d > 0 ? wxString::Format("%02ldd,%s", d, t2) : t2;
        };
        t.push_back({r.project, r.account, r.team,
                     fmtNumber(r.credit, 0),
                     fmtNumber(r.avgCredit, 2),
                     wxString::Format("%.0f", r.share),
                     fmtNumber(r.hostCredit, 0),
                     fmtNumber(r.hostAvgCredit, 2),
                     wxString::Format("%d", r.taskCount),
                     hms(r.timeLeft),
                     wxString::Format("%d", r.perDay),
                     wxString::Format("%d", r.perWeek),
                     r.venue,
                     r.status, r.computer,
                     // the host's ID on that project, blank when Free-DC has
                     // no page for it
                     BtFreeDcHostIdUrl(r.masterUrl, r.project, r.hostId).IsEmpty()
                         ? wxString()
                         : wxString::Format("%d", r.hostId)});
        // a run-dry warning outranks the other colours: it is the one that
        // means "this needs attention now"
        c.push_back(!gSettings.colourRows ? wxColour()
                  : r.warning   ? gSettings.warnColour
                  : r.suspended ? gSettings.errorColour()
                  : r.noNewWork ? gSettings.noNewWorkColour
                                : wxColour());
    }
    SetTable(std::move(t), std::move(c));
}

// ---- HistoryView ------------------------------------------------------------
HistoryView::HistoryView(wxWindow* parent) : BtListView(parent)
{
    AppendColumn("Project",      wxLIST_FORMAT_LEFT, 170);
    AppendColumn("Application",  wxLIST_FORMAT_LEFT, 190);
    AppendColumn("Name",         wxLIST_FORMAT_LEFT, 270);
    AppendColumn("Elapsed Time", wxLIST_FORMAT_RIGHT, 150);
    AppendColumn("Completed",    wxLIST_FORMAT_LEFT, 195);
    AppendColumn("CPU %",        wxLIST_FORMAT_RIGHT, 70);
    AppendColumn("Status",       wxLIST_FORMAT_LEFT, 140);
    AppendColumn("Computer",     wxLIST_FORMAT_LEFT, 130);
    SetNumericColumns({5});                // CPU %
    EnableSorting();
    EnableColumnMenu("history");
}

void HistoryView::SetRows(const std::vector<BtHistoryRow>& rows)
{
    auto hms = [](double secs) {
        if (secs <= 0) return wxString("-");
        long s = (long)secs, d = s / 86400;
        s %= 86400;
        wxString t = wxString::Format("%02ld:%02ld:%02ld", s/3600, (s%3600)/60, s%60);
        return d > 0 ? wxString::Format("%02ldd,%s", d, t) : t;
    };

    std::vector<std::vector<wxString>> t;
    std::vector<wxColour> c;
    t.reserve(rows.size());
    for (const auto& r : rows) {
        double cpuPct = r.elapsed > 1 ? 100.0 * r.cpuTime / r.elapsed : 0;
        t.push_back({r.project, r.application, r.name,
                     hms(r.elapsed) + " (" + hms(r.cpuTime) + ")",
                     r.completedAt > 0
                        ? wxDateTime((time_t)r.completedAt).Format("%m/%d/%Y %I:%M:%S %p")
                        : wxString("-"),
                     cpuPct > 0 ? wxString::Format("%.2f", cpuPct) : wxString("-"),
                     r.status, r.computer});
        c.push_back(!gSettings.colourRows ? wxColour()
                  : r.exitStatus == 0 ? gSettings.runningColour()  // completed OK
                                      : gSettings.errorColour());  // error / aborted
    }
    SetTable(std::move(t), std::move(c));
}

// ---- ComputersView ----------------------------------------------------------
ComputersView::ComputersView(wxWindow* parent) : BtListView(parent)
{
    AppendColumn("Group",    wxLIST_FORMAT_LEFT, 130);
    AppendColumn("Computer", wxLIST_FORMAT_LEFT, 160);
    AppendColumn("IP / host",wxLIST_FORMAT_LEFT, 160);
    AppendColumn("Port",     wxLIST_FORMAT_RIGHT, 70);
    AppendColumn("Password", wxLIST_FORMAT_LEFT, 90);
    AppendColumn("BOINC",    wxLIST_FORMAT_LEFT, 80);
    AppendColumn("Platform", wxLIST_FORMAT_LEFT, 170);
    AppendColumn("Status",   wxLIST_FORMAT_LEFT, 150);
    SetNumericColumns({3});
    EnableSorting();
    EnableColumnMenu("computers");
    Bind(wxEVT_CONTEXT_MENU, &ComputersView::OnContextMenu, this);
    Bind(wxEVT_LIST_ITEM_ACTIVATED, &ComputersView::OnActivated, this);

    // Tick box per row: unticked means "keep the settings but don't show or
    // poll this computer", like the Windows list.
    EnableCheckBoxes(true);
    auto toggled = [this](wxListEvent& ev) {
        long i = SrcIndex(ev.GetIndex());
        if (i < 0 || i >= (long)m_names.size() || !m_onOp) return;
        m_onOp({ m_names[i] },
               ev.GetEventType() == wxEVT_LIST_ITEM_CHECKED ? "enable" : "disable");
    };
    Bind(wxEVT_LIST_ITEM_CHECKED, toggled);
    Bind(wxEVT_LIST_ITEM_UNCHECKED, toggled);

    // Click a field to edit it in place rather than going through a dialog.
    // The generic wxListCtrl used on GTK delivers mouse events to an internal
    // child window, not to the control itself, so bind there.
    // Both, because the two platforms deliver this from different windows: GTK's
    // generic list puts the rows in an inner child, MSW's native one has no
    // children at all. Binding only to the children left cell editing dead on
    // Windows. Mouse events do not propagate to a parent, so this cannot fire twice.
    Bind(wxEVT_LEFT_DOWN, &ComputersView::OnLeftDown, this);
    for (wxWindow* child : GetChildren())
        child->Bind(wxEVT_LEFT_DOWN, &ComputersView::OnLeftDown, this);
}

void ComputersView::OnActivated(wxListEvent& ev)
{
    long item = SrcIndex(ev.GetIndex());
    if (item >= 0 && item < (long)m_names.size() && m_onOp)
        m_onOp({ m_names[item] }, "edit");
}

void ComputersView::SetRows(const std::vector<BtComputer>& computers,
                            const std::map<wxString, BtSnapshot*>& status)
{
    std::vector<std::vector<wxString>> t;
    std::vector<wxColour> c;
    m_names.clear();
    m_enabled.clear();
    t.reserve(computers.size());
    for (const auto& comp : computers) {
        auto it = status.find(comp.name);
        const BtSnapshot* snap = (it != status.end()) ? it->second : nullptr;
        bool up = snap && snap->connected;
        t.push_back({comp.group, comp.name, comp.host,
                     wxString::Format("%ld", comp.port),
                     comp.password.IsEmpty() ? wxString("(auto)") : wxString("*****"),
                     up ? snap->clientVersion : wxString(),
                     up ? snap->platform : wxString(),
                     up ? wxString("Connected") : wxString("Not connected")});
        // unticked computers are greyed the way the Windows list greys them
        c.push_back(!comp.enabled ? wxColour(205, 205, 205)
                  : up            ? wxColour()
                                  : wxColour(225, 225, 225));
        m_names.push_back(comp.name);
        m_enabled.push_back(comp.enabled);
    }
    SetTable(std::move(t), std::move(c));
}

void ComputersView::OnContextMenu(wxContextMenuEvent& ev)
{
    std::vector<wxString> sel;
    long item = -1;
    while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1)
        { long i = SrcIndex(item);
          if (i >= 0 && i < (long)m_names.size()) sel.push_back(m_names[i]); }
    if (!m_onOp) { ev.Skip(); return; }

    enum { ID_ADD = wxID_HIGHEST + 400, ID_SCAN, ID_REMOVE, ID_EDIT };
    wxMenu menu;
    menu.Append(ID_ADD,  "&Add computer...");
    menu.Append(ID_SCAN, "&Find computers...");
    if (!sel.empty()) {
        menu.AppendSeparator();
        menu.Append(ID_EDIT, wxString::Format("&Edit '%s'...", sel[0]));
        menu.Append(ID_REMOVE, wxString::Format("&Remove (%zu)", sel.size()));
    }
    menu.Bind(wxEVT_MENU, [this, sel](wxCommandEvent& e) {
        switch (e.GetId()) {
            case ID_ADD:    m_onOp({}, "add"); break;
            case ID_SCAN:   m_onOp({}, "scan"); break;
            case ID_EDIT:   m_onOp({ sel[0] }, "edit"); break;
            case ID_REMOVE:
                if (wxMessageBox(wxString::Format("Remove %zu computer(s)?", sel.size()),
                        "Remove", wxYES_NO | wxICON_QUESTION, this) == wxYES)
                    m_onOp(sel, "remove");
                break;
        }
    });
    PopupMenu(&menu);
}

// ---- LogView ----------------------------------------------------------------
LogView::LogView(wxWindow* parent) : BtListView(parent)
{
    AppendColumn("Time",     wxLIST_FORMAT_LEFT, 180);
    AppendColumn("Computer", wxLIST_FORMAT_LEFT, 150);
    AppendColumn("Event",    wxLIST_FORMAT_LEFT, 820);
    EnableSorting();
    EnableColumnMenu("log");
}

void LogView::Append(const wxString& computer, const wxString& text)
{
    m_lines.insert(m_lines.begin(),
        { wxDateTime::Now().Format("%m/%d/%Y %I:%M:%S %p"), computer, text });
    if (m_lines.size() > 5000) m_lines.pop_back();
    std::vector<std::vector<wxString>> copy = m_lines;
    SetTable(std::move(copy));
}

// ---- NoticesView ------------------------------------------------------------
NoticesView::NoticesView(wxWindow* parent) : BtListView(parent)
{
    AppendColumn("Time",     wxLIST_FORMAT_LEFT, 180);
    AppendColumn("Project",  wxLIST_FORMAT_LEFT, 180);
    AppendColumn("Notice",   wxLIST_FORMAT_LEFT, 760);
    AppendColumn("Computer", wxLIST_FORMAT_LEFT, 130);
    EnableSorting();
    EnableColumnMenu("notices");
}

void NoticesView::SetRows(const std::vector<BtNoticeRow>& rows)
{
    // newest first
    std::vector<const BtNoticeRow*> sorted;
    sorted.reserve(rows.size());
    for (const auto& r : rows) sorted.push_back(&r);
    std::sort(sorted.begin(), sorted.end(),
              [](const BtNoticeRow* a, const BtNoticeRow* b) {
                  return a->createTime > b->createTime;
              });

    std::vector<std::vector<wxString>> t;
    t.reserve(sorted.size());
    for (const auto* r : sorted) {
        // notices arrive as HTML inside a CDATA wrapper; unwrap, then flatten
        wxString body = r->description;
        if (body.Contains("<![CDATA[")) {
            body.Replace("<![CDATA[", "");
            int end = body.Find("]]>");
            if (end != wxNOT_FOUND) body.Truncate(end);
        }
        body.Replace("\n", " ");
        body.Replace("\r", " ");
        body.Replace("<br>", " "); body.Replace("<br/>", " "); body.Replace("<br />", " ");
        body.Replace("<p>", " "); body.Replace("</p>", " ");
        while (body.Contains("<")) {
            int a = body.Find('<'), b = body.find('>', a);
            if (a == wxNOT_FOUND || b == (int)wxString::npos) break;
            body.Remove(a, b - a + 1);
        }
        body.Replace("&amp;", "&"); body.Replace("&quot;", "\"");
        body.Replace("&lt;", "<"); body.Replace("&gt;", ">");
        body = body.Trim().Trim(false);

        wxString text = r->title.IsEmpty() ? body
                      : (body.IsEmpty() ? r->title : r->title + " - " + body);
        t.push_back({r->createTime > 0
                        ? wxDateTime((time_t)r->createTime).Format("%m/%d/%Y %I:%M:%S %p")
                        : wxString("-"),
                     r->project, text, r->computer});
    }
    SetTable(std::move(t));
}

// ---- TransfersView ----------------------------------------------------------
TransfersView::TransfersView(wxWindow* parent) : BtListView(parent)
{
    AppendColumn("Project",  wxLIST_FORMAT_LEFT, 180);
    AppendColumn("File",     wxLIST_FORMAT_LEFT, 300);
    AppendColumn("Size",     wxLIST_FORMAT_RIGHT, 95);
    AppendColumn("Progress", wxLIST_FORMAT_RIGHT, 85);
    AppendColumn("Speed",    wxLIST_FORMAT_RIGHT, 100);
    AppendColumn("Status",   wxLIST_FORMAT_LEFT, 150);
    AppendColumn("Computer", wxLIST_FORMAT_LEFT, 130);
    SetNumericColumns({2, 3, 4});          // size, progress, speed
    EnableSorting();
    EnableColumnMenu("transfers");
    Bind(wxEVT_CONTEXT_MENU, &TransfersView::OnContextMenu, this);
}

void TransfersView::OnContextMenu(wxContextMenuEvent& ev)
{
    std::vector<BtTransferRow> sel;
    long item = -1;
    while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1)
        { long i = SrcIndex(item);
          if (i >= 0 && i < (long)m_rowData.size()) sel.push_back(m_rowData[i]); }
    if (sel.empty() || !m_onOp) { ev.Skip(); return; }

    enum { ID_RETRY = wxID_HIGHEST + 500, ID_ABORT_XFER };
    wxMenu menu;
    menu.Append(ID_RETRY, wxString::Format("&Retry now (%zu)", sel.size()));
    menu.AppendSeparator();
    menu.Append(ID_ABORT_XFER, wxString::Format("&Abort transfer (%zu)", sel.size()));
    menu.Bind(wxEVT_MENU, [this, sel](wxCommandEvent& e) {
        if (e.GetId() == ID_RETRY) { m_onOp(sel, "retry"); return; }
        if (wxMessageBox(wxString::Format(
                "Abort %zu transfer(s)?\n\nThe associated task will be lost.", sel.size()),
                "Abort transfers", wxYES_NO | wxICON_EXCLAMATION, this) == wxYES)
            m_onOp(sel, "abort");
    });
    PopupMenu(&menu);
}

void TransfersView::SetRows(const std::vector<BtTransferRow>& rows)
{
    m_rowData = rows;
    std::vector<std::vector<wxString>> t;
    t.reserve(rows.size());
    for (const auto& r : rows)
        t.push_back({r.project, r.file, fmtSize(r.size),
                     wxString::Format("%.2f%%", r.progress),
                     r.speed > 0 ? fmtSize(r.speed) + "/s" : wxString("-"),
                     r.status, r.computer});
    SetTable(std::move(t));
}

// ---- MessagesView -----------------------------------------------------------
MessagesView::MessagesView(wxWindow* parent) : BtListView(parent)
{
    AppendColumn("Nr",       wxLIST_FORMAT_RIGHT, 70);
    AppendColumn("Project",  wxLIST_FORMAT_LEFT, 180);
    AppendColumn("Time",     wxLIST_FORMAT_LEFT, 160);
    AppendColumn("Message",  wxLIST_FORMAT_LEFT, 720);
    AppendColumn("Computer", wxLIST_FORMAT_LEFT, 130);
    SetNumericColumns({0});                // Nr
    EnableSorting();
    EnableColumnMenu("messages");
}

void MessagesView::SetRows(const std::vector<BtMessageRow>& rows)
{
    // newest first, like the Windows app
    std::vector<const BtMessageRow*> sorted;
    sorted.reserve(rows.size());
    for (const auto& r : rows) sorted.push_back(&r);
    std::sort(sorted.begin(), sorted.end(),
              [](const BtMessageRow* a, const BtMessageRow* b) {
                  if (a->timestamp != b->timestamp) return a->timestamp > b->timestamp;
                  return a->seqno > b->seqno;
              });

    std::vector<std::vector<wxString>> t;
    std::vector<wxColour> c;
    t.reserve(sorted.size());
    for (const auto* r : sorted) {
        t.push_back({wxString::Format("%d", r->seqno), r->project,
                     wxDateTime((time_t)r->timestamp).Format("%m/%d/%Y %I:%M:%S %p"),
                     r->body, r->computer});
        c.push_back(r->priority > 1 ? wxColour(255, 190, 190)
                  : r->body.Contains("Scheduler request") ||
                    r->body.Contains("Requesting new tasks")
                        ? wxColour(255, 255, 150) : wxColour());
    }
    SetTable(std::move(t), std::move(c));
}

// ---- Tasks column visibility ----------------------------------------------
std::vector<wxString> TasksView::ColumnNames() const
{
    std::vector<wxString> names;
    for (unsigned i = 0; i < m_dv->GetColumnCount(); i++)
        names.push_back(m_dv->GetColumn(i)->GetTitle());
    return names;
}

void TasksView::LoadColumnVisibility()
{
    unsigned n = m_dv->GetColumnCount();
    m_colShown.assign(n, true);
    wxFileConfig cfg(BTPP_SHORT, "eFMer", BtConfigPath());
    for (unsigned i = 0; i < n; i++) {
        // the columns beyond Computer are the extra Windows ones; off unless
        // the user has turned them on
        bool dflt = (i < BtTaskModel::COL_ACCOUNT);
        m_colShown[i] = cfg.ReadBool(wxString::Format("/Columns/tasks/%u", i), dflt);
        m_dv->GetColumn(i)->SetHidden(!m_colShown[i]);
    }
}

void TasksView::SetColumnsShown(const std::vector<bool>& shown)
{
    if (shown.size() != m_colShown.size()) return;
    m_colShown = shown;
    wxFileConfig cfg(BTPP_SHORT, "eFMer", BtConfigPath());
    for (size_t i = 0; i < m_colShown.size(); i++) {
        m_dv->GetColumn((unsigned)i)->SetHidden(!m_colShown[i]);
        cfg.Write(wxString::Format("/Columns/tasks/%zu", i), (bool)m_colShown[i]);
    }
    cfg.Flush();
}

void TasksView::OnHeaderMenu(wxDataViewEvent&)
{
    wxMenu menu;
    const int base = wxID_HIGHEST + 950;
    auto names = ColumnNames();
    for (size_t i = 0; i < names.size(); i++) {
        menu.AppendCheckItem(base + (int)i, names[i]);
        menu.Check(base + (int)i, m_colShown[i]);
    }
    menu.Bind(wxEVT_MENU, [this, base](wxCommandEvent& e) {
        int idx = e.GetId() - base;
        if (idx < 0 || idx >= (int)m_colShown.size()) return;
        auto shown = m_colShown;
        shown[idx] = !shown[idx];
        SetColumnsShown(shown);
    });
    PopupMenu(&menu);
}

// ---- editing a cell in place ----------------------------------------------
void ComputersView::OnLeftDown(wxMouseEvent& ev)
{
    ev.Skip();                       // let the list handle selection as usual
    if (m_editor) CommitEdit();
    // events arrive from the list's inner window; edit boxes go there too so
    // the coordinates line up
    m_listMain = wxDynamicCast(ev.GetEventObject(), wxWindow);

    int flags = 0;
    long item = HitTest(ev.GetPosition(), flags);
    if (item == wxNOT_FOUND) return;
    if (flags & wxLIST_HITTEST_ONITEMICON) return;    // that is the tick box

    // work out which column was hit
    int x = ev.GetPosition().x, acc = 0, col = -1;
    for (int i = 0; i < GetColumnCount(); i++) {
        int w = GetColumnWidth(i);
        if (x >= acc && x < acc + w) { col = i; break; }
        acc += w;
    }
    if (col < 0) return;
    // only the configuration fields are editable; the rest are live status
    if (col != COL_GROUP && col != COL_NAME && col != COL_HOST &&
        col != COL_PORT  && col != COL_PASSWORD) return;

    // edit on a click in the row that is already selected, so the first click
    // still just selects - the same feel as renaming a file
    if (!(GetItemState(item, wxLIST_STATE_SELECTED) & wxLIST_STATE_SELECTED)) return;
    CallAfter([this, item, col]() { BeginEdit(item, col); });
}

void ComputersView::BeginEdit(long item, int col)
{
    wxRect r;
    if (!GetSubItemRect(item, col, r)) return;

    long src = SrcIndex(item);
    if (src < 0 || src >= (long)m_names.size()) return;

    // the password cell shows a mask, so start it empty rather than editing "*****"
    wxString current = (col == COL_PASSWORD) ? wxString() : OnGetItemText(item, col);

    m_editItem = item;
    m_editCol  = col;
    // GetSubItemRect measures from the control's client area, header included,
    // so the editor has to be a child of the control - parenting it to the
    // inner window would place it a row too low.
    m_editor = new wxTextCtrl(this, wxID_ANY, current, r.GetPosition(), r.GetSize(),
                              wxTE_PROCESS_ENTER);
    m_editor->SetInsertionPointEnd();
    m_editor->SelectAll();
    m_editor->SetFocus();

    m_editor->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { CommitEdit(); });
    m_editor->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { e.Skip(); CommitEdit(); });
    m_editor->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_ESCAPE) CancelEdit();
        else e.Skip();
    });
}

void ComputersView::CommitEdit()
{
    if (!m_editor) return;
    wxString value = m_editor->GetValue();
    long src = SrcIndex(m_editItem);
    int  col = m_editCol;

    // CommitEdit runs from the editor's own kill-focus handler, so the control
    // must outlive this call: hide it now and destroy it once GTK has finished
    // delivering the event. Destroying it here is a use-after-free.
    wxTextCtrl* dying = m_editor;
    m_editor = nullptr;
    m_editItem = -1;
    m_editCol = -1;
    dying->Hide();
    CallAfter([dying]() { dying->Destroy(); });

    if (src < 0 || src >= (long)m_names.size() || !m_onEdit) return;
    m_onEdit(m_names[src], col, value);
}

void ComputersView::CancelEdit()
{
    if (!m_editor) return;
    wxTextCtrl* dying = m_editor;
    m_editor = nullptr;
    m_editItem = -1;
    m_editCol = -1;
    dying->Hide();
    CallAfter([dying]() { dying->Destroy(); });
}
