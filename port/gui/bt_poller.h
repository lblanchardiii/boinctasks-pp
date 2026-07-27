// =============================================================================
// bt_poller.h - worker thread owning one computer's RPC_CLIENT connection
// =============================================================================
#pragma once
#include "bt_types.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <map>

class RPC_CLIENT;

class BtPoller
{
public:
    using SnapshotFn = std::function<void(std::shared_ptr<BtSnapshot>)>;

    // intervalMs: how often to poll this client. staggerMs: initial delay, so a
    // large farm doesn't have every client fire in the same instant.
    BtPoller(const BtComputer& computer, SnapshotFn onSnapshot,
             int intervalMs = 2000, int staggerMs = 0)
        : m_computer(computer), m_onSnapshot(std::move(onSnapshot)),
          m_intervalMs(intervalMs), m_staggerMs(staggerMs),
          m_stop(false), m_thread(&BtPoller::Run, this) {}

    ~BtPoller() { m_stop = true; if (m_thread.joinable()) m_thread.join(); }

    // Queue work to run on this computer's RPC connection. The connection is
    // owned by the poller thread, so commands must not touch it directly.
    using Command = std::function<void(RPC_CLIENT&)>;
    void Post(Command cmd)
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);
        m_commands.push_back(std::move(cmd));
    }

private:
    // previous (elapsed, cpu) per task, for the short-window CPU % option
    std::map<std::string, std::pair<double, double>> m_cpuPrev;
    void Run();
    std::string Password() const;

    BtComputer  m_computer;
    SnapshotFn  m_onSnapshot;
    int         m_intervalMs;
    int         m_staggerMs;
    std::atomic<bool> m_stop;
    std::mutex           m_cmdMutex;
    std::vector<Command> m_commands;
    std::thread m_thread;
};
