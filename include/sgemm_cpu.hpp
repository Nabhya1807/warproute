#pragma once
#include <cstddef>

namespace warproute {

void sgemm_naive(const float* A, const float* B, float* C, std::size_t n);

void sgemm_blocked(const float* A, const float* B, float* C,
                   std::size_t n, std::size_t tile);

bool sgemm_verify();

void sgemm_bench_naive();

}  // namespace warproute