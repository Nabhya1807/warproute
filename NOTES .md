# WarpRoute
C++17 CPU-GPU autotuner. macOS arm64 (Apple M3 Pro), Apple clang 16.
Build: cmake -B build && cmake --build build

## Rules
- Do NOT modify src/timer.cpp, include/timer.hpp, src/probe.cpp, or kernel inner loops. I write those by hand.
- No new dependencies. C++17 stdlib only (CUDA later, guarded).
- Never use -march=native. This is arm64; use -mcpu=apple-m3.
- No CUDA on this machine. All GPU code must be behind find_package(CUDAToolkit QUIET).
- Every benchmark config must pass verify() before its timing is reported.
- Small changes, explained. I need to defend this code in interviews.

Day 1 harness validated. Three cold launches: medians 1.053 / 1.481 / 1.065 ms for 1M-double sum. Runs 1 and 3 agree within 1.2%. Run 2's IQR was 271 µs vs 5.8 µs for run 1 — the IQR correctly flagged it as contaminated. Practice: close background apps, stay on AC power, and treat high-IQR results as suspect rather than reporting them.
## Day 2 results — pointer-chase capacity sweep
1M hops per size, fixed seed, median of 10 runs after 3 warmups.

buffer    ns/hop    iqr
4 KB      1.531     0.008
8 KB      1.540     0.007
16 KB     1.537     0.024
32 KB     1.543     0.012
64 KB     1.543     0.016
128 KB    1.549     0.017
256 KB    6.091     0.037
512 KB    7.958     0.038
1 MB      8.939     1.352
2 MB      9.306     0.034
4 MB      11.471    0.088
8 MB      13.086    2.601
16 MB     19.763    3.484
32 MB     86.011    46.431
64 MB     158.128   15.583

L1d = 128 KB   (last fast size before 4x jump)  -- MATCHES prediction
L2  = 16 MB    (last size before 4.4x jump)     -- MATCHES prediction
Confirms P-core execution: an E-core would have broken at 64 KB.

Ratios (frequency-independent):
  L1 : L2 : DRAM  =  1 : 4.0 : 103
  Published ranges are ~1 : 3-4 : 50-100. Consistent.
