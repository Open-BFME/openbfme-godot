// OpenBFME. GPL-3.0.
// Wall-clock diagnostics of the device layer (load timings, per-frame costs): none of it reaches simulation state.  Kept out of the simulation-input sources (the
// implementation is excluded in tools/sim/sim_policy.json) so those files carry no timing arithmetic.
#pragma once

namespace timing
{
double nowMs();
double elapsedMs(double sinceMs);
double elapsedSeconds(double sinceMs);
double spanMs(double laterMs, double earlierMs);
double sumMs(double a, double b);
// lane PERF-1: the calling thread's CPU time in ms (time it ran, not time it waited or was preempted: a frame cost that other processes' load does not inflate)
double threadCpuMs();
} // namespace timing
