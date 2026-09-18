# Surprises

Anomalies, wrong turns, and things the measurements did that I did not expect.
Status tags: OPEN · PARTIAL · RESOLVED

---

## L2 plateau climbs 3x instead of staying flat   [PARTIAL]

Day-2 chase: 6.09 ns at 256 KB rising to 19.76 ns at 16 MB. Expected a flat
plateau at L2 hit latency across the whole L2 range.

Day 5 measured TLB reach at ~2.4 MB. **This explains the steep part** — from
2 MB to 16 MB latency doubles (9.31 -> 19.76), and both measured TLB knees fall
in that range.

**It does not explain the early part.** At 512 KB latency is already 7.96 ns,
+31% over the 256 KB baseline — and 512 KB is only 32 pages, well inside TLB
reach. By 2 MB (128 pages, still inside) it is up 53%.

Remaining candidates for 256 KB - 2 MB:
- L2 is shared across the P-core cluster; effective capacity is contended
- L2 access latency may vary with footprint (physically distributed banks)

Isolating experiment (not yet run): build a chain over an 8 MB buffer but
restrict the chase to slots within ~32 pages. Same footprint, TLB pressure held
near zero. If latency drops to L2-hit level, TLB explains it; if it stays at
~13 ns, it does not.

### Day 8 update: two-population model   [still PARTIAL]

Latency re-measured in cycles across the climb:

    buffer     cycles/hop     f = 128 KB / buffer
    256 KB        16.009            0.500
    512 KB        21.077            0.250
    1 MB          23.180            0.125
    2 MB          24.020            0.0625

**TLB reach is ruled out by arithmetic.** At 16 KB pages, 2 MB is 128 pages
against the 160-entry L1 dTLB measured on Day 5. Every size in the climb
fits inside TLB reach, so misses cannot occur there. The portion of this
entry attributing the climb to TLB pressure applies only above 2.5 MB.

**The climb decelerates**, each doubling adding roughly half what the
previous one added: +5.07, +2.10, +0.84. A curve converging on an
asymptote, not a step.

**Hypothesis.** The measurement is a blend, not a single population. The
buffer exceeds L1, but L1 still holds 128 KB of the chain, so a fraction f
of hops are L1 hits at 4.005 cycles and the rest are L2 accesses at L:

    measured = f * 4.005 + (1 - f) * L

Solving for L at each size:

    512 KB    f = 0.250      L = 26.8
    1 MB      f = 0.125      L = 25.9
    2 MB      f = 0.0625     L = 25.4

Three independent sizes agree within 5%.

**Implication: L2 latency on M3 Pro is approximately 26 cycles.** The climb
is not a property of the L2. It is the L1-resident fraction decaying as the
working set grows.

**The point that does not fit.** 256 KB gives f = 0.5 and implies L = 28.0,
drifting from the other three. At only 2x L1 capacity the uniform-spread
assumption is weakest, since associativity effects dominate at low capacity
multiples. See the Day 7 conflict-miss entry for the same physics in a
different experiment.

**Why this stays PARTIAL.** Every f above is calculated from the capacity
ratio, not measured. The model assumes the chain spreads uniformly across
cache sets. Confirmation requires L1D_CACHE_MISS_LD_NONSPEC (event 191) to
measure the actual L1 hit fraction at each size. If the measured fractions
land near 0.25 / 0.125 / 0.0625, this closes. If not, the climb needs a
different explanation.

Data: results/2026-09-15/latency_cycles.txt

---

## Measured TLB knee sits above the published 128 entries   [OPEN]

Published figures for Apple Silicon report a 128-entry L1 dTLB. My sweep is flat
to three decimals (1.529 ns) through **144** pages, first rise at 160, jump at
192.

Four candidates, none tested:
1. **Offset scheme.** Slot offset steps one cache line and wraps every 128 pages
   (`(n * 128) % 16384`). The knee may be interacting with that period.
   Test: rerun with a 256-byte step and see whether the knee moves.
2. **TLB set associativity.** Page numbers 0..N-1 are consecutive integers, so
   they distribute perfectly uniformly across TLB sets — best case. A uniform
   pattern may achieve full nominal capacity where real workloads do not.
3. **Published figure may be M2 Pro, not M3 Pro.** The commonly cited source
   (Chips and Cheese) measured M2.
4. **L2 TLB bleed-through.** If L2 TLB hits are cheap enough, early L1 dTLB
   misses stay invisible until the miss fraction grows.

Also unexplained: several tail values repeat to three decimals (3.823 three
times, 4.168 twice), suggesting quantization. The TLB probe prints median only —
add IQR.

---

## -O3 deleted the entire TLB chase loop   [RESOLVED]

First TLB sweep reported 0.38-0.74 ns/hop at every page count — faster than the
measured 1.53 ns L1 hit latency, so physically impossible. That impossibility is
what flagged it.

Cause: the chase result was stored to a file-scope `static volatile void*` that
nothing ever read. The compiler could see the whole translation unit, proved the
store dead, and dead-coded the store plus the loop feeding it. objdump confirmed
the timed region reduced to

```
  mov  w8, #200001
loop: subs x8, x8, #1
      b.ne loop
```

with zero memory instructions. `volatile` alone was not sufficient.

Fix: per-iteration compiler barrier, `asm volatile("" : "+r"(p))`. Emits no
instructions; only prevents the optimizer proving `p` unused.

`src/probe.cpp` (day 2) does **not** have this problem — verified by
disassembly. Its chase lives in a separate translation unit with no LTO, so the
compiler never sees chain construction and traversal together; the dependency is
structural rather than barrier-enforced.

**Lesson: a benchmark that measures nothing reports an impossibly good number.
Sanity-check against known physical floors.**

---

## SAXPY small sizes read 2.5x low on a cold machine   [RESOLVED]

Reran the day-3 sweep after restoring the driver: 59-74 GB/s at small sizes
where day 3 recorded 148-157, while large sizes reproduced exactly. The curve
rose with size instead of falling — backwards.

Cause: **DVFS frequency ramp, not a code regression.** At n=1024 the entire
timed region is ~83 ns, so all 13 invocations (3 warmup + 10 measured) complete
in ~1 microsecond — far too short to pull the P-core off its idle frequency. The
clock ramps *during* the sweep, so early sizes are measured slow and later ones
at full clock. The curve tracks wall-clock time since the sweep started, not
buffer size.

Large sizes matched because they are DRAM-bandwidth-bound rather than
clock-bound — that tail was already at its ceiling.

Verified by rebuilding the unmodified day-3 source (4ede387) and running both
binaries cold and warm:

```
day-3 binary,   cold  ->  333 ns,  36.9 GB/s
current binary, cold  ->  125 ns,  98.3 GB/s
current binary, warm  ->   83 ns, 148.0 GB/s   (commit: 83 ns, 148.0)
```

The day-3 code reproduces the bad numbers when cold. The refactor is exonerated.
Also confirmed P-cores not E-cores: the capacity sweep stays flat through
128 KB, which is the falsification test written down on day 2.

**Lesson: warmup reps warm the CACHE, not the CLOCK. Different timescales —
cache warmup takes microseconds, DVFS ramp takes milliseconds.**

---

## SAXPY 12 MB dip (54.9 GB/s vs ~93 either side)   [RESOLVED]

Isolated dip at N=1048576, with neighbours at 95.6 and 93.1 and no cache
boundary nearby. Logged plan was "rerun once; if it does not reproduce, treat as
noise."

Did not reproduce on any warm run — warm gives ~68 GB/s, in line with
neighbours. Same DVFS family as the entry above; the original reading was taken
mid-ramp.

---

## K=1 measures 0.38 ns/hop, below L1 hit latency   [RESOLVED]

K=1 builds a single self-referencing entry (`chain[0] = 0`), so the chase is
`p = chain[0]` forever. Same address every hop: served by load-forwarding rather
than a real cache access, with a trivial dependency chain. 0.38 ns is ~1.5
cycles — loop overhead, not memory latency.

Not a data point. Excluded from analysis.

Same failure family as the `-O3` entry above: **any measured latency faster than
the fastest known cache level is a methodology bug, not a discovery.**

Action for the shipped tool: start the sweep at K=2, or print K=1 with an
explicit "degenerate" label.

---

## Associativity curve ramps rather than steps   [RESOLVED]

Expected a clean step from all-hits to all-misses at K=9. Instead the curve
climbs (3.5 -> 4.5 -> 5.2 -> 6.0 -> 7.2), plateaus ~7 ns for K=16-20, then
climbs again to ~11 ns by K=24.

At K=9 the set is only slightly over capacity, so depending on replacement
policy and access order some accesses still hit. The miss fraction grows with K
rather than flipping to 100% instantly.

Not a problem for the measurement — the K=8/K=9 boundary is still sharp and
unambiguous — but the degradation is smooth, not a step.

---

## Naive SGEMM declines smoothly with n, with no cache cliff   [RESOLVED]

*(Merged from the stray root-level `surprises.md`, Day 6.)*

Measured, single P-core, -O3, median of 10:

```
n      GFLOP/s
128    2.259
256    1.915
512    1.639
1024   1.484
```

Expected a plateau followed by a cliff once the working set left L1d. Got a
smooth ~12-15% decline per doubling instead.

Explanation: the B access is `B[k*n + j]`, stride `4n` bytes. At n=128 that is
already 512 bytes — four times the 128-byte line. So every k step touches a
fresh line at **every** size tested. The kernel has no spatial locality on B to
lose, so there is no cliff when it "stops fitting".

What changes with n is which level serves the miss, not whether one occurs. At
n=128, B is 64 KB and fits in the 128 KB L1d, so repeat passes hit L1. At
n=1024, B is 4 MB with a 12 MB working set, pressing on the 16 MB L2, so more
misses reach DRAM. Day 2 ratios L1:L2:DRAM = 1:4.0:103 — the blend shifts
gradually, hence the smooth slope.

Peak is ~7% of estimated single-core NEON FMA throughput (~32 GFLOP/s). This is
the motivation for the Day 7 blocking work.

(The n=1024 figure here is 1.484, from `results/2026-09-11/sgemm_naive.csv`; the Day 7
entries below quote 1.486 as the naive baseline, from a later rerun. Rerun
variance of 0.1%, not a discrepancy worth chasing.)

---

## Optimal SGEMM tile is T=8-16, not the predicted T=64-104   [RESOLVED]

Day-2 prediction, written before any SGEMM existed and committed in notes.md:
three float tiles of T x T must live in L1d at once, so `3 * T^2 * 4 <= 131072`
gives T <= 104, and the nearest useful power of two is **64x64**. The whole
argument is capacity — how much fits.

The Day-7 sweep refutes it. Full data in
`results/2026-09-10/sgemm_tile_sweep.csv`. Peak is at the *smallest* tiles
measured, and throughput falls monotonically above T=32:

```
        T=8     T=16    T=32    T=48    T=64    T=96    T=128   T=256
n=1024  3.922   3.919   2.445   1.757   1.579   1.449   1.430   1.428
n=512   3.705   3.942   3.938   3.417   2.819   2.366   2.106   1.754
```

The predicted optimum, T=64 at n=1024, runs at 1.579 GFLOP/s — 2.5x *slower*
than T=8, and only 6% above the 1.486 naive baseline. Best measured is 3.922 at
T=8, a 2.64x speedup over naive. IQR is under 1% on nearly every point (the one
exception is n=512 T=8 at 13.6%), so this is not noise: the prediction is wrong
in direction, not just in magnitude.

**Hypothesis: the binding constraint is conflict misses, not capacity.** The
capacity argument silently assumes a T x T tile is a contiguous block of
`T^2 * 4` bytes. It is not. In a row-major n x n matrix a tile is T *rows*,
each `T * 4` bytes long, spaced `n * 4` bytes apart. What the cache sees is the
stride, not the tile area.

At n=1024 the row stride is 4096 B = 32 cache lines of 128 B. With the 128 sets
measured on day 4, the set index advances 32 per row and wraps every 4 rows, so
an entire tile — however tall — lands in only **4 distinct set groups**. A
64-row tile puts 16 rows into each group against the 8 ways measured on day 4.
That is 2x over-subscribed, which is the same thrashing condition as the Day 4
K=9 result, and it should evict tile rows before they are reused. At T=16 it is
4 rows per set group, inside 8 ways, and it fits.

**Supporting evidence: the n=512 column behaves as the stride argument predicts
and the capacity argument does not.** At n=512 the row stride is 2048 B = 16
lines, so the set index wraps every 8 rows instead of 4 — twice as many set
groups, and therefore roughly twice the tolerable tile height. That is what the
data shows: n=512 still holds ~3.9 GFLOP/s at T=32, where n=1024 has already
collapsed to 2.445, and n=512's own collapse is deferred to T=48-64. Under a
capacity explanation the two sizes share one L1d and should turn over at the
same T. They do not. The turnover tracks the row stride.

Caveats, held open deliberately:
- Small tiles also shorten the innermost `k` run, so T=8-16 changes loop
  overhead, prefetch behaviour and the store pattern on C all at once. Nothing
  here separates the conflict effect from those.
- Two matrix sizes, both powers of two, is a two-point fit. The 2x claim is
  consistent with 2x, not measured against a third stride.

**CONFIRMED by the padding experiment.** The matrices were reallocated with a
row stride of `n + 16` floats instead of `n`, staying logically n x n. Tile
area, loop structure, flop count and tile size are all unchanged; the only
difference is that rows are no longer spaced at a power of two. Padded kernel
verified against `sgemm_naive` within 1e-3 before timing. Data in
`results/2026-09-10/sgemm_tile_sweep_padded.csv`.

```
        T=8     T=16    T=32    T=48    T=64    T=96    T=128   T=256
n=1024  3.876   3.949   3.992   3.386   3.158   2.552   2.307   1.785
n=512   3.923   4.124   4.005   3.416   3.150   2.582   2.308   1.782
```

Three things fall out, all in the direction the set-group model predicts.

**1. Padding alone roughly doubles throughput at the collapsed point.** At
n=1024, T=64: 1.579 -> 3.158 GFLOP/s, +100%. Same arithmetic, same tile size,
same instruction count — only the spacing between rows changed. This is the
comparison that isolates stride from everything else, because tile height is
held fixed across it.

**2. The benefit is strongly asymmetric between the two matrix sizes, and
asymmetric the right way.** The model says n=1024 has 4 set groups against
n=512's 8, so n=1024 carries about twice the conflict pressure and has about
twice as much to gain. Measured at T=64:

```
n=1024   1.579 -> 3.158   (+100%)
n=512    2.819 -> 3.150   (+12%)
```

After padding the two sizes converge to 3.158 and 3.150 — a 0.3% spread, where
before they differed by 79%. If conflict misses were the only thing separating
the two strides, removing conflicts should collapse the difference. It does.
This is what promotes the entry from "consistent with" to confirmed: a capacity
explanation predicts no asymmetry here at all, because both sizes share one
128 KiB L1d and the tiles are identical.

**3. With conflicts removed the optimum moves up, toward where the capacity
model pointed.** Unpadded peak was T=8/16 at 3.92; padded peak is **T=32 at
3.992 GFLOP/s**, 2.69x the 1.486 naive baseline. So the Day 2 reasoning was
sound but **incomplete rather than wrong** — capacity is a real constraint and
does set the optimum once conflicts are out of the way, but on a power-of-two
stride conflict misses bind first and bind much harder, dragging the optimum
down to T=8-16. The prediction failed because it modelled only the weaker of
the two constraints.

Measurement quality: padded IQR is under 0.8% on every point, and the sweep was
run twice with the two runs agreeing within 0.3%.

The caveats above still stand as written. The first one — that small tiles
change loop overhead and prefetch along with locality — is *narrowed* but not
eliminated by point 1, since that comparison holds T fixed at 64 and therefore
cannot be explained by inner-loop length. The second still holds: this is two
power-of-two strides plus one padded stride, not a stride sweep.

What remains unexplained is the padded curve's own decline above T=32 — logged
as a separate OPEN entry below.


---

## Padded SGEMM still declines above T=32 while the tiles still fit   [OPEN]

With the power-of-two row stride removed (see the entry above), the padded tile
sweep peaks at T=32 and then falls monotonically, and capacity does not account
for where the fall starts.

Three float tiles of T x T occupy `3 * T^2 * 4` bytes. Against the 128 KiB L1d
measured on day 2:

```
T      working set    n=1024 padded GFLOP/s
32     12 KiB         3.992   <- peak
48     27 KiB         3.386   -15%
64     48 KiB         3.158   -21%
96     108 KiB        2.552   -36%
128    192 KiB        2.307        (exceeds L1d)
256    768 KiB        1.785        (exceeds L1d)
```

The decline is already 15% at T=48, where the working set is roughly 27 KiB —
about a fifth of L1d, fitting comfortably. It reaches 36% at T=96, still inside
128 KiB. Capacity does eventually become a real constraint at T=128 and T=256,
which genuinely overflow L1d, but the curve has lost more than a third of its
throughput before reaching that point. n=512 behaves identically (3.416 / 3.150
/ 2.582 at T=48/64/96), so this is not specific to one matrix size.

IQR is under 0.8% on every point and the sweep reproduced within 0.3% across two
runs, so the decline is real and not measurement noise.

No hypothesis recorded yet — deliberately. The remaining candidates are not
separable from throughput numbers alone, and guessing here is what produced the
Day 2 error in the first place. Day 8 is planned to add hardware performance
counters, which should distinguish L1 data-cache misses from dTLB misses from
other effects and say directly which one tracks this curve.


---

## DRAM latency 158 ns against an expected 90-100 ns   [OPEN]

*(Merged from the stray root-level `Surprise.md`, Day 2. Recorded there as a
one-line stub; kept here so it is not lost, at the level of detail the stub
carried.)*

The day-2 capacity sweep bottoms out at 158.128 ns/hop at 64 MB, where the
expectation going in was 90-100 ns. The 32 MB and 64 MB rows also carry the
largest IQRs in the sweep (46.4 and 15.6), so part of the gap may be
measurement quality rather than the machine.

Not investigated further. The ratio that the day-2 conclusions actually rest on
(L1:L2:DRAM = 1:4.0:103) is frequency- and methodology-independent and lands
inside the published 1:3-4:50-100 range, so nothing downstream depends on the
absolute number being right.

**Day 8 note.** Direct cycle measurement at 64 MB gives 338.519 cycles/hop
= 84.2 ns at 4.021 GHz, inside the expected 90-100 ns range. This suggests
the same cold-clock artifact identified in the L1 hit latency entry. NOT
resolved here: the gap is larger than the L1 case, and the original probe
should be rerun under counters before this closes.

Data: results/2026-09-15/latency_cycles.txt

---

## L1 hit latency is ~6.2 cycles against a published ~4   [RESOLVED]

**Original observation (Day 2/4).** The pointer chase measured 1.54 ns/hop
in the L1-resident region. Converted at the M3 Pro's published P-core
clock, that implied ~6.2 cycles, against a published L1 hit latency near 4.

**Why it stayed open.** macOS exposes no userspace cycle counter, so the
conversion depended on an assumed frequency. The anomaly could not be
separated from an error in that assumption.

**Resolution (Day 8).** With kperf/kpc counter access working, cycles were
read directly around the same chase. A 32 KB chain (one quarter of the
128 KB L1d), 10M dependent hops, compiler barrier per hop, three untimed
warmup passes:

    4.005 / 4.011 / 4.018 cycles per hop   (three runs, 0.3% spread)
    3.002 instructions per hop             (confirms a dependent load chain)
    checksum identical across runs (1771)

L1 hit latency is 4.0 cycles, matching the published figure.

**What the 6.2 actually was.** The Day 2 nanoseconds were correct. The
frequency used to convert them was not.

    4.021 GHz / (6.19 / 4.005) = 2.60 GHz

That is the cold-core frequency observed in run 1 of the Day 8 counter
validation (2.608 GHz, ramping to 4.02 over three runs). The Day 2 chase
ran on a core still climbing under DVFS.

**Methodology consequence.** Any cycle count derived by dividing a
wall-clock measurement by an assumed frequency silently inherits the
frequency governor's state at measurement time, and the error is
undetectable without a cycle counter. This is stronger than the Day 3 DVFS
entry, which only established that cold runs read low on throughput.

Every ns-derived cycle figure taken before Day 8 should be treated as
suspect unless the run was demonstrably warm.

Data: results/2026-09-15/latency_cycles.txt

---

## Accelerate sgemm measures 1120 GFLOP/s, 35x the NEON ceiling   [OPEN]

Added Accelerate's `cblas_sgemm` as a reference ceiling for the SGEMM work. At
n=1024, single-threaded, it measures **1120 GFLOP/s** against the ~32 GFLOP/s
single-core NEON FMA figure estimated in the Day 6 notes — 35x the estimate.

That tripped the rule written down in the `-O3` and K=1 entries above: any
number better than a known physical ceiling is a methodology bug until proven
otherwise. Two prior entries in this file were exactly that failure.

**It survived the check.** Verified outside the timing harness entirely: 400
`cblas_sgemm` calls timed with `steady_clock`, with a checksum read of C
afterwards so no part of the work could be dead-coded. That independent path
agreed at 1100 GFLOP/s. The harness is not producing this number by accident,
and the multiply is genuinely happening.

**`VECLIB_MAXIMUM_THREADS=1` had no effect.** Both configurations measure the
same, and CPU time against wall time over the 400 calls was 1:1 — one core's
worth of CPU for the whole run, where six P-cores would have shown roughly 6x.
The work is genuinely single-threaded. So this is not 1120 GFLOP/s of parallel
throughput being mislabelled; it is one thread.

**Hypothesis, not proven: Accelerate is dispatching to Apple's AMX matrix
coprocessor.** AMX is undocumented, is attached per-cluster rather than
per-core, and has no public instruction interface — Accelerate is the only
supported way to reach it. That would explain both facts at once: the magnitude,
because AMX is a dedicated matrix unit rather than the NEON pipeline, and the
irrelevance of the thread cap, because a per-cluster block can be saturated by a
single thread. Nothing here demonstrates it. The evidence is consistent with
AMX and also consistent with any other explanation that puts a wide matrix unit
behind one thread; no counter was read, and the instruction stream was not
inspected. Day 8's performance counters may or may not help, since an
undocumented coprocessor may not be visible to them.

**Consequence for the project: Accelerate is not a like-for-like ceiling for my
kernels.** The naive and blocked kernels are scalar C++ on the general-purpose
pipeline — no NEON intrinsics, no matrix unit. Comparing them to Accelerate
measures the gap between a scalar loop and a dedicated coprocessor, which is not
the quantity the blocking work is trying to move. Three separate ceilings, kept
distinct from here on:

```
scalar, general-purpose pipeline   ~4 GFLOP/s      MEASURED (best blocked kernel)
NEON FMA, single core              ~32 GFLOP/s     ESTIMATED, still unmeasured
AMX via Accelerate                 ~1120 GFLOP/s   MEASURED
```

The middle row is the one the current kernels are actually working against, and
it is the only one of the three that has never been measured. Measuring it —
a hand-vectorised NEON SGEMM — is the honest next comparison, and until that
exists the scalar-to-Accelerate ratio should not be quoted as a speedup target.

Open: whether the mechanism is AMX, and what the real NEON ceiling is.
