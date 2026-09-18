#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace warproute {

struct CounterReading {
  std::uint64_t cycles = 0;        // FIXED_CYCLES       (fixed class)
  std::uint64_t instructions = 0;  // FIXED_INSTRUCTIONS (fixed class)

  std::vector<std::uint64_t> events;
};

// Arms per-thread counting for `events`, looked up by name in Apple's kpep
// database. Requires root. Returns false on any failure.
bool counters_init(const std::vector<std::string>& events);

// Arms FIXED_CYCLES, FIXED_INSTRUCTIONS, INST_ALL.
bool counters_init();

// Running totals for the calling thread; subtract two readings for a delta.
CounterReading counters_read();

// Fails if any event counts exactly 0 over a known workload, which means the
// counters are not live. The arm fails intermittently, so always call this.
bool counters_self_test();

// Verifies the counters are still armed. Call before counters_shutdown().
bool counters_still_forced();

// Disables counting and releases the kpep config. Idempotent.
void counters_shutdown();

}  // namespace warproute
