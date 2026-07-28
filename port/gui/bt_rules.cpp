#include "bt_rules.h"
#include "bt_config.h"
#include <wx/fileconf.h>
#include <wx/utils.h>
#include <wx/tokenzr.h>
#include <memory>
#include <cmath>

// Labels come from the Windows English language file (BoincTasks_ENU.btlang,
// GroupDialogRules) so rules read identically on both platforms.
static const char* kTypeNames[BTR_TYPE_COUNT] = {
    "", "Elapsed Time", "CPU %", "Progress %", "Progress / min %", "Time Left",
    "Connection", "Use", "Temperature", "Status", "Wall-clock Time", "Deadline",
    "Time Left Project"
};

static const char* kEventNames[BTE_EVENT_COUNT] = {
    "", "Suspend project", "Resume project", "Snooze", "X Snooze", "Snooze GPU",
    "X Snooze GPU", "No more work", "Allow new work", "Suspend task",
    "Run program", "Suspend network", "Resume network"
};

const char* BtRuleTypeName(int type)
{
    return (type > 0 && type < BTR_TYPE_COUNT) ? kTypeNames[type] : "";
}

const char* BtRuleEventName(int event)
{
    return (event > 0 && event < BTE_EVENT_COUNT) ? kEventNames[event] : "";
}

const char* BtRuleOpName(int op)
{
    switch (op) {
        case BTOP_IS:             return "=";
        case BTOP_MORE:           return ">";
        case BTOP_LESS:           return "<";
        case BTOP_NOTEQUAL:       return "<>";
        case BTOP_NOLONGEREQUAL:  return "=<>";
    }
    return "";
}

int BtRuleValueClassOf(int type)
{
    switch (type) {
        case BTR_ELAPSED:
        case BTR_TIME_LEFT:
        case BTR_DEADLINE:
        case BTR_TIME_LEFT_PROJECT:  return BTVC_TIME;
        case BTR_CPU_PCT:
        case BTR_PROGRESS:
        case BTR_PROGRESS_DELTA:     return BTVC_PERCENT;
        case BTR_USE:                return BTVC_USE;
        case BTR_TEMPERATURE:        return BTVC_TEMPERATURE;
        case BTR_STATUS:             return BTVC_STATUS;
        case BTR_CONNECTION:         return BTVC_CONNECTION;
        case BTR_WALLCLOCK:          return BTVC_INTERVAL;
    }
    return BTVC_NONE;
}

wxString BtFormatRuleValue(int valueClass, double value, double value2)
{
    switch (valueClass) {
        case BTVC_TIME: {
            long s = (long)value, d = s / 86400;
            s %= 86400;
            wxString t = wxString::Format("%02ld:%02ld:%02ld", s/3600, (s%3600)/60, s%60);
            return d > 0 ? wxString::Format("%ldd,%s", d, t) : t;
        }
        case BTVC_PERCENT:     return wxString::Format("%.2f %%", value);
        case BTVC_TEMPERATURE: return wxString::Format("%.0f C", value);
        case BTVC_USE:         return wxString::Format("%.2f", value);
        case BTVC_STATUS:      return wxString::Format("%d,%d", (int)value, (int)value2);
        case BTVC_CONNECTION:  return "";
    }
    return "";
}

wxString BtRuleDescribe(const BtRule& r)
{
    wxString out;
    for (const auto& c : r.cond) {
        if (c.type == BTR_NONE) continue;
        if (!out.IsEmpty()) out += " and ";
        out += BtRuleTypeName(c.type);
        if (c.type == BTR_WALLCLOCK) {
            out += wxString::Format(" (%zu interval(s))", r.intervals.size());
        } else {
            out += " ";
            out += BtRuleOpName(c.op);
            out += " " + BtFormatRuleValue(BtRuleValueClassOf(c.type), c.value, c.value2);
        }
    }
    if (out.IsEmpty()) out = "(no conditions)";
    out += " -> ";
    out += BtRuleEventName(r.event);
    return out;
}

// ---------------------------------------------------------------------------
// persistence
// ---------------------------------------------------------------------------
static wxFileConfig* OpenConfig()
{
    return new wxFileConfig(BTPP_SHORT, "eFMer", BtConfigPath());
}

std::vector<BtRule> BtLoadRules()
{
    std::unique_ptr<wxFileConfig> cfg(OpenConfig());
    std::vector<BtRule> rules;
    long count = cfg->ReadLong("/Rules/count", 0);
    for (long i = 0; i < count; i++) {
        wxString p = wxString::Format("/Rules/%ld/", i);
        BtRule r;
        r.name        = cfg->Read(p + "name", "");
        if (r.name.IsEmpty()) continue;
        r.computer    = cfg->Read(p + "computer", "");
        r.project     = cfg->Read(p + "project", "");
        r.application = cfg->Read(p + "application", "");
        r.applicationPartial = cfg->ReadBool(p + "app_partial", false);
        for (int c = 0; c < 3; c++) {
            r.cond[c].type   = (int)cfg->ReadLong(wxString::Format("%stype%d", p, c), BTR_NONE);
            r.cond[c].op     = (int)cfg->ReadLong(wxString::Format("%sop%d", p, c), BTOP_NONE);
            r.cond[c].value  = cfg->ReadDouble(wxString::Format("%sval%d", p, c), 0);
            r.cond[c].value2 = cfg->ReadDouble(wxString::Format("%sval2_%d", p, c), 0);
        }
        // intervals: "start:stop:invers,start:stop:invers"
        wxStringTokenizer tk(cfg->Read(p + "intervals", ""), ",");
        while (tk.HasMoreTokens()) {
            wxStringTokenizer f(tk.GetNextToken(), ":");
            BtRuleInterval iv;
            long v = 0;
            if (f.HasMoreTokens() && f.GetNextToken().ToLong(&v)) iv.startSec = (int)v;
            if (f.HasMoreTokens() && f.GetNextToken().ToLong(&v)) iv.stopSec  = (int)v;
            if (f.HasMoreTokens() && f.GetNextToken().ToLong(&v)) iv.invers   = (v != 0);
            r.intervals.push_back(iv);
        }
        r.event         = (int)cfg->ReadLong(p + "event", BTE_NONE);
        r.program       = cfg->Read(p + "program", "");
        r.show          = (int)cfg->ReadLong(p + "show", BTSHOW_LOG);
        r.snoozeMinutes = (int)cfg->ReadLong(p + "snooze", 60);
        r.enabled       = cfg->ReadBool(p + "enabled", false);
        wxString col = cfg->Read(p + "colour", "");
        if (!col.IsEmpty()) { wxColour c(col); if (c.IsOk()) r.colour = c; }
        rules.push_back(r);
    }
    return rules;
}

void BtSaveRules(const std::vector<BtRule>& rules)
{
    std::unique_ptr<wxFileConfig> cfg(OpenConfig());
    cfg->DeleteGroup("/Rules");
    cfg->Write("/Rules/count", (long)rules.size());
    for (size_t i = 0; i < rules.size(); i++) {
        const BtRule& r = rules[i];
        wxString p = wxString::Format("/Rules/%zu/", i);
        cfg->Write(p + "name",        r.name);
        cfg->Write(p + "computer",    r.computer);
        cfg->Write(p + "project",     r.project);
        cfg->Write(p + "application", r.application);
        cfg->Write(p + "app_partial", r.applicationPartial);
        for (int c = 0; c < 3; c++) {
            cfg->Write(wxString::Format("%stype%d", p, c), (long)r.cond[c].type);
            cfg->Write(wxString::Format("%sop%d",   p, c), (long)r.cond[c].op);
            cfg->Write(wxString::Format("%sval%d",  p, c), r.cond[c].value);
            cfg->Write(wxString::Format("%sval2_%d",p, c), r.cond[c].value2);
        }
        wxString iv;
        for (const auto& it : r.intervals) {
            if (!iv.IsEmpty()) iv += ",";
            iv += wxString::Format("%d:%d:%d", it.startSec, it.stopSec, it.invers ? 1 : 0);
        }
        cfg->Write(p + "intervals", iv);
        cfg->Write(p + "event",     (long)r.event);
        cfg->Write(p + "program",   r.program);
        cfg->Write(p + "show",      (long)r.show);
        cfg->Write(p + "snooze",    (long)r.snoozeMinutes);
        cfg->Write(p + "enabled",   r.enabled);
        cfg->Write(p + "colour",    r.colour.GetAsString(wxC2S_HTML_SYNTAX));
    }
    cfg->Flush();
}

// ---------------------------------------------------------------------------
// evaluation
// ---------------------------------------------------------------------------
namespace {

bool Compare(int op, double lhs, double rhs)
{
    switch (op) {
        case BTOP_IS:       return std::fabs(lhs - rhs) < 1e-9;
        case BTOP_MORE:     return lhs > rhs;
        case BTOP_LESS:     return lhs < rhs;
        case BTOP_NOTEQUAL: return std::fabs(lhs - rhs) >= 1e-9;
    }
    return false;
}

// Seconds since Sunday 00:00 for the local clock.
int SecondsIntoWeek(double now)
{
    time_t t = (time_t)now;
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);        // MSVC/MinGW argument order is reversed
#else
    localtime_r(&t, &lt);
#endif
    return lt.tm_wday * 86400 + lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
}

bool InAnyInterval(const std::vector<BtRuleInterval>& intervals, int weekSec)
{
    // An inverted interval vetoes; otherwise any plain interval that contains
    // the moment matches. With only inverted intervals present, being outside
    // all of them is the match - that is how the Windows scheduler behaves.
    bool sawPlain = false, inPlain = false;
    for (const auto& iv : intervals) {
        bool inside = iv.stopSec > iv.startSec
                    ? (weekSec >= iv.startSec && weekSec < iv.stopSec)
                    : (weekSec >= iv.startSec || weekSec < iv.stopSec);  // wraps
        if (iv.invers) { if (inside) return false; }
        else           { sawPlain = true; if (inside) inPlain = true; }
    }
    return sawPlain ? inPlain : true;
}

} // namespace

bool BtRuleEngine::MatchesFields(const BtRule& r, const wxString& computer,
                                 const wxString& project,
                                 const wxString& application) const
{
    // Empty means "any". Computer is a partial match (the Windows behaviour:
    // "Linux" matches Linux1 and Linux2); project and application are exact
    // unless the rule asks for a partial application match.
    if (!r.computer.IsEmpty() && computer.Find(r.computer) == wxNOT_FOUND) return false;
    if (!r.project.IsEmpty() && project != r.project) return false;
    if (!r.application.IsEmpty()) {
        if (r.applicationPartial) {
            if (application.Find(r.application) == wxNOT_FOUND) return false;
        } else if (application != r.application) {
            return false;
        }
    }
    return true;
}

std::vector<BtRuleAction> BtRuleEngine::Evaluate(const World& world)
{
    std::vector<BtRuleAction> actions;
    if (!world.tasks) return actions;

    const int weekSec = SecondsIntoWeek(world.now);

    // Refresh the progress history first so "Progress / min %" has a baseline
    // even on the cycle a task first appears.
    std::map<wxString, std::pair<double, double>> nextHistory;
    for (const auto& t : *world.tasks)
        nextHistory[t.computer + "\x1f" + t.name] = { world.now, t.progress };

    for (auto& rule : m_rules) {
        if (!rule.enabled || rule.event == BTE_NONE) continue;
        if (world.now < rule.backoffUntil) continue;

        bool fired = false;
        BtRuleAction act;
        act.ruleName       = rule.name;
        act.event          = rule.event;
        act.program        = rule.program;
        act.snoozeMinutes  = rule.snoozeMinutes;
        act.show           = rule.show;
        act.colour         = rule.colour;
        act.computer       = rule.computer;

        // ---- computer-scoped conditions ---------------------------------
        for (const auto& c : rule.cond) {
            if (c.type == BTR_CONNECTION) {
                for (const auto& kv : world.connected) {
                    if (!rule.computer.IsEmpty() &&
                        kv.first.Find(rule.computer) == wxNOT_FOUND) continue;
                    bool down = !kv.second;
                    // "<>" is any disconnected client; "=<>" only counts one
                    // that had connected earlier, so startup does not fire it
                    bool hit = (c.op == BTOP_NOTEQUAL && down) ||
                               (c.op == BTOP_NOLONGEREQUAL && down &&
                                world.everConnected.count(kv.first) > 0);
                    if (!hit) continue;
                    fired = true;
                    act.computer = kv.first;
                    act.text = "Connection: " + kv.first + " not connected";
                    break;
                }
            } else if (c.type == BTR_WALLCLOCK) {
                if (!rule.intervals.empty() && InAnyInterval(rule.intervals, weekSec)) {
                    fired = true;
                    act.text = "Wall-clock interval";
                }
            } else if (c.type == BTR_TIME_LEFT_PROJECT && world.projects) {
                // sum the remaining time of a project's tasks per computer
                std::map<wxString, double> left;
                for (const auto& t : *world.tasks) {
                    if (!MatchesFields(rule, t.computer, t.project, t.application)) continue;
                    left[t.computer] += t.timeLeft;
                }
                for (const auto& kv : left) {
                    if (!Compare(c.op, kv.second, c.value)) continue;
                    fired = true;
                    act.computer = kv.first;
                    act.text = wxString::Format("Time Left Project %s %s on %s",
                                 BtRuleOpName(c.op),
                                 BtFormatRuleValue(BTVC_TIME, c.value, 0), kv.first);
                    break;
                }
            } else if (c.type == BTR_STATUS) {
                // value = task state, value2 = how many of them
                int want = (int)c.value, need = (int)c.value2;
                std::map<wxString, int> hits;
                for (const auto& t : *world.tasks) {
                    if (!MatchesFields(rule, t.computer, t.project, t.application)) continue;
                    if (t.state == want) hits[t.computer]++;
                }
                for (const auto& kv : hits) {
                    if (!Compare(c.op == BTOP_NONE ? BTOP_MORE : c.op,
                                 (double)kv.second, (double)need)) continue;
                    fired = true;
                    act.computer = kv.first;
                    act.text = wxString::Format("Status %d count %d on %s",
                                                want, kv.second, kv.first);
                    break;
                }
            }
            if (fired) break;
        }

        // ---- task-scoped conditions --------------------------------------
        if (!fired) {
            for (const auto& t : *world.tasks) {
                if (!MatchesFields(rule, t.computer, t.project, t.application)) continue;

                bool all = false;
                for (const auto& c : rule.cond) {
                    if (c.type == BTR_NONE) continue;
                    double actual = 0;
                    switch (c.type) {
                        case BTR_ELAPSED:   actual = t.elapsed;  break;
                        case BTR_CPU_PCT:   actual = t.cpuPct;   break;
                        case BTR_PROGRESS:  actual = t.progress; break;
                        case BTR_TIME_LEFT: actual = t.timeLeft; break;
                        case BTR_USE:       actual = t.useCpus;  break;
                        case BTR_DEADLINE:
                            actual = t.deadline > 0 ? t.deadline - world.now : 0;
                            break;
                        case BTR_PROGRESS_DELTA: {
                            auto it = m_progressHistory.find(t.computer + "\x1f" + t.name);
                            if (it == m_progressHistory.end()) { all = false; goto nextTask; }
                            double dt = world.now - it->second.first;
                            if (dt < 30) { all = false; goto nextTask; }
                            actual = (t.progress - it->second.second) / (dt / 60.0);
                            break;
                        }
                        case BTR_TEMPERATURE:    // TThrottle only - never matches
                        case BTR_CONNECTION:
                        case BTR_WALLCLOCK:
                        case BTR_STATUS:
                        case BTR_TIME_LEFT_PROJECT:
                            all = false; goto nextTask;
                        default: break;
                    }
                    if (!Compare(c.op, actual, c.value)) { all = false; goto nextTask; }
                    all = true;
                }
                if (!all) continue;

                fired          = true;
                act.computer   = t.computer;
                act.projectUrl = t.projectUrl;
                act.taskName   = t.name;
                act.text       = t.name + " on " + t.computer + ": " + BtRuleDescribe(rule);
                break;
            nextTask:;
            }
        }

        if (!fired) { rule.wasTrue = false; continue; }
        rule.wasTrue = true;

        // Project events need a project URL; take it from the rule's project
        // when the condition was not task-scoped.
        if (act.projectUrl.IsEmpty() && world.projects && !rule.project.IsEmpty()) {
            for (const auto& p : *world.projects)
                if (p.project == rule.project &&
                    (act.computer.IsEmpty() || p.computer == act.computer)) {
                    act.projectUrl = p.masterUrl;
                    break;
                }
        }
        if (act.text.IsEmpty()) act.text = BtRuleDescribe(rule);
        actions.push_back(act);

        // Don't re-fire while the condition stays true. Wall-clock rules re-arm
        // after a minute so an interval's edge is not missed; the rest back off
        // for the snooze window, or five minutes when there isn't one.
        double backoff = rule.cond[0].type == BTR_WALLCLOCK ? 60
                       : (rule.snoozeMinutes > 0 ? rule.snoozeMinutes * 60.0 : 300);
        rule.backoffUntil = world.now + backoff;
    }

    m_progressHistory.swap(nextHistory);
    return actions;
}
