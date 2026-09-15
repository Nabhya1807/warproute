#pragma once

#include <cstdint>

namespace warproute {

// Raw, cumulative per-thread PMU counts. Values are monotonic since counting
// was enabled; diff two readings taken on the same thread to get a delta.
struct CounterReading {
  std::uint64_t cycles = 0;        // FIXED_CYCLES       (fixed class)
  std::uint64_t instructions = 0;  // FIXED_INSTRUCTIONS (fixed class)
  std::uint64_t inst_all = 0;      // INST_ALL           (configurable class)
};

// Loads Apple's private kperf/kperfdata frameworks and enables per-thread
// counting for the three events above. Requires root. On any failure, prints a
// "FAIL ..." line to stderr, leaves counters disabled, and returns false so the
// caller can fall back to timing only. Safe to call again after shutdown.
bool counters_init();

// Reads the calling thread's counters. Returns an all-zero reading if
// counters_init() has not succeeded or the read fails (a "FAIL ..." line is
// printed in the latter case).
CounterReading counters_read();

// Disables counting and releases the kpep config/database. No-op if
// counters_init() never succeeded. Idempotent.
void counters_shutdown();

}  // namespace warproute
