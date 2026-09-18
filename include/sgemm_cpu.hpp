#pragma once
#include <cstddef>

namespace warproute {

// Straightforward triple loop. Walks B down a column, so it misses L1 on
// nearly every access.
void sgemm_naive(const float* A, const float* B, float* C, std::size_t n);


// Works on tile x tile submatrices so each loaded value gets reused while it
// is still in L1.
void sgemm_blocked(const float* A, const float* B, float* C,
                   std::size_t n, std::size_t tile);


// Same as sgemm_blocked, but rows are ld floats apart instead of n. The extra
// padding breaks the power-of-two stride that maps tile rows into too few
// cache sets. This is done to avoid the problem of associativity. 
void sgemm_blocked_padded(const float* A, const float* B, float* C,
                          std::size_t n, std::size_t ld, std::size_t tile);

bool sgemm_verify();

void sgemm_bench_naive();
bool sgemm_verify_blocked();
void sgemm_sweep_tiles();
bool sgemm_verify_blocked_padded();
void sgemm_sweep_tiles_padded();
bool sgemm_verify_blas();
void sgemm_bench_blas();

} 