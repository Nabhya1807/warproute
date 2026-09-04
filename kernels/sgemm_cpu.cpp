#include "sgemm_cpu.hpp"
#include "timer.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace warproute {

// YOU WRITE THIS.
// Triple loop. Matrices are flat: element (i,j) is at [i*n + j].
//   for each row i of C:
//     for each col j of C:
//       sum over k of A[i][k] * B[k][j]
void sgemm_naive(const float* A, const float* B, float* C, std::size_t n) {
    for(size_t i=0; i<n;i++){
        for(size_t j=0;j<n;j++){
            float s=0.0f;
            for(size_t k=0;k<n;k++){
                s+=A[i*n +k] *B[k*n +j];
            }
            C[i*n+j]=s;

        }
    }
}

// YOU WRITE THIS (after naive works and is verified).
void sgemm_blocked(const float* A, const float* B, float* C,
                   std::size_t n, std::size_t tile) {
}

// ---------------------------------------------------------------------------
// Plumbing below.
// ---------------------------------------------------------------------------

static void fill_random(std::vector<float>& m, std::mt19937& rng) {
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  for (auto& x : m) x = dist(rng);
}

bool sgemm_verify() {
  bool ok = true;

  // Check 1: hand-computable 2x2.
  //   [1 2]   [5 6]   [19 22]
  //   [3 4] x [7 8] = [43 50]
  {
    const float A[4] = {1, 2, 3, 4};
    const float B[4] = {5, 6, 7, 8};
    const float want[4] = {19, 22, 43, 50};
    float C[4] = {0, 0, 0, 0};
    sgemm_naive(A, B, C, 2);
    for (int i = 0; i < 4; i++) {
      if (std::fabs(C[i] - want[i]) > 1e-4f) {
        std::printf("FAIL 2x2: C[%d] = %f, want %f\n", i, C[i], want[i]);
        ok = false;
      }
    }
  }

  // Check 2: A * I == A, at a size big enough to exercise the loops.
  {
    const std::size_t n = 64;
    std::mt19937 rng(12345);
    std::vector<float> A(n * n), I(n * n, 0.0f), C(n * n, 0.0f);
    fill_random(A, rng);
    for (std::size_t i = 0; i < n; i++) I[i * n + i] = 1.0f;

    sgemm_naive(A.data(), I.data(), C.data(), n);
    for (std::size_t i = 0; i < n * n; i++) {
      if (std::fabs(C[i] - A[i]) > 1e-4f) {
        std::printf("FAIL identity at %zu: %f vs %f\n", i, C[i], A[i]);
        ok = false;
        break;
      }
    }
  }

  std::printf(ok ? "sgemm_verify: PASS\n" : "sgemm_verify: FAIL\n");
  return ok;
}

void sgemm_bench_naive() {
  std::printf("n,gflops\n");

  for (std::size_t n : {128u, 256u, 512u, 1024u}) {
    std::mt19937 rng(12345);
    std::vector<float> A(n * n), B(n * n), C(n * n, 0.0f);
    fill_random(A, rng);
    fill_random(B, rng);

    // Adjust to match your run_n signature and Stats type.
    Stats s = run_n([&]() {
      sgemm_naive(A.data(), B.data(), C.data(), n);
    });

    const double flops = 2.0 * (double)n * n * n;
    const double gflops = flops / s.median_ns;  // ops/ns == Gop/s
    std::printf("%zu,%.3f\n", n, gflops);
    std::fflush(stdout);
  }
}

}  // namespace warproute