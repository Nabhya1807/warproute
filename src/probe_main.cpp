#include "probe.hpp"
#include "timer.hpp"
#include "system_info.hpp"
#include <iostream>

int main() {
  warproute::set_high_qos();

  warproute::SystemInfo info = warproute::query();
  size_t l1_bytes = info.p_cores.l1d_cache_size;

  if (l1_bytes == 0) {
    std::cout << "Could not determine L1d size on this machine. "
                 "Associativity probe cannot run.\n";
    return 1;
  }

  size_t set_stride = l1_bytes / sizeof(size_t);
  const size_t hops = 1000000;

  std::cout << "L1d = " << l1_bytes / 1024 << " KB\n"
            << "stride = " << set_stride << " slots ("
            << l1_bytes << " bytes)\n\n";

  for (size_t K = 1; K <= 32; K++) {
    auto chain = warproute::build_colliding_chain(K, set_stride);

    volatile size_t sink = 0;
    warproute::Stats s = warproute::run_n([&]() {
      sink = warproute::chase(chain, hops);
    });

    std::cout << "K=" << K << "\t"
              << s.median_ns / hops << " ns/hop\t"
              << "(iqr " << s.iqr_ns / hops << ")\n";
  }

  return 0;
}