#include "probe.hpp"
#include "timer.hpp"
#include "system_info.hpp"
#include "workload.hpp"
#include "saxpy_cpu.hpp"
#include <cstring>
#include <iostream>

// Day 2: cache capacity sweep.
static int run_capacity() {
  for(size_t i=4 ; i<=65536; i*=2){
      const size_t buffer_bytes = i * 1024;
      const size_t n_slots = buffer_bytes / sizeof(size_t);
      const size_t hops = 1000000;

      auto chain = warproute::build_chain(n_slots);

      volatile size_t sink = 0;
      warproute::Stats s = warproute::run_n([&]() {
        sink = warproute::chase(chain, hops);
      });

      std::cout << buffer_bytes / 1024 << " KB   "
                << s.median_ns / hops << " ns/hop   "
                << "(iqr " << s.iqr_ns / hops << ")\n";
     
  }
  return 0;
  
  }

// Day 4: L1d associativity sweep.
static int run_assoc() {
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

// Day 3: single-threaded SAXPY bandwidth sweep.
static int run_saxpy() {
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

static void usage(const char* prog) {
  std::cout << "usage: " << prog << " <sweep>\n\n"
            << "  capacity   cache capacity sweep (day 2)\n"
            << "  assoc      L1d associativity sweep (day 4)\n"
            << "  saxpy      single-threaded SAXPY bandwidth sweep (day 3)\n";
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  warproute::set_high_qos();

  if (std::strcmp(argv[1], "capacity") == 0) return run_capacity();
  if (std::strcmp(argv[1], "assoc") == 0)    return run_assoc();
  if (std::strcmp(argv[1], "saxpy") == 0)    return run_saxpy();

  std::cout << "unknown sweep: " << argv[1] << "\n\n";
  usage(argv[0]);
  return 1;
}
