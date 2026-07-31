#include "bt_poller.h"
#include "bt_settings.h"
#include "bt_config.h"
#include "gui_rpc_client.h"
#include "common_defs.h"
#include <wx/datetime.h>
#include <wx/utils.h>
#include <fstream>
#include <deque>
#include <set>

static wxString taskStatus(const RESULT& r)
{
    if (r.suspended_via_gui)         return "Suspended";
    if (r.project_suspended_via_gui) return "Project suspended";
    switch (r.state) {
        case RESULT_NEW:               return "New";
        case RESULT_FILES_DOWNLOADING: return "Downloading";
        case RESULT_FILES_DOWNLOADED:
            if (r.active_task) {
                if (r.active_task_state == PROCESS_EXECUTING)
                    return r.edf_scheduled ? "Running High P." : "Running";
                if (r.active_task_state == PROCESS_SUSPENDED) return "Suspended";
                return "Waiting to run";   // in memory, preempted
            }
            if (r.scheduler_state == 0) return "Ready to start";
            return "Waiting to run";
        case RESULT_COMPUTE_ERROR:     return "Computation error";
        case RESULT_FILES_UPLOADING:   return "Uploading";
        case RESULT_FILES_UPLOADED:    return r.ready_to_report ? "Ready to report" : "Uploaded";
        case RESULT_ABORTED:           return "Aborted";
        case RESULT_UPLOAD_FAILED:     return "Upload failed";
    }
    return wxString::Format("State %d", r.state);
}

// Which colour bucket a result falls into (mirrors taskStatus()).
static int taskState(const RESULT& r)
{
    if (r.suspended_via_gui || r.project_suspended_via_gui) return BTS_SUSPENDED;
    switch (r.state) {
        case RESULT_FILES_DOWNLOADING: return BTS_UPLOAD_DOWNLOAD;
        case RESULT_FILES_UPLOADING:   return BTS_UPLOAD_DOWNLOAD;
        case RESULT_FILES_UPLOADED:    return r.ready_to_report ? BTS_READY_TO_REPORT
                                                                : BTS_UPLOAD_DOWNLOAD;
        case RESULT_COMPUTE_ERROR:
        case RESULT_UPLOAD_FAILED:     return BTS_ERROR;
        case RESULT_ABORTED:           return BTS_ABORT;
        case RESULT_FILES_DOWNLOADED:
            if (r.active_task) {
                if (r.active_task_state == PROCESS_EXECUTING)
                    return r.edf_scheduled ? BTS_HIGH_PRIORITY : BTS_RUNNING;
                if (r.active_task_state == PROCESS_SUSPENDED) return BTS_SUSPENDED;
                return BTS_WAITING_TO_RUN;
            }
            return r.scheduler_state == 0 ? BTS_READY_TO_START : BTS_WAITING_TO_RUN;
    }
    return BTS_READY_TO_START;
}

static wxString projectStatus(const PROJECT& p)
{
    // These are independent flags, not one state: a project can be suspended
    // AND set to fetch no new work AND backing off from the scheduler all at
    // once. Picking the first true one hides the rest, so list them all.
    std::vector<wxString> parts;

    if (p.suspended_via_gui)          parts.push_back("Suspended");
    if (p.dont_request_more_work)     parts.push_back("No new tasks");
    if (p.ended)                      parts.push_back("Project ended");
    if (p.detach_when_done)           parts.push_back("Detach when done");
    if (p.non_cpu_intensive)          parts.push_back("Non CPU intensive");
    if (p.scheduler_rpc_in_progress)  parts.push_back("Scheduler request in progress");
    else if (p.sched_rpc_pending)     parts.push_back("Scheduler request pending");
    if (p.master_url_fetch_pending)   parts.push_back("Master file fetch pending");
    if (p.trickle_up_pending)         parts.push_back("Trickle up pending");

    // how long the client has decided to wait before talking to this project
    double now = (double)wxDateTime::Now().GetTicks();
    if (p.min_rpc_time > now) {
        long secs = (long)(p.min_rpc_time - now);
        parts.push_back(wxString::Format("Communication deferred %02ld:%02ld:%02ld",
                                         secs / 3600, (secs % 3600) / 60, secs % 60));
    }
    if (p.download_backoff > 0)       parts.push_back("Download backoff");
    if (p.upload_backoff > 0)         parts.push_back("Upload backoff");

    if (parts.empty()) return "Active";

    wxString s;
    for (const auto& part : parts) {
        if (!s.IsEmpty()) s += ", ";
        s += part;
    }
    return s;
}

static wxString transferStatus(const FILE_TRANSFER& t)
{
    if (t.xfer_active)   return t.is_upload ? "Uploading" : "Downloading";
    if (t.status != 0)   return wxString::Format("Error %d", t.status);
    if (t.next_request_time > (int)wxDateTime::Now().GetTicks())
        return "Retry pending";
    return t.is_upload ? "Upload pending" : "Download pending";
}

// ---------------------------------------------------------------------------
std::string BtPoller::Password() const
{
    if (!m_computer.password.empty())
        return std::string(m_computer.password.mb_str());
    // localhost fallback: read the client's own auth file
    if (m_computer.host == "127.0.0.1" || m_computer.host.Lower().Contains("localhost")) {
        std::ifstream f((BtBoincDataDir() + wxFILE_SEP_PATH +
                         "gui_rpc_auth.cfg").mb_str());
        std::string pw;
        std::getline(f, pw);
        while (!pw.empty() && (pw.back() == '\n' || pw.back() == '\r')) pw.pop_back();
        return pw;
    }
    return "";
}

void BtPoller::Run()
{
    // spread the first poll across the interval so N clients don't stampede
    for (int i = 0; i < m_staggerMs / 50 && !m_stop; i++) wxMilliSleep(50);

    RPC_CLIENT rpc;
    CC_STATE   state;
    bool       connected = false;
    int        stateAge  = 999;
    int        lastSeqno = 0;
    std::deque<BtMessageRow> msgLog;    // accumulated messages, capped
    std::set<wxString> seenCompleted;   // already-emitted completed tasks
    std::vector<BtStatSeries> cachedStats;
    int statsAge = 9999;

    while (!m_stop) {
        auto snap = std::make_shared<BtSnapshot>();

        if (!connected) {
            if (rpc.init(m_computer.host.mb_str(), (int)m_computer.port) == 0 &&
                rpc.authorize(Password().c_str()) == 0) {
                connected = true;
                stateAge = 999;
            } else {
                snap->computer = m_computer.name;
                snap->error    = "not connected";
                m_onSnapshot(snap);
                for (int i = 0; i < 50 && !m_stop; i++) wxMilliSleep(100);
                continue;
            }
        }

        {   // run any queued operations on this connection first
            std::vector<Command> pending;
            {
                std::lock_guard<std::mutex> lock(m_cmdMutex);
                pending.swap(m_commands);
            }
            for (auto& cmd : pending) cmd(rpc);
            if (!pending.empty()) stateAge = 999;   // refresh view promptly
        }

        const int stateEvery = (30000 / (m_intervalMs > 0 ? m_intervalMs : 2000)) + 1;
        if (stateAge >= stateEvery) {   // refresh project/app names every ~30s
            if (rpc.get_state(state) == 0) stateAge = 0;
            else { connected = false; continue; }
        }
        stateAge++;

        // ---- tasks -------------------------------------------------------
        RESULTS results;
        if (rpc.get_results(results) != 0) { connected = false; continue; }
        snap->tasks.reserve(results.results.size());
        for (auto* r : results.results) {
            BtTaskRow row;
            row.computer = m_computer.name;
            PROJECT* proj = state.lookup_project(r->project_url);
            row.project = proj && !proj->project_name.empty()
                        ? wxString(proj->project_name) : wxString(r->project_url);
            row.nonCpuIntensive = proj && proj->non_cpu_intensive;
            WORKUNIT* wu = proj ? state.lookup_wu(proj, r->wu_name) : nullptr;
            APP* app = nullptr;
            if (wu) {
                app = state.lookup_app(proj, wu->app_name);
                wxString appName = app && !app->user_friendly_name.empty()
                                 ? wxString(app->user_friendly_name) : wxString(wu->app_name);
                // BoincTasks shows "<version> <app name>", e.g. "7.61 Mapping Cancer Markers"
                row.application = wxString::Format("%.2f %s",
                                    r->version_num / 100.0, appName);
            }
            if (proj && app) {
                APP_VERSION* av = state.lookup_app_version(proj, app, r->version_num,
                                                           r->plan_class);
                if (av) {
                    row.useCpus = av->avg_ncpus;
                    // ncudas/natis are authoritative; plan_class covers the rest
                    row.isGpu = (av->ncudas > 0) || (av->natis > 0);
                }
            }
            row.name       = r->name;
            row.projectUrl = r->project_url;
            row.checkpoint = r->checkpoint_cpu_time;
            row.received   = r->received_time;
            row.swapSize   = r->swap_size;
            row.memSize    = r->working_set_size_smoothed;
            if (proj) {
                row.debt    = proj->sched_priority;
                row.account = wxString::FromUTF8(proj->user_name.c_str());
            }
            row.progress = r->active_task ? r->fraction_done * 100.0
                         : (r->state >= RESULT_FILES_UPLOADING ? 100.0 : 0.0);
            row.elapsed  = r->active_task ? r->elapsed_time : r->final_elapsed_time;
            row.cpuTime  = r->active_task ? r->current_cpu_time : r->final_cpu_time;
            row.timeLeft = r->estimated_cpu_time_remaining;
            row.deadline = r->report_deadline;
            // CPU % of wall time actually spent on CPU. The long-time average
            // is over the whole task; with it off, compare against the previous
            // poll so the figure reflects what the task is doing right now.
            row.cpuPct = 0.0;
            if (r->active_task && r->elapsed_time > 1) {
                if (gSettings.cpuLongAverage) {
                    row.cpuPct = 100.0 * r->current_cpu_time / r->elapsed_time;
                } else {
                    auto prev = m_cpuPrev.find(r->name);
                    if (prev != m_cpuPrev.end()) {
                        double dElapsed = r->elapsed_time - prev->second.first;
                        double dCpu     = r->current_cpu_time - prev->second.second;
                        if (dElapsed > 0.5 && dCpu >= 0)
                            row.cpuPct = 100.0 * dCpu / dElapsed;
                        else
                            row.cpuPct = 100.0 * r->current_cpu_time / r->elapsed_time;
                    } else {
                        row.cpuPct = 100.0 * r->current_cpu_time / r->elapsed_time;
                    }
                }
                m_cpuPrev[r->name] = { r->elapsed_time, r->current_cpu_time };
            }
            if (!row.isGpu) {
                wxString pc = wxString(r->plan_class.c_str()).Lower();
                row.isGpu = pc.Contains("cuda") || pc.Contains("nvidia") ||
                            pc.Contains("ati")  || pc.Contains("opencl") ||
                            pc.Contains("intel_gpu");
            }
            row.status   = taskStatus(*r);

            // Warnings sit alongside the task's own status rather than
            // replacing it, so a row reads "Ready to report, Deadline warning".
            if (gSettings.warnDeadline && r->report_deadline > 0) {
                double left = r->report_deadline - (double)wxDateTime::Now().GetTicks();
                double window = gSettings.warnDeadlineDays * 86400.0
                              + gSettings.warnDeadlineHours * 3600.0;
                if (left < window) {
                    row.status += left > 0 ? ", Deadline warning" : ", Deadline passed";
                    row.warning = true;
                }
            }
            row.state    = taskState(*r);
            row.running  = r->active_task && r->active_task_state == PROCESS_EXECUTING
                           && !r->suspended_via_gui;
            row.error    = (r->state == RESULT_COMPUTE_ERROR ||
                            r->state == RESULT_ABORTED ||
                            r->state == RESULT_UPLOAD_FAILED);
            snap->tasks.push_back(std::move(row));
        }

        // ---- projects ----------------------------------------------------
        PROJECTS projects;
        if (rpc.get_project_status(projects) == 0) {
            for (auto* p : projects.projects) {
                BtProjectRow row;
                row.computer  = m_computer.name;
                row.masterUrl = p->master_url;
                row.project   = !p->project_name.empty() ? wxString(p->project_name)
                                                         : wxString(p->master_url);
                row.account   = p->user_name;
                row.team      = p->team_name;
                row.credit        = p->user_total_credit;
                row.avgCredit     = p->user_expavg_credit;
                row.share         = p->resource_share;
                row.hostCredit    = p->host_total_credit;
                row.hostAvgCredit = p->host_expavg_credit;
                row.venue         = wxString::FromUTF8(p->venue.c_str());
                row.hostCpid  = wxString::FromUTF8(state.host_info.host_cpid);
                row.hostId    = p->hostid;
                row.status    = projectStatus(*p);
                row.noNewWork = p->dont_request_more_work;
                row.suspended = p->suspended_via_gui;
                // Windows shows how many tasks a project is holding and how
                // much work that is; both come from the task list we just built
                for (const auto& t : snap->tasks) {
                    if (t.projectUrl != row.masterUrl) continue;
                    row.taskCount++;
                    if (t.isGpu) row.gpuTasks++; else row.cpuTasks++;
                    row.timeLeft += t.timeLeft;
                }
                snap->projects.push_back(std::move(row));
            }
        }

        // ---- transfers ---------------------------------------------------
        FILE_TRANSFERS transfers;
        if (rpc.get_file_transfers(transfers) == 0) {
            for (auto* t : transfers.file_transfers) {
                BtTransferRow row;
                row.computer = m_computer.name;
                row.project  = !t->project_name.empty() ? wxString(t->project_name)
                                                        : wxString(t->project_url);
                row.file       = t->name;
                row.projectUrl = wxString::FromUTF8(t->project_url.c_str());
                row.size     = t->nbytes;
                row.progress = t->nbytes > 0 ? 100.0 * t->bytes_xferred / t->nbytes : 0.0;
                row.speed    = t->xfer_active ? t->xfer_speed : 0.0;
                row.status   = transferStatus(*t);
                snap->transfers.push_back(std::move(row));
            }
        }

        // ---- completed tasks --------------------------------------------
        // <get_old_results/> is a small rolling buffer on the client with the
        // real completion time; poll it every cycle and emit only what's new.
        std::vector<OLD_RESULT> oldResults;
        if (rpc.get_old_results(oldResults) == 0) {
            for (const auto& o : oldResults) {
                wxString name = wxString::FromUTF8(o.result_name);
                if (seenCompleted.count(name)) continue;
                seenCompleted.insert(name);
                BtHistoryRow h;
                h.computer   = m_computer.name;
                h.name       = name;
                h.project    = wxString::FromUTF8(o.project_url);
                std::string purl(o.project_url);
                PROJECT* pr  = state.lookup_project(purl);
                if (pr && !pr->project_name.empty()) h.project = pr->project_name;
                h.application = wxString::FromUTF8(o.app_name);
                h.elapsed     = o.elapsed_time;
                h.cpuTime     = o.cpu_time;
                h.completedAt = o.completed_time;
                h.exitStatus  = o.exit_status;
                h.status      = o.exit_status == 0
                              ? wxString("Reported: OK")
                              : wxString::Format("Error (%d)", o.exit_status);
                snap->completed.push_back(std::move(h));
            }
            // the client's window is small; don't let the guard set grow forever
            if (seenCompleted.size() > 5000) seenCompleted.clear();
        }

        // ---- messages (incremental) -------------------------------------
        MESSAGES messages;
        if (rpc.get_messages(lastSeqno, messages) == 0) {
            for (auto* m : messages.messages) {
                if (m->seqno <= lastSeqno) continue;
                lastSeqno = m->seqno;
                BtMessageRow row;
                row.computer  = m_computer.name;
                row.seqno     = m->seqno;
                row.timestamp = m->timestamp;
                row.project   = m->project;
                row.body      = wxString(m->body).Trim();
                row.priority  = m->priority;
                msgLog.push_back(std::move(row));
            }
            while (msgLog.size() > 2000) msgLog.pop_front();
        }
        snap->messages.assign(msgLog.begin(), msgLog.end());

        // ---- statistics (daily credit history; changes slowly) -----------
        if (statsAge >= (60000 / (m_intervalMs > 0 ? m_intervalMs : 2000)) + 1) {
            PROJECTS stats;
            if (rpc.get_statistics(stats) == 0) {
                statsAge = 0;
                cachedStats.clear();
                for (auto* p : stats.projects) {
                    BtStatSeries series;
                    series.computer = m_computer.name;
                    series.project  = !p->project_name.empty()
                                    ? wxString(p->project_name)
                                    : wxString(p->master_url);
                    for (const auto& d : p->statistics)
                        series.points.emplace_back(d.day, d.user_expavg_credit);
                    if (!series.points.empty())
                        cachedStats.push_back(std::move(series));
                }
            }
        }
        statsAge++;
        snap->stats = cachedStats;

        // ---- notices -----------------------------------------------------
        NOTICES notices;
        if (rpc.get_notices(0, notices) == 0) {
            for (auto* n : notices.notices) {
                BtNoticeRow row;
                row.computer    = m_computer.name;
                row.seqno       = n->seqno;
                row.project     = wxString::FromUTF8(n->project_name);
                row.title       = wxString::FromUTF8(n->title);
                row.description = wxString::FromUTF8(n->description.c_str());
                row.link        = wxString::FromUTF8(n->link);
                row.createTime  = n->create_time;
                snap->notices.push_back(std::move(row));
            }
        }

        snap->connected = true;
        snap->computer  = m_computer.name;
        snap->hostname  = state.host_info.domain_name;
        if (!state.platforms.empty())
            snap->platform = wxString::FromUTF8(state.platforms[0].c_str());
        {
            VERSION_INFO vi;
            if (rpc.exchange_versions(vi) == 0)
                snap->clientVersion = wxString::Format("%d.%d.%d",
                                        vi.major, vi.minor, vi.release);
        }
        m_onSnapshot(snap);

        for (int i = 0; i < m_intervalMs / 100 && !m_stop; i++) {
            wxMilliSleep(100);                       // cut short when work arrives
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            if (!m_commands.empty()) break;
        }
    }
}
