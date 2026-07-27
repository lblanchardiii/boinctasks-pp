// =============================================================================
// bt_history.h - persistent completed-task history (SQLite)
//
// The client only keeps a small rolling window of finished tasks
// (<get_old_results/>, ~30 records) and purges reported results from
// client_state.xml, so anything longer-lived has to be accumulated locally.
// One DB for every computer; the view queries a window instead of loading all
// of it, so memory stays flat no matter how large the history grows.
// =============================================================================
#pragma once
#include "bt_types.h"
#include <wx/string.h>
#include <vector>
#include <map>

struct sqlite3;

class BtHistory
{
public:
    BtHistory() = default;
    ~BtHistory();

    bool Open(const wxString& path);
    void Close();
    bool IsOpen() const { return m_db != nullptr; }

    // Insert ignoring duplicates (computer+name is the key).
    void Insert(const std::vector<BtHistoryRow>& rows);

    // Most recent rows, optionally restricted to a set of computers.
    // longTerm reads the archive plus the live window; otherwise just the
    // live window, which is what the History tab shows.
    std::vector<BtHistoryRow> Query(const std::vector<wxString>& computers,
                                    int limit = 5000, bool longTerm = false) const;

    // Move anything completed more than `days` ago out of the live window and
    // into the long term table, where retention can't remove it. 0 = never.
    int MoveToLongTerm(int days);

    // Drop anything completed more than `days` ago. 0 = keep everything.
    int Prune(int days);

    long long Count() const;

    // Completions since a point in time, keyed "computer\x1fproject". Feeds the
    // Tasks a day / Tasks a week columns - no project reports those, so
    // BoincTasks counts them from what it has actually seen finish.
    std::map<wxString, int> CountSince(double since) const;

private:
    sqlite3* m_db = nullptr;
};

// Read completed tasks out of a local client's job_log_*.txt files. Only works
// for a client on this machine, but it gives the local host real history from
// the first run instead of an empty table.
std::vector<BtHistoryRow> BtReadJobLogs(const wxString& computer,
                                        const wxString& dataDir);
