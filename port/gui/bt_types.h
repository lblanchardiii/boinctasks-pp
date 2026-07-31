// =============================================================================
// bt_types.h - shared data types between poller threads and GUI
// =============================================================================
#pragma once
#include <wx/string.h>
#include <vector>
#include <utility>

struct BtComputer
{
    wxString name;       // display name (tree label / Computer column)
    wxString group;      // optional group label in the computer tree
    wxString host;       // ip / hostname
    long     port = 31416;
    wxString password;   // empty + localhost -> auto-read gui_rpc_auth.cfg
    // Unticked on the Computers tab: kept in the config but not polled and not
    // shown anywhere else, the way the Windows app hides a computer.
    bool     enabled = true;
};

// Colour categories mirror the Windows app's palette, which splits every
// running state into CPU and GPU variants.
enum BtTaskState
{
    BTS_UPLOAD_DOWNLOAD = 0,
    BTS_READY_TO_REPORT,
    BTS_RUNNING,
    BTS_HIGH_PRIORITY,
    BTS_ABORT,
    BTS_WAITING_TO_RUN,
    BTS_READY_TO_START,
    BTS_ERROR,
    BTS_SUSPENDED,
    BTS_COUNT
};

// One task row. Grouping key is computer+project+application+status.
struct BtTaskRow
{
    wxString computer, project, application, name, status;
    wxString projectUrl;      // needed for result_op
    double   cpuPct   = 0;      // CPU % of the running process
    double   elapsed  = 0;      // seconds
    double   cpuTime  = 0;      // seconds (shown in parens after elapsed)
    double   timeLeft = 0;      // seconds
    double   progress = 0;      // 0..100
    double   deadline = 0;      // unix time
    double   useCpus  = 0;      // avg_ncpus for the "Use" column
    bool     running  = false;
    bool     error    = false;
    bool     isGpu    = false;      // app version uses a GPU
    bool     nonCpuIntensive = false;   // project runs non-CPU-intensive work
    bool     warning  = false;      // a Warnings rule matched; highlight the row
    // Windows shows these too; all come straight off the client's RESULT
    double   checkpoint = 0;    // checkpoint_cpu_time
    double   received   = 0;    // received_time, unix
    double   swapSize   = 0;    // virtual memory, bytes
    double   memSize    = 0;    // working set, bytes
    double   debt       = 0;    // project scheduling priority
    wxString account;           // project account name
    int      state    = BTS_READY_TO_START;   // colour category
};

struct BtProjectRow
{
    wxString computer, project, account, team, status;
    wxString masterUrl;       // needed for project_op
    // Free-DC links. Both already arrive with the data we poll, so keeping them
    // costs no extra RPC: host_cpid comes from CC_STATE::host_info, and hostid
    // is this host's ID on that project.
    wxString hostCpid;
    int      hostId = 0;
    double   credit = 0, avgCredit = 0, share = 0;
    double   hostCredit = 0, hostAvgCredit = 0;   // this computer's contribution
    wxString venue;
    int      taskCount = 0;     // tasks currently held for this project
    int      cpuTasks  = 0;     // split, for the run-dry warning
    int      gpuTasks  = 0;
    bool     warning   = false; // a Warnings slot matched: too few tasks left
    int      perDay    = 0;     // completions seen in the last day / week
    int      perWeek   = 0;
    double   timeLeft  = 0;     // their remaining time, summed
    bool     noNewWork = false;
    bool     suspended = false;
};

struct BtTransferRow
{
    wxString computer, project, file, status;
    wxString projectUrl;      // needed for file_transfer_op
    double   size = 0, progress = 0, speed = 0;
};

struct BtMessageRow
{
    wxString computer, project, body;
    int      seqno = 0;
    double   timestamp = 0;
    int      priority = 1;
};

// Per-project daily credit history, from <get_statistics/>.
struct BtStatSeries
{
    wxString computer, project;
    std::vector<std::pair<double, double>> points;   // (day, expavg credit)
};

struct BtNoticeRow
{
    wxString computer, project, title, description, link;
    double   createTime = 0;
    int      seqno = 0;
};

// A completed task, persisted locally (the client only keeps a small window).
struct BtHistoryRow
{
    wxString computer, project, application, name, status;
    double   elapsed = 0, cpuTime = 0, completedAt = 0;
    int      exitStatus = 0;
};

// One computer's data, merged by the frame into the combined views.
struct BtSnapshot
{
    bool                       connected = false;
    wxString                   computer;      // display name
    wxString                   error;
    wxString                   hostname;      // domain_name reported by client
    wxString                   clientVersion; // e.g. "8.2.9"
    wxString                   platform;      // e.g. "x86_64-pc-linux-gnu"
    std::vector<BtTaskRow>     tasks;
    std::vector<BtProjectRow>  projects;
    std::vector<BtTransferRow> transfers;
    std::vector<BtMessageRow>  messages;
    std::vector<BtHistoryRow>  completed;   // newly finished since last poll
    std::vector<BtNoticeRow>   notices;
    std::vector<BtStatSeries>  stats;
};
