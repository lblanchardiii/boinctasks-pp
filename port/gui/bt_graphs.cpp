#include "bt_graphs.h"
#include <wx/dcbuffer.h>
#include <wx/sizer.h>
#include <wx/datetime.h>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
BtGraphPanel::BtGraphPanel(wxWindow* parent) : wxPanel(parent)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);   // required for wxAutoBufferedPaintDC
    SetBackgroundColour(*wxWHITE);
    Bind(wxEVT_PAINT, &BtGraphPanel::OnPaint, this);
}

wxColour BtGraphPanel::SeriesColour(size_t i)
{
    static const wxColour palette[] = {
        wxColour( 31, 119, 180), wxColour(255, 127,  14),
        wxColour( 44, 160,  44), wxColour(214,  39,  40),
        wxColour(148, 103, 189), wxColour(140,  86,  75),
        wxColour(227, 119, 194), wxColour(127, 127, 127),
    };
    return palette[i % (sizeof(palette) / sizeof(palette[0]))];
}

void BtGraphPanel::SetTasks(const std::vector<BtTaskRow>& tasks)
{
    // bucket by whole days until deadline; last bucket collects everything older
    const int kBuckets = 15;
    m_deadlineBuckets.assign(kBuckets, 0);
    double now = (double)wxDateTime::Now().GetTicks();
    for (const auto& t : tasks) {
        if (t.deadline <= 0) continue;
        int days = (int)((t.deadline - now) / 86400.0);
        if (days < 0) days = 0;
        if (days >= kBuckets) days = kBuckets - 1;
        m_deadlineBuckets[days]++;
    }
    Refresh();
}

void BtGraphPanel::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(*wxWHITE_BRUSH);
    dc.Clear();

    wxRect plot = GetClientRect();
    switch (m_kind) {
        case GRAPH_STATISTICS:
            DrawFrame(dc, plot, "Average credit per day", "credit");
            DrawStatistics(dc, plot);
            break;
        case GRAPH_DEADLINE:
            DrawFrame(dc, plot, "Tasks by days until deadline", "tasks");
            DrawDeadline(dc, plot);
            break;
        case GRAPH_TASKS:
            DrawFrame(dc, plot, "Tasks over time", "tasks");
            DrawTasks(dc, plot);
            break;
        case GRAPH_TRANSFER:
            DrawFrame(dc, plot, "Data transfer rate", "bytes/s");
            DrawTransfer(dc, plot);
            break;
    }
}

void BtGraphPanel::DrawFrame(wxDC& dc, wxRect& plot, const wxString& title,
                             const wxString& yLabel)
{
    dc.SetFont(GetFont().Bold());
    dc.SetTextForeground(wxColour(60, 60, 60));
    dc.DrawText(title, 14, 8);
    dc.SetFont(GetFont());

    plot.x      += 70;
    plot.y      += 34;
    plot.width  -= 100;
    plot.height -= 74;
    if (plot.width < 40 || plot.height < 40) return;

    dc.SetPen(wxPen(wxColour(160, 160, 160)));
    dc.DrawLine(plot.x, plot.y, plot.x, plot.GetBottom());              // Y axis
    dc.DrawLine(plot.x, plot.GetBottom(), plot.GetRight(), plot.GetBottom()); // X

    dc.SetTextForeground(wxColour(110, 110, 110));
    dc.DrawRotatedText(yLabel, 16, plot.y + plot.height / 2 + 20, 90);
}

void BtGraphPanel::DrawStatistics(wxDC& dc, const wxRect& plot)
{
    if (plot.width < 40 || m_stats.empty()) {
        dc.DrawText("No statistics yet - the client sends these once a day.",
                    plot.x + 10, plot.y + 10);
        return;
    }

    double minDay = 0, maxDay = 0, maxVal = 0;
    bool first = true;
    for (const auto& s : m_stats)
        for (const auto& p : s.points) {
            if (first) { minDay = maxDay = p.first; first = false; }
            minDay = std::min(minDay, p.first);
            maxDay = std::max(maxDay, p.first);
            maxVal = std::max(maxVal, p.second);
        }
    if (first || maxDay <= minDay || maxVal <= 0) {
        dc.DrawText("Not enough data to plot yet.", plot.x + 10, plot.y + 10);
        return;
    }

    // horizontal gridlines with value labels
    dc.SetTextForeground(wxColour(120, 120, 120));
    for (int i = 0; i <= 4; i++) {
        int y = plot.GetBottom() - (plot.height * i) / 4;
        dc.SetPen(wxPen(wxColour(232, 232, 232)));
        if (i) dc.DrawLine(plot.x + 1, y, plot.GetRight(), y);
        wxString lab = wxString::Format("%.0f", maxVal * i / 4);
        dc.DrawText(lab, plot.x - 8 - dc.GetTextExtent(lab).GetWidth(), y - 8);
    }

    auto X = [&](double d) {
        return plot.x + (int)((d - minDay) / (maxDay - minDay) * plot.width);
    };
    auto Y = [&](double v) {
        return plot.GetBottom() - (int)(v / maxVal * plot.height);
    };

    int legendY = plot.y + 4;
    for (size_t i = 0; i < m_stats.size(); i++) {
        const auto& s = m_stats[i];
        if (s.points.size() < 2) continue;
        auto pts = s.points;
        std::sort(pts.begin(), pts.end());

        dc.SetPen(wxPen(SeriesColour(i), 2));
        for (size_t k = 1; k < pts.size(); k++)
            dc.DrawLine(X(pts[k-1].first), Y(pts[k-1].second),
                        X(pts[k].first),   Y(pts[k].second));

        dc.SetTextForeground(SeriesColour(i));
        dc.DrawText(s.project, plot.GetRight() - 210, legendY);
        legendY += 16;
    }

    // date range along the X axis
    dc.SetTextForeground(wxColour(120, 120, 120));
    dc.DrawText(wxDateTime((time_t)minDay).Format("%d %b"), plot.x, plot.GetBottom() + 6);
    wxString endLabel = wxDateTime((time_t)maxDay).Format("%d %b");
    dc.DrawText(endLabel, plot.GetRight() - dc.GetTextExtent(endLabel).GetWidth(),
                plot.GetBottom() + 6);
}

void BtGraphPanel::DrawDeadline(wxDC& dc, const wxRect& plot)
{
    if (plot.width < 40 || m_deadlineBuckets.empty()) return;

    int maxCount = 0;
    for (int c : m_deadlineBuckets) maxCount = std::max(maxCount, c);
    if (maxCount == 0) {
        dc.DrawText("No tasks with deadlines.", plot.x + 10, plot.y + 10);
        return;
    }

    dc.SetTextForeground(wxColour(120, 120, 120));
    for (int i = 0; i <= 4; i++) {
        int y = plot.GetBottom() - (plot.height * i) / 4;
        dc.SetPen(wxPen(wxColour(232, 232, 232)));
        if (i) dc.DrawLine(plot.x + 1, y, plot.GetRight(), y);
        wxString lab = wxString::Format("%d", maxCount * i / 4);
        dc.DrawText(lab, plot.x - 8 - dc.GetTextExtent(lab).GetWidth(), y - 8);
    }

    const int n = (int)m_deadlineBuckets.size();
    int slot = std::max(6, plot.width / n);
    for (int i = 0; i < n; i++) {
        int h = (int)((double)m_deadlineBuckets[i] / maxCount * plot.height);
        wxRect bar(plot.x + 4 + i * slot, plot.GetBottom() - h, slot - 6, h);
        // nearer deadlines are the ones worth noticing
        wxColour c = (i <= 1) ? wxColour(214, 39, 40)
                   : (i <= 3) ? wxColour(255, 150, 60)
                              : wxColour(70, 130, 200);
        dc.SetBrush(wxBrush(c));
        dc.SetPen(wxPen(c));
        if (h > 0) dc.DrawRectangle(bar);

        dc.SetTextForeground(wxColour(110, 110, 110));
        wxString lbl = (i == n - 1) ? wxString::Format("%d+", i) : wxString::Format("%d", i);
        dc.DrawText(lbl, bar.x + 2, plot.GetBottom() + 6);
        if (m_deadlineBuckets[i] > 0) {
            wxString cnt = wxString::Format("%d", m_deadlineBuckets[i]);
            dc.SetTextForeground(wxColour(60, 60, 60));
            dc.DrawText(cnt, bar.x + 2, bar.y - 16);
        }
    }
    dc.SetTextForeground(wxColour(120, 120, 120));
    dc.DrawText("days until deadline", plot.x + plot.width / 2 - 50, plot.GetBottom() + 24);
}

void BtGraphPanel::DrawTasks(wxDC& dc, const wxRect& plot)
{
    if (plot.width < 40 || m_samples.size() < 2) {
        dc.DrawText("Collecting samples...", plot.x + 10, plot.y + 10);
        return;
    }

    int maxVal = 1;
    for (const auto& s : m_samples) maxVal = std::max(maxVal, s.total);

    dc.SetTextForeground(wxColour(120, 120, 120));
    for (int i = 0; i <= 4; i++) {
        int y = plot.GetBottom() - (plot.height * i) / 4;
        dc.SetPen(wxPen(wxColour(232, 232, 232)));
        if (i) dc.DrawLine(plot.x + 1, y, plot.GetRight(), y);
        wxString lab = wxString::Format("%d", maxVal * i / 4);
        dc.DrawText(lab, plot.x - 8 - dc.GetTextExtent(lab).GetWidth(), y - 8);
    }

    const size_t n = m_samples.size();
    auto X = [&](size_t i) { return plot.x + (int)(i * plot.width / (n - 1)); };
    auto Y = [&](int v)    { return plot.GetBottom() - (int)((double)v / maxVal * plot.height); };

    struct { const char* label; wxColour colour; int BtTaskSample::*field; } series[] = {
        { "total",   wxColour( 31, 119, 180), &BtTaskSample::total   },
        { "ready",   wxColour(255, 150,  60), &BtTaskSample::ready   },
        { "running", wxColour( 44, 160,  44), &BtTaskSample::running },
    };

    int legendY = plot.y + 4;
    for (const auto& s : series) {
        dc.SetPen(wxPen(s.colour, 2));
        for (size_t i = 1; i < n; i++)
            dc.DrawLine(X(i-1), Y(m_samples[i-1].*(s.field)),
                        X(i),   Y(m_samples[i].*(s.field)));
        dc.SetTextForeground(s.colour);
        dc.DrawText(s.label, plot.GetRight() - 70, legendY);
        legendY += 16;
    }

    dc.SetTextForeground(wxColour(120, 120, 120));
    double span = m_samples.back().time - m_samples.front().time;
    dc.DrawText(wxString::Format("%.0f min ago", span / 60.0),
                plot.x, plot.GetBottom() + 6);
    dc.DrawText("now", plot.GetRight() - 30, plot.GetBottom() + 6);
}

// ---------------------------------------------------------------------------
GraphsView::GraphsView(wxWindow* parent) : wxPanel(parent)
{
    m_choice = new wxChoice(this, wxID_ANY);
    m_choice->Append("Statistics (credit per day)");
    m_choice->Append("Deadline distribution");
    m_choice->Append("Tasks over time");
    m_choice->Append("Data transfer");
    m_choice->SetSelection(0);

    m_panel = new BtGraphPanel(this);

    m_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        m_panel->SetKind((BtGraphPanel::Kind)m_choice->GetSelection());
    });

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_choice, 0, wxALL, 6);
    sizer->Add(m_panel, 1, wxEXPAND);
    SetSizer(sizer);
}

void BtGraphPanel::DrawTransfer(wxDC& dc, const wxRect& plot)
{
    if (plot.width < 40 || m_samples.size() < 2) {
        dc.DrawText("Collecting samples...", plot.x + 10, plot.y + 10);
        return;
    }

    double maxRate = 1;
    for (const auto& s : m_samples)
        maxRate = std::max(maxRate, std::max(s.upBytesSec, s.downBytesSec));

    auto human = [](double v) {
        if (v >= 1e6) return wxString::Format("%.1f MB/s", v / 1e6);
        if (v >= 1e3) return wxString::Format("%.0f KB/s", v / 1e3);
        return wxString::Format("%.0f B/s", v);
    };

    dc.SetTextForeground(wxColour(120, 120, 120));
    for (int i = 0; i <= 4; i++) {
        int y = plot.GetBottom() - (plot.height * i) / 4;
        dc.SetPen(wxPen(wxColour(232, 232, 232)));
        if (i) dc.DrawLine(plot.x + 1, y, plot.GetRight(), y);
        wxString lab = human(maxRate * i / 4);
        dc.DrawText(lab, plot.x - 8 - dc.GetTextExtent(lab).GetWidth(), y - 8);
    }

    const size_t n = m_samples.size();
    auto X = [&](size_t i) { return plot.x + (int)(i * plot.width / (n - 1)); };
    auto Y = [&](double v) {
        return plot.GetBottom() - (int)(v / maxRate * plot.height);
    };

    struct { const char* label; wxColour colour; double BtTaskSample::*field; } series[] = {
        { "download", wxColour( 31, 119, 180), &BtTaskSample::downBytesSec },
        { "upload",   wxColour(255, 127,  14), &BtTaskSample::upBytesSec   },
    };

    int legendY = plot.y + 4;
    for (const auto& s : series) {
        dc.SetPen(wxPen(s.colour, 2));
        for (size_t i = 1; i < n; i++)
            dc.DrawLine(X(i-1), Y(m_samples[i-1].*(s.field)),
                        X(i),   Y(m_samples[i].*(s.field)));
        dc.SetTextForeground(s.colour);
        dc.DrawText(s.label, plot.GetRight() - 80, legendY);
        legendY += 16;
    }

    dc.SetTextForeground(wxColour(120, 120, 120));
    double span = m_samples.back().time - m_samples.front().time;
    dc.DrawText(wxString::Format("%.0f min ago", span / 60.0), plot.x, plot.GetBottom() + 6);
    dc.DrawText("now", plot.GetRight() - 30, plot.GetBottom() + 6);
}
