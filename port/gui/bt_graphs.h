// =============================================================================
// bt_graphs.h - custom-drawn charts (Statistics / Deadline / Tasks over time)
// =============================================================================
#pragma once
#include "bt_types.h"
#include <wx/panel.h>
#include <wx/choice.h>
#include <deque>
#include <vector>

// One sampled moment of farm-wide counts, kept in a ring buffer by the frame.
struct BtTaskSample
{
    double time = 0;
    int    running = 0, ready = 0, total = 0;
    double upBytesSec = 0, downBytesSec = 0;   // aggregate transfer rates
};

class BtGraphPanel : public wxPanel
{
public:
    enum Kind { GRAPH_STATISTICS, GRAPH_DEADLINE, GRAPH_TASKS, GRAPH_TRANSFER };

    BtGraphPanel(wxWindow* parent);

    void SetKind(Kind k) { m_kind = k; Refresh(); }
    Kind GetKind() const { return m_kind; }

    void SetStatistics(const std::vector<BtStatSeries>& s) { m_stats = s; Refresh(); }
    void SetTasks(const std::vector<BtTaskRow>& t);
    void SetSamples(const std::deque<BtTaskSample>& s) { m_samples = s; Refresh(); }

private:
    void OnPaint(wxPaintEvent&);
    void DrawStatistics(wxDC& dc, const wxRect& plot);
    void DrawDeadline(wxDC& dc, const wxRect& plot);
    void DrawTasks(wxDC& dc, const wxRect& plot);
    void DrawTransfer(wxDC& dc, const wxRect& plot);
    void DrawFrame(wxDC& dc, wxRect& plot, const wxString& title,
                   const wxString& yLabel);
    static wxColour SeriesColour(size_t i);

    Kind m_kind = GRAPH_STATISTICS;
    std::vector<BtStatSeries>  m_stats;
    std::vector<int>           m_deadlineBuckets;   // tasks per day-until-deadline
    std::deque<BtTaskSample>   m_samples;
};

class GraphsView : public wxPanel
{
public:
    GraphsView(wxWindow* parent);
    // Show menu picks a graph directly, like the Windows app's five entries
    void SetKind(BtGraphPanel::Kind k)
    {
        m_panel->SetKind(k);
        m_choice->SetSelection((int)k);
    }
    void SetStatistics(const std::vector<BtStatSeries>& s) { m_panel->SetStatistics(s); }
    void SetTasks(const std::vector<BtTaskRow>& t)         { m_panel->SetTasks(t); }
    void SetSamples(const std::deque<BtTaskSample>& s)     { m_panel->SetSamples(s); }

private:
    wxChoice*     m_choice;
    BtGraphPanel* m_panel;
};
