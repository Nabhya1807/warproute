#include "probe.hpp"
#include "timer.hpp"
#include <iostream>

int main() {
  warproute::set_high_qos();
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