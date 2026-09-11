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

## Optimal SGEMM tile is T=8-16, not the predicted T=64-104   [PARTIAL]

Day-2 prediction, written before any SGEMM existed and committed in machine.md:
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

Falsifying experiment (Day 7 task 2, running): allocate the matrices with a row
stride of `n + 16` floats instead of `n`, keeping them logically n x n. That
leaves tile area, loop structure and flop count identical and changes only the
spacing between rows, so a power-of-two stride is no longer in play. If conflict
misses are the cause, the large-T collapse should lift and the curve should
flatten or move its peak upward. If the padded curve looks like the unpadded
one, the hypothesis is wrong and something else — loop overhead or the C store
pattern — is doing the work.

Status PARTIAL until that runs. What is established is the refutation: the
capacity prediction is measured wrong. The conflict-miss story is the leading
explanation, not a demonstrated one.
