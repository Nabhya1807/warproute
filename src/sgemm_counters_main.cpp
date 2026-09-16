// Day 9: naive vs padded blocked SGEMM under PMU counters, n=1024.
//
// Same matrices, same seed, same warmup policy for both kernels. Setup,
// verification, checksums and CSV writing all sit outside the bracketed
// kernel calls. Kernels come unmodified from kernels/sgemm_cpu.cpp.
//
// Run from the repo root, through the retry wrapper:
//   sudo ./scripts/run_counters.sh ./build/sgemm_counters
//
// CSV: results/2026-09-16/day9_sgemm_counters.csv. Written only if both
// kernels verify and counters are still forced at the end. Refuses to
// overwrite an existing file, so raw results are never clobbered.
// tile = 0 and ld = n mark the unblocked, unpadded naive kernel.

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
static constexpr std::size_t LD   = N + 16;   // Day 7 padded row stride
static constexpr std::size_t TILE = 32;       // Day 7 padded optimum (2.69x)
static constexpr std::uint32_t SEED = 12345;  // project-wide SGEMM seed
static constexpr float TOL = 1e-3f;           // absolute, as in sgemm_verify_*
static const char* CSV_PATH = "results/2026-09-16/day9_sgemm_counters.csv";

static const std::vector<std::string> EVENTS = {
    "FIXED_CYCLES",
    "FIXED_INSTRUCTIONS",
    "L1D_CACHE_MISS_LD_NONSPEC",
    "L1D_TLB_MISS_NONSPEC",
    "L2_TLB_MISS_DATA",
    "L1D_TLB_ACCESS",
};

// Same distribution and draw order as fill_random() in sgemm_cpu.cpp.
static void fill_random(std::vector<float>& m, std::mt19937& rng) {
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  for (auto& x : m) x = dist(rng);
}

// Copy a packed n x n matrix into an n x ld buffer, padding zeroed.
// Same as to_padded() in sgemm_cpu.cpp, so both kernels see identical values.
static std::vector<float> to_padded(const std::vector<float>& m) {
  std::vector<float> out(N * LD, 0.0f);
  for (std::size_t i = 0; i < N; i++)
    for (std::size_t j = 0; j < N; j++) out[i * LD + j] = m[i * N + j];
  return out;
}

// Deltas in EVENTS order. Returns false if the bracketing was not inserted
// (counters_read() never ran, so events is empty) rather than recording zeros.
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

  // ---- Setup (outside every measured region) --------------------------------
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

  // ---- Naive -----------------------------------------------------------------
  // One untimed warmup: DVFS ramp plus first-touch of every page.
  warproute::sgemm_naive(A.data(), B.data(), C_naive.data(), N);

  // BEGIN USER COUNTER BRACKETING: naive
  naive_before = warproute::counters_read();
  warproute::sgemm_naive(A.data(), B.data(), C_naive.data(), N);
  naive_after = warproute::counters_read();

  // ---- Padded blocked, T=32 -------------------------------------------------
  warproute::sgemm_blocked_padded(Ap.data(), Bp.data(), C_pad.data(), N, LD,
                                  TILE);

  pad_before = warproute::counters_read();
  warproute::sgemm_blocked_padded(Ap.data(), Bp.data(), C_pad.data(), N, LD,
                                  TILE);
  pad_after = warproute::counters_read();

  // Checked before shutdown, which clears the force flag itself.
  const bool still_forced = warproute::counters_still_forced();
  warproute::counters_shutdown();

  // ---- Verification and checksums (outside measured regions) ----------------
  // Reading both outputs here is also what keeps the kernels' work observable.
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

  // ---- CSV (raw counter deltas only; nothing derived) ------------------------
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

  // "x": fail rather than overwrite an existing results file.
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
