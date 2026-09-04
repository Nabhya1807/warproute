
## Day 6: naive SGEMM declines smoothly, no cache cliff

Measured (single P-core, -O3, median of 10):

  n     GFLOP/s
  128   2.259
  256   1.915
  512   1.639
  1024  1.484

Expected a plateau followed by a cliff once the working set left L1d.
Got a smooth ~12-15% decline per doubling instead.

Explanation: the B access is B[k*n + j], stride 4n bytes. At n=128 that
is already 512 bytes -- four times the 128-byte line. So every k step
touches a fresh line at EVERY size tested. The kernel has no spatial
locality on B to lose, so there is no cliff when it "stops fitting."

What changes with n is which level serves the miss, not whether one
occurs. At n=128, B is 64 KB and fits in the 128 KB L1d, so repeat
passes hit L1. At n=1024, B is 4 MB with a 12 MB working set, pressing
on the 16 MB L2, so more misses reach DRAM. Day 3 ratios L1:L2:DRAM
= 1:4.0:103 -- the blend shifts gradually, hence the smooth slope.

Peak is ~7% of estimated single-core NEON FMA throughput (~32 GFLOP/s).
This is the motivation for Day 7 blocking.
