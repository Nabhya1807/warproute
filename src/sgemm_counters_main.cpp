#include "counters.hpp"
#include "sgemm_cpu.hpp"
#include "timer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

static constexpr std::size_t N    = 1024;
static constexpr std::size_t LD   = N + 16;   
static constexpr std::size_t TILE = 32;     
static constexpr std::uint32_t SEED = 12345; 
static constexpr float TOL = 1e-3f;           
static const char* CSV_PATH = "results/2026-09-16/day9_sgemm_counters.csv";

static const std::vector<std::string> EVENTS = {
    "FIXED_CYCLES",
    "FIXED_INSTRUCTIONS",
    "L1D_CACHE_MISS_LD_NONSPEC",
    "L1D_TLB_MISS_NONSPEC",
    "L2_TLB_MISS_DATA",
    "L1D_TLB_ACCESS",
};


static void fill_random(std::vector<float>& m, std::mt19937& rng) {
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  for (auto& x : m) x = dist(rng);
}


static std::vector<float> to_padded(const std::vector<float>& m) {
  std::vector<float> out(N * LD, 0.0f);
  for (std::size_t i = 0; i < N; i++)
    for (std::size_t j = 0; j < N; j++) out[i * LD + j] = m[i * N + j];
  return out;
}



static bool deltas(const char* name, const warproute::CounterReading& before,
                   const warproute::CounterReading& after,
                   std::vector<std::uint64_t>& out) {
  if (before.events.size() != EVENTS.size() ||
      after.events.size() != EVENTS.size()) {
    std::printf("FAIL %s: counter readings missing (bracketing not inserted?). "
                "No measurement recorded.\n", name);
    return false;
  }
  out.clear();
  for (std::size_t e = 0; e < EVENTS.size(); e++)
    out.push_back(after.events[e] - before.events[e]);
  return true;
}

int main() {
  warproute::set_high_qos();


  std::mt19937 rng(SEED);
  std::vector<float> A(N * N), B(N * N);
  fill_random(A, rng);
  fill_random(B, rng);
  std::vector<float> C_naive(N * N, 0.0f);

  const std::vector<float> Ap = to_padded(A);
  const std::vector<float> Bp = to_padded(B);
  std::vector<float> C_pad(N * LD, 0.0f);

  if (!warproute::counters_init(EVENTS)) {
    std::fprintf(stderr, "counters unavailable\n");
    return 1;
  }
  if (!warproute::counters_self_test()) {
    std::fprintf(stderr, "ABORT: counter self-test failed, PMU counters "
                         "are not live. No measurement taken.\n");
    warproute::counters_shutdown();
    return 1;
  }

  warproute::CounterReading naive_before, naive_after;
  warproute::CounterReading pad_before, pad_after;


  warproute::sgemm_naive(A.data(), B.data(), C_naive.data(), N);

  
  naive_before = warproute::counters_read();
  warproute::sgemm_naive(A.data(), B.data(), C_naive.data(), N);
  naive_after = warproute::counters_read();

 
  warproute::sgemm_blocked_padded(Ap.data(), Bp.data(), C_pad.data(), N, LD,
                                  TILE);

  pad_before = warproute::counters_read();
  warproute::sgemm_blocked_padded(Ap.data(), Bp.data(), C_pad.data(), N, LD,
                                  TILE);
  pad_after = warproute::counters_read();

 
  const bool still_forced = warproute::counters_still_forced();
  warproute::counters_shutdown();


  float max_err = 0.0f;
  double sum_naive = 0.0, sum_pad = 0.0;
  for (std::size_t i = 0; i < N; i++) {
    for (std::size_t j = 0; j < N; j++) {
      const float a = C_naive[i * N + j];
      const float b = C_pad[i * LD + j];
      max_err = std::max(max_err, std::fabs(a - b));
      sum_naive += a;
      sum_pad += b;
    }
  }
  const bool verified = max_err <= TOL;

  std::printf("n=%zu ld=%zu tile=%zu seed=%u\n", N, LD, TILE, (unsigned)SEED);
  std::printf("checksum naive   : %.6f\n", sum_naive);
  std::printf("checksum padded  : %.6f\n", sum_pad);
  std::printf("max abs error    : %g (tol %g) %s\n", (double)max_err,
              (double)TOL, verified ? "PASS" : "FAIL");

  if (!verified) {
    std::printf("FAIL: padded T=%zu does not match naive. No measurement "
                "recorded.\n", TILE);
    return 4;
  }
  if (!still_forced) {
    const char* msg =
        "WARNING: PMU counters were NOT forced at end of run. Counts may have "
        "been taken with counters lost midway and are NOT valid. CSV not "
        "written.\n";
    std::printf("%s", msg);
    std::fprintf(stderr, "%s", msg);
    return 2;
  }

  std::vector<std::uint64_t> d_naive, d_pad;
  if (!deltas("naive", naive_before, naive_after, d_naive)) return 3;
  if (!deltas("padded_t32", pad_before, pad_after, d_pad)) return 3;

 
  const char* header =
      "kernel,n,tile,ld,seed,verified,max_abs_err,cycles,instructions,"
      "l1d_cache_miss_ld_nonspec,l1d_tlb_miss_nonspec,l2_tlb_miss_data,"
      "l1d_tlb_access\n";
  char rows[1024];
  const auto& n_ = d_naive;
  const auto& p_ = d_pad;
  std::snprintf(rows, sizeof rows,
                "naive,%zu,0,%zu,%u,1,%g,%llu,%llu,%llu,%llu,%llu,%llu\n"
                "padded_blocked,%zu,%zu,%zu,%u,1,%g,%llu,%llu,%llu,%llu,%llu,"
                "%llu\n",
                N, N, (unsigned)SEED, (double)max_err,
                (unsigned long long)n_[0], (unsigned long long)n_[1],
                (unsigned long long)n_[2], (unsigned long long)n_[3],
                (unsigned long long)n_[4], (unsigned long long)n_[5],
                N, TILE, LD, (unsigned)SEED, (double)max_err,
                (unsigned long long)p_[0], (unsigned long long)p_[1],
                (unsigned long long)p_[2], (unsigned long long)p_[3],
                (unsigned long long)p_[4], (unsigned long long)p_[5]);

  std::printf("%s%s", header, rows);


  std::FILE* f = std::fopen(CSV_PATH, "wx");
  if (!f) {
    std::printf("FAIL: could not create %s (exists already, or directory "
                "missing). Rows above were NOT saved.\n", CSV_PATH);
    return 5;
  }
  std::fputs(header, f);
  std::fputs(rows, f);
  std::fclose(f);
  std::printf("wrote %s\n", CSV_PATH);
  return 0;
}
