# Day 9 prediction — naive vs padded blocked SGEMM, n=1024

Written before any Day 9 measurement.

Machine facts used, all from earlier days: L1d 128 KiB, 128 B lines, 8-way,
128 sets (day 2/4) · page 16 KiB · L1 dTLB 160 entries, ~2.5 MB (day 5) ·
second TLB level holds at least ~768 pages (day 5 plateau) · L2 cache 16 MiB.

Consequence of 128 sets x 128 B = 16 KiB = page size: the L1d set index is the
line's offset within its page. This drives question 1.

Nothing below was checked against disassembly or a counter. Instructions per
iteration are estimates.

## 1. Naive SGEMM MPKI

**Prediction: ~150.**

Inner loop `s += A[i*n+k] * B[k*n+j]` (i, j, k order, k innermost).

- **B:** stride 4096 B = 32 lines, so each access is on a new line. Could the
  line survive until the next j reuses it (j+1 is 4 bytes over, so 31 of 32
  times it's the same line)? No. 4096 B inside a 16 KiB page gives only 4
  distinct page offsets, so a column's 1024 lines map to 4 sets x 8 ways =
  32 slots. That's the day 7 conflict model at its worst.
  **~1.0 miss per access.**
- **A:** sequential along row i, 32 floats per line, so one new line every 32
  iterations. The row is reused for all 1024 values of j, so it stays hot apart
  from the one of its 32 sets that B thrashes. **~1/32 = 0.03 miss per
  iteration.**
- **C:** one access per j. **~0.001 per iteration, negligible.**
- **Misses per iteration: ~1.03.**
- **Instructions per iteration: ~7** (two indexed loads, fmadd from default
  fp-contract, two induction updates, compare, branch). Summing `s` in strict
  order stops vectorization without `-ffast-math`, so the loop is scalar.

MPKI = 1.03 / 7 x 1000 ≈ **147**, call it 150.

How this could be wrong: a stride prefetcher catching the 4096 B stride. The
stride crosses a page every 4th access, so at most ~3 of 4 misses could be
saved; if that happens, MPKI lands well below 150.

## 2. Padded blocked SGEMM MPKI (T=32)

**Prediction: ~0.33.**

Row stride 1040 floats = 4160 B. Page offsets cycle through 256 values instead
of 4, so tile lines spread across all 128 sets and conflicts mostly go away.

Per tile (ii, jj, kk), 32^3 = 32768 iterations:

- **A:** 32 rows x 32 floats. Rows start at offset 0 or 64 within a line
  (4160 mod 128 = 64), so ~1.5 lines per row, **~48 lines**. Each line gets
  ~1024 accesses.
- **B:** same shape, **~48 lines**, ~1024 accesses each.
- **C:** ~48 lines, but C depends only on (ii, jj), so it reloads once every
  32 tiles.
- **Revisits:** a tile's A and B lines come back only after a full kk sweep.
  That sweep touches A's 32-row band (~130 KB) plus B's column band across all
  1024 rows (~190 KB), more than 128 KB. So every revisit reloads them.

Misses ≈ 32768 tiles x 96 + 1024 x 48 ≈ 3.2 M in total, about 0.003 per
iteration. That's roughly 1024 uses per line, the reuse factor.

**Instructions per iteration: ~9.** It's the naive loop plus a load and a store
of C each iteration, because C can't be proven not to alias A and B. The
~1/32 outer-loop overhead is left out.

MPKI = 0.003 / 9 x 1000 ≈ **0.33**.

## 3. MPKI ratio and relationship to the measured 2.69x speedup

**Predicted ratio: ~450x** (150 / 0.33). That's two orders of magnitude more
than the 2.69x runtime speedup.

**What a large miss drop with only 2.69x runtime means** (20x or 450x, the
argument is the same): runtime isn't proportional to L1d miss count. Each naive
miss is cheap, for three reasons:

- **Misses are independent.** The B addresses come from the induction variable,
  not from loaded data. Unlike the day 8 pointer chase, out-of-order execution
  can have many misses in flight at once. They don't pay 26 cycles in series.
- **Misses land in L2, not DRAM.** The working set is 12 MB, inside the 16 MiB
  L2.
- **Something else sets the pace.** The carried FP accumulator (`s` in naive,
  `C[i][j]` in blocked) limits throughput whether or not misses happen.

Amdahl bound (derived): if blocking removed essentially all miss cost and
bought 2.69x, then memory stalls were at most 1 - 1/2.69 ≈ **63%** of naive
runtime. The rest is compute and dependency-chain cost that blocking doesn't
touch.

Rough per-miss cost (derived from day 7 GFLOP/s and the day 8 warm frequency
of 4.02 GHz, so it carries the frequency-assumption caveat in surprises.md):

- Naive: ~5.4 cycles per iteration.
- Blocked: ~2.0 cycles per iteration.
- Saving: ~3.4 cycles for ~1 miss avoided, **~3-4 cycles per miss**. That's
  far below the 26-cycle serial L2 latency.

Falsifiable: the cycles per iteration measured directly on Day 9 should
confirm or kill this.

## 4. Naive and blocked dTLB behavior (L1D_TLB_MISS_NONSPEC and L2_TLB_MISS_DATA)

Each matrix is 4 MB = 256 pages (padded: 1024 x 1040 x 4 B ≈ 260 pages).
All three come to ~768-780 pages.

**Naive**

- **Pages per j:** each j walks B down a whole column, which is all 256 B
  pages, 4 rows (4 iterations) per page. A and C add 1-2 pages. ~259 pages per
  sweep against 160 L1 entries, so every B page is evicted before it comes
  back.
- **L1D_TLB_MISS_NONSPEC: yes, heavily.** ~1 miss per new B page, 0.25 per
  iteration. **~2.7e8 in total, ~36 per 1000 instructions.**
- **L2_TLB_MISS_DATA: ~0.** The active set (~260 pages) and the whole run
  (~768 pages) both fit the second level. After warmup there's nothing to
  walk. **Expect ~0, at most low thousands.**

**Blocked, T=32 padded**

- **Pages per tile:** 32 rows x 4160 B ≈ 133 KB ≈ 8 pages each for A, B and C,
  ~25 pages in all. That fits 160 easily, and those pages serve 32768
  iterations.
- **L1D_TLB_MISS_NONSPEC: yes, but rarely.** A tile's pages are scattered
  across the 4 MB matrix, but that doesn't matter: what counts is the number of
  distinct pages per tile, not where they are. The A and C pages stay fixed for
  a whole ii band. B changes ~8 pages per kk step, and a jj sweep covers all
  ~260 B pages (>160), so those ~8 miss again. **~8 misses per tile x 32768
  ≈ 2.6e5 in total, ~0.03 per 1000 instructions, about 1000x below naive.**
- **L2_TLB_MISS_DATA: ~0, same as naive.** Blocking has nothing to fix at this
  level. The one risk: padding brings the total to ~780 pages, just over 768.
  If the second level is exactly 768 entries, a small nonzero count could
  appear.

The main uncertainty for both: the second level's exact size was never pinned
down on day 5, only that it is at least ~768 pages.
