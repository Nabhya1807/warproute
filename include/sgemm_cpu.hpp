#pragma once
#include <cstddef>

namespace warproute {

void sgemm_naive(const float* A, const float* B, float* C, std::size_t n);

void sgemm_blocked(const float* A, const float* B, float* C,
                   std::size_t n, std::size_t tile);

// Same kernel, but the matrices are logically n x n stored with a row stride of
// `ld` floats (ld >= n). Element (i,j) is at [i*ld + j].
void sgemm_blocked_padded(const float* A, const float* B, float* C,
                          std::size_t n, std::size_t ld, std::size_t tile);

bool sgemm_verify();

void sgemm_bench_naive();
bool sgemm_verify_blocked();
void sgemm_sweep_tiles();
bool sgemm_verify_blocked_padded();
void sgemm_sweep_tiles_padded();

}  // namespace warproute