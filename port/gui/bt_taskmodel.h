// =============================================================================
// bt_taskmodel.h - grouped Tasks model for wxDataViewCtrl
//
// Rows are grouped by computer + project + application + status, like Windows
// BoincTasks: a collapsed group shows "N [Tasks], double click to expand" with
// aggregate values; a group holding one task renders as that task.
//
// Expansion is owned by this model and the control is fed a *flat* virtual row
// list. wxGTK's native tree never lazily queries the children of an unexpanded
// container, and a real tree node per task would not survive 100k+ tasks
// anyway; a flat virtual list costs nothing per collapsed group.
// =============================================================================
#pragma once
#include "bt_types.h"
#include <wx/dataview.h>
#include <map>
#include <memory>
#include <vector>

class BtGroupNode
{
public:
    wxString key;
    wxString computer, project, application, status;
    bool     running = false, error = false;
    bool     isGpu = false;
    int      state = 0;
    bool     expanded = false;

    // aggregates shown on a collapsed group row
    double cpuPct = 0, elapsed = 0, cpuTime = 0, timeLeft = 0,
           progress = 0, deadline = 0, useCpus = 0;

    std::vector<BtTaskRow> tasks;
};

class BtTaskModel : public wxDataViewVirtualListModel
{
public:
    enum { COL_PROJECT, COL_APP, COL_NAME, COL_CPUPCT, COL_ELAPSED, COL_TIMELEFT,
           COL_PROGRESS, COL_DEADLINE, COL_USE, COL_STATUS, COL_COMPUTER,
           COL_ACCOUNT, COL_CHECKPOINT, COL_RECEIVED, COL_DEBT, COL_VIRTMEM,
           COL_MEMORY, COL_COUNT };

    // Merge a fresh row set; group identity (and expansion) survives polls.
    void Update(std::vector<BtTaskRow>&& rows);

    // Toggle the group shown on this row; false if it isn't a collapsible group.
    bool ToggleRow(unsigned row);

    // Sort by a display column; clicking the active column flips direction.
    void SortBy(int column);
    int  SortColumn() const { return m_sortCol; }
    bool SortAscending() const { return m_sortAsc; }

    // Tasks represented by these view rows; a group row yields every task in
    // it, which is how "suspend a collapsed group" works.
    std::vector<BtTaskRow> TasksForRows(const std::vector<unsigned>& rows) const;

    size_t TaskCount() const { return m_taskCount; }
    size_t RowCount()  const { return m_flat.size(); }

    // wxDataViewVirtualListModel ------------------------------------------
    unsigned int GetColumnCount() const override { return COL_COUNT; }
    wxString GetColumnType(unsigned int col) const override
    { return col == COL_PROGRESS ? "long" : "string"; }

    void GetValueByRow(wxVariant& v, unsigned row, unsigned col) const override;
    bool SetValueByRow(const wxVariant&, unsigned, unsigned) override { return false; }
    bool GetAttrByRow(unsigned row, unsigned col,
                      wxDataViewItemAttr& attr) const override;

private:
    struct FlatRow
    {
        BtGroupNode* group;
        int          task;      // -1 => the group row itself
    };

    void Reflatten();
    void ApplySort();

    std::vector<std::unique_ptr<BtGroupNode>> m_groups;
    std::map<wxString, BtGroupNode*>          m_byKey;
    std::vector<FlatRow>                      m_flat;
    size_t m_taskCount = 0;
    int    m_sortCol = -1;      // -1 => natural (computer/project/app/status)
    bool   m_sortAsc = true;
};
