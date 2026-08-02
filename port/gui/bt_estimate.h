// =============================================================================
// bt_estimate.h - how long a pile of work will actually take
//
// Classic sums the remaining time of every task, which reads 53 days for a
// queue one machine finishes in two: 1162 tasks of an hour each is 1162 hours
// of work, but a 32-thread host runs 32 of them at once.
//
// What is wanted is wall-clock: how long until the host is through the queue.
// =============================================================================
#pragma once
#include "bt_types.h"
#include <vector>

struct BtCapacity
{
    int    ncpus         = 0;     // HOST_INFO::p_ncpus
    double maxNcpusPct   = 100;   // global pref: share of cores BOINC may use
    double cpuUsageLimit = 100;   // global pref: throttle
};

// Wall-clock seconds for `tasks` on a host of `cap`, or -1 when it cannot be
// worked out (no CPU count yet, or nothing to do).
double BtWallClockRemaining(const std::vector<BtTaskRow>& tasks, const BtCapacity& cap);
