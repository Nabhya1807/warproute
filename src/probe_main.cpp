#include "probe.hpp"
#include "timer.hpp"
#include "workload.hpp"
#include "saxpy_cpu.hpp"
#include <iostream>

int main() {
  warproute::set_high_qos();

  warproute::ulayout w = warproute::make_saxpy();
  warproute::slayout cfg;   // defaults: 1 thread

  for (size_t n = 1024; n <= (64ull * 1024 * 1024); n *= 2) {
    w.setup(n);
    w.run(cfg);
    bool ok = w.verify();
    if (!ok) {
      std::cout << n << "  FAILED VERIFY\n";
      continue;
    }

    w.setup(n);  // reset before timing
    warproute::Stats s = warproute::run_n([&]() {
      w.run(cfg);
    });

    double bytes = static_cast<double>(n) * 3 * sizeof(float);
    double gbps = bytes / (s.median_ns / 1e9) / 1e9;

    std::cout << n << " elems   "
              << bytes / 1024 << " KB   "
              << s.median_ns << " ns   "
              << gbps << " GB/s\n";
  }

  return 0;
}