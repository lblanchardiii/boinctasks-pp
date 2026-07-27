// =============================================================================
// bt_rules.h - the Rules automation engine
//
// Modelled on the Windows app's Rules.h: a rule matches on computer / project /
// application, carries up to three conditions that must all hold, and fires one
// event when they do. Type, operator and event vocabularies are taken from
// eFMer's source and English language file so a Windows user's rules mean the
// same thing here.
//
// Temperature conditions are parsed and stored but never match: they come from
// TThrottle, which is Windows-only.
// =============================================================================
#pragma once
#include "bt_types.h"
#include <wx/colour.h>
#include <wx/string.h>
#include <functional>
#include <vector>
#include <map>

enum BtRuleType {
    BTR_NONE = 0, BTR_ELAPSED, BTR_CPU_PCT, BTR_PROGRESS, BTR_PROGRESS_DELTA,
    BTR_TIME_LEFT, BTR_CONNECTION, BTR_USE, BTR_TEMPERATURE, BTR_STATUS,
    BTR_WALLCLOCK, BTR_DEADLINE, BTR_TIME_LEFT_PROJECT, BTR_TYPE_COUNT
};

// The class decides how a value is entered and displayed.
enum BtRuleValueClass {
    BTVC_NONE = 0, BTVC_TIME, BTVC_PERCENT, BTVC_USE, BTVC_TEMPERATURE,
    BTVC_STATUS, BTVC_CONNECTION, BTVC_INTERVAL
};

enum BtRuleOp {
    BTOP_NONE = 0, BTOP_IS, BTOP_MORE, BTOP_LESS, BTOP_NOTEQUAL, BTOP_NOLONGEREQUAL
};

enum BtRuleEvent {
    BTE_NONE = 0, BTE_SUSPEND_PROJECT, BTE_RESUME_PROJECT, BTE_SNOOZE,
    BTE_CANCEL_SNOOZE, BTE_SNOOZE_GPU, BTE_CANCEL_SNOOZE_GPU, BTE_NO_NEW_WORK,
    BTE_ALLOW_NEW_WORK, BTE_SUSPEND_TASK, BTE_RUN_PROGRAM, BTE_SUSPEND_NETWORK,
    BTE_RESUME_NETWORK, BTE_EVENT_COUNT
};

enum BtRuleShow { BTSHOW_NONE = 0, BTSHOW_NOTICE, BTSHOW_LOG };

// A slice of the week, in seconds from Sunday 00:00. `invers` turns it into an
// exclusion, matching the Windows scheduler's inverted intervals.
struct BtRuleInterval
{
    int  startSec = 0;
    int  stopSec  = 0;
    bool invers   = false;
};

struct BtRuleCondition
{
    int    type  = BTR_NONE;
    int    op    = BTOP_NONE;
    double value = 0;          // seconds, percent, or count depending on type
    double value2 = 0;         // status rules carry "state,count"
};

struct BtRule
{
    wxString name, computer, project, application;
    bool     applicationPartial = false;   // substring instead of exact

    BtRuleCondition cond[3];
    std::vector<BtRuleInterval> intervals;   // wall-clock rules

    int      event  = BTE_NONE;
    wxString program;                        // BTE_RUN_PROGRAM
    int      show   = BTSHOW_LOG;
    int      snoozeMinutes = 60;
    bool     enabled = false;
    wxColour colour  = wxColour(255, 255, 200);

    // runtime, not persisted
    double   backoffUntil = 0;               // unix time; re-arm point
    bool     wasTrue      = false;           // edge detection for "no longer"
};

// What a fired rule wants done, handed back to the frame which owns the pollers.
struct BtRuleAction
{
    wxString ruleName;
    wxString computer;      // empty = every computer the rule matched
    wxString projectUrl;
    wxString taskName;
    int      event = BTE_NONE;
    wxString program;
    int      snoozeMinutes = 60;
    int      show = BTSHOW_LOG;
    wxString text;          // human-readable "why this fired"
    wxColour colour;
};

const char* BtRuleTypeName(int type);
const char* BtRuleEventName(int event);
const char* BtRuleOpName(int op);
int         BtRuleValueClassOf(int type);
wxString    BtRuleDescribe(const BtRule& r);
wxString    BtFormatRuleValue(int valueClass, double value, double value2);

// Persistence, alongside the rest of the settings.
std::vector<BtRule> BtLoadRules();
void                BtSaveRules(const std::vector<BtRule>& rules);

// -----------------------------------------------------------------------------
// The engine. Fed a snapshot of the world every cycle; returns what should
// happen. It owns no RPC connections - the frame dispatches the actions.
// -----------------------------------------------------------------------------
class BtRuleEngine
{
public:
    struct World
    {
        double now = 0;                                  // unix time
        const std::vector<BtTaskRow>*    tasks    = nullptr;
        const std::vector<BtProjectRow>* projects = nullptr;
        std::map<wxString, bool>         connected;      // computer -> up
        std::map<wxString, bool>         everConnected;  // seen up at least once
    };

    void SetRules(std::vector<BtRule> rules) { m_rules = std::move(rules); }
    const std::vector<BtRule>& Rules() const { return m_rules; }

    // Evaluate every enabled rule and return the actions to run. Rules that
    // fired go into backoff so a still-true condition does not fire each cycle.
    std::vector<BtRuleAction> Evaluate(const World& world);

private:
    bool MatchesFields(const BtRule& r, const wxString& computer,
                       const wxString& project, const wxString& application) const;
    std::vector<BtRule> m_rules;
    // progress per task from the previous cycle, for "Progress / min %"
    std::map<wxString, std::pair<double, double>> m_progressHistory;  // name -> (t, pct)
};
