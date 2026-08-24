#include "timer.hpp"
#include <iostream>
#include <vector>

int main() {
  warproute::set_high_qos();

  const size_t N = 1000000;
  std::vector<double> data(N);
  for (size_t i = 0; i < N; i++) data[i] = i * 0.5;

  volatile double sink = 0.0;

  warproute::Stats s = warproute::run_n([&]() {
    double sum = 0.0;
    for (size_t i = 0; i < N; i++) sum += data[i];
    sink = sum;
  });

  std::cout << "median " << s.median_ns << " ns\n";
  std::cout << "min    " << s.min_ns << " ns\n";
  std::cout << "iqr    " << s.iqr_ns << " ns\n";
  return 0;
}
