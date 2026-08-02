// =============================================================================
// bt_views.h - combined multi-computer views
// =============================================================================
#pragma once
#include "bt_types.h"
#include "bt_taskmodel.h"
#include "bt_settings.h"
#include <wx/dataview.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <functional>
#include <algorithm>

// Open a URL, and say so when that is not possible instead of doing nothing.
// A machine with no browser installed - or with no xdg-open to find one - is
// not unusual for a BOINC host, and wxLaunchDefaultBrowser fails silently on
// one. The address is copied to the clipboard so it can still be used.
bool BtOpenUrl(wxWindow* parent, const wxString& url);
#include <map>

// ---- Tasks: grouped tree with progress bars --------------------------------
class TasksView : public wxPanel
{
public:
    // (tasks, op) with op one of "suspend" / "resume" / "abort"
    using OpHandler = std::function<void(const std::vector<BtTaskRow>&, const wxString&)>;

    TasksView(wxWindow* parent);
    void SetRows(std::vector<BtTaskRow>&& rows) { m_model->Update(std::move(rows)); }
    void SetCapacities(std::map<wxString, BtCapacity> caps)
        { m_model->SetCapacities(std::move(caps)); }
    size_t TaskCount() const { return m_model->TaskCount(); }
    void SetOpHandler(OpHandler h) { m_onOp = std::move(h); }

    // Same column-visibility interface the list views expose, so the settings
    // dialog can carry a Tasks tab like the Windows one.
    std::vector<wxString> ColumnNames() const;
    std::vector<bool>     ColumnsShown() const { return m_colShown; }
    void SetColumnsShown(const std::vector<bool>& shown);
    wxString ViewKey() const { return "tasks"; }

private:
    void LoadColumnVisibility();
    void OnHeaderMenu(wxDataViewEvent& ev);
    std::vector<bool> m_colShown;

private:
    void OnContextMenu(wxDataViewEvent& ev);
    std::vector<BtTaskRow> Selected() const;
    OpHandler m_onOp;

private:
    wxDataViewCtrl*              m_dv;
    wxObjectDataPtr<BtTaskModel> m_model;
};

// ---- simple virtual list views ---------------------------------------------
class BtListView : public wxListCtrl
{
public:
    BtListView(wxWindow* parent)
        : wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxLC_REPORT | wxLC_VIRTUAL | wxLC_HRULES)
    {
        SetDoubleBuffered(true);   // repaint off-screen; no tearing when it does
        ApplyViewStyle();
    }

    // View tab: grid lines and alternating stripes
    void ApplyViewStyle()
    {
        SetSingleStyle(wxLC_HRULES, gSettings.gridLinesH);
        SetSingleStyle(wxLC_VRULES, gSettings.gridLinesV);
        Refresh();
    }

    void SetTable(std::vector<std::vector<wxString>>&& rows,
                  std::vector<wxColour>&& colours = {})
    {
        // Views refresh on every poll, but most of the time nothing visible
        // changed. Repainting anyway makes the quiet views blink once a second,
        // so compare first and do nothing if identical. m_rows is always held in
        // the order the caller supplied - sorting only permutes m_order - so a
        // sorted view still compares equal here when its data is unchanged.
        if (rows == m_rows && colours == m_colours) return;

        bool sameSize = (rows.size() == m_rows.size());

        // Which rows actually differ. Repainting the whole visible page every
        // poll made the view look like it was flashing, when in a settled fleet
        // only a couple of cells move. Worked out before the move, so no copy of
        // the table is needed.
        std::vector<char> changed;
        if (sameSize) {
            changed.resize(m_rows.size());
            for (size_t i = 0; i < m_rows.size(); i++)
                changed[i] = (rows[i] != m_rows[i]) ||
                             (i < colours.size() && i < m_colours.size() &&
                              colours[i] != m_colours[i]);
        }

        const std::vector<size_t> oldOrder = m_order;
        m_rows = std::move(rows);
        m_colours = std::move(colours);
        ApplySort();
        Freeze();
        if (!sameSize) SetItemCount((long)m_rows.size());
        if (m_rows.empty()) { Thaw(); Refresh(); return; }

        long first = GetTopItem();
        long last = std::min<long>((long)m_rows.size() - 1,
                                   first + GetCountPerPage() + 1);
        if (first < 0 || last < first) { Refresh(); Thaw(); return; }

        // A row count change moves every row after the insertion point, and a
        // re-sort moves them arbitrarily, so those still need the whole page.
        if (!sameSize || m_order != oldOrder) {
            RefreshItems(first, last);
        } else {
            for (long d = first; d <= last; d++) {
                long src = SrcIndex(d);
                if (src >= 0 && src < (long)changed.size() && changed[src])
                    RefreshItem(d);
            }
        }
        Thaw();
    }

    // A single explanatory row, used where a view needs a selection first.
    // The text goes in the widest column so it isn't clipped to "Select ...".
    void ShowHint(const wxString& text)
    {
        int cols = GetColumnCount(), best = 0, bestWidth = -1;
        for (int c = 0; c < cols; c++) {
            int w = GetColumnWidth(c);
            if (w > bestWidth) { bestWidth = w; best = c; }
        }
        std::vector<wxString> cells((size_t)std::max(cols, 1));
        cells[best] = text;
        std::vector<std::vector<wxString>> row{ std::move(cells) };
        SetTable(std::move(row), {});
    }

    // Columns holding formatted numbers must sort numerically, not as text.
    void SetNumericColumns(std::vector<int> cols) { m_numericCols = std::move(cols); }

    // Display position -> index into the caller's row vector. Views keep their
    // own parallel data (project rows, computer names) in source order, so any
    // lookup driven by a selected item has to come through here.
    long SrcIndex(long display) const
    {
        if (display < 0) return display;
        if (m_order.empty()) return display;
        return display < (long)m_order.size() ? (long)m_order[display] : -1;
    }

protected:
    wxString OnGetItemText(long item, long col) const override
    {
        long i = SrcIndex(item);
        if (i < 0 || i >= (long)m_rows.size()) return "";
        const auto& r = m_rows[i];
        return col < (long)r.size() ? r[col] : wxString();
    }

    wxListItemAttr* OnGetItemAttr(long item) const override
    {
        long i = SrcIndex(item);
        bool haveColour = (i >= 0 && i < (long)m_colours.size() && m_colours[i].IsOk());
        if (!haveColour) {
            // no status colour: stripe every other row if that's switched on
            if (!gSettings.alternatingStripes || (item % 2) == 0) return nullptr;
            m_attr.SetBackgroundColour(wxColour(242, 242, 242));
            return &m_attr;
        }
        m_attr.SetBackgroundColour(m_colours[i]);
        return &m_attr;
    }

public:
    // Column visibility, also driven from the settings dialog's per-view tabs.
    std::vector<wxString> ColumnNames() const
    {
        std::vector<wxString> names;
        for (int i = 0; i < GetColumnCount(); i++) {
            wxListItem col;
            col.SetMask(wxLIST_MASK_TEXT);
            const_cast<BtListView*>(this)->GetColumn(i, col);
            names.push_back(col.GetText());
        }
        return names;
    }
    std::vector<bool> ColumnsShown() const { return m_colShown; }
    void SetColumnsShown(const std::vector<bool>& shown);
    wxString ViewKey() const { return m_viewKey; }

protected:
    // Right-click the header to toggle columns; widths are remembered so a
    // hidden column comes back the size it was.
    void EnableColumnMenu(const wxString& viewKey);
    void ApplyColumnVisibility();
    void OnHeaderMenu(wxListEvent& ev);

    void EnableSorting()
    {
        Bind(wxEVT_LIST_COL_CLICK, [this](wxListEvent& ev) {
            int col = ev.GetColumn();
            if (col == m_sortCol) m_sortAsc = !m_sortAsc;
            else { m_sortCol = col; m_sortAsc = true; }
            ApplySort();
            Refresh();
        });
    }

    void ApplySort()
    {
        if (m_sortCol < 0) { m_order.clear(); return; }   // identity order
        const int col = m_sortCol;
        const bool asc = m_sortAsc;
        const bool numeric =
            std::find(m_numericCols.begin(), m_numericCols.end(), col) != m_numericCols.end();

        // Sort a permutation rather than the rows themselves: m_rows has to stay
        // in the order the caller built it so the unchanged-data check in
        // SetTable still works, and so views can map a clicked row back to their
        // own parallel data with SrcIndex().
        std::vector<size_t> idx(m_rows.size());
        for (size_t i = 0; i < idx.size(); i++) idx[i] = i;

        auto cell = [&](size_t i) {
            return col < (int)m_rows[i].size() ? m_rows[i][col] : wxString();
        };
        auto num = [&](size_t i) {
            wxString v = cell(i);
            v.Replace(",", ""); v.Replace("%", ""); v.Replace("/s", "");
            double d = 0;
            v.BeforeFirst(' ').ToDouble(&d);
            return d;
        };

        std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            bool less = numeric ? num(a) < num(b) : cell(a).CmpNoCase(cell(b)) < 0;
            bool eq   = numeric ? num(a) == num(b) : cell(a).CmpNoCase(cell(b)) == 0;
            if (eq) return a < b;
            return asc ? less : !less;
        });

        m_order = std::move(idx);
    }

private:
    std::vector<std::vector<wxString>> m_rows;      // always in caller order
    std::vector<wxColour>              m_colours;   // parallel to m_rows
    std::vector<size_t>                m_order;     // display -> m_rows; empty = identity
    std::vector<int>                   m_numericCols;
    wxString                           m_viewKey;
    std::vector<int>                   m_colWidths;
    std::vector<bool>                  m_colShown;
    int                                m_sortCol = -1;
    bool                               m_sortAsc = true;
    mutable wxListItemAttr             m_attr;
};

class ProjectsView : public BtListView
{
public:
    // (projects, op) with op one of "update"/"suspend"/"resume"/
    // "nomorework"/"allowmorework"/"reset"/"detach"
    using OpHandler = std::function<void(const std::vector<BtProjectRow>&, const wxString&)>;

    ProjectsView(wxWindow* parent);
    void SetRows(const std::vector<BtProjectRow>& rows);
    void SetOpHandler(OpHandler h) { m_onOp = std::move(h); }

    // Last column: this host's ID on that project, as a link to its Free-DC
    // page. Appended rather than inserted so saved column widths and the
    // show/hide settings of the existing columns keep their meaning.
    enum { COL_FREEDC = 15 };

private:
    void OnContextMenu(wxContextMenuEvent& ev);
    void OnLeftDown(wxMouseEvent& ev);
    void OnMotion(wxMouseEvent& ev);
    // row/column under a mouse event, coping with both coordinate spaces
    bool CellAt(const wxMouseEvent& ev, long& row, int& col) const;
    wxString LinkAt(long row) const;
    // The link cell is drawn blue and underlined; everything else is left alone.
    wxListItemAttr* OnGetItemColumnAttr(long item, long col) const override;

    std::vector<BtProjectRow> m_rowData;
    OpHandler m_onOp;
    // one attribute object reused for every cell, plus the underlined font
    mutable wxListItemAttr m_cellAttr;
    wxFont                 m_linkFont;
    bool                   m_overLink = false;
};

class HistoryView : public BtListView
{
public:
    HistoryView(wxWindow* parent);
    void SetRows(const std::vector<BtHistoryRow>& rows);
};

class ComputersView : public BtListView
{
public:
    // first selected row's computer name, empty if nothing is selected
    wxString SelectedName() const
    {
        long item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        long i = SrcIndex(item);
        return (i >= 0 && i < (long)m_names.size()) ? m_names[i] : wxString();
    }

    // (computers, op) with op "add" / "edit" / "remove" / "scan"
    using OpHandler = std::function<void(const std::vector<wxString>&, const wxString&)>;

    // (computer, column, new value) from editing a cell in place
    using EditHandler = std::function<void(const wxString&, int, const wxString&)>;

    ComputersView(wxWindow* parent);
    // live status per configured computer
    void SetRows(const std::vector<BtComputer>& computers,
                 const std::map<wxString, BtSnapshot*>& status);
    void SetOpHandler(OpHandler h) { m_onOp = std::move(h); }
    void SetEditHandler(EditHandler h) { m_onEdit = std::move(h); }

    // columns, in the order they are added
    enum { COL_GROUP = 0, COL_NAME, COL_HOST, COL_PORT, COL_PASSWORD,
           COL_BOINC, COL_PLATFORM, COL_STATUS };

protected:
    // the tick in the first column mirrors BtComputer::enabled
    bool OnGetItemIsChecked(long item) const override
    {
        long i = SrcIndex(item);
        return (i >= 0 && i < (long)m_enabled.size()) ? m_enabled[i] : true;
    }

private:
    void OnContextMenu(wxContextMenuEvent& ev);
    void OnActivated(wxListEvent& ev);
    void OnLeftDown(wxMouseEvent& ev);
    void BeginEdit(long item, int col);
    void CommitEdit();
    void CancelEdit();

    EditHandler m_onEdit;
    std::vector<bool> m_enabled;      // parallel to m_names
    class wxTextCtrl* m_editor = nullptr;
    wxWindow*         m_listMain = nullptr;   // the list's inner window
    long m_editItem = -1;
    int  m_editCol  = -1;
    std::vector<wxString> m_names;      // parallel to displayed rows
    OpHandler m_onOp;
};

// The app's own activity log: connections, operations, failures. The Windows
// app calls this "Log"; it is about BoincTasks itself, not the clients.
class LogView : public BtListView
{
public:
    LogView(wxWindow* parent);
    void Append(const wxString& computer, const wxString& text);
private:
    std::vector<std::vector<wxString>> m_lines;
};

class NoticesView : public BtListView
{
public:
    NoticesView(wxWindow* parent);
    void SetRows(const std::vector<BtNoticeRow>& rows);
};

class TransfersView : public BtListView
{
public:
    // (transfers, op) with op "retry" / "abort"
    using OpHandler = std::function<void(const std::vector<BtTransferRow>&, const wxString&)>;

    TransfersView(wxWindow* parent);
    void SetRows(const std::vector<BtTransferRow>& rows);
    void SetOpHandler(OpHandler h) { m_onOp = std::move(h); }

private:
    void OnContextMenu(wxContextMenuEvent& ev);
    std::vector<BtTransferRow> m_rowData;
    OpHandler m_onOp;
};

class MessagesView : public BtListView
{
public:
    MessagesView(wxWindow* parent);
    void SetRows(const std::vector<BtMessageRow>& rows);
};
