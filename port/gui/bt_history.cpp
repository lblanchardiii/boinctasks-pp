#include "bt_history.h"
#include <sqlite3.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>

// ---------------------------------------------------------------------------
BtHistory::~BtHistory() { Close(); }

void BtHistory::Close()
{
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

bool BtHistory::Open(const wxString& path)
{
    Close();
    if (sqlite3_open(path.mb_str(), &m_db) != SQLITE_OK) {
        Close();
        return false;
    }
    // Durability matters less here than not stalling the UI: history is
    // re-derivable from the clients, so trade fsyncs for speed.
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    const char* schema =
        "CREATE TABLE IF NOT EXISTS history("
        "  computer TEXT NOT NULL,"
        "  name TEXT NOT NULL,"
        "  project TEXT,"
        "  application TEXT,"
        "  elapsed REAL,"
        "  cpu_time REAL,"
        "  completed_at REAL,"
        "  exit_status INTEGER,"
        "  status TEXT,"
        "  PRIMARY KEY(computer, name));"
        "CREATE INDEX IF NOT EXISTS idx_hist_completed"
        "  ON history(completed_at DESC);"
        "CREATE INDEX IF NOT EXISTS idx_hist_computer"
        "  ON history(computer, completed_at DESC);"
        // Rows moved out of the live window live here. Retention never touches
        // this table - moving to long term is how you keep something.
        "CREATE TABLE IF NOT EXISTS longterm("
        "  computer TEXT NOT NULL,"
        "  name TEXT NOT NULL,"
        "  project TEXT,"
        "  application TEXT,"
        "  elapsed REAL,"
        "  cpu_time REAL,"
        "  completed_at REAL,"
        "  exit_status INTEGER,"
        "  status TEXT,"
        "  PRIMARY KEY(computer, name));"
        "CREATE INDEX IF NOT EXISTS idx_lt_completed"
        "  ON longterm(completed_at DESC);"
        "CREATE INDEX IF NOT EXISTS idx_lt_computer"
        "  ON longterm(computer, completed_at DESC);";
    char* err = nullptr;
    if (sqlite3_exec(m_db, schema, nullptr, nullptr, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err);
        Close();
        return false;
    }
    return true;
}

void BtHistory::Insert(const std::vector<BtHistoryRow>& rows)
{
    if (!m_db || rows.empty()) return;

    sqlite3_exec(m_db, "BEGIN;", nullptr, nullptr, nullptr);
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT OR IGNORE INTO history"
        "(computer,name,project,application,elapsed,cpu_time,completed_at,"
        " exit_status,status) VALUES(?,?,?,?,?,?,?,?,?);";
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) == SQLITE_OK) {
        for (const auto& r : rows) {
            sqlite3_bind_text(st, 1, r.computer.mb_str(),    -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, r.name.mb_str(),        -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, r.project.mb_str(),     -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, r.application.mb_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(st, 5, r.elapsed);
            sqlite3_bind_double(st, 6, r.cpuTime);
            sqlite3_bind_double(st, 7, r.completedAt);
            sqlite3_bind_int(st, 8, r.exitStatus);
            sqlite3_bind_text(st, 9, r.status.mb_str(),      -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_reset(st);
        }
        sqlite3_finalize(st);
    }
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
}

std::vector<BtHistoryRow> BtHistory::Query(const std::vector<wxString>& computers,
                                           int limit, bool longTerm) const
{
    std::vector<BtHistoryRow> out;
    if (!m_db) return out;

    const char* cols = "computer,name,project,application,elapsed,cpu_time,"
                       "completed_at,exit_status,status";
    wxString where;
    if (!computers.empty()) {
        where = " WHERE computer IN (";
        for (size_t i = 0; i < computers.size(); i++) where += (i ? ",?" : "?");
        where += ")";
    }

    // The long term view is the whole record - what has been moved out plus
    // what is still in the live window - so it stays complete whether or not
    // moving is switched on.
    wxString sql;
    if (longTerm)
        sql = wxString("SELECT ") + cols + " FROM longterm" + where +
              " UNION ALL SELECT " + cols + " FROM history" + where;
    else
        sql = wxString("SELECT ") + cols + " FROM history" + where;
    sql += " ORDER BY completed_at DESC LIMIT ?;";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.mb_str(), -1, &st, nullptr) != SQLITE_OK)
        return out;

    int bind = 1;
    for (const auto& c : computers)                       // first WHERE clause
        sqlite3_bind_text(st, bind++, c.mb_str(), -1, SQLITE_TRANSIENT);
    if (longTerm)
        for (const auto& c : computers)                   // second, for the union
            sqlite3_bind_text(st, bind++, c.mb_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, bind, limit);

    while (sqlite3_step(st) == SQLITE_ROW) {
        BtHistoryRow r;
        auto txt = [&](int col) {
            const unsigned char* p = sqlite3_column_text(st, col);
            return p ? wxString::FromUTF8((const char*)p) : wxString();
        };
        r.computer    = txt(0);
        r.name        = txt(1);
        r.project     = txt(2);
        r.application = txt(3);
        r.elapsed     = sqlite3_column_double(st, 4);
        r.cpuTime     = sqlite3_column_double(st, 5);
        r.completedAt = sqlite3_column_double(st, 6);
        r.exitStatus  = sqlite3_column_int(st, 7);
        r.status      = txt(8);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(st);
    return out;
}

int BtHistory::Prune(int days)
{
    if (!m_db || days <= 0) return 0;
    double cutoff = (double)wxDateTime::Now().GetTicks() - (double)days * 86400.0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM history WHERE completed_at < ?;",
                           -1, &st, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_double(st, 1, cutoff);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return sqlite3_changes(m_db);
}

long long BtHistory::Count() const
{
    if (!m_db) return 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
            "SELECT (SELECT COUNT(*) FROM history)+(SELECT COUNT(*) FROM longterm);",
            -1, &st, nullptr) != SQLITE_OK) return 0;
    long long n = (sqlite3_step(st) == SQLITE_ROW) ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    return n;
}

// ---------------------------------------------------------------------------
// job_log_<project>.txt: one line per completed task, kept forever by the
// client. Fields are "key value" pairs; we want ct (cpu), et (elapsed),
// nm (name) and the leading timestamp.
// ---------------------------------------------------------------------------
std::vector<BtHistoryRow> BtReadJobLogs(const wxString& computer,
                                        const wxString& dataDir)
{
    std::vector<BtHistoryRow> out;
    wxDir dir(dataDir);
    if (!dir.IsOpened()) return out;

    wxString file;
    bool more = dir.GetFirst(&file, "job_log_*.txt", wxDIR_FILES);
    while (more) {
        // project host is embedded in the filename: job_log_<host>.txt
        wxString project = file.Mid(8);
        project.Replace(".txt", "");

        wxTextFile tf(wxFileName(dataDir, file).GetFullPath());
        if (tf.Open()) {
            for (wxString line = tf.GetFirstLine(); !tf.Eof(); line = tf.GetNextLine()) {
                wxStringTokenizer tk(line, " ");
                if (!tk.HasMoreTokens()) continue;
                BtHistoryRow r;
                r.computer = computer;
                r.project  = project;
                tk.GetNextToken().ToDouble(&r.completedAt);
                while (tk.HasMoreTokens()) {
                    wxString key = tk.GetNextToken();
                    if (!tk.HasMoreTokens()) break;
                    wxString val = tk.GetNextToken();
                    if      (key == "ct") val.ToDouble(&r.cpuTime);
                    else if (key == "et") val.ToDouble(&r.elapsed);
                    else if (key == "nm") r.name = val;
                    else if (key == "es") { long v = 0; val.ToLong(&v); r.exitStatus = (int)v; }
                }
                if (r.name.IsEmpty() || r.completedAt <= 0) continue;
                r.status = r.exitStatus == 0 ? "Reported: OK"
                                             : wxString::Format("Error (%d)", r.exitStatus);
                out.push_back(std::move(r));
            }
        }
        more = dir.GetNext(&file);
    }
    return out;
}

std::map<wxString, int> BtHistory::CountSince(double since) const
{
    std::map<wxString, int> counts;
    if (!m_db) return counts;

    const char* sql = "SELECT computer, project, COUNT(*) FROM history "
                      "WHERE completed_at >= ? GROUP BY computer, project";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) != SQLITE_OK) return counts;
    sqlite3_bind_double(st, 1, since);
    while (sqlite3_step(st) == SQLITE_ROW) {
        wxString computer = wxString::FromUTF8((const char*)sqlite3_column_text(st, 0));
        wxString project  = wxString::FromUTF8((const char*)sqlite3_column_text(st, 1));
        counts[computer + "\x1f" + project] = sqlite3_column_int(st, 2);
    }
    sqlite3_finalize(st);
    return counts;
}

int BtHistory::MoveToLongTerm(int days)
{
    if (!m_db || days <= 0) return 0;
    double cutoff = (double)wxDateTime::Now().GetTicks() - (double)days * 86400.0;

    // Copy first, delete only what actually landed - a failed insert must not
    // lose rows. INSERT OR IGNORE keeps the move idempotent.
    sqlite3_stmt* st = nullptr;
    const char* ins =
        "INSERT OR IGNORE INTO longterm "
        "SELECT computer,name,project,application,elapsed,cpu_time,"
        "completed_at,exit_status,status FROM history WHERE completed_at < ?;";
    if (sqlite3_prepare_v2(m_db, ins, -1, &st, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_double(st, 1, cutoff);
    bool ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    if (!ok) return 0;

    int moved = sqlite3_changes(m_db);
    if (sqlite3_prepare_v2(m_db, "DELETE FROM history WHERE completed_at < ?;",
                           -1, &st, nullptr) != SQLITE_OK) return moved;
    sqlite3_bind_double(st, 1, cutoff);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return moved;
}
