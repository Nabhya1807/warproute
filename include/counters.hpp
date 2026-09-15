#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace warproute {

// Raw, cumulative per-thread PMU counts. Values are monotonic since counting
// was enabled; diff two readings taken on the same thread to get a delta.
struct CounterReading {
  std::uint64_t cycles = 0;        // FIXED_CYCLES       (fixed class)
  std::uint64_t instructions = 0;  // FIXED_INSTRUCTIONS (fixed class)
  // One value per event passed to counters_init(), in the caller's order.
  // Includes FIXED_CYCLES / FIXED_INSTRUCTIONS if the caller listed them.
  std::vector<std::uint64_t> events;
};

// Loads Apple's private kperf/kperfdata frameworks and enables per-thread
// counting for `events`, each looked up by name in the kpep database. Prints
// one line per event to stderr (name, event number, counter slot). cycles and
// instructions in CounterReading are filled only if FIXED_CYCLES /
// FIXED_INSTRUCTIONS are in the list. Requires root. On any failure (including
// kpep rejecting an event, which is reported by name), prints a "FAIL ..." line
// to stderr, leaves counters disabled, and returns false. Calling again while
// active returns true only for the same list; call counters_shutdown() first
// to switch lists.
bool counters_init(const std::vector<std::string>& events);

// Equivalent to counters_init({"FIXED_CYCLES", "FIXED_INSTRUCTIONS",
// "INST_ALL"}).
bool counters_init();

// Reads the calling thread's counters. Returns an all-zero reading with an
// empty events vector if counters_init() has not succeeded. If the read fails,
// prints a "FAIL ..." line and returns zeros with events sized to the list.
CounterReading counters_read();

// Disables counting and releases the kpep config/database. No-op if
// counters_init() never succeeded. Idempotent.
void counters_shutdown();

}  // namespace warproute
