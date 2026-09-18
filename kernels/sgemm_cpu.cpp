#include "sgemm_cpu.hpp"
#include "timer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include <algorithm>
#include <string>

#define ACCELERATE_NEW_LAPACK 1
#include <Accelerate/Accelerate.h>

namespace warproute {

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


void sgemm_blocked(const float* A, const float* B, float* C,
                   std::size_t n, std::size_t tile) {
      
      for(size_t i=0; i<n*n;i++){
        C[i]=0.0f;
      }
      for(size_t ii=0;ii<n;ii+=tile){
        for(size_t jj=0;jj<n;jj+=tile){
          for(size_t kk=0;kk<n;kk+=tile){

            const size_t i_m=std::min(ii+tile,n);
            const size_t j_m=std::min(jj+tile,n);
            const size_t k_m=std::min(kk+tile,n);

            for(size_t i=ii;i<i_m;i++){
              for(size_t j=jj;j<j_m;j++){
                for(size_t k=kk;k<k_m;k++){
                  C[i*n+j]+=A[i*n+k]*B[k*n+j];
                }
              }
            }
          }
        }
      }
}


void sgemm_blocked_padded(const float* A, const float* B, float* C,
                          std::size_t n, std::size_t ld, std::size_t tile) {

      for(size_t i=0; i<n*ld;i++){
        C[i]=0.0f;
      }
      for(size_t ii=0;ii<n;ii+=tile){
        for(size_t jj=0;jj<n;jj+=tile){
          for(size_t kk=0;kk<n;kk+=tile){

            const size_t i_m=std::min(ii+tile,n);
            const size_t j_m=std::min(jj+tile,n);
            const size_t k_m=std::min(kk+tile,n);

            for(size_t i=ii;i<i_m;i++){
              for(size_t j=jj;j<j_m;j++){
                for(size_t k=kk;k<k_m;k++){
                  C[i*ld+j]+=A[i*ld+k]*B[k*ld+j];
                }
              }
            }
          }
        }
      }
}

static void fill_random(std::vector<float>& m, std::mt19937& rng) {
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  for (auto& x : m) x = dist(rng);
}

bool sgemm_verify() {
  bool ok = true;

  
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

  
    Stats s = run_n([&]() {
      sgemm_naive(A.data(), B.data(), C.data(), n);
    });

    const double flops = 2.0 * (double)n * n * n;
    const double gflops = flops / s.median_ns;  
    std::printf("%zu,%.3f\n", n, gflops);
    std::fflush(stdout);
  }
}
bool sgemm_verify_blocked() {
  bool ok = true;

  for (std::size_t n : {64u, 128u, 256u}) {
    std::mt19937 rng(12345);
    std::vector<float> A(n * n), B(n * n), C_ref(n * n, 0.0f);
    fill_random(A, rng);
    fill_random(B, rng);
    sgemm_naive(A.data(), B.data(), C_ref.data(), n);

    for (std::size_t tile : {8u, 16u, 32u, 64u}) {
      std::vector<float> C_test(n * n, 0.0f);
      sgemm_blocked(A.data(), B.data(), C_test.data(), n, tile);

      float max_err = 0.0f;
      for (std::size_t i = 0; i < n * n; i++) {
        max_err = std::max(max_err, std::fabs(C_test[i] - C_ref[i]));
      }
      if (max_err > 1e-3f) {
        std::printf("FAIL n=%zu tile=%zu: max error %g\n",
                    n, tile, (double)max_err);
        ok = false;
      }
    }
  }

  std::printf(ok ? "sgemm_verify_blocked: PASS\n"
                 : "sgemm_verify_blocked: FAIL\n");
  return ok;
}

void sgemm_sweep_tiles() {
  std::printf("n,tile,gflops,iqr_pct\n");

  for (std::size_t n : {512u, 1024u}) {
    std::mt19937 rng(12345);
    std::vector<float> A(n * n), B(n * n), C(n * n, 0.0f);
    fill_random(A, rng);
    fill_random(B, rng);

    for (std::size_t tile : {8u, 16u, 32u, 48u, 64u, 96u, 128u, 256u}) {
      Stats s = run_n([&]() {
        sgemm_blocked(A.data(), B.data(), C.data(), n, tile);
      });

      const double flops = 2.0 * (double)n * n * n;
      const double gflops = flops / s.median_ns;
      const double iqr_pct = 100.0 * s.iqr_ns / s.median_ns;
      std::printf("%zu,%zu,%.3f,%.1f\n", n, tile, gflops, iqr_pct);
      std::fflush(stdout);
    }
  }
}



static std::size_t padded_ld(std::size_t n) { return n + 16; }


static std::vector<float> to_padded(const std::vector<float>& m,
                                    std::size_t n, std::size_t ld) {
  std::vector<float> out(n * ld, 0.0f);
  for (std::size_t i = 0; i < n; i++) {
    for (std::size_t j = 0; j < n; j++) out[i * ld + j] = m[i * n + j];
  }
  return out;
}

bool sgemm_verify_blocked_padded() {
  bool ok = true;

  for (std::size_t n : {64u, 128u, 256u}) {
    const std::size_t ld = padded_ld(n);
    std::mt19937 rng(12345);
    std::vector<float> A(n * n), B(n * n), C_ref(n * n, 0.0f);
    fill_random(A, rng);
    fill_random(B, rng);
    sgemm_naive(A.data(), B.data(), C_ref.data(), n);

    std::vector<float> Ap = to_padded(A, n, ld);
    std::vector<float> Bp = to_padded(B, n, ld);

    for (std::size_t tile : {8u, 16u, 32u, 64u}) {
      std::vector<float> C_test(n * ld, 0.0f);
      sgemm_blocked_padded(Ap.data(), Bp.data(), C_test.data(), n, ld, tile);

      float max_err = 0.0f;
      for (std::size_t i = 0; i < n; i++) {
        for (std::size_t j = 0; j < n; j++) {
          max_err = std::max(max_err,
                             std::fabs(C_test[i * ld + j] - C_ref[i * n + j]));
        }
      }
      if (max_err > 1e-3f) {
        std::printf("FAIL n=%zu ld=%zu tile=%zu: max error %g\n",
                    n, ld, tile, (double)max_err);
        ok = false;
      }
    }
  }

  std::printf(ok ? "sgemm_verify_blocked_padded: PASS\n"
                 : "sgemm_verify_blocked_padded: FAIL\n");
  return ok;
}

void sgemm_sweep_tiles_padded() {
  std::printf("n,tile,gflops,iqr_pct\n");

  for (std::size_t n : {512u, 1024u}) {
    const std::size_t ld = padded_ld(n);
    std::mt19937 rng(12345);
    std::vector<float> A(n * ld), B(n * ld), C(n * ld, 0.0f);
    fill_random(A, rng);
    fill_random(B, rng);

    for (std::size_t tile : {8u, 16u, 32u, 48u, 64u, 96u, 128u, 256u}) {
      Stats s = run_n([&]() {
        sgemm_blocked_padded(A.data(), B.data(), C.data(), n, ld, tile);
      });

      const double flops = 2.0 * (double)n * n * n;
      const double gflops = flops / s.median_ns;
      const double iqr_pct = 100.0 * s.iqr_ns / s.median_ns;
      std::printf("%zu,%zu,%.3f,%.1f\n", n, tile, gflops, iqr_pct);
      std::fflush(stdout);
    }
  }
}



static void sgemm_blas(const float* A, const float* B, float* C,
                       std::size_t n) {
  const int in = (int)n;
  cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
              in, in, in, 1.0f, A, in, B, in, 0.0f, C, in);
}


static const char* blas_config() {
  const char* v = std::getenv("VECLIB_MAXIMUM_THREADS");
  return (v && std::string(v) == "1") ? "single_threaded" : "default";
}

bool sgemm_verify_blas() {
  bool ok = true;

  for (std::size_t n : {64u, 128u, 256u}) {
    std::mt19937 rng(12345);
    std::vector<float> A(n * n), B(n * n), C_ref(n * n, 0.0f), C_test(n * n, 0.0f);
    fill_random(A, rng);
    fill_random(B, rng);
    sgemm_naive(A.data(), B.data(), C_ref.data(), n);
    sgemm_blas(A.data(), B.data(), C_test.data(), n);

    float max_err = 0.0f;
    for (std::size_t i = 0; i < n * n; i++) {
      max_err = std::max(max_err, std::fabs(C_test[i] - C_ref[i]));
    }
    std::printf("n=%zu max error %g%s\n", n, (double)max_err,
                max_err > 1e-3f ? "   FAIL" : "");
    if (max_err > 1e-3f) ok = false;
  }

  std::printf(ok ? "sgemm_verify_blas: PASS\n" : "sgemm_verify_blas: FAIL\n");
  return ok;
}

void sgemm_bench_blas() {
  std::printf("config,n,gflops,iqr_pct\n");

  for (std::size_t n : {512u, 1024u}) {
    std::mt19937 rng(12345);
    std::vector<float> A(n * n), B(n * n), C(n * n, 0.0f);
    fill_random(A, rng);
    fill_random(B, rng);

    Stats s = run_n([&]() {
      sgemm_blas(A.data(), B.data(), C.data(), n);
    });

    const double flops = 2.0 * (double)n * n * n;
    const double gflops = flops / s.median_ns;
    const double iqr_pct = 100.0 * s.iqr_ns / s.median_ns;
    std::printf("%s,%zu,%.3f,%.1f\n", blas_config(), n, gflops, iqr_pct);
    std::fflush(stdout);
  }
}

}  