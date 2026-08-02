#include "bt_estimate.h"
#include <algorithm>
#include <cmath>

double BtWallClockRemaining(const std::vector<BtTaskRow>& tasks, const BtCapacity& cap)
{
    if (tasks.empty()) return -1;

    // Total CPU slots BOINC may use on this host.
    double slots = cap.ncpus > 0 ? std::floor(cap.ncpus * (cap.maxNcpusPct / 100.0)) : 0;
    if (slots < 1) slots = cap.ncpus > 0 ? 1 : 0;
    if (slots <= 0) return -1;                   // capacity unknown; say nothing

    // GPU tasks are scheduled against the GPU but BOINC still reserves CPU for
    // them, and that reservation comes out of the same pool. A 32-thread host
    // running one GPU task that holds four threads has 28 left for CPU work.
    double gpuHeld = 0;
    for (const auto& t : tasks)
        if (t.running && t.useGpus > 0) gpuHeld += t.useCpus;

    double cpuSlots = std::max(0.0, slots - gpuHeld);

    // Split the work by the device it will occupy.
    double cpuWork = 0, gpuWork = 0, longest = 0;
    double cpuPerTask = 0; int cpuTasks = 0;
    double gpuPerTask = 0; int gpuTasks = 0;
    for (const auto& t : tasks) {
        if (t.timeLeft <= 0) continue;
        longest = std::max(longest, t.timeLeft);
        if (t.useGpus > 0) {
            gpuWork += t.timeLeft;
            gpuPerTask += t.useGpus; gpuTasks++;
        } else {
            cpuWork += t.timeLeft;
            cpuPerTask += (t.useCpus > 0 ? t.useCpus : 1.0); cpuTasks++;
        }
    }
    if (longest <= 0) return -1;

    // How many can run side by side, given what each one occupies.
    double cpuConcurrent = 1;
    if (cpuTasks > 0) {
        double avg = cpuPerTask / cpuTasks;
        cpuConcurrent = std::max(1.0, std::floor(cpuSlots / std::max(0.01, avg)));
    }
    // The GPU pool is separate. Without a device count from the client, assume
    // whatever is running now is the ceiling - better than pretending the CPU
    // count applies to GPU work.
    double gpuConcurrent = 1;
    if (gpuTasks > 0) {
        int runningGpu = 0;
        for (const auto& t : tasks) if (t.running && t.useGpus > 0) runningGpu++;
        gpuConcurrent = std::max(1, runningGpu);
    }

    double cpuSpan = cpuTasks ? cpuWork / cpuConcurrent : 0;
    double gpuSpan = gpuTasks ? gpuWork / gpuConcurrent : 0;

    // The host is done when the slower pool is done, and never sooner than its
    // single longest task.
    double span = std::max({ cpuSpan, gpuSpan, longest });

    // A throttled client does the same work over a longer wall-clock.
    if (cap.cpuUsageLimit > 0 && cap.cpuUsageLimit < 100)
        span *= 100.0 / cap.cpuUsageLimit;

    return span;
}
