# Surprises

Anomalies. What was expected, what was measured, what explained it. The
chronological record, with the full tables and run logs, is in `notes.md`.

---

## Resolved

### Optimal SGEMM tile is T=8-16, not the predicted T=64

Day 2 predicted from capacity alone: three float tiles of T x T must be
L1d-resident, so `3 * T^2 * 4 <= 131072` gives T <= 104, nearest useful power of
two 64x64.

Unpadded sweep, GFLOP/s, `results/2026-09-10/sgemm_tile_sweep.csv`:

```
        T=8     T=16    T=32    T=48    T=64    T=96    T=128   T=256
n=1024  3.922   3.919   2.445   1.757   1.579   1.449   1.430   1.428
n=512   3.705   3.942   3.938   3.417   2.819   2.366   2.106   1.754
```

T=64 at n=1024 gives 1.579, 2.5x slower than T=8 and 6% above the 1.486 naive
baseline. Best is 3.922 at T=8, 2.64x naive. IQR under 1% except n=512 T=8
(13.6%). Wrong in direction, not just magnitude.

Cause: conflict misses bind before capacity. A T x T tile is not contiguous. It
is T rows of `T * 4` bytes spaced `n * 4` bytes apart, so the cache sees the
stride, not the area. At n=1024 the row stride is 4096 B = 32 lines of 128 B.
Against the 128 sets measured on Day 4 the set index advances 32 per row and
wraps every 4 rows, so a tile of any height lands in 4 set groups. A 64-row tile
puts 16 rows per group against 8 ways: 2x over-subscribed, the Day 4 K=9
thrashing condition. At T=16 it is 4 rows per group, inside 8 ways. n=512 has a
2048 B stride = 16 lines, wrapping every 8 rows, so twice the set groups and
twice the tolerable tile height. It holds 3.938 at T=32 where n=1024 has
collapsed to 2.445, and collapses at T=48-64 instead. Capacity predicts both
turn over at the same T, since they share one L1d.

**Confirmed by padding.** Row stride respaced to `n + 16` floats, matrices still
logically n x n. Tile area, loop structure, flop count and tile size unchanged;
only the spacing is no longer a power of two. Verified against `sgemm_naive`
within 1e-3 before timing. `results/2026-09-10/sgemm_tile_sweep_padded.csv`:

```
        T=8     T=16    T=32    T=48    T=64    T=96    T=128   T=256
n=1024  3.876   3.949   3.992   3.386   3.158   2.552   2.307   1.785
n=512   3.923   4.124   4.005   3.416   3.150   2.582   2.308   1.782
```

At fixed T=64, n=1024 goes 1.579 -> 3.158 (+100%) and n=512 goes 2.819 -> 3.150
(+12%). Tile height is held fixed, so only spacing changed. The asymmetry is the
right size: n=1024 has 4 set groups against n=512's 8, so twice the conflict
pressure and twice as much to gain. After padding the two agree to 0.3% where
they differed by 79%. Capacity predicts no asymmetry at all, since both share
one L1d and the tiles are identical. That is what makes this confirmed rather
than consistent with.

With conflicts gone the optimum moves up toward where capacity pointed: padded
peak T=32 at 3.992, 2.69x the 1.486 naive baseline, against an unpadded peak of
3.92 at T=8/16. The Day 2 reasoning was incomplete, not wrong. Capacity sets the
optimum once conflicts are out of the way, but on a power-of-two stride conflict
misses bind first and harder. Padded IQR under 0.8% everywhere, two runs
agreeing within 0.3%.

Caveats held open. Small tiles also shorten the innermost `k` run, changing loop
overhead, prefetch and the C store pattern at once; the fixed-T=64 comparison
narrows this but does not eliminate it. Two power-of-two strides plus one padded
stride is not a stride sweep, so the 2x claim is consistent with 2x, not
measured against a third stride. The padded curve's own decline above T=32 is a
separate Open entry.

### The L2 plateau climbs 3x instead of staying flat

Day 2 expected a flat plateau at L2 hit latency across the L2 range. It measured
6.09 ns at 256 KB rising to 19.76 ns at 16 MB.

TLB reach explains only the steep part, where 2 MB to 16 MB doubles
(9.31 -> 19.76). It does not explain the early part. At 512 KB latency is
already 7.96 ns, +31% over the 256 KB baseline, and 512 KB is only 32 pages,
well inside TLB reach; by 2 MB (128 pages, still inside) it is up 53%. The
arithmetic rules TLB out there: at 16 KB pages, 2 MB is 128 pages against the
160-entry L1 dTLB measured on Day 5, so every size in the climb fits inside TLB
reach. A TLB explanation applies only above ~2.5 MB.

Day 8 re-measured in cycles. The climb decelerates, each doubling adding half
what the previous one added (+5.07, +2.10, +0.84): converging on an asymptote,
not a step. The measurement is a blend. The buffer exceeds L1 but L1 still holds
part of the chain, so a fraction f of hops are L1 hits at 4.005 cycles and the
rest are L2 accesses at L:

    measured = f * 4.005 + (1 - f) * L

    buffer     cycles/hop   f_capacity   implied L
    256 KB       16.009       0.500        28.0   <- drifts
    512 KB       21.077       0.250        26.8
    1 MB         23.180       0.125        25.9
    2 MB         24.020       0.0625       25.4

Three sizes agree within 5% with f assumed from the capacity ratio
128 KB / buffer. The 256 KB point drifts because at 2x L1 capacity the
uniform-spread assumption is weakest and associativity effects dominate.

**Closed by measuring f** with `L1D_CACHE_MISS_LD_NONSPEC` (event 191) counted
alongside cycles on the same chase, `f = 1 - (L1 load misses / 10,000,000 hops)`.
`results/2026-09-16/l1_residency.txt`:

    buffer    cycles/hop   L1 misses     f_measured   f_measured/f_capacity
    32 KB        4.003            14        ~1.0        --
    256 KB      15.934     5,608,951      0.4391      0.878
    512 KB      20.865     7,926,763      0.2073      0.829
    1 MB        23.022     9,005,199      0.0995      0.796
    2 MB        24.210     9,503,597      0.0496      0.794
    64 MB      226.178     9,983,029      0.0017      --

Effective L1 residency is ~79% of nominal for a random-permutation walk,
converging from above, so effective capacity for this pattern is ~102 KB against
128 KB nominal. Re-solving with measured f gives 25.30 (512 KB), 25.28 (1 MB),
25.36 (2 MB): agreement 0.3%, against 5% with calculated f.

**L2 latency is 25.3 cycles.** The climb is not a property of the L2. It is the
L1-resident fraction decaying as the working set grows.

The 2 MB row is from the first sweep of the day; that size failed its counter
arm on the second. 512 KB has been measured in four sessions at 7,929,204 /
7,928,558 / 7,927,998 / 7,926,763, agreeing to 0.03%.

Data: `results/2026-09-15/latency_cycles.txt`

### L1 hit latency read 6.2 cycles against a published 4

The Day 2/4 chase measured 1.54 ns/hop in the L1-resident region, which at the
published P-core clock implied ~6.2 cycles. It stayed open because macOS exposes
no userspace cycle counter, so the conversion depended on an assumed frequency
and the anomaly could not be separated from an error in that assumption.

Day 8 read cycles directly around the same chase. 32 KB chain (a quarter of
L1d), 10M dependent hops, barrier per hop, three untimed warmups:

    4.005 / 4.011 / 4.018 cycles per hop   (three runs, 0.3% spread)
    3.002 instructions per hop             (confirms a dependent load chain)
    checksum identical across runs (1771)

L1 hit latency is 4.0 cycles. The Day 2 nanoseconds were correct; the frequency
used to convert them was not:

    4.021 GHz / (6.19 / 4.005) = 2.60 GHz

That is the cold-core frequency in run 1 of the Day 8 counter validation
(2.608 GHz, ramping to 4.02 over three runs). The Day 2 chase ran on a core
still climbing under DVFS.

Consequence: any cycle count from a wall-clock measurement divided by an assumed
frequency inherits the governor's state at measurement time, undetectably
without a cycle counter. Stronger than the SAXPY DVFS entry, which only
established that cold runs read low on throughput. Every ns-derived cycle figure
before Day 8 is suspect unless the run was demonstrably warm.

### Naive SGEMM declines smoothly with n, with no cache cliff

Expected a plateau then a cliff once the working set left L1d. Measured, single
P-core, `-O3`, median of 10:

    n      GFLOP/s
    128    2.259
    256    1.915
    512    1.639
    1024   1.484

A smooth 12-15% decline per doubling instead.

Cause: the B access is `B[k*n + j]`, stride `4n` bytes. At n=128 that is already
512 bytes, four times the 128-byte line, so every k step touches a fresh line at
every size tested. There is no spatial locality on B to lose, so no cliff.

What changes with n is which level serves the miss. At n=128, B is 64 KB and
fits the 128 KB L1d. At n=1024, B is 4 MB with a 12 MB working set pressing on
the 16 MB L2, so more misses reach DRAM. With Day 2 ratios L1:L2:DRAM =
1:4.0:103 the blend shifts gradually, hence the smooth slope.

Peak is ~7% of the estimated ~32 GFLOP/s single-core NEON FMA throughput. That
gap motivated the Day 7 blocking work.

The n=1024 figure here is 1.484, from `results/2026-09-11/sgemm_naive.csv`. Day 7
quotes 1.486 from a later rerun. 0.1% rerun variance.

### -O3 deleted the entire TLB chase loop

The first TLB sweep reported 0.38-0.74 ns/hop at every page count, faster than
the measured 1.53 ns L1 hit latency and so physically impossible. That
impossibility is what flagged it.

Cause: the chase result was stored to a file-scope `static volatile void*` that
nothing ever read. The compiler saw the whole translation unit, proved the store
dead, and dead-coded the store plus the loop feeding it. `objdump` confirmed the
timed region reduced to

```
  mov  w8, #200001
loop: subs x8, x8, #1
      b.ne loop
```

with zero memory instructions. `volatile` alone was not sufficient.

Fix: per-iteration compiler barrier, `asm volatile("" : "+r"(p))`. It emits no
instructions and only prevents the optimizer proving `p` unused.

`src/probe.cpp` (Day 2) does not have this problem, verified by disassembly. Its
chase is in a separate translation unit with no LTO, so the compiler never sees
chain construction and traversal together. The dependency is structural.

### SAXPY small sizes read 2.5x low on a cold machine

Rerunning the Day 3 sweep gave 59-74 GB/s at small sizes where Day 3 recorded
148-157, while large sizes reproduced exactly. The curve rose with size instead
of falling.

Cause: DVFS frequency ramp, not a code regression. At n=1024 the timed region is
~83 ns, so all 13 invocations (3 warmup + 10 measured) finish in ~1 microsecond,
far too short to pull a P-core off its idle frequency. The clock ramps during
the sweep, so the curve tracks wall-clock time since the sweep started, not
buffer size. Large sizes matched because they are DRAM-bandwidth-bound rather
than clock-bound, already at their ceiling.

Verified by rebuilding the unmodified Day 3 source (`4ede387`):

```
day-3 binary,   cold  ->  333 ns,  36.9 GB/s
current binary, cold  ->  125 ns,  98.3 GB/s
current binary, warm  ->   83 ns, 148.0 GB/s   (commit: 83 ns, 148.0)
```

The Day 3 code reproduces the bad numbers when cold, so the refactor is
exonerated. The same run reconfirmed P-cores rather than E-cores: flat through
128 KB, the falsification test written down on Day 2.

Warmup reps warm the cache, not the clock. Cache warmup takes microseconds, the
DVFS ramp takes milliseconds.

---

## Open

### Padded SGEMM still declines above T=32 while the tiles still fit

With the power-of-two stride removed the padded sweep peaks at T=32 and falls
monotonically. Capacity does not account for where the fall starts. Three float
tiles of T x T occupy `3 * T^2 * 4` bytes against the 128 KiB L1d:

```
T      working set    n=1024 padded GFLOP/s
32     12 KiB         3.992   <- peak
48     27 KiB         3.386   -15%
64     48 KiB         3.158   -21%
96     108 KiB        2.552   -36%
128    192 KiB        2.307        (exceeds L1d)
256    768 KiB        1.785        (exceeds L1d)
```

The decline is 15% at T=48, where ~27 KiB is about a fifth of L1d, and 36% at
T=96, still inside 128 KiB. Capacity does bind at T=128 and T=256, which
genuinely overflow L1d, but a third of the throughput is gone before that.
n=512 behaves identically (3.416 / 3.150 / 2.582 at T=48/64/96). IQR under 0.8%
on every point, reproduced within 0.3% across two runs, so the decline is real.

No hypothesis recorded, deliberately. The candidates are not separable from
throughput numbers alone, and guessing is what produced the Day 2 error.
Hardware counters were added on Day 8 to settle it; the counter sweep across T
has not been run.

### Accelerate sgemm measures 1120 GFLOP/s, 35x the NEON ceiling

At n=1024, single-threaded, `cblas_sgemm` measures 1120 GFLOP/s against the
~32 GFLOP/s single-core NEON FMA figure estimated on Day 6. That tripped the
rule that any number better than a known physical ceiling is a methodology bug
until proven otherwise. Two entries above were exactly that failure.

It survived the check. Verified outside the timing harness entirely: 400
`cblas_sgemm` calls timed with `steady_clock`, plus a checksum read of C so
nothing could be dead-coded. That path agreed at 1100 GFLOP/s.
`VECLIB_MAXIMUM_THREADS=1` had no effect: both configurations measure the same,
and CPU time against wall time over the 400 calls was 1:1, where six P-cores
would have shown roughly 6x. The work is one thread, not parallel throughput
mislabelled.

**Hypothesis, not proven: Accelerate dispatches to Apple's AMX matrix
coprocessor.** AMX is undocumented, attached per-cluster rather than per-core,
with no public instruction interface, so Accelerate is the only supported way to
reach it. That explains both facts at once: the magnitude, because AMX is a
dedicated matrix unit rather than the NEON pipeline, and the irrelevance of the
thread cap, because a per-cluster block can be saturated by one thread. No
counter was read and the instruction stream was not inspected, so the evidence
is equally consistent with any wide matrix unit behind one thread.

**Consequence: Accelerate is not a like-for-like ceiling.** The naive and
blocked kernels are scalar C++ on the general-purpose pipeline, no NEON
intrinsics and no matrix unit. Three ceilings, kept distinct:

```
scalar, general-purpose pipeline   ~4 GFLOP/s      MEASURED (best blocked kernel)
NEON FMA, single core              ~32 GFLOP/s     ESTIMATED, still unmeasured
AMX via Accelerate                 ~1120 GFLOP/s   MEASURED
```

The middle row is what the current kernels work against and the only one never
measured. Until a hand-vectorised NEON SGEMM exists, the scalar-to-Accelerate
ratio should not be quoted as a speedup target. Open: whether the mechanism is
AMX, and what the real NEON ceiling is.

### DRAM latency 158 ns against an expected 90-100 ns

The Day 2 capacity sweep bottoms out at 158.128 ns/hop at 64 MB. The 32 MB and
64 MB rows carry that sweep's largest IQRs (46.4 and 15.6), so part of the gap
may be measurement quality rather than the machine.

Day 8 direct cycle measurement at 64 MB gives 338.519 cycles/hop = 84.2 ns at
4.021 GHz, inside the expected range, pointing at the same cold-clock artifact
as the L1 hit latency entry. Not resolved: the gap is larger than the L1 case,
and the original probe should be rerun under counters before this closes.

Nothing downstream depends on the absolute number. The Day 2 conclusions rest on
the ratio L1:L2:DRAM = 1:4.0:103, which is frequency- and
methodology-independent and lands inside the published 1:3-4:50-100 range.

Data: `results/2026-09-15/latency_cycles.txt`

### Measured TLB knee sits above the published 128 entries

Published figures for Apple Silicon report a 128-entry L1 dTLB. The sweep is
flat to three decimals (1.529 ns) through 144 pages, first rises at 160, jumps
at 192.

Four candidates, none tested:

1. **Offset scheme.** The slot offset steps one cache line and wraps every 128
   pages (`(n * 128) % 16384`). The knee may be interacting with that period.
   Test: rerun with a 256-byte step and see whether the knee moves.
2. **TLB set associativity.** Page numbers 0..N-1 are consecutive integers, so
   they distribute perfectly uniformly across TLB sets, the best case. A uniform
   pattern may reach full nominal capacity where real workloads do not.
3. **The published figure may be M2 Pro, not M3 Pro.** The commonly cited source
   (Chips and Cheese) measured M2.
4. **L2 TLB bleed-through.** If L2 TLB hits are cheap enough, early L1 dTLB
   misses stay invisible until the miss fraction grows.

Also unexplained: several tail values repeat to three decimals (3.823 three
times, 4.168 twice), suggesting quantization. The TLB probe prints median only.
Add IQR.
