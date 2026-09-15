// API smoke test for the PMU counter module. Not a benchmark: there is no
// workload between the two reads, so the delta is just the cost of one
// counters_read() call. Requires root: sudo ./counters
#include "counters.hpp"

#include <iostream>

int main() {
  if (!warproute::counters_init()) {
    std::cout << "counters unavailable; a benchmark would degrade to timing "
                 "only.\n";
    return 1;
  }

  const warproute::CounterReading a = warproute::counters_read();
  const warproute::CounterReading b = warproute::counters_read();
  warproute::counters_shutdown();

  std::cout << "read 1        : cycles=" << a.cycles
            << " instructions=" << a.instructions
            << " inst_all=" << a.inst_all << "\n"
            << "read 2        : cycles=" << b.cycles
            << " instructions=" << b.instructions
            << " inst_all=" << b.inst_all << "\n"
            << "delta (1 read): cycles=" << b.cycles - a.cycles
            << " instructions=" << b.instructions - a.instructions
            << " inst_all=" << b.inst_all - a.inst_all << "\n";

  if (b.cycles == 0 || b.instructions == 0 || b.inst_all == 0) {
    std::cout << "FAIL counter(s) read ZERO:" << (b.cycles ? "" : " cycles")
              << (b.instructions ? "" : " instructions")
              << (b.inst_all ? "" : " INST_ALL") << "\n";
    return 2;
  }
  return 0;
}
