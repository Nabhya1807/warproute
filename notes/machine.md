# Machine under test

## Apple M3 Pro (primary dev machine)
- macOS 24.6.0 (Darwin), arm64 · Apple clang 16.0.0
- Build flags: `-O3` only. `-mcpu=apple-m3` is rejected by clang 16 even on M3
  hardware; the CMake flag check correctly falls back.

### OS-reported (ground truth for validation)
| Parameter | P-cores | E-cores |
|---|---|---|
| Physical cores | 6 | 6 |
| L1d | 128 KiB | 64 KiB |
| L2 (shared per cluster) | 16 MiB | 4 MiB |

Cache line 128 B (2x the x86 norm) · Page size 16384 B · GPU absent (Colab)

### Benchmarking procedure
Run bandwidth sweeps on a **warm** machine. `run_n`'s 3 warmup reps warm the
cache, not the clock — at n=1024 all 13 invocations finish in ~1 us, far too
short to ramp the P-core off its idle DVFS frequency. Run any sweep twice and
use the second result. See surprises.md.

---

## Day 2 — pointer-chase capacity sweep
1M hops per size, fixed seed, median of 10 after 3 warmups.

```
buffer   ns/hop    iqr        buffer   ns/hop    iqr
4 KB      1.531   0.008       1 MB      8.939   1.352
8 KB      1.540   0.007       2 MB      9.306   0.034
16 KB     1.537   0.024       4 MB     11.471   0.088
32 KB     1.543   0.012       8 MB     13.086   2.601
64 KB     1.543   0.016       16 MB    19.763   3.484  <- last L2
128 KB    1.549   0.017 <-    32 MB    86.011  46.431  <- CLIFF 4.4x
256 KB    6.091   0.037 <-    64 MB   158.128  15.583
512 KB    7.958   0.038         (CLIFF 4.0x at 256 KB -> L1d = 128 KB)
```

L1d = 128 KB · L2 = 16 MB · ratios L1 : L2 : DRAM = 1 : 4.0 : 103
The L2 region is not flat — climbs 3x from 256 KB to 16 MB (see surprises.md).
IQRs at 1/8/16/32 MB are large relative to their medians; those rows are noisy.

---

## Day 3 — SAXPY bandwidth (single-threaded, sequential)
`y[i] = a*x[i] + y[i]`. All sizes passed verify().

```
bytes      GB/s              bytes       GB/s
12 KB      148.0             12288 KB     68.0
24 KB      147.2             24576 KB     93.1
48 KB      147.6             49152 KB     90.1
96 KB      157.3             98304 KB     88.0
192 KB     119.4  <- L1      196608 KB    92.2
384 KB      93.4             393216 KB    79.1  <- L2
768 KB      94.9             786432 KB    78.0
1536 KB     95.4
3072 KB     95.6
6144 KB     95.2
```

Peak (L1) ~150-157 GB/s · Plateau (L2) ~90-96 · Steady state (DRAM) ~78-80

Same two boundaries as the day-2 chase, but gentler bends: random access defeats
prefetch and shows 4x cliffs; sequential streaming gets row-buffer hits and
prefetching and drops only ~35% / ~15%.

---

## Day 4 — L1d associativity (conflict-miss sweep)

Method: K addresses spaced exactly one L1d capacity apart (131072 B), so all K
share a set index but differ in tag. Linked into one shuffled cycle, 1M hops.
Stride derived from `hw.perflevel0.l1dcachesize`, not hardcoded.

```
K     ns/hop     iqr          K     ns/hop     iqr
1     0.382      0.0001  <-   17    6.975      0.543
2     1.548      0.059        18    6.969      0.120
3     1.544      0.042        19    7.114      0.353
4     1.546      0.115        20    7.030      0.053
5     1.556      0.037        21    8.239      0.390
6     1.550      0.018        22    9.775      0.063
7     1.535      0.011        23   10.356      0.305
8     1.542      0.031  <-    24   11.034      0.351
9     3.527      1.098  <-    25   10.935      0.661
10    4.529      0.442        26   10.528      0.831
11    4.942      0.318        27   10.236      1.256
12    5.184      0.135        28   10.946      0.595
13    6.035      0.680        29   11.006      0.251
14    6.613      0.982        30   11.130      0.304
15    6.927      0.118        31   10.928      0.211
16    7.204      0.390        32   11.244      0.241
```

K=1 is degenerate (see surprises.md). K=2..8 flat within 0.02 ns; K=9 jumps 2.3x.

**RESULT: L1d is 8-way set-associative.** Full structure, all measured:
capacity 128 KB · block 128 B · 1024 blocks · 8 ways · 128 sets.
Check: 128 x 8 x 128 B = 131072 B = 128 KB.

The K=16-20 plateau (~7.0 ns) sits near the day-2 L2 hit latency (~6.1 ns) —
once L1 fully thrashes, every access is served by L2. Two experiments agreeing.

---

## Day 5 — TLB reach (page-strided pointer chase)

Method: one pointer slot per page, linked into a single shuffled cycle so every
hop lands on a distinct page and consumes a distinct TLB entry. Slot offset
advances one cache line per page (`(n * line_size) % page_size`) so slots spread
across L1 sets — without this the sweep re-measures associativity, not TLB reach.
Page size from `hw.pagesize`. 200k hops, median of 10 after 3 warmups.

Warm run (flat region matches the 1.53 ns L1 hit latency from days 2 and 4):

```
pages   ns/hop           pages   ns/hop
96      1.529            192     2.784  <- JUMP, 1.8x
112     1.529            224     3.823
128     1.529            256     4.168
132     1.529            320     3.823
136     1.529            384     4.168
142     1.530            448     4.275
144     1.529  <- flat   512     3.823
160     1.610  <- +5%    768     4.332
                         1024    3.854
```

Coarse tail: 2048 -> 6.749 · 4096 -> 15.335 · 8192 -> 17.631

**RESULT: knee falls between 144 and 192 pages (~2.4-3.1 MB).** Flat to three
decimals through 144 across seven consecutive sizes; first movement at 160; jump
at 192. Published figures report a 128-entry L1 dTLB (2 MB reach) — consistent,
but this does not resolve 128 vs 160. See surprises.md.

Second plateau (224-768 pages) holds 3.8-4.3 ns across a 3.4x range, consistent
with a second-level TLB absorbing L1 dTLB misses. Past ~1024 pages the coarse
sweep climbs to 17.6 ns — full page-table walks.

Cross-check against day 2: the L1 region (4-128 KB) spans at most 8 pages, so
TLB pressure cannot exist there. That is why the day-2 L1 plateau is genuinely
flat while the L2 plateau is not — TLB reach (~2.4 MB) sits **inside** the L2
range (16 MB), so the upper portion of the L2 sweep pays translation cost on top
of data cost.

---

## Predictions still open

**Day 6 (blocked SGEMM).** Three float tiles live at once:
32x32 = 12 KiB total (fits) · 64x64 = 48 KiB (fits) · 128x128 = 192 KiB (spills
128 KiB L1). Predicted best tile: **64x64**. Written day 2, before measurement.

## Colab T4 (GPU runs)
Fill in after first Colab session: SM count, compute capability, CUDA version.