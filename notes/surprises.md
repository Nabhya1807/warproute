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

Verified by rebuilding the unmodified day-3 source (d32b24e) and running both
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