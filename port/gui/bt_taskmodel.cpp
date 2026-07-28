#include "bt_taskmodel.h"
#include <wx/datetime.h>
#include <algorithm>
#include "bt_settings.h"

// ---------------------------------------------------------------------------
// formatting
// ---------------------------------------------------------------------------
static wxString fmtElapsed(double secs)
{
    if (secs <= 0) return "-";
    long s = (long)secs;
    if (!gSettings.timeFormatDays) {          // View tab: let hours accumulate
        return wxString::Format("%02ld:%02ld:%02ld", s / 3600, (s % 3600) / 60, s % 60);
    }
    long d = s / 86400;
    s %= 86400;
    wxString hms = wxString::Format("%02ld:%02ld:%02ld", s / 3600, (s % 3600) / 60, s % 60);
    return d > 0 ? wxString::Format("%02ldd,%s", d, hms) : hms;
}

// Windows shows these in MB with two decimals
static wxString fmtBytes(double bytes)
{
    if (bytes <= 0) return "-";
    return wxString::Format("%.2f MB", bytes / (1024.0 * 1024.0));
}

static wxString fmtDeadline(double t)
{
    if (t <= 0) return "-";
    return wxDateTime((time_t)t).Format("%m/%d/%Y %I:%M:%S %p");
}

// ---------------------------------------------------------------------------
void BtTaskModel::Update(std::vector<BtTaskRow>&& rows)
{
    // Bucket incoming rows by group key. Which fields make up the key comes
    // from Extra > "Filter (combine) tasks on" - dropping one lets tasks that
    // differ only in that field collapse together. With every field off each
    // task stands alone, which is the flat list.
    const bool byComputer = gSettings.combineComputer;
    const bool byProject  = gSettings.combineProject;
    const bool byApp      = gSettings.combineApplication;
    const bool byStatus   = gSettings.combineStatus;
    const bool combineAny = byComputer || byProject || byApp || byStatus;

    std::map<wxString, std::vector<BtTaskRow*>> incoming;
    long serial = 0;
    for (auto& r : rows) {
        wxString key;
        if (!combineAny) {
            key = wxString::Format("%08ld", serial++);   // never groups
        } else {
            if (byComputer) key += r.computer;
            key += "\x1f";
            if (byProject)  key += r.project;
            key += "\x1f";
            if (byApp)      key += r.application;
            key += "\x1f";
            if (byStatus)   key += r.status;
        }
        incoming[key].push_back(&r);
    }
    m_taskCount = rows.size();

    // drop groups that no longer exist
    for (size_t i = m_groups.size(); i-- > 0; ) {
        if (incoming.find(m_groups[i]->key) == incoming.end()) {
            m_byKey.erase(m_groups[i]->key);
            m_groups.erase(m_groups.begin() + i);
        }
    }

    for (auto& kv : incoming) {
        const wxString& key = kv.first;
        auto& list = kv.second;

        BtGroupNode* g;
        auto it = m_byKey.find(key);
        if (it == m_byKey.end()) {
            auto owned = std::make_unique<BtGroupNode>();
            g = owned.get();
            g->key         = key;
            g->computer    = list[0]->computer;
            g->project     = list[0]->project;
            g->application = list[0]->application;
            g->status      = list[0]->status;
            // A field left out of the key can hold different values across the
            // group; say so rather than showing whichever task happened to be
            // first.
            auto mixed = [&list](wxString BtTaskRow::*field) {
                for (size_t i = 1; i < list.size(); i++)
                    if (list[i]->*field != list[0]->*field) return true;
                return false;
            };
            if (!byComputer && mixed(&BtTaskRow::computer))    g->computer    = "(mixed)";
            if (!byProject  && mixed(&BtTaskRow::project))     g->project     = "(mixed)";
            if (!byApp      && mixed(&BtTaskRow::application)) g->application = "(mixed)";
            if (!byStatus   && mixed(&BtTaskRow::status))      g->status      = "(mixed)";
            m_groups.push_back(std::move(owned));
            m_byKey[key] = g;
        } else {
            g = it->second;
        }

        g->running = list[0]->running;
        g->error   = list[0]->error;
        g->isGpu   = list[0]->isGpu;
        g->state   = list[0]->state;
        // one warned task is enough to flag the whole collapsed group,
        // otherwise a warning hides inside a group nobody expands
        g->warning = false;
        for (const auto* t : list) if (t->warning) { g->warning = true; break; }

        // aggregates: averages over the group, earliest deadline
        double cpu = 0, el = 0, ct = 0, tl = 0, pr = 0, use = 0, dl = 0;
        g->tasks.clear();
        g->tasks.reserve(list.size());
        for (auto* r : list) {
            cpu += r->cpuPct; el += r->elapsed; ct += r->cpuTime;
            tl  += r->timeLeft; pr += r->progress; use += r->useCpus;
            if (r->deadline > 0 && (dl == 0 || r->deadline < dl)) dl = r->deadline;
            g->tasks.push_back(*r);
        }
        double n = (double)list.size();
        g->cpuPct = cpu / n; g->elapsed = el / n; g->cpuTime = ct / n;
        g->timeLeft = tl / n; g->progress = pr / n; g->useCpus = use / n;
        g->deadline = dl;

        // keep tasks in a stable order so rows don't dance between polls
        std::sort(g->tasks.begin(), g->tasks.end(),
                  [](const BtTaskRow& a, const BtTaskRow& b) { return a.name < b.name; });
    }

    ApplySort();
    Reflatten();
}

void BtTaskModel::Reflatten()
{
    size_t before = m_flat.size();
    m_flat.clear();
    for (auto& g : m_groups) {
        m_flat.push_back({g.get(), -1});
        if (g->expanded && g->tasks.size() > 1) {
            for (size_t i = 0; i < g->tasks.size(); i++)
                m_flat.push_back({g.get(), (int)i});
        }
    }

    if (m_flat.size() != before)
        Reset((unsigned)m_flat.size());
    else
        for (unsigned i = 0; i < (unsigned)m_flat.size(); i++) RowChanged(i);
}

bool BtTaskModel::ToggleRow(unsigned row)
{
    if (row >= m_flat.size()) return false;
    const FlatRow& fr = m_flat[row];
    if (fr.task >= 0 || fr.group->tasks.size() <= 1) return false;
    fr.group->expanded = !fr.group->expanded;
    Reflatten();
    return true;
}

// ---------------------------------------------------------------------------
void BtTaskModel::GetValueByRow(wxVariant& v, unsigned row, unsigned col) const
{
    if (row >= m_flat.size()) { v = wxString(); return; }
    const FlatRow& fr = m_flat[row];
    BtGroupNode* g = fr.group;

    // an expanded group's child, or a group of one, shows the task itself
    const BtTaskRow* r = nullptr;
    if (fr.task >= 0)              r = &g->tasks[fr.task];
    else if (g->tasks.size() == 1) r = &g->tasks[0];

    if (r) {
        switch (col) {
            case COL_PROJECT:  v = (fr.task >= 0) ? wxString("      ") + r->project
                                                  : r->project; return;
            case COL_APP:      v = r->application; return;
            case COL_NAME:     v = r->name; return;
            case COL_CPUPCT:   v = r->cpuPct > 0
                                 ? wxString::Format("%.*f", gSettings.cpuDigits, r->cpuPct)
                                 : wxString("-"); return;
            case COL_ELAPSED:  v = fmtElapsed(r->elapsed) + " (" + fmtElapsed(r->cpuTime) + ")";
                               return;
            case COL_TIMELEFT: v = fmtElapsed(r->timeLeft); return;
            case COL_PROGRESS: v = (long)(r->progress + 0.5); return;
            case COL_DEADLINE: v = fmtDeadline(r->deadline); return;
            case COL_USE:      v = r->useCpus <= 0 ? wxString("")
                             : gSettings.condenseUse
                                 ? wxString::Format("%.3gC", r->useCpus)
                                 : wxString::Format("%.2f CPU", r->useCpus); return;
            case COL_STATUS:   v = r->status; return;
            case COL_COMPUTER: v = r->computer; return;
            case COL_ACCOUNT:  v = r->account; return;
            case COL_CHECKPOINT: v = r->checkpoint > 0 ? fmtElapsed(r->checkpoint)
                                                       : wxString("-"); return;
            case COL_RECEIVED: v = r->received > 0
                                 ? wxDateTime((time_t)r->received)
                                     .Format("%m/%d/%Y %I:%M:%S %p")
                                 : wxString("-"); return;
            case COL_DEBT:     v = wxString::Format("%.2f", r->debt); return;
            case COL_VIRTMEM:  v = fmtBytes(r->swapSize); return;
            case COL_MEMORY:   v = fmtBytes(r->memSize); return;
        }
        v = wxString();
        return;
    }

    switch (col) {
        case COL_PROJECT:  v = g->project; return;
        case COL_APP:      v = g->application; return;
        case COL_NAME:     v = wxString::Format("%s%zu [Tasks], double click to %s",
                                 g->expanded ? "- " : "+ ", g->tasks.size(),
                                 g->expanded ? "collapse" : "expand"); return;
        case COL_CPUPCT:   v = g->cpuPct > 0
                             ? wxString::Format("%.*f", gSettings.cpuDigits, g->cpuPct)
                             : wxString("-"); return;
        case COL_ELAPSED:  v = g->elapsed > 0
                             ? fmtElapsed(g->elapsed) + " (" + fmtElapsed(g->cpuTime) + ")"
                             : wxString("- (-)"); return;
        case COL_TIMELEFT: v = fmtElapsed(g->timeLeft); return;
        case COL_PROGRESS: v = (long)(g->progress + 0.5); return;
        case COL_DEADLINE: v = fmtDeadline(g->deadline); return;
        case COL_USE:      v = wxString(); return;
        case COL_STATUS:   v = g->status; return;
        case COL_COMPUTER: v = g->computer; return;
        // aggregate rows leave the per-task detail columns blank
        case COL_ACCOUNT:
        case COL_CHECKPOINT:
        case COL_RECEIVED:
        case COL_DEBT:
        case COL_VIRTMEM:
        case COL_MEMORY:   v = wxString(); return;
    }
    v = wxString();
}

bool BtTaskModel::GetAttrByRow(unsigned row, unsigned,
                               wxDataViewItemAttr& attr) const
{
    if (row >= m_flat.size()) return false;
    const FlatRow& fr = m_flat[row];
    if (!gSettings.colourRows) return false;

    bool warned = fr.group->warning;
    int  state = fr.group->state;
    bool gpu   = fr.group->isGpu;
    if (fr.task >= 0) {
        state  = fr.group->tasks[fr.task].state;
        gpu    = fr.group->tasks[fr.task].isGpu;
        warned = fr.group->tasks[fr.task].warning;
    }
    // A warning outranks the status colour - the whole point is that it stands
    // out against rows that are merely running or waiting.
    if (warned && gSettings.warnColour.IsOk()) {
        attr.SetBackgroundColour(gSettings.warnColour);
        return true;
    }

    if (state < 0 || state >= BTS_COUNT) return false;

    wxColour c = gpu ? gSettings.taskColourGpu[state] : gSettings.taskColour[state];
    if (!c.IsOk() || c == wxColour(255, 255, 255)) return false;   // plain rows
    attr.SetBackgroundColour(c);
    return true;
}

std::vector<BtTaskRow> BtTaskModel::TasksForRows(const std::vector<unsigned>& rows) const
{
    std::vector<BtTaskRow> out;
    std::map<wxString, bool> seen;      // dedupe: a group row plus its children
    for (unsigned row : rows) {
        if (row >= m_flat.size()) continue;
        const FlatRow& fr = m_flat[row];
        if (fr.task >= 0) {
            const BtTaskRow& t = fr.group->tasks[fr.task];
            if (!seen[t.computer + t.name]) { seen[t.computer + t.name] = true; out.push_back(t); }
        } else {
            for (const auto& t : fr.group->tasks)
                if (!seen[t.computer + t.name]) { seen[t.computer + t.name] = true; out.push_back(t); }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// sorting
// ---------------------------------------------------------------------------
namespace {

// Numeric columns must compare as numbers, not as their formatted text.
bool ColIsNumeric(int col)
{
    switch (col) {
        case BtTaskModel::COL_CPUPCT:
        case BtTaskModel::COL_ELAPSED:
        case BtTaskModel::COL_TIMELEFT:
        case BtTaskModel::COL_PROGRESS:
        case BtTaskModel::COL_DEADLINE:
        case BtTaskModel::COL_USE:
            return true;
        default:
            return false;
    }
}

double NumOf(const BtTaskRow& r, int col)
{
    switch (col) {
        case BtTaskModel::COL_CPUPCT:   return r.cpuPct;
        case BtTaskModel::COL_ELAPSED:  return r.elapsed;
        case BtTaskModel::COL_TIMELEFT: return r.timeLeft;
        case BtTaskModel::COL_PROGRESS: return r.progress;
        case BtTaskModel::COL_DEADLINE: return r.deadline;
        case BtTaskModel::COL_USE:      return r.useCpus;
    }
    return 0;
}

wxString TextOf(const BtTaskRow& r, int col)
{
    switch (col) {
        case BtTaskModel::COL_PROJECT:  return r.project;
        case BtTaskModel::COL_APP:      return r.application;
        case BtTaskModel::COL_NAME:     return r.name;
        case BtTaskModel::COL_STATUS:   return r.status;
        case BtTaskModel::COL_COMPUTER: return r.computer;
    }
    return wxString();
}

double NumOfGroup(const BtGroupNode& g, int col)
{
    switch (col) {
        case BtTaskModel::COL_CPUPCT:   return g.cpuPct;
        case BtTaskModel::COL_ELAPSED:  return g.elapsed;
        case BtTaskModel::COL_TIMELEFT: return g.timeLeft;
        case BtTaskModel::COL_PROGRESS: return g.progress;
        case BtTaskModel::COL_DEADLINE: return g.deadline;
        case BtTaskModel::COL_USE:      return g.useCpus;
    }
    return 0;
}

wxString TextOfGroup(const BtGroupNode& g, int col)
{
    switch (col) {
        case BtTaskModel::COL_PROJECT:  return g.project;
        case BtTaskModel::COL_APP:      return g.application;
        case BtTaskModel::COL_NAME:     return wxString::Format("%09zu", g.tasks.size());
        case BtTaskModel::COL_STATUS:   return g.status;
        case BtTaskModel::COL_COMPUTER: return g.computer;
    }
    return wxString();
}

} // namespace

void BtTaskModel::ApplySort()
{
    const int  col = m_sortCol;
    const bool asc = m_sortAsc;

    if (col < 0) {      // natural order
        std::sort(m_groups.begin(), m_groups.end(),
                  [](const std::unique_ptr<BtGroupNode>& a,
                     const std::unique_ptr<BtGroupNode>& b) { return a->key < b->key; });
        for (auto& g : m_groups)
            std::sort(g->tasks.begin(), g->tasks.end(),
                      [](const BtTaskRow& a, const BtTaskRow& b) { return a.name < b.name; });
        return;
    }

    std::sort(m_groups.begin(), m_groups.end(),
        [col, asc](const std::unique_ptr<BtGroupNode>& a,
                   const std::unique_ptr<BtGroupNode>& b) {
            if (col == COL_STATUS) {
                int ra = gSettings.StatusRank(a->status);
                int rb = gSettings.StatusRank(b->status);
                if (ra == rb) return a->key < b->key;
                return asc ? ra < rb : ra > rb;
            }
            bool less = ColIsNumeric(col)
                      ? NumOfGroup(*a, col) < NumOfGroup(*b, col)
                      : TextOfGroup(*a, col).CmpNoCase(TextOfGroup(*b, col)) < 0;
            bool eq   = ColIsNumeric(col)
                      ? NumOfGroup(*a, col) == NumOfGroup(*b, col)
                      : TextOfGroup(*a, col).CmpNoCase(TextOfGroup(*b, col)) == 0;
            if (eq) return a->key < b->key;         // stable tiebreak
            return asc ? less : !less;
        });

    for (auto& g : m_groups)
        std::sort(g->tasks.begin(), g->tasks.end(),
            [col, asc](const BtTaskRow& a, const BtTaskRow& b) {
                if (col == COL_STATUS) {
                    int ra = gSettings.StatusRank(a.status);
                    int rb = gSettings.StatusRank(b.status);
                    if (ra == rb) return a.name < b.name;
                    return asc ? ra < rb : ra > rb;
                }
                bool less = ColIsNumeric(col) ? NumOf(a, col) < NumOf(b, col)
                                              : TextOf(a, col).CmpNoCase(TextOf(b, col)) < 0;
                bool eq   = ColIsNumeric(col) ? NumOf(a, col) == NumOf(b, col)
                                              : TextOf(a, col).CmpNoCase(TextOf(b, col)) == 0;
                if (eq) return a.name < b.name;
                return asc ? less : !less;
            });
}

void BtTaskModel::SortBy(int column)
{
    if (column == m_sortCol) m_sortAsc = !m_sortAsc;
    else { m_sortCol = column; m_sortAsc = true; }
    ApplySort();
    Reflatten();
}
