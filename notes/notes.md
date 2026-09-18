# WarpRoute — Lab Notebook, Days 1–10

C++17 CPU/GPU autotuner. Every number below was measured on this machine unless
it is explicitly labelled PREDICTED, ESTIMATED or PUBLISHED.

This file is the consolidated notebook. It merges what was previously split
across the root `NOTES.md`, `notes/machine.md`, `notes/surprises.md`,
`notes/day9_prediction.md` and `results/2026-09-16/day9_mpki.md`.

**`notes/surprises.md` remains the canonical standalone entry point for the
anomaly record, and this file is the combined reference. The overlap between the
two is deliberate: open surprises.md directly for the anomalies, come here for
everything at once. `notes/machine.md` no longer exists — its content is §1 and
Appendix A below.**

**Contents**

- [0. Project rules](#0-project-rules)
- [1. Machine under test](#1-machine-under-test)
- [2. Benchmarking procedure](#2-benchmarking-procedure)
- [Day 1 — timing harness](#day-1--timing-harness)
- [Day 2 — cache capacity sweep](#day-2--cache-capacity-sweep)
- [Day 3 — SAXPY bandwidth](#day-3--saxpy-bandwidth)
- [Day 4 — L1d associativity](#day-4--l1d-associativity)
- [Day 5 — TLB reach](#day-5--tlb-reach)
- [Day 6 — naive SGEMM baseline](#day-6--naive-sgemm-baseline)
- [Day 7 — tiling, the refuted prediction, and the padding fix](#day-7--tiling-the-refuted-prediction-and-the-padding-fix)
- [Day 7b — Accelerate as a ceiling](#day-7b--accelerate-as-a-ceiling)
- [Day 8 — hardware performance counters](#day-8--hardware-performance-counters)
- [Day 9 — MPKI and TLB counters on SGEMM](#day-9--mpki-and-tlb-counters-on-sgemm)
- [Day 10 — not yet run](#day-10--not-yet-run)
- [Appendix A — measured machine model](#appendix-a--measured-machine-model)
- [Appendix B — open questions](#appendix-b--open-questions)
- [Appendix C — prediction scorecard](#appendix-c--prediction-scorecard)
- [Appendix D — methodology lessons](#appendix-d--methodology-lessons)
- [Appendix E — data files](#appendix-e--data-files)

---

## 0. Project rules

Build: `cmake -B build && cmake --build build`

- Do **not** modify `src/timer.cpp`, `include/timer.hpp`, `src/probe.cpp`, or
  kernel inner loops. Those are hand-written.
- No new dependencies. C++17 stdlib only (CUDA later, guarded).
- Never use `-march=native`. This is arm64; use `-mcpu=apple-m3`.
- No CUDA on this machine. All GPU code behind `find_package(CUDAToolkit QUIET)`.
- Every benchmark config must pass `verify()` before its timing is reported.
- Small changes, explained. This code has to be defensible in an interview.

---

## 1. Machine under test

### Apple M3 Pro (primary dev machine)

- macOS 24.6.0 (Darwin) / macOS 15.7.9, arm64 · Apple clang 16.0.0
- Build flags: `-O3` only. `-mcpu=apple-m3` is **rejected by clang 16 even on M3
  hardware**; the CMake flag check correctly falls back to plain `-O3`.

OS-reported topology (ground truth used for validating the probes):

| Parameter | P-cores | E-cores |
|---|---|---|
| Physical cores | 6 | 6 |
| L1d | 128 KiB | 64 KiB |
| L2 (shared per cluster) | 16 MiB | 4 MiB |

Cache line 128 B (2× the x86 norm) · page size 16384 B · no local GPU (GPU work
deferred to Colab).

### Colab T4 (GPU runs)

Not yet run. Fill in after the first Colab session: SM count, compute
capability, CUDA version.

---

## 2. Benchmarking procedure

Rules distilled from the failures in Days 3, 5 and 8. Ignoring any one of these
has already produced a wrong number in this project.

1. **Run sweeps on a warm machine.** `run_n`'s 3 warmup reps warm the *cache*,
   not the *clock*. At n=1024 all 13 invocations finish in ~1 µs, far too short
   to pull a P-core off its idle DVFS frequency. Run any sweep twice and use the
   second result.
2. **Discard the first two runs of any counter measurement.** Measured ramp:
   2.608 → 3.211 → 3.990 → 4.027 GHz, then it holds (Day 8).
3. **Treat high IQR as contamination, not data.** Close background apps, stay on
   AC power.
4. **Never derive cycles by dividing ns by an assumed frequency.** That silently
   inherits the governor's state at measurement time and the error is invisible
   without a cycle counter. This produced the bogus 6.2-cycle L1 latency.
5. **Any number better than a known physical floor/ceiling is a methodology bug
   until proven otherwise.** Caught the deleted `-O3` loop, the degenerate K=1
   point, and was correctly cleared for Accelerate.
6. **Verify before timing.** Every kernel config passes `verify()` (max abs err
   vs. a reference, tolerance 1e-3) before its number is reported.

---

## Day 1 — timing harness

Median/IQR harness, validated with three cold launches summing 1M doubles.

| Launch | Median (ms) | IQR (µs) | Verdict |
|---|---:|---:|---|
| 1 | 1.053 | 5.8 | clean |
| 2 | 1.481 | 271 | **contaminated** |
| 3 | 1.065 | — | clean |

Runs 1 and 3 agree within 1.2%. Run 2's IQR is ~47× run 1's, and the IQR
correctly flagged it without any outside knowledge.

**Takeaway:** the IQR is the contamination detector. A median alone would have
hidden run 2's problem.

---

## Day 2 — cache capacity sweep

Pointer chase, 1M hops per size, fixed seed, median of 10 runs after 3 warmups.

| buffer | ns/hop | IQR | | buffer | ns/hop | IQR |
|---|---:|---:|---|---|---:|---:|
| 4 KB | 1.531 | 0.008 | | 1 MB | 8.939 | 1.352 |
| 8 KB | 1.540 | 0.007 | | 2 MB | 9.306 | 0.034 |
| 16 KB | 1.537 | 0.024 | | 4 MB | 11.471 | 0.088 |
| 32 KB | 1.543 | 0.012 | | 8 MB | 13.086 | 2.601 |
| 64 KB | 1.543 | 0.016 | | 16 MB | 19.763 | 3.484 ← last L2 |
| **128 KB** | **1.549** | 0.017 ← last fast | | 32 MB | 86.011 | 46.431 ← cliff 4.4× |
| 256 KB | 6.091 | 0.037 ← cliff 4.0× | | 64 MB | 158.128 | 15.583 |

**Results**

- **L1d = 128 KB** (last fast size before the 4.0× jump) — matches prediction.
- **L2 = 16 MB** (last size before the 4.4× jump) — matches prediction.
- Confirms **P-core** execution: an E-core would have broken at 64 KB. This was
  written down beforehand as the falsification test.

Frequency-independent ratios:

| | L1 | L2 | DRAM |
|---|---:|---:|---:|
| Measured | 1 | 4.0 | 103 |
| Published range | 1 | 3–4 | 50–100 |

Two things the sweep does *not* do cleanly, both chased later:

- The L2 region is **not flat** — it climbs 3× from 256 KB to 16 MB. Resolved
  Day 8 (it is L1 residency decay, not an L2 property).
- IQRs at 1/8/16/32 MB are large relative to their medians; those rows are noisy.
- DRAM bottoms out at 158 ns against an expected 90–100 ns. Revisited Day 8.

---

## Day 3 — SAXPY bandwidth

`y[i] = a*x[i] + y[i]`, single-threaded, sequential. All sizes passed `verify()`.

| bytes | GB/s | | bytes | GB/s |
|---|---:|---|---|---:|
| 12 KB | 148.0 | | 12288 KB | 68.0 |
| 24 KB | 147.2 | | 24576 KB | 93.1 |
| 48 KB | 147.6 | | 49152 KB | 90.1 |
| 96 KB | 157.3 | | 98304 KB | 88.0 |
| 192 KB | 119.4 ← L1 edge | | 196608 KB | 92.2 |
| 384 KB | 93.4 | | 393216 KB | 79.1 ← L2 edge |
| 768 KB | 94.9 | | 786432 KB | 78.0 |
| 1536 KB | 95.4 | | | |
| 3072 KB | 95.6 | | | |
| 6144 KB | 95.2 | | | |

| Region | GB/s |
|---|---|
| Peak (L1) | ~150–157 |
| Plateau (L2) | ~90–96 |
| Steady state (DRAM) | ~78–80 |

Same two boundaries as the Day 2 chase, but **gentler bends**: random access
defeats prefetch and shows 4× cliffs, while sequential streaming gets row-buffer
hits and prefetching and drops only ~35% / ~15%.

**Caveat — the small-size numbers above were taken mid-DVFS-ramp.** See the
DVFS entry under Day 5 findings; a warm rerun reads 59–74 GB/s at the small
sizes and the 12 MB row reads ~68 rather than the 54.9 originally logged.

---

## Day 4 — L1d associativity

Method: K addresses spaced exactly one L1d capacity apart (131072 B), so all K
share a set index but differ in tag. Linked into one shuffled cycle, 1M hops.
Stride derived from `hw.perflevel0.l1dcachesize`, not hardcoded.

| K | ns/hop | IQR | | K | ns/hop | IQR |
|---|---:|---:|---|---|---:|---:|
| 1 | 0.382 | 0.0001 ← degenerate | | 17 | 6.975 | 0.543 |
| 2 | 1.548 | 0.059 | | 18 | 6.969 | 0.120 |
| 3 | 1.544 | 0.042 | | 19 | 7.114 | 0.353 |
| 4 | 1.546 | 0.115 | | 20 | 7.030 | 0.053 |
| 5 | 1.556 | 0.037 | | 21 | 8.239 | 0.390 |
| 6 | 1.550 | 0.018 | | 22 | 9.775 | 0.063 |
| 7 | 1.535 | 0.011 | | 23 | 10.356 | 0.305 |
| **8** | **1.542** | 0.031 ← last flat | | 24 | 11.034 | 0.351 |
| **9** | **3.527** | 1.098 ← 2.3× jump | | 25 | 10.935 | 0.661 |
| 10 | 4.529 | 0.442 | | 26 | 10.528 | 0.831 |
| 11 | 4.942 | 0.318 | | 27 | 10.236 | 1.256 |
| 12 | 5.184 | 0.135 | | 28 | 10.946 | 0.595 |
| 13 | 6.035 | 0.680 | | 29 | 11.006 | 0.251 |
| 14 | 6.613 | 0.982 | | 30 | 11.130 | 0.304 |
| 15 | 6.927 | 0.118 | | 31 | 10.928 | 0.211 |
| 16 | 7.204 | 0.390 | | 32 | 11.244 | 0.241 |

K=2..8 is flat within 0.02 ns; K=9 jumps 2.3×.

**RESULT: L1d is 8-way set-associative.** Full structure, every field measured:

| Property | Value |
|---|---|
| Capacity | 128 KB |
| Block size | 128 B |
| Blocks | 1024 |
| Ways | 8 |
| Sets | 128 |

Check: 128 sets × 8 ways × 128 B = 131072 B = 128 KB. ✓

Two cross-checks fall out:

- The K=16–20 plateau (~7.0 ns) sits near the Day 2 L2 hit latency (~6.1 ns) —
  once L1 fully thrashes, every access is served by L2. Two independent
  experiments agreeing.
- 128 sets × 128 B = 16 KiB = **exactly the page size**, so the L1d set index is
  the line's offset within its page. This fact drives the Day 7 and Day 9
  conflict-miss analysis.

---

## Day 5 — TLB reach

Method: one pointer slot per page, linked into a single shuffled cycle so every
hop lands on a distinct page and consumes a distinct TLB entry. Slot offset
advances one cache line per page (`(n * line_size) % page_size`) so slots spread
across L1 sets — **without this the sweep re-measures associativity, not TLB
reach**. Page size from `hw.pagesize`. 200k hops, median of 10 after 3 warmups.

Warm run (the flat region matches the 1.53 ns L1 hit latency from Days 2 and 4):

| pages | ns/hop | | pages | ns/hop |
|---|---:|---|---|---:|
| 96 | 1.529 | | 192 | 2.784 ← jump, 1.8× |
| 112 | 1.529 | | 224 | 3.823 |
| 128 | 1.529 | | 256 | 4.168 |
| 132 | 1.529 | | 320 | 3.823 |
| 136 | 1.529 | | 384 | 4.168 |
| 142 | 1.530 | | 448 | 4.275 |
| **144** | **1.529** ← last flat | | 512 | 3.823 |
| 160 | 1.610 ← +5% | | 768 | 4.332 |
| | | | 1024 | 3.854 |

Coarse tail: 2048 → 6.749 · 4096 → 15.335 · 8192 → 17.631

**RESULT: the knee falls between 144 and 192 pages (~2.4–3.1 MB).** Flat to
three decimals through 144 across seven consecutive sizes; first movement at
160; jump at 192. Published figures report a 128-entry L1 dTLB (2 MB reach) —
consistent, but this sweep does not resolve 128 vs 160.

Second plateau (224–768 pages) holds 3.8–4.3 ns across a 3.4× range, consistent
with a **second-level TLB** absorbing L1 dTLB misses. Past ~1024 pages the
coarse sweep climbs to 17.6 ns — full page-table walks.

**Cross-check against Day 2.** The L1 region (4–128 KB) spans at most 8 pages,
so TLB pressure cannot exist there. That is why the Day 2 L1 plateau is
genuinely flat while the L2 plateau is not: TLB reach (~2.4 MB) sits *inside*
the L2 range (16 MB), so the upper portion of the L2 sweep pays translation cost
on top of data cost.

### Day 5 side finding — `-O3` deleted the entire chase loop [RESOLVED]

The first TLB sweep reported 0.38–0.74 ns/hop at *every* page count — faster
than the measured 1.53 ns L1 hit latency, hence physically impossible. That
impossibility is what flagged it.

Cause: the chase result was stored to a file-scope `static volatile void*` that
nothing ever read. The compiler saw the whole translation unit, proved the store
dead, and dead-coded the store plus the loop feeding it. `objdump` confirmed the
timed region reduced to:

```
  mov  w8, #200001
loop: subs x8, x8, #1
      b.ne loop
```

Zero memory instructions. **`volatile` alone was not sufficient.**

Fix: a per-iteration compiler barrier, `asm volatile("" : "+r"(p))`. It emits no
instructions; it only prevents the optimizer proving `p` unused.

`src/probe.cpp` (Day 2) does **not** have this problem — verified by
disassembly. Its chase lives in a separate translation unit with no LTO, so the
compiler never sees chain construction and traversal together; the dependency is
structural rather than barrier-enforced.

### Day 5 side finding — SAXPY small sizes read 2.5× low on a cold machine [RESOLVED]

Rerunning the Day 3 sweep gave 59–74 GB/s at small sizes where Day 3 recorded
148–157, while large sizes reproduced exactly. The curve *rose* with size
instead of falling — backwards.

Cause: **DVFS frequency ramp, not a code regression.** At n=1024 the entire
timed region is ~83 ns, so all 13 invocations (3 warmup + 10 measured) complete
in ~1 µs — far too short to pull the P-core off its idle frequency. The clock
ramps *during* the sweep, so early sizes are measured slow and later ones at
full clock. The curve tracks wall-clock time since the sweep started, not buffer
size. Large sizes matched because they are DRAM-bandwidth-bound rather than
clock-bound; that tail was already at its ceiling.

Verified by rebuilding the unmodified Day 3 source (`4ede387`) and running both
binaries cold and warm:

| binary | state | time | GB/s |
|---|---|---:|---:|
| Day 3 | cold | 333 ns | 36.9 |
| current | cold | 125 ns | 98.3 |
| current | warm | 83 ns | 148.0 (commit: 83 ns, 148.0) |

The Day 3 code reproduces the bad numbers when cold, so the refactor is
exonerated. The same run reconfirmed P-cores not E-cores (flat through 128 KB).

The isolated **12 MB SAXPY dip** (54.9 GB/s at N=1048576, against 95.6 and 93.1
either side, with no cache boundary nearby) is the same family: it never
reproduced on a warm run, which gives ~68 GB/s in line with its neighbours.
[RESOLVED]

### Day 5 side finding — K=1 measures 0.38 ns/hop, below L1 hit latency [RESOLVED]

K=1 builds a single self-referencing entry (`chain[0] = 0`), so the chase is
`p = chain[0]` forever. Same address every hop: served by load-forwarding rather
than a real cache access, with a trivial dependency chain. 0.38 ns is ~1.5
cycles — loop overhead, not memory latency.

Not a data point; excluded from analysis. Action for the shipped tool: start the
sweep at K=2, or print K=1 with an explicit "degenerate" label.

### Day 5 side finding — the associativity curve ramps rather than steps [RESOLVED]

Expected a clean step from all-hits to all-misses at K=9. Instead the curve
climbs (3.5 → 4.5 → 5.2 → 6.0 → 7.2), plateaus ~7 ns for K=16–20, then climbs
again to ~11 ns by K=24. At K=9 the set is only slightly over capacity, so
depending on replacement policy and access order some accesses still hit; the
miss fraction grows with K rather than flipping to 100% instantly. Not a problem
for the measurement — the K=8/K=9 boundary is still sharp and unambiguous — but
the degradation is smooth, not a step.

---

## Day 6 — naive SGEMM baseline

Single P-core, `-O3`, median of 10. `i, j, k` order, `k` innermost.

| n | GFLOP/s |
|---|---:|
| 128 | 2.259 |
| 256 | 1.915 |
| 512 | 1.639 |
| 1024 | 1.484 |

**Expected a plateau followed by a cliff once the working set left L1d. Got a
smooth ~12–15% decline per doubling instead.** [RESOLVED]

Explanation: the B access is `B[k*n + j]`, stride `4n` bytes. At n=128 that is
already 512 B — four times the 128-byte line. So every `k` step touches a fresh
line at **every** size tested. The kernel has no spatial locality on B to lose,
so there is no cliff when it "stops fitting."

What changes with n is *which level serves the miss*, not whether one occurs. At
n=128, B is 64 KB and fits in the 128 KB L1d, so repeat passes hit L1. At
n=1024, B is 4 MB with a 12 MB working set, pressing on the 16 MB L2, so more
misses reach DRAM. With Day 2 ratios L1:L2:DRAM = 1:4.0:103, the blend shifts
gradually — hence the smooth slope.

Peak is ~7% of estimated single-core NEON FMA throughput (~32 GFLOP/s,
ESTIMATED). That gap is the motivation for the Day 7 blocking work.

*(Note: the n=1024 figure here is 1.484 from `results/sgemm_naive.csv`; Day 7
quotes 1.486 as the naive baseline, from a later rerun. 0.1% rerun variance, not
a discrepancy worth chasing.)*

---

## Day 7 — tiling, the refuted prediction, and the padding fix

### The prediction (written Day 2, before any SGEMM existed)

Three float tiles live at once, so `3 * T² * 4 ≤ 131072` gives T ≤ 104:

| Tile | Working set | Fits 128 KiB L1d? |
|---|---|---|
| 32×32 | 12 KiB | yes |
| 64×64 | 48 KiB | yes |
| 128×128 | 192 KiB | **no** |

**Predicted best tile: 64×64.** The whole argument is capacity — how much fits.

### The sweep refutes it [RESOLVED]

`results/2026-09-10/sgemm_tile_sweep.csv`, GFLOP/s, unpadded (row stride = n):

| | T=8 | T=16 | T=32 | T=48 | T=64 | T=96 | T=128 | T=256 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| n=1024 | **3.922** | 3.919 | 2.445 | 1.757 | 1.579 | 1.449 | 1.430 | 1.428 |
| n=512 | 3.705 | **3.942** | 3.938 | 3.417 | 2.819 | 2.366 | 2.106 | 1.754 |

Peak is at the *smallest* tiles measured, and throughput falls monotonically
above T=32. The predicted optimum, T=64 at n=1024, runs at 1.579 GFLOP/s —
2.5× *slower* than T=8 and only **6% above the 1.486 naive baseline**. IQR is
under 1% on nearly every point (one exception: n=512 T=8 at 13.6%), so this is
not noise. **The prediction is wrong in direction, not just magnitude.**

### Hypothesis: the binding constraint is conflict misses, not capacity

The capacity argument silently assumes a T×T tile is a contiguous block of
`T² * 4` bytes. It is not. In a row-major n×n matrix a tile is T *rows*, each
`T * 4` bytes long, spaced `n * 4` bytes apart. **What the cache sees is the
stride, not the tile area.**

At n=1024 the row stride is 4096 B = 32 cache lines of 128 B. With the 128 sets
measured on Day 4, the set index advances 32 per row and wraps every 4 rows, so
an entire tile — however tall — lands in only **4 distinct set groups**. A
64-row tile puts 16 rows into each group against the 8 ways measured on Day 4:
2× over-subscribed, the same thrashing condition as the Day 4 K=9 result. At
T=16 it is 4 rows per set group, inside 8 ways, and it fits.

**Supporting evidence: the n=512 column behaves as the stride argument predicts
and the capacity argument does not.** At n=512 the row stride is 2048 B = 16
lines, so the set index wraps every 8 rows instead of 4 — twice as many set
groups, hence roughly twice the tolerable tile height. That is what the data
shows: n=512 still holds ~3.9 GFLOP/s at T=32 where n=1024 has already
collapsed to 2.445, and n=512's own collapse is deferred to T=48–64. Under a
capacity explanation the two sizes share one L1d and should turn over at the
same T. They do not — **the turnover tracks the row stride.**

### CONFIRMED by the padding experiment

The matrices were reallocated with a row stride of `n + 16` floats instead of
`n`, staying logically n×n. Tile area, loop structure, flop count and tile size
are all unchanged; the only difference is that rows are no longer spaced at a
power of two. The padded kernel was verified against `sgemm_naive` within 1e-3
before timing. `results/2026-09-10/sgemm_tile_sweep_padded.csv`:

| | T=8 | T=16 | T=32 | T=48 | T=64 | T=96 | T=128 | T=256 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| n=1024 | 3.876 | 3.949 | **3.992** | 3.386 | 3.158 | 2.552 | 2.307 | 1.785 |
| n=512 | 3.923 | **4.124** | 4.005 | 3.416 | 3.150 | 2.582 | 2.308 | 1.782 |

Three things fall out, all in the direction the set-group model predicts.

**1. Padding alone roughly doubles throughput at the collapsed point.** At
n=1024, T=64: 1.579 → 3.158 GFLOP/s, **+100%**. Same arithmetic, same tile size,
same instruction count — only row spacing changed. This is the comparison that
isolates stride from everything else, because tile height is held fixed.

**2. The benefit is asymmetric between the two sizes, and asymmetric the right
way.** The model says n=1024 has 4 set groups against n=512's 8, so n=1024
carries ~2× the conflict pressure and has ~2× as much to gain. At T=64:

| n | unpadded | padded | gain |
|---|---:|---:|---:|
| 1024 | 1.579 | 3.158 | **+100%** |
| 512 | 2.819 | 3.150 | +12% |

After padding the two converge to 3.158 and 3.150 — a **0.3% spread**, where
before they differed by 79%. A capacity explanation predicts *no* asymmetry
here, because both sizes share one 128 KiB L1d and the tiles are identical.
This is what promotes the entry from "consistent with" to confirmed.

**3. With conflicts removed the optimum moves up, toward where the capacity
model pointed.** Unpadded peak T=8/16 at 3.92; padded peak **T=32 at 3.992
GFLOP/s, 2.69× the 1.486 naive baseline**.

**Verdict: the Day 2 reasoning was incomplete rather than wrong.** Capacity is a
real constraint and does set the optimum once conflicts are out of the way, but
on a power-of-two stride conflict misses bind first and bind much harder,
dragging the optimum down to T=8–16. The prediction failed because it modelled
only the weaker of the two constraints.

Measurement quality: padded IQR under 0.8% on every point; the sweep ran twice
with the two runs agreeing within 0.3%.

Caveats held open deliberately:

- Small tiles also shorten the innermost `k` run, so T=8–16 changes loop
  overhead, prefetch behaviour and the store pattern on C all at once. Point 1
  *narrows* this (it holds T=64 fixed, so inner-loop length cannot explain it)
  but does not eliminate it.
- This is two power-of-two strides plus one padded stride, **not a stride
  sweep**. The 2× claim is consistent with 2×, not measured against a third
  stride.

### Still OPEN: padded SGEMM declines above T=32 while the tiles still fit

| T | working set | n=1024 padded GFLOP/s | vs peak |
|---|---|---:|---:|
| 32 | 12 KiB | 3.992 | ← peak |
| 48 | 27 KiB | 3.386 | −15% |
| 64 | 48 KiB | 3.158 | −21% |
| 96 | 108 KiB | 2.552 | −36% |
| 128 | 192 KiB | 2.307 | (exceeds L1d) |
| 256 | 768 KiB | 1.785 | (exceeds L1d) |

The decline is already 15% at T=48, where the working set is ~27 KiB — about a
fifth of L1d, fitting comfortably. It reaches 36% at T=96, still inside 128 KiB.
Capacity does eventually become a real constraint at T=128 and T=256, but the
curve has lost more than a third of its throughput before reaching that point.
n=512 behaves identically (3.416 / 3.150 / 2.582 at T=48/64/96), so this is not
specific to one matrix size. IQR under 0.8% everywhere and reproduced within
0.3% across two runs, so the decline is real.

**No hypothesis recorded — deliberately.** The remaining candidates are not
separable from throughput numbers alone, and guessing here is what produced the
Day 2 error in the first place. This is what motivated adding hardware counters
on Day 8.

---

## Day 7b — Accelerate as a ceiling

Added Apple Accelerate's `cblas_sgemm` as a reference ceiling
(`results/2026-09-10/sgemm_blas.csv`):

| config | n | GFLOP/s | IQR % |
|---|---|---:|---:|
| single_threaded | 512 | 1192.495 | 0.4 |
| single_threaded | 1024 | 1118.408 | 1.0 |
| default | 512 | 1192.495 | 0.6 |
| default | 1024 | 1113.407 | 1.1 |

**1120 GFLOP/s against the ~32 GFLOP/s single-core NEON FMA estimate — 35× the
estimate.** That tripped the project rule that any number better than a known
ceiling is a methodology bug until proven otherwise. Two prior entries in this
notebook were exactly that failure.

**It survived the check.** Verified outside the timing harness entirely: 400
`cblas_sgemm` calls timed with `steady_clock`, plus a checksum read of C
afterwards so no part of the work could be dead-coded. That independent path
agreed at 1100 GFLOP/s. The multiply is genuinely happening.

**`VECLIB_MAXIMUM_THREADS=1` had no effect.** Both configurations measure the
same, and CPU time against wall time over the 400 calls was 1:1 — one core's
worth of CPU for the whole run, where six P-cores would have shown ~6×. The work
is genuinely single-threaded, so this is not parallel throughput mislabelled.

**Hypothesis, NOT proven: Accelerate is dispatching to Apple's AMX matrix
coprocessor.** AMX is undocumented, attached per-cluster rather than per-core,
and has no public instruction interface — Accelerate is the only supported way
to reach it. That would explain both facts at once: the magnitude (a dedicated
matrix unit, not the NEON pipeline) and the irrelevance of the thread cap (a
per-cluster block saturated by one thread). Nothing here demonstrates it; no
counter was read and the instruction stream was not inspected.

**Consequence: Accelerate is not a like-for-like ceiling for these kernels.**
The naive and blocked kernels are scalar C++ on the general-purpose pipeline —
no NEON intrinsics, no matrix unit. Three ceilings, kept distinct from here on:

| Ceiling | Value | Status |
|---|---|---|
| Scalar, general-purpose pipeline | ~4 GFLOP/s | **MEASURED** (best blocked kernel) |
| NEON FMA, single core | ~32 GFLOP/s | **ESTIMATED, never measured** |
| AMX via Accelerate | ~1120 GFLOP/s | **MEASURED** |

The middle row is what the current kernels actually work against, and it is the
only one of the three that has never been measured. Measuring it — a
hand-vectorised NEON SGEMM — is the honest next comparison. Until it exists, the
scalar-to-Accelerate ratio should not be quoted as a speedup target.

---

## Day 8 — hardware performance counters

PMU access via Apple's private `kperf`/`kperfdata` frameworks, `dlopen`'d at
runtime. Root required (`sudo ./build/counters`).

### Access method, event database, and instrument provenance

Canonical source for all of this is the PROVENANCE block at
`src/counters.cpp:1-40` and the config-bit comment at `src/counters.cpp:113-137`.
Summarised here so the notebook stands alone; the code comments are
authoritative if the two ever diverge.

**Access method.** Both frameworks are `dlopen`'d at runtime and the symbols
`dlsym`'d, so there is no link-time dependency on them:

```
/System/Library/PrivateFrameworks/kperf.framework/kperf
/System/Library/PrivateFrameworks/kperfdata.framework/kperfdata
```

Call sequence: `kpc_force_all_ctrs_set` → `kpep_db_create` → build a
`kpep_config` and add events → `kpep_config_kpc` → `kpc_set_config` →
`kpc_set_counting` / `kpc_set_thread_counting` → `kpc_get_thread_counters`.
None of this is documented or guaranteed by Apple; it can break on any OS
update.

**Event database.** Event numbers and names come from Apple's kpep database for
this core, `as3.plist` (`/usr/share/kpep/as3.plist`). Event numbers there exceed
255 — `L2_TLB_MISS_DATA` is 1035 — which is why the borrowed gist's `u8 number`
field at 0x2c is wrong for this machine. The `kpep_event` struct offsets used
here were re-read from `dyld_info -disassemble` of kperfdata on Darwin 24.6.0
and are this project's own work, not the gist's: name/description/errata/alias
at 0x00/0x08/0x10/0x18, fallback at 0x20, `counters_mask` (u32) at 0x28, number
as a u16 at 0x2c (`kpep_config_kpc` loads it with `ldrh [ev, #0x2c]`), and the
fixed-counter path taken on bit 0 of the byte at 0x30. Three `static_assert`s
pin the layout so a silent OS-side change fails the build rather than the
measurement.

**Finding — bit 17 (0x20000) is an EL0 AArch64 enable the kernel sets itself.**
`kpc_get_config` returns config words with bit 17 set for a word
`kpc_set_config` was given *without* it, so a naive read-back check reports a
mismatch on every nonzero counter. Bit 17 means "count this counter in EL0
AArch64 (user mode)".

Provenance: `dyld_info -disassemble` of kperfdata, and `objdump --macho -d` of
`/System/Library/Kernels/kernel.release.t6030`, Darwin 24.6.0
(xnu-11417.140.69.711.44~1).

- kperfdata `kpep_config_kpc`: when the plist architecture is "arm64" (id 3, per
  the `strcmp` chain in `init_db_from_plist`) it ORs 0x20000 into the word only
  for events added with `kpep_config_add_event` flag bit 0 (user space only).
  This project passes flag 0, so the words it writes carry no mode bits.
- Kernel get path (from `_kpc_get_config`, `0xfffffe00074879a8`): builds each
  word as the 16-bit PMESR0/1 event field plus 0x10000 / 0x20000 / 0xc0000 when
  PMCR1 bit s / s+8 / s+16 is set, where s = index + 2 (index + 26 for
  configurable indices 6–7). **s+8 is the EL0 AArch64 enable.**
- Kernel set path (`0xfffffe0007488278`): bits 16 and 17 of the word set PMCR1
  bits s and s+8; bit 18 sets s+16 only if kernel counting is allowed. A nonzero
  word with bits 16–17 clear gets s+8 alone when kernel counting is not allowed
  (otherwise s, s+8 and s+16). A zero word gets none.

**Consequence: a nonzero word written with no mode bits reads back with 0x20000
added.** The read-back guard therefore ignores exactly that bit, and only on
nonzero words; any other differing bit still FAILs. The allowed-kernel default
would read back 0xf0000 instead, which has not been observed on this machine, so
those bits are deliberately left out and would still FAIL if they appeared.

### Counter validation

100,000,000 iterations × 7 scalar instructions (verified by disassembly).
Predicted instruction count: 700,000,000.

| Run | fixed instructions | INST_ALL (configurable) | Δ vs fixed | core freq |
|---|---:|---:|---:|---:|
| 1 (cold) | 700,215,102 | 667,148,875 | −4.7223% | 2.608 GHz |
| 2 | 700,040,465 | 700,000,516 | −0.0057% | 3.211 GHz |
| 3 | 700,045,947 | 700,000,516 | −0.0065% | 3.990 GHz |
| 4 | 700,041,055 | 700,000,516 | −0.0058% | 4.027 GHz |
| 5 | 700,040,299 | 700,000,516 | −0.0057% | 3.969 GHz |
| 6 | 700,037,772 | 700,000,516 | −0.0053% | 4.021 GHz |

1. **Frequency ramps 2.608 → 4.02 GHz over the first three runs, then holds.**
   DVFS, independently confirming the Day 3 SAXPY diagnosis by direct
   measurement rather than inference. **Protocol: discard the first two runs of
   any counter measurement.**
2. The configurable counter agrees with the fixed counter to 0.006% once warm.
   The 4.7% gap in run 1 was DVFS contamination, not counter error.
3. INST_ALL returns bit-identical 700,000,516 on every warm run while the fixed
   counter varies by a few thousand — the configurable counter measures a
   tighter window and is the more precise of the two.
4. **Warm P-core frequency = 4.02 GHz**, measured.

### Latency in cycles, no frequency conversion anywhere

Random-permutation pointer chase, 10,000,000 dependent hops, cycles from the
fixed PMU counter, compiler barrier per hop, three untimed warmup passes inside
the binary. `results/2026-09-15/latency_cycles.txt`.

| buffer | elements | cycles/hop | insns/hop |
|---|---:|---:|---:|
| 32 KB | 4096 | 4.005 | 3.002 |
| 256 KB | 32768 | 16.009 | 3.015 |
| 512 KB | 65536 | 21.077 | 3.020 |
| 1 MB | 131072 | 23.180 | 3.007 |
| 2 MB | 262144 | 24.020 | 3.012 |
| 64 MB | 8388608 | 338.519 | 3.111 |

Repeatability at 32 KB over three consecutive runs: 4.005 / 4.011 / 4.018
(0.3% spread), checksum identical (1771). `insns/hop` sits at 3.00 for all
in-cache sizes, confirming the loop compiled to a dependent load chain; the
drift to 3.111 at 64 MB is page-walk machinery appearing in the instruction
stream once the working set exceeds the 2.5 MB TLB reach measured on Day 5.

### FINDING — L1 hit latency is 4.0 cycles, matching the published figure [RESOLVED]

The Day 2/4 chase measured 1.54 ns/hop in the L1-resident region, which
converted at the M3 Pro's published clock implied ~6.2 cycles against a
published ~4. It stayed open because macOS exposes no userspace cycle counter,
so the conversion depended on an assumed frequency and the anomaly could not be
separated from an error in that assumption.

Direct measurement (32 KB chain = ¼ of L1d, 10M dependent hops):

```
4.005 / 4.011 / 4.018 cycles per hop   (three runs, 0.3% spread)
3.002 instructions per hop             (confirms a dependent load chain)
checksum identical across runs (1771)
```

**L1 hit latency is 4.0 cycles.** What the 6.2 actually was: the Day 2
nanoseconds were correct; the frequency used to convert them was not.

```
4.021 GHz / (6.19 / 4.005) = 2.60 GHz
```

That is the cold-core frequency observed in run 1 of the counter validation
(2.608 GHz). The Day 2 chase ran on a core still climbing under DVFS.

**Methodology consequence: any cycle count derived by dividing a wall-clock
measurement by an assumed frequency silently inherits the frequency governor's
state at measurement time, and the error is undetectable without a cycle
counter.** This is stronger than the Day 3 DVFS finding, which only established
that cold runs read low on throughput. **Every ns-derived cycle figure taken
before Day 8 should be treated as suspect unless the run was demonstrably
warm.**

### FINDING — the "L2 plateau climb" is L1 residency decay, not an L2 property

Day 2 expected a flat plateau at L2 hit latency across the whole L2 range and
instead saw 6.09 ns at 256 KB rising to 19.76 ns at 16 MB. Day 5's ~2.4 MB TLB
reach explains the *steep* part (2 MB → 16 MB latency doubles), but not the
early part: at 512 KB latency is already +31% over the 256 KB baseline, and
512 KB is only 32 pages — well inside TLB reach.

**TLB reach is ruled out by arithmetic** for the climb region: at 16 KB pages,
2 MB is 128 pages against the 160-entry L1 dTLB measured on Day 5. Every size in
the climb fits inside TLB reach, so misses cannot occur there. Any TLB
explanation applies only above ~2.5 MB.

**The climb decelerates**, each doubling adding roughly half what the previous
one added — a curve converging on an asymptote, not a step:

| transition | added cycles |
|---|---:|
| 256 KB → 512 KB | +5.07 |
| 512 KB → 1 MB | +2.10 |
| 1 MB → 2 MB | +0.84 |

**Two-population model.** The measurement is a blend, not a single population.
The buffer exceeds L1, but L1 still holds ~128 KB of the chain, so a fraction f
of hops are L1 hits at 4.005 cycles and the rest are L2 accesses at L:

```
measured = f * 4.005 + (1 - f) * L
```

With f *assumed* from the capacity ratio 128 KB / buffer:

| buffer | cycles/hop | f (capacity) | implied L |
|---|---:|---:|---:|
| 256 KB | 16.009 | 0.500 | 28.0 ← drifts |
| 512 KB | 21.077 | 0.250 | 26.8 |
| 1 MB | 23.180 | 0.125 | 25.9 |
| 2 MB | 24.020 | 0.0625 | 25.4 |

Three independent sizes agree within 5% → **L2 latency ≈ 26 cycles**, and the
climb is not a property of the L2 at all; it is the L1-resident fraction decaying
as the working set grows. The 256 KB point drifts because at only 2× L1 capacity
the uniform-spread assumption is weakest (associativity effects dominate at low
capacity multiples — the same physics as the Day 7 conflict-miss finding).

This stayed PARTIAL because every f above was *calculated*, not measured.

### Day 8 close-out — measured L1 residency, L2 latency 25.3 cycles

Same chase with `L1D_CACHE_MISS_LD_NONSPEC` (event 191) counted alongside
cycles, so the L1-resident fraction is **measured** rather than assumed:
`f_measured = 1 − (L1 load misses / 10,000,000 hops)`.
`results/2026-09-16/l1_residency.txt`.

| buffer | cycles/hop | L1 misses | f_measured | f_capacity | ratio |
|---|---:|---:|---:|---:|---:|
| 32 KB | 4.003 | 14 | ~1.0 | 1.0 | — |
| 256 KB | 15.934 | 5,608,951 | 0.4391 | 0.500 | 0.878 |
| 512 KB | 20.865 | 7,926,763 | 0.2073 | 0.250 | 0.829 |
| 1 MB | 23.022 | 9,005,199 | 0.0995 | 0.125 | 0.796 |
| 2 MB | 24.210 | 9,503,597 | 0.0496 | 0.0625 | 0.794 |
| 64 MB | 226.178 | 9,983,029 | 0.0017 | ~0 | — |

*(The 2 MB row is from the first sweep of the day; that size failed its counter
arm on the second sweep. 512 KB has now been measured in four separate sessions
at 7,929,204 / 7,928,558 / 7,927,998 / 7,926,763 — agreeing to 0.03%.)*

**FINDING: effective L1 residency is ~79% of nominal capacity** for a
random-permutation walk, converging from above. The 256 KB point sits highest at
0.878, consistent with associativity effects dominating at low capacity
multiples. **Effective capacity for this access pattern is ~102 KB against
128 KB nominal.**

**FINDING: L2 latency is 25.3 cycles.** Re-solving
`measured = f*4.005 + (1−f)*L` with MEASURED f:

| buffer | f_measured | implied L |
|---|---:|---:|
| 512 KB | 0.2073 | 25.30 |
| 1 MB | 0.0995 | 25.28 |
| 2 MB | 0.0496 | 25.36 |

**Agreement to 0.3%, against 5% when f was calculated from capacity.** Using
measured residency instead of assumed residency tightened the estimate by more
than an order of magnitude — the strongest evidence that the two-population
model is correct.

### Day 8 note on DRAM latency [still OPEN]

The Day 2 sweep bottoms out at 158.128 ns/hop at 64 MB against an expected
90–100 ns, and the 32/64 MB rows carry the sweep's largest IQRs (46.4 and 15.6),
so part of the gap may be measurement quality rather than the machine. Direct
cycle measurement at 64 MB gives **338.519 cycles/hop = 84.2 ns at 4.021 GHz**,
inside the expected range — suggesting the same cold-clock artifact as the L1
case. **NOT resolved:** the gap is larger than the L1 case, and the original
probe should be rerun under counters before this closes.

Nothing downstream depends on the absolute number: the Day 2 conclusions rest on
the ratio L1:L2:DRAM = 1:4.0:103, which is frequency- and
methodology-independent and lands inside the published 1:3–4:50–100 range.

### INSTRUMENT problem — the PMU arm fails intermittently

Roughly **one attempt in three** fails. When it fails, all configurable counters
read exactly 0 while the fixed counters (cycles, instructions) continue
normally. It is random, not correlated with buffer size: 32 KB needed 3 retries
in one sweep while 64 MB succeeded on attempt 8.

Guarded by `counters_self_test()`, which aborts rather than reporting zeros, and
by `scripts/run_counters.sh`, which retries up to 10 times. Root cause not
identified — most likely another process reclaiming the system-wide counter bank
after `kpc_force_all_ctrs_set`. Scoped out.

---

## Day 9 — MPKI and TLB counters on SGEMM

Goal: use counters to say directly which effect tracks the Day 7 open question.
Predictions were written and committed **before** any measurement
(`d88f049`), then scored.

### 9.0 Method

- Binary `src/sgemm_counters_main.cpp`, run through `scripts/run_counters.sh`.
- n = 1024, seed 12345. A and B filled once. The padded copies hold the same
  values in an n×1040 buffer with the padding zeroed.
- Kernels: `sgemm_naive` (i, j, k order, stride 1024) and
  `sgemm_blocked_padded` with T = 32 and row stride ld = 1040.
- Each kernel gets one untimed warmup call first. Counters are read immediately
  before and after the **second** call only. Setup, verification and CSV writing
  all happen outside the bracketed calls.
- Events: FIXED_CYCLES, FIXED_INSTRUCTIONS, L1D_CACHE_MISS_LD_NONSPEC (191),
  L1D_TLB_MISS_NONSPEC (193), L2_TLB_MISS_DATA (1035), L1D_TLB_ACCESS (1440).
- Validity gates, **all passed in both runs**: PMU self-test passed before
  measuring; counters still forced at the end; outputs matched with max abs
  error = 0 (tolerance 1e-3); checksums matched at −25486.462534 for both
  kernels.

**Provenance of each run.** Run 1: the binary wrote the CSV itself. Run 2: the
binary exited 5 because the CSV already existed, so the rows were copied from
its stdout in the log (they match character for character). **Lost
measurements:** in the same wrapper invocation, attempts 1 and 3 also exited 5.
The binary only returns 5 *after* the self-test, verification, forced-counter
and bracketing checks have all passed, so those two attempts were also complete
measurements — but the wrapper, before that day's fix, printed only the final
attempt's output, so their values cannot be recovered. **Two runs are recorded
here; at least four were taken.**

### 9.1 The predictions (written first)

Machine facts used, all from earlier days: L1d 128 KiB, 128 B lines, 8-way, 128
sets (Days 2/4) · page 16 KiB · L1 dTLB 160 entries, ~2.5 MB (Day 5) · second
TLB level holds at least ~768 pages (Day 5 plateau) · L2 16 MiB. Consequence of
128 sets × 128 B = 16 KiB = page size: **the L1d set index is the line's offset
within its page.** Nothing was checked against disassembly or a counter;
instructions per iteration are estimates.

**Q1 — naive MPKI: ~150.**
- **B:** stride 4096 B = 32 lines, so each access is a new line. 4096 B inside a
  16 KiB page gives only 4 distinct page offsets, so a column's 1024 lines map
  to 4 sets × 8 ways = 32 slots — the Day 7 conflict model at its worst.
  **~1.0 miss/access.**
- **A:** sequential along row i, 32 floats per line, reused for all 1024 j, so
  it stays hot apart from the one set B thrashes. **~1/32 = 0.03/iteration.**
- **C:** one access per j, **~0.001/iteration, negligible.**
- Misses/iteration ≈ 1.03. Instructions/iteration ≈ 7 (two indexed loads, fmadd
  from default fp-contract, two induction updates, compare, branch; strict-order
  summation blocks vectorization without `-ffast-math`).
- MPKI = 1.03 / 7 × 1000 ≈ **147**, call it 150.
- Stated failure mode: a stride prefetcher catching the 4096 B stride could save
  at most ~3 of 4 misses, putting MPKI well below 150.

**Q2 — padded blocked MPKI (T=32): ~0.33.** Row stride 1040 floats = 4160 B, so
page offsets cycle through 256 values instead of 4 and tile lines spread across
all 128 sets. Per tile (32³ = 32768 iterations): A ≈ 48 lines (rows start at
offset 0 or 64 since 4160 mod 128 = 64, so ~1.5 lines/row), B ≈ 48 lines, C ≈ 48
lines but reloaded once every 32 tiles. Revisits: a tile's A and B lines return
only after a full kk sweep, which touches ~130 KB + ~190 KB > 128 KB, so every
revisit reloads. Misses ≈ 32768 × 96 + 1024 × 48 ≈ **3.2 M**, ≈ 0.003/iteration
(≈1024 uses per line). Instructions/iteration ≈ 9 (naive plus a load and store
of C, since C cannot be proven not to alias A and B). MPKI ≈ **0.33**.

**Q3 — MPKI ratio ~450×** (150 / 0.33), two orders of magnitude more than the
2.69× runtime speedup. What a large miss drop with a small speedup means (the
argument is identical at 20× or 450×): runtime is not proportional to L1d miss
count, because (a) **misses are independent** — B addresses come from the
induction variable, not from loaded data, so unlike the Day 8 pointer chase OoO
execution keeps many misses in flight and they do not pay 26 cycles in series;
(b) **misses land in L2, not DRAM** — the 12 MB working set is inside the 16 MiB
L2; (c) **something else sets the pace** — the carried FP accumulator limits
throughput whether or not misses happen. Derived Amdahl bound: if blocking
removed essentially all miss cost and bought 2.69×, memory stalls were at most
1 − 1/2.69 ≈ **63%** of naive runtime. Rough per-miss cost from Day 7 GFLOP/s
and the 4.02 GHz warm frequency (so it carries the frequency caveat): naive ~5.4
cycles/iteration, blocked ~2.0, saving ~3.4 cycles for ~1 miss avoided →
**~3–4 cycles per miss**, far below the 26-cycle serial L2 latency.

**Q4 — dTLB.** Each matrix is 4 MB = 256 pages (padded ≈ 260); all three ≈
768–780 pages. *Naive:* each j walks B down a whole column = all 256 pages, 4
iterations per page, plus 1–2 for A and C → ~259 pages per sweep against 160 L1
entries, so every B page is evicted before it returns. L1D_TLB_MISS_NONSPEC
**~2.7e8 total, ~36 per 1000 instructions** (0.25/iteration);
L2_TLB_MISS_DATA **~0** since the active set (~260) and whole run (~768) both
fit the second level. *Blocked T=32 padded:* ~8 pages each for A, B, C ≈ 25
pages per tile, fitting 160 easily and serving 32768 iterations; A and C pages
stay fixed for a whole ii band, B changes ~8 pages per kk step and a jj sweep
covers all ~260 B pages (>160) so those ~8 miss again → **~2.6e5 total, ~0.03
per 1000 instructions, ~1000× below naive**; L2_TLB_MISS_DATA **~0**, with the
one risk that padding brings the total to ~780 pages, just over 768. Main
uncertainty for both: Day 5 never pinned the second level's exact size, only
that it is ≥ ~768 pages.

### 9.2 Measured — raw counter deltas

Copied straight from the two CSVs; nothing here is rounded or computed. All four
rows: n = 1024, seed = 12345, verified = 1, max_abs_err = 0.

| kernel | run | cycles | instructions | l1d_cache_miss_ld_nonspec | l1d_tlb_miss_nonspec | l2_tlb_miss_data | l1d_tlb_access |
|---|---|---:|---:|---:|---:|---:|---:|
| naive | 1 | 3926644952 | 7543223992 | 1085255539 | 570300068 | 65436 | 3153101446 |
| naive | 2 | 3753081056 | 7527141437 | 1087336294 | 577960666 | 3119244 | 3158082453 |
| padded_blocked (T=32, ld=1040) | 1 | 1405335597 | 9056893736 | 21664124 | 894144 | 1838 | 3272368931 |
| padded_blocked (T=32, ld=1040) | 2 | 1406371723 | 9056417913 | 21617759 | 876418 | 1029 | 3272466708 |

### 9.3 Derived values

All computed from the table above. "Misses" means `l1d_cache_miss_ld_nonspec`
unless stated. "Iterations" means n³ = 1024³ = 1,073,741,824 inner-loop
iterations; both kernels execute that many.

**L1d MPKI** = `misses / instructions * 1000`

| kernel | run 1 | run 2 |
|---|---:|---:|
| naive | 143.87 | 144.46 |
| padded_blocked | 2.392 | 2.387 |

**MPKI ratio** = `MPKI_naive / MPKI_padded`: **60.15×** (run 1), **60.52×** (run 2)

**Cycle speedup** = `cycles_naive / cycles_padded`: **2.794×** (run 1),
**2.669×** (run 2)

**Cycles per avoided miss** =
`(cycles_naive − cycles_padded) / (misses_naive − misses_padded)`

| run | cycles saved | misses avoided | cycles per avoided miss |
|---|---:|---:|---:|
| 1 | 2,521,309,355 | 1,063,591,415 | 2.371 |
| 2 | 2,346,709,333 | 1,065,718,535 | 2.202 |

*This formula attributes the entire cycle difference to avoided misses. The two
kernels also differ in other ways — padded runs ~20% more instructions — so it
is not a clean per-miss cost and not a bound in either direction.*

**Other derived quantities**

| quantity | formula | naive r1 | naive r2 | padded r1 | padded r2 |
|---|---|---:|---:|---:|---:|
| instructions per iteration | `instructions / 1024³` | 7.025 | 7.010 | 8.435 | 8.434 |
| cycles per iteration | `cycles / 1024³` | 3.657 | 3.495 | 1.309 | 1.310 |
| L1d misses per iteration | `misses / 1024³` | 1.0107 | 1.0127 | 0.02018 | 0.02013 |
| L1 dTLB misses per 1000 instr | `l1d_tlb_miss_nonspec / instructions * 1000` | 75.60 | 76.78 | 0.0987 | 0.0968 |
| L1 dTLB misses per iteration | `l1d_tlb_miss_nonspec / 1024³` | 0.5311 | 0.5383 | 0.000833 | 0.000816 |
| L2 TLB misses per 1000 instr (**UNRELIABLE**) | `l2_tlb_miss_data / instructions * 1000` | 0.00868 | 0.4144 | 0.000203 | 0.000114 |
| L1 dTLB miss ratio naive / padded | ratio of the two counters | 637.8× | 659.5× | — | — |

### 9.4 Run-to-run agreement

`spread = |run2 − run1| / mean × 100`

| counter | naive | padded_blocked |
|---|---:|---:|
| cycles | 4.52% | 0.07% |
| instructions | 0.21% | 0.005% |
| l1d_cache_miss_ld_nonspec | 0.19% | 0.21% |
| l1d_tlb_miss_nonspec | 1.33% | 2.00% |
| l2_tlb_miss_data | **191.8%** (run 2 = 47.7× run 1) | **56.4%** (run 2 = 0.56× run 1) |
| l1d_tlb_access | 0.16% | 0.003% |

- **Stable:** instructions, L1d misses, L1 dTLB misses and dTLB accesses agree
  within ~2% in both kernels; padded cycles within 0.07%.
- **Less stable:** naive cycles differ 4.5% between runs. That is the only
  reason the speedup moves from 2.794× to 2.669×.
- **UNRELIABLE — `l2_tlb_miss_data`.** Naive gives 65,436 and 3,119,244, a 47.7×
  spread, while every other counter in those same runs agrees to under 5%.
  Padded differs by 56.4% while every other padded counter agrees within 2%.
  With only two runs there is no way to tell whether this is real (a count
  depending on page/TLB state that differs between runs) or a counter problem.
  **No conclusion should be drawn from either value, for either kernel.** It
  appears here only as a raw measurement.

### 9.5 Predictions vs. measurement

**Q1 — naive MPKI (predicted ~150)**

| item | predicted | measured | verdict |
|---|---:|---:|---|
| MPKI | ~150 (computed 147) | 143.87 / 144.46 | **Held** — 4.1% / 3.7% below 150, within 2.2% of the unrounded 147 |
| misses per iteration | ~1.03 | 1.0107 / 1.0127 | **Held**, within 2% |
| instructions per iteration | ~7 | 7.025 / 7.010 | **Held**, within 0.4% |

The stated failure mode — a stride prefetcher saving up to ~3 of 4 misses — did
not happen to any measurable degree: misses per iteration are still ≥ 1.0.

**Q2 — padded blocked MPKI, T=32 (predicted ~0.33)**

| item | predicted | measured | verdict |
|---|---:|---:|---|
| MPKI | ~0.33 | 2.392 / 2.387 | **Wrong** — 7.25× / 7.23× the prediction |
| total L1d misses | ≈3.2 M (32768×96 + 1024×48 = 3,194,880) | 21,664,124 / 21,617,759 | **Wrong** — 6.78× / 6.77× |
| misses per iteration | ~0.003 | 0.02018 / 0.02013 | **Wrong** — ~6.7× |
| instructions per iteration | ~9 | 8.435 / 8.434 | **Held roughly** — 6.3% below |

The prediction rested on each loaded L1d line serving ~1024 accesses before
eviction. Measured miss count is ~6.8× the prediction while instructions per
iteration came in close, so each miss is followed by far fewer accesses than
1024. If the number of memory accesses is as assumed, the implied figure is
1024 × 3,194,880 / measured ≈ **151 accesses per miss** (both runs), not ~1024.
That is derived and depends on the access-count assumption.

> **The miss model for the padded blocked kernel is wrong by a factor of ~7, and
> the cause is not yet explained.** Nothing measured on Day 9 identifies which
> part of the per-tile model (line counts, the revisit argument, the C reload
> rate, or something left out) accounts for the gap.

**Q3 — MPKI ratio and its relation to the 2.69× speedup**

| item | predicted | measured | verdict |
|---|---:|---:|---|
| MPKI ratio | ~450× | 60.15× / 60.52× | **Wrong** — ~7.5× smaller. Follows directly from Q2: naive MPKI held, so the whole error is in the denominator |
| "miss drop far exceeds the speedup" | ratio ≫ speedup | 60× vs 2.79× / 2.67× | **Held** — still >20× the cycle speedup; the note said the argument works the same at 20× or 450× |
| speedup | 2.69× (Day 7 wall time, used as given) | 2.794× / 2.669× cycles | **Consistent** — the Day 7 figure falls between the two runs (+3.9% / −0.8%) |
| naive cycles per iteration | ~5.4 | 3.657 / 3.495 | **Wrong** — 32% / 35% below |
| blocked cycles per iteration | ~2.0 | 1.309 / 1.310 | **Wrong** — 35% below |
| cycles per avoided miss | ~3–4 | 2.371 / 2.202 | **Wrong on the number, right on the conclusion** — 21% / 27% under the low end, but still far below the 26-cycle serial L2 latency, which was the point |
| Amdahl bound: memory stalls ≤ 63% | ≤ 63% | not measured | **Not tested** — no Day 9 counter measures stall time |

The cycles-per-iteration predictions were built from Day 7 GFLOP/s and an
assumed 4.02 GHz, and the note flagged that assumption itself. Measured values
are ~⅓ lower than both. Day 9 does not show whether the frequency assumption is
the reason.

**Q4 — dTLB behaviour, naive**

| item | predicted | measured | verdict |
|---|---:|---:|---|
| L1D_TLB_MISS_NONSPEC total | ~2.7e8 | 570,300,068 / 577,960,666 | **Wrong by ~2×** (2.11× / 2.14×) |
| per 1000 instructions | ~36 | 75.60 / 76.78 | **Wrong by ~2×** (2.10× / 2.13×) |
| per iteration | 0.25 | 0.5311 / 0.5383 | **Wrong by ~2×** — the "heavily" part held, the magnitude did not |
| L2_TLB_MISS_DATA (**UNRELIABLE**) | ~0, at most low thousands | 65,436 / 3,119,244 | **Cannot be assessed** — both above "low thousands", but the counter varies 47.7× between runs. Not scored |

**Q4 — dTLB behaviour, padded blocked T=32**

| item | predicted | measured | verdict |
|---|---:|---:|---|
| L1D_TLB_MISS_NONSPEC total | ~2.6e5 | 894,144 / 876,418 | **Wrong** — 3.44× / 3.37× |
| per 1000 instructions | ~0.03 | 0.0987 / 0.0968 | **Wrong** — 3.29× / 3.23× |
| naive / blocked dTLB miss ratio | ~1000× | 637.8× / 659.5× | **Partly held** — same order of magnitude, 36% / 34% below |
| L2_TLB_MISS_DATA (**UNRELIABLE**) | ~0; small nonzero possible if the second level is exactly 768 entries | 1838 / 1029 | **Cannot be assessed.** The 768-entry explanation was not tested |

`l1d_tlb_access` was measured but no prediction covered it.

### 9.6 Day 9 summary

- **Held:** naive MPKI (~150 → 143.87 / 144.46), naive instructions per
  iteration, and the qualitative claim that the miss drop far exceeds the
  speedup.
- **Wrong by ~7×:** padded blocked MPKI (0.33 → 2.39), and therefore the MPKI
  ratio (450× → 60×). Cause not yet explained.
- **Wrong by ~2–3.5×:** L1 dTLB miss counts for both kernels.
- **Cannot be assessed:** both L2_TLB_MISS_DATA predictions — the counter is
  UNRELIABLE (47.7× spread across runs that otherwise agree to under 5%).
- **Wrong by ~⅓:** cycles per iteration for both kernels; cycles per avoided
  miss came in below the predicted 3–4 range.
- **Not tested:** the 63% Amdahl bound, and the 768-entry second-level TLB
  explanation.

---

## Day 10 — not yet run

No Day 10 measurement exists in the repo as of 2026-09-18; the last commit is
the Day 9 counter driver (`2eec2f6`). This section is the queue, in the order the
evidence argues for. Nothing here is a result.

1. **Explain the 7× padded-blocked miss gap** (the largest open item). The
   implied ~151 accesses per line against a predicted ~1024 is the number to
   attack. Candidates the per-tile model may have gotten wrong: line counts per
   tile, the revisit argument, the C reload rate, or an effect left out
   entirely. The Day 8 finding that effective L1 residency is only ~79% of
   nominal (≈102 KB) is a ready-made candidate and was not in the Day 9 model.
2. **Repeat `L2_TLB_MISS_DATA` enough times to decide whether it is real.** Two
   runs cannot separate genuine page-state dependence from a counter problem.
   Until that is settled, no Q4 second-level conclusion can close, and the
   768-entry hypothesis stays untested.
3. **Counter the padded tile sweep across T** (32 / 48 / 64 / 96) to settle the
   Day 7 OPEN entry: which counter actually tracks the decline above T=32, when
   capacity does not account for it.
4. **Measure the NEON ceiling.** It is the only one of the three ceilings never
   measured, and it is the one the current scalar kernels are really working
   against.
5. **Rerun the original DRAM probe under counters** to close the 158 ns vs
   84.2 ns entry.
6. **Resolve the TLB knee at 144–160 pages** vs the published 128 entries (see
   Appendix B for the four candidate causes and their tests).
7. **Colab T4 session** to fill in the GPU half of the machine table.

---

## Appendix A — measured machine model

Everything in this table was measured on this machine by the probes above, not
read from documentation.

| Property | Value | Measured on |
|---|---|---|
| L1d capacity | 128 KB | Day 2 (capacity cliff) |
| L1d line size | 128 B | Day 4 (structure check) |
| L1d ways | 8 | Day 4 (conflict sweep) |
| L1d sets | 128 | Day 4 (derived: 128 KB / 8 / 128 B) |
| L1d hit latency | 4.0 cycles | Day 8 (direct cycle count) |
| Effective L1d residency (random walk) | ~79% of nominal ≈ 102 KB | Day 8 close-out |
| L2 capacity | 16 MB (P-cluster, shared) | Day 2 |
| L2 hit latency | 25.3 cycles | Day 8 close-out (measured f) |
| DRAM latency | 338.5 cycles = 84.2 ns | Day 8 (Day 2's 158 ns was cold-clock) |
| Latency ratio L1:L2:DRAM | 1 : 4.0 : 103 | Day 2 (frequency-independent) |
| Page size | 16384 B | `hw.pagesize` + Day 5 |
| L1 dTLB entries | ~160 (knee 144–192 pages) | Day 5 |
| L1 dTLB reach | ~2.4–2.5 MB | Day 5 |
| Second-level TLB | ≥ ~768 pages | Day 5 (second plateau) |
| Warm P-core frequency | 4.02 GHz | Day 8 |
| Cold/idle P-core frequency | ~2.61 GHz | Day 8 run 1 |
| Peak bandwidth, L1 (SAXPY) | ~150–157 GB/s | Day 3 (warm) |
| Bandwidth, L2 plateau | ~90–96 GB/s | Day 3 |
| Bandwidth, DRAM steady state | ~78–80 GB/s | Day 3 |
| Scalar SGEMM ceiling reached | 3.992 GFLOP/s (T=32 padded, n=1024) | Day 7 |
| Naive SGEMM baseline, n=1024 | 1.484–1.486 GFLOP/s | Days 6/7 |
| AMX-via-Accelerate SGEMM | ~1120 GFLOP/s, single-threaded | Day 7b |

Two derived identities that keep coming up:

- **128 sets × 128 B = 16 KiB = page size** → the L1d set index *is* the line's
  offset within its page. This is why a power-of-two row stride is so
  destructive (Days 7 and 9).
- **A T×T tile is not a contiguous block.** It is T rows of `T*4` bytes spaced
  `stride*4` bytes apart, so the cache sees the stride, not the area.

---

## Appendix B — open questions

| # | Question | Status | Next step |
|---|---|---|---|
| 1 | Padded blocked SGEMM misses are 7× the model (151 accesses/line vs ~1024 predicted) | **OPEN**, Day 9 | Test the per-tile model piece by piece; the ~79% effective residency from Day 8 is an unmodelled candidate |
| 2 | Padded SGEMM declines above T=32 while tiles still fit L1d (−15% at 27 KiB, −36% at 108 KiB) | **OPEN**, Day 7 | Counter the padded sweep across T; deliberately no hypothesis recorded |
| 3 | `L2_TLB_MISS_DATA` varies 47.7× between runs | **OPEN / instrument** | Many repeats to separate real page-state dependence from counter fault |
| 4 | Is the Accelerate mechanism AMX, and what is the real NEON ceiling? | **OPEN**, Day 7b | Hand-vectorised NEON SGEMM; inspect the instruction stream |
| 5 | Measured TLB knee (flat to 144 pages, jump at 192) sits above the published 128 entries | **OPEN**, Day 5 | Four untested candidates, below |
| 6 | DRAM latency 158 ns (Day 2) vs 84.2 ns (Day 8) | **OPEN** | Rerun the original probe under counters |
| 7 | Exact size of the second-level TLB | **OPEN**, Day 5 | Only bounded below (≥ ~768 pages); blocks the 768-entry explanation in Q4 |
| 8 | PMU arm fails ~1 in 3 attempts, configurable counters silently read 0 | **Mitigated, root cause unknown** | Guarded by self-test + 10 retries; scoped out |
| 9 | L2 plateau climb | **RESOLVED** Day 8 | It is L1 residency decay; L2 latency is 25.3 cycles |

**Q5 in detail — four candidates for the TLB knee, none tested:**

1. **Offset scheme.** The slot offset steps one cache line and wraps every 128
   pages (`(n * 128) % 16384`). The knee may be interacting with that period.
   *Test: rerun with a 256-byte step and see whether the knee moves.*
2. **TLB set associativity.** Page numbers 0..N−1 are consecutive integers, so
   they distribute perfectly uniformly across TLB sets — best case. A uniform
   pattern may achieve full nominal capacity where real workloads do not.
3. **The published figure may be M2 Pro, not M3 Pro.** The commonly cited source
   (Chips and Cheese) measured M2.
4. **L2 TLB bleed-through.** If L2 TLB hits are cheap enough, early L1 dTLB
   misses stay invisible until the miss fraction grows.

Also unexplained there: several tail values repeat to three decimals (3.823
three times, 4.168 twice), suggesting quantization. The TLB probe prints median
only — **add IQR.**

---

## Appendix C — prediction scorecard

Every prediction written down before its measurement, and how it scored.

| Day written | Prediction | Outcome |
|---|---|---|
| 1 | High IQR flags contamination | **Held** — run 2 caught at 271 µs IQR |
| 2 (pre-measure) | L1d = 128 KB, L2 = 16 MB | **Held** — both matched |
| 2 (pre-measure) | P-core would stay flat through 128 KB, E-core would break at 64 KB | **Held** — flat through 128 KB, twice |
| 2 | L1:L2:DRAM in the published 1:3–4:50–100 range | **Held** — 1:4.0:103 |
| 2 | Best SGEMM tile = 64×64, from a pure capacity argument | **REFUTED** Day 7 — measured peak T=8–16 unpadded; 64 gave 1.579 vs 3.922. Incomplete, not wrong: it named a real constraint but modelled the weaker of two |
| 4 (pre-measure) | L1d 8-way | **Held** — clean K=8/K=9 boundary |
| 5 | Published 128-entry L1 dTLB | **Partly** — consistent with the ~2.4 MB reach, but flat to 144 pages; unresolved |
| 6 | Naive SGEMM would show a cache cliff | **REFUTED** — smooth 12–15%/doubling; no spatial locality on B to lose at any tested n |
| 7 | Conflict misses, not capacity, bind on a power-of-two stride | **CONFIRMED** — padding at fixed T=64 gives +100% at n=1024 vs +12% at n=512, and the two converge to 0.3% |
| 7b | Accelerate's 1120 GFLOP/s must be a measurement bug | **Cleared** — survived an independent harness and a 1:1 CPU-to-wall check |
| 8 | L1 6.2-cycle anomaly is real | **REFUTED** — it was 4.0 cycles; the *frequency* was wrong, not the nanoseconds |
| 8 | Two-population model, f from capacity → L2 ≈ 26 cycles | **Confirmed and tightened** — measured f gives 25.3 cycles with 0.3% agreement |
| 9 | Naive MPKI ~150 | **Held** — 143.87 / 144.46 |
| 9 | Padded blocked MPKI ~0.33 | **WRONG ~7×** — 2.39. Unexplained |
| 9 | MPKI ratio ~450× | **WRONG** — 60×, entirely from the Q2 denominator |
| 9 | Miss drop ≫ runtime speedup | **Held** — 60× vs 2.7× |
| 9 | L1 dTLB misses, both kernels | **WRONG 2–3.5×** in magnitude; direction right |
| 9 | L2_TLB_MISS_DATA ≈ 0 | **Unassessable** — counter unreliable |

Pattern worth noting: **direction has been reliable, magnitude has not.** Every
refutation so far came from a model that was arithmetically fine but omitted a
constraint — capacity without conflicts (Day 2), locality without stride
(Day 6), frequency assumed rather than measured (Days 3/8), residency assumed
rather than measured (Days 8/9).

---

## Appendix D — methodology lessons

Each of these cost a wrong number to learn.

1. **A benchmark that measures nothing reports an impossibly good number.**
   Sanity-check against known physical floors. (`-O3` deleted the chase loop;
   0.38 ns/hop was below L1 hit latency.)
2. **`volatile` alone does not defeat the optimizer.** Use a per-iteration
   compiler barrier, `asm volatile("" : "+r"(p))` — it emits no instructions and
   only prevents proving the value unused.
3. **Warmup reps warm the CACHE, not the CLOCK.** Different timescales: cache
   warmup takes microseconds, the DVFS ramp takes milliseconds.
4. **Never divide nanoseconds by an assumed frequency to get cycles.** The error
   is invisible without a cycle counter. Treat every pre-Day-8 ns-derived cycle
   figure as suspect unless the run was demonstrably warm.
5. **Measure the fraction, don't assume it.** Replacing an assumed L1-resident
   fraction with a measured one tightened the L2 latency estimate from 5%
   agreement to 0.3%.
6. **Any measured latency faster than the fastest known cache level is a
   methodology bug, not a discovery.** (K=1 load-forwarding.)
7. **Degenerate configurations must be labelled, not silently averaged.** Start
   the associativity sweep at K=2 or print K=1 as "degenerate".
8. **A silent instrument failure is worse than a loud one.** The PMU arm fails
   ~1 in 3 and returns exact zeros; `counters_self_test()` aborts instead of
   reporting them.
9. **Keep failed attempts.** Day 9 lost two complete measurements because the
   wrapper printed only the final attempt's output.
10. **Compare like with like.** Accelerate is a matrix coprocessor, not a
    scalar-pipeline ceiling; quoting the ratio as a speedup target would be
    dishonest.
11. **Write predictions down before measuring, with their failure mode.** The
    Day 9 note stated what a prefetcher would do to the number; that made the
    result interpretable instead of merely surprising.
12. **When a hypothesis would be a guess, record no hypothesis.** The Day 7
    T>32 decline is deliberately left without one — guessing is what produced
    the Day 2 error.

---

## Appendix E — data files

| Path | Contents |
|---|---|
| `results/sgemm_naive.csv` | Day 6 naive SGEMM GFLOP/s vs n |
| `results/assoc_m3pro.csv`, `.png` | Day 4 associativity sweep and plot |
| `results/2026-09-10/sgemm_tile_sweep.csv` | Day 7 unpadded tile sweep |
| `results/2026-09-10/sgemm_tile_sweep_padded.csv` | Day 7 padded tile sweep (ld = n+16) |
| `results/2026-09-10/sgemm_blas.csv` | Day 7b Accelerate ceiling, both thread configs |
| `results/2026-09-15/counter_validation.txt` | Day 8 PMU validation, DVFS ramp, 4.02 GHz |
| `results/2026-09-15/latency_cycles.txt` | Day 8 latency in cycles; L1 = 4.0, L2 ≈ 26, DRAM = 338.5 |
| `results/2026-09-15/miss_sweep_raw.txt` | Day 8 raw miss sweep log |
| `results/2026-09-16/l1_residency.txt` | Day 8 close-out: measured residency, L2 = 25.3 cycles |
| `results/2026-09-16/miss_sweep_raw.txt`, `_v2.txt` | Day 8 close-out raw logs, incl. PMU arm retries |
| `results/2026-09-16/day9_mpki.md` | Day 9 writeup (merged into this file) |
| `results/2026-09-16/day9_sgemm_counters_run1.csv`, `run2.csv` | Day 9 raw counter deltas |
| `results/2026-09-16/day9_sgemm_counters_log.txt` | Day 9 full run log, incl. the lost attempts |
| `scripts/run_counters.sh` | Counter runner with self-test gate and up to 10 retries |
| `scripts/miss_sweep.sh` | Miss-sweep driver |
| `scripts/plot_assoc.py`, `plot_tlb.py` | Plotting |
