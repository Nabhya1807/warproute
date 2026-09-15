// Hour 3: L1 hit latency, measured in cycles directly.
// No wall clock, no frequency assumption.

#include "counters.hpp"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <numeric>
#include <random>

// 32 KB chain, one quarter of the 128 KB L1d.
static constexpr size_t BUF_BYTES = 32 * 1024;
static constexpr size_t N_ELEMS   = BUF_BYTES / sizeof(size_t);
static constexpr size_t N_HOPS    = 10'000'000;

// Build a single permutation cycle visiting every element once.
// Fixed seed so the layout is identical across runs.
static std::vector<size_t> build_chain(size_t n, uint32_t seed) {
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), 0);

    std::mt19937 rng(seed);
    // Shuffle everything except index 0, which stays the entry point.
    for (size_t i = n - 1; i > 1; i--) {
        std::uniform_int_distribution<size_t> d(1, i);
        std::swap(order[i], order[d(rng)]);
    }

    std::vector<size_t> chain(n);
    for (size_t i = 0; i < n; i++)
        chain[order[i]] = order[(i + 1) % n];
    return chain;
}

int main() {
    std::vector<size_t> chain = build_chain(N_ELEMS, 12345);

    if (!warproute::counters_init()) {
        std::fprintf(stderr, "counters unavailable\n");
        return 1;
    }

    // Warm: DVFS ramp plus bring the chain into L1.
    // Two discarded runs minimum, per the Day 8 frequency finding.
    size_t warm = 0;
    for (int r = 0; r < 3; r++) {
        size_t p = 0;
        for (size_t i = 0; i < N_HOPS; i++) {
            p = chain[p];
            asm volatile("" : "+r"(p));
        }
        warm += p;
    }
        // ---------------- YOUR CODE ----------------
    warproute::CounterReading before = warproute::counters_read();

    size_t p = 0;
    for (size_t i = 0; i < N_HOPS; i++) {
        p = chain[p];
        asm volatile("" : "+r"(p));
    }

    warproute::CounterReading after = warproute::counters_read();

    uint64_t cycles = after.cycles       - before.cycles;
    uint64_t insns  = after.instructions - before.instructions;

    double cyc_per_hop = (double)cycles / (double)N_HOPS;
    double ins_per_hop = (double)insns  / (double)N_HOPS;

    std::printf("hops             : %zu\n", N_HOPS);
    std::printf("buffer           : %zu bytes (%zu elements)\n",
                BUF_BYTES, N_ELEMS);
    std::printf("cycles           : %llu\n", (unsigned long long)cycles);
    std::printf("instructions     : %llu\n", (unsigned long long)insns);
    std::printf("cycles/hop       : %.3f\n", cyc_per_hop);
    std::printf("insns/hop        : %.3f\n", ins_per_hop);
    std::printf("checksum p       : %zu\n", p);

    warproute::counters_shutdown();
    std::printf("warm checksum: %zu\n", warm);
    return 0;
}