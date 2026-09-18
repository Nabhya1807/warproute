# Day 9 — naive vs padded blocked SGEMM: MPKI and TLB counters

Sources: `day9_sgemm_counters_run1.csv`, `day9_sgemm_counters_run2.csv`,
`day9_sgemm_counters_log.txt` (all in this directory), and
`notes/notes.md` section 9.1, written before any Day 9 measurement.

## 1. Method

- Binary: `src/sgemm_counters_main.cpp`, run through `scripts/run_counters.sh`.
- n = 1024, seed 12345. A and B are filled once. The padded copies hold the
  same values in an n x 1040 buffer with the padding zeroed.
- Kernels: `sgemm_naive` (i, j, k order, stride 1024), and
  `sgemm_blocked_padded` with tile T = 32 and row stride ld = 1040.
- Each kernel gets one untimed warmup call first. The counters are read
  immediately before and after the second call only. Setup, verification and
  CSV writing all happen outside the bracketed calls.
- Events: FIXED_CYCLES, FIXED_INSTRUCTIONS, L1D_CACHE_MISS_LD_NONSPEC,
  L1D_TLB_MISS_NONSPEC, L2_TLB_MISS_DATA, L1D_TLB_ACCESS.
- Validity gates, all passed in both runs:
  - The PMU self-test passed before measuring.
  - The counters were still forced at the end.
  - The outputs matched: max abs error = 0 (tolerance 1e-3). The checksums
    also matched, -25486.462534 for both kernels.
- Both runs are recorded.

**Where each run's numbers came from:**

- **Run 1:** the binary wrote the CSV itself (`wrote ...` in the log).
- **Run 2:** the binary exited 5 because the CSV already existed, so it never
  wrote a file. The rows were copied from the binary's stdout in the log
  (last block) into `day9_sgemm_counters_run2.csv`. They match the log
  character for character.
- **Lost measurements:** in the same wrapper invocation, attempts 1 and 3
  also exited 5. The binary only returns 5 after the self-test, verification,
  forced-counter and bracketing checks have all passed. So those two attempts
  were also complete measurements. The wrapper, before today's fix, printed
  only the final attempt's output, so their counter values were not saved and
  cannot be recovered. Two measurements are recorded here; at least four were
  taken.

## 2. Measured: raw counter deltas

Values copied straight from the two CSVs. Nothing in this table is rounded
or computed.

| kernel | run | cycles | instructions | l1d_cache_miss_ld_nonspec | l1d_tlb_miss_nonspec | l2_tlb_miss_data | l1d_tlb_access |
|---|---|---:|---:|---:|---:|---:|---:|
| naive | 1 | 3926644952 | 7543223992 | 1085255539 | 570300068 | 65436 | 3153101446 |
| naive | 2 | 3753081056 | 7527141437 | 1087336294 | 577960666 | 3119244 | 3158082453 |
| padded_blocked (T=32, ld=1040) | 1 | 1405335597 | 9056893736 | 21664124 | 894144 | 1838 | 3272368931 |
| padded_blocked (T=32, ld=1040) | 2 | 1406371723 | 9056417913 | 21617759 | 876418 | 1029 | 3272466708 |

In all four rows: n = 1024, seed = 12345, verified = 1, max_abs_err = 0.

## 3. Derived values

Everything in this section is computed from the measured table above. Values
are shown to 2–4 significant decimals. "Misses" means
`l1d_cache_miss_ld_nonspec` unless stated otherwise. "Iterations" means
n³ = 1024³ = 1073741824 inner-loop iterations. Both kernels execute that many.

### 3.1 L1d MPKI

`MPKI = l1d_cache_miss_ld_nonspec / instructions * 1000`

| kernel | run 1 | run 2 |
|---|---:|---:|
| naive | 143.87 | 144.46 |
| padded_blocked | 2.392 | 2.387 |

### 3.2 MPKI ratio

`MPKI ratio = MPKI_naive / MPKI_padded`

| run 1 | run 2 |
|---:|---:|
| 60.15x | 60.52x |

### 3.3 Cycle speedup

`speedup = cycles_naive / cycles_padded`

| run 1 | run 2 |
|---:|---:|
| 2.794x | 2.669x |

### 3.4 Cycles per avoided miss

`cycles per avoided miss = (cycles_naive - cycles_padded) / (misses_naive - misses_padded)`

| run | cycles saved | misses avoided | cycles per avoided miss |
|---|---:|---:|---:|
| 1 | 2521309355 | 1063591415 | 2.371 |
| 2 | 2346709333 | 1065718535 | 2.202 |

The formula puts the entire cycle difference down to avoided misses. The two
kernels also differ in other ways: padded runs about 20% more instructions.
So this is not a clean per-miss cost, and it is not a bound in either
direction.

### 3.5 Other derived values used in section 5

| quantity | formula | naive r1 | naive r2 | padded r1 | padded r2 |
|---|---|---:|---:|---:|---:|
| instructions per iteration | `instructions / 1024³` | 7.025 | 7.010 | 8.435 | 8.434 |
| cycles per iteration | `cycles / 1024³` | 3.657 | 3.495 | 1.309 | 1.310 |
| L1d misses per iteration | `misses / 1024³` | 1.0107 | 1.0127 | 0.02018 | 0.02013 |
| L1 dTLB misses per 1000 instr | `l1d_tlb_miss_nonspec / instructions * 1000` | 75.60 | 76.78 | 0.0987 | 0.0968 |
| L1 dTLB misses per iteration | `l1d_tlb_miss_nonspec / 1024³` | 0.5311 | 0.5383 | 0.000833 | 0.000816 |
| L2 TLB misses per 1000 instr (**UNRELIABLE**, see section 4) | `l2_tlb_miss_data / instructions * 1000` | 0.00868 | 0.4144 | 0.000203 | 0.000114 |
| L1 dTLB miss ratio naive / padded | `l1d_tlb_miss_nonspec_naive / l1d_tlb_miss_nonspec_padded` | 637.8x | 659.5x | — | — |

## 4. Run-to-run agreement

`spread = |run2 - run1| / ((run1 + run2) / 2) * 100`

| counter | naive | padded_blocked |
|---|---:|---:|
| cycles | 4.52% | 0.07% |
| instructions | 0.21% | 0.005% |
| l1d_cache_miss_ld_nonspec | 0.19% | 0.21% |
| l1d_tlb_miss_nonspec | 1.33% | 2.00% |
| l2_tlb_miss_data | **191.8%** (run 2 is 47.7x run 1) | **56.4%** (run 2 is 0.56x run 1) |
| l1d_tlb_access | 0.16% | 0.003% |

**Stable:** instructions, L1d misses, L1 dTLB misses and dTLB accesses agree
within about 2% in both kernels. Padded cycles agree within 0.07%.

**Less stable:** naive cycles differ by 4.5% between runs. That is the only
reason the speedup moves from 2.794x to 2.669x.

**UNRELIABLE: `l2_tlb_miss_data`.** For naive, the two runs give 65436 and
3119244, a 47.7x spread. Every other counter in those same two runs agrees to
under 5%. Padded also differs by 56.4% (1838 vs 1029), while every other
padded counter agrees to within 2%.

With only two runs, there is no way to tell whether this variation is real
(the count depending on page or TLB state that differs between runs) or a
problem with the counter itself. **No conclusion should be drawn from either
value of this counter, for either kernel.** It appears in this document only
as a raw measurement.

## 5. Comparison against `notes/notes.md` section 9.1

### Q1. Naive SGEMM MPKI — prediction ~150

| item | predicted | measured / derived | verdict |
|---|---:|---:|---|
| MPKI | ~150 (computed 147) | 143.87 / 144.46 | **Held.** Measured is 4.1% (run 1) and 3.7% (run 2) below 150, and within 2.2% of the unrounded 147. |
| misses per iteration | ~1.03 | 1.0107 / 1.0127 | **Held**, within 2%. |
| instructions per iteration | ~7 | 7.025 / 7.010 | **Held**, within 0.4%. |

The note's stated failure mode was a stride prefetcher saving up to ~3 of 4
misses. That did not happen to any measurable degree: misses per iteration
are still ≥ 1.0.

### Q2. Padded blocked SGEMM MPKI (T=32) — prediction ~0.33

| item | predicted | measured / derived | verdict |
|---|---:|---:|---|
| MPKI | ~0.33 | 2.392 / 2.387 | **Wrong.** Measured is 7.25x (run 1) and 7.23x (run 2) the prediction. |
| total L1d misses | ≈ 3.2 M (32768 x 96 + 1024 x 48 = 3194880) | 21664124 / 21617759 | **Wrong.** 6.78x / 6.77x the prediction. |
| misses per iteration | ~0.003 | 0.02018 / 0.02013 | **Wrong.** ~6.7x. |
| instructions per iteration | ~9 | 8.435 / 8.434 | **Held roughly.** 6.3% below the prediction. |

The prediction was built on each loaded L1d line serving about 1024
accesses before it is evicted (the note's "reuse factor"). The measured miss
count is about 6.8x the predicted count, while instructions per iteration came
in close to the prediction. So each miss is followed by far fewer accesses
than 1024. If the number of memory accesses is as the note assumed, the
implied figure is 1024 x 3194880 / measured misses ≈ **151 accesses per miss
(both runs)**, not ~1024. This is derived, and it depends on that assumption
about access count.

**The miss model for the padded blocked kernel is wrong by a factor of about
7. The cause is not yet explained.** Nothing measured on Day 9 identifies
which part of the per-tile model (line counts, the revisit argument, the C
reload rate, or something the model left out) accounts for the gap.

### Q3. MPKI ratio and relationship to the 2.69x speedup

| item | predicted | measured / derived | verdict |
|---|---:|---:|---|
| MPKI ratio | ~450x | 60.15x / 60.52x | **Wrong.** Measured is about 7.5x smaller than predicted. This follows directly from the Q2 miss: the naive MPKI held, so the whole error is in the denominator. |
| "miss drop is far larger than the speedup" | ratio ≫ speedup | 60x vs 2.79x / 2.67x | **Held.** The miss ratio is still more than 20x the cycle speedup. The note said the argument works the same at 20x or 450x. |
| speedup | 2.69x (Day 7 wall time, used as given) | 2.794x / 2.669x cycles | **Consistent.** The Day 7 figure falls between the two runs. Measured is 3.9% above it in run 1 and 0.8% below it in run 2. |
| naive cycles per iteration | ~5.4 | 3.657 / 3.495 | **Wrong.** Measured is 32% / 35% below the prediction. |
| blocked cycles per iteration | ~2.0 | 1.309 / 1.310 | **Wrong.** Measured is 35% below the prediction. |
| cycles per avoided miss | ~3–4 (from 3.4 saved per ~1 miss) | 2.371 / 2.202 | **Wrong on the number, right on the conclusion.** Measured is below the predicted range: 21% under its low end in run 1, 27% in run 2. It remains far below the 26-cycle serial L2 latency, which was the point the prediction was making. |
| Amdahl bound: memory stalls ≤ 63% of naive runtime | ≤ 63% | not measured | **Not tested.** None of the Day 9 counters measures stall time. |

The note built its cycles-per-iteration figures from Day 7 GFLOP/s and an
assumed 4.02 GHz frequency, and flagged that assumption itself. The measured
values are about a third lower than both predictions. Day 9 does not show
whether the frequency assumption is the reason.

### Q4. dTLB behavior

**Naive**

| item | predicted | measured / derived | verdict |
|---|---:|---:|---|
| L1D_TLB_MISS_NONSPEC total | ~2.7e8 | 570300068 / 577960666 | **Wrong by about 2x.** 2.11x / 2.14x the prediction. |
| L1 dTLB misses per 1000 instr | ~36 | 75.60 / 76.78 | **Wrong by about 2x.** 2.10x / 2.13x. |
| L1 dTLB misses per iteration | 0.25 (1 per new B page, 4 iterations per page) | 0.5311 / 0.5383 | **Wrong by about 2x.** The "heavily" part held; the magnitude did not. |
| L2_TLB_MISS_DATA (**UNRELIABLE**) | ~0, at most low thousands | 65436 / 3119244 | **Cannot be assessed.** Both raw values are above "low thousands". But the counter is unreliable (section 4): the two runs differ by 47.7x while every other counter agrees to under 5%. No conclusion is drawn from either value, so this is not scored as a prediction hit or miss. |

**Padded blocked, T=32**

| item | predicted | measured / derived | verdict |
|---|---:|---:|---|
| L1D_TLB_MISS_NONSPEC total | ~2.6e5 | 894144 / 876418 | **Wrong.** 3.44x / 3.37x the prediction. |
| L1 dTLB misses per 1000 instr | ~0.03 | 0.0987 / 0.0968 | **Wrong.** 3.29x / 3.23x. |
| naive / blocked L1 dTLB miss ratio | ~1000x | 637.8x / 659.5x | **Partly held.** Same order of magnitude, but 36% / 34% below the prediction. |
| L2_TLB_MISS_DATA (**UNRELIABLE**) | ~0, "same as naive"; a small nonzero count possible if the second level is exactly 768 entries | 1838 / 1029 | **Cannot be assessed.** The raw values are small and nonzero, but the counter is unreliable (section 4). Neither the magnitude nor the naive-vs-blocked comparison is scored. The 768-entry explanation was not tested. |

L2_TLB_MISS_DATA is unreliable across runs (section 4). With two runs, there
is no way to tell whether its variation is real page-state dependence or a
counter problem. None of the Q4 verdicts above use its values.

`l1d_tlb_access` was measured but not covered by any prediction.

## Summary of prediction outcomes

- **Held:** naive MPKI (~150 → 143.87 / 144.46), naive instructions per
  iteration, and the qualitative claim that the miss drop far exceeds the
  speedup.
- **Wrong by about 7x:** padded blocked MPKI (0.33 → 2.39) and therefore the
  MPKI ratio (450x → 60x). The cause is not yet explained.
- **Wrong by about 2–3.5x:** L1 dTLB miss counts for both kernels.
- **Cannot be assessed:** both L2_TLB_MISS_DATA predictions. The counter is
  UNRELIABLE: naive gives 65436 and 3119244, a 47.7x spread, across runs that
  otherwise agree to under 5%. No conclusion is drawn from either value.
- **Wrong by about a third:** cycles per iteration for both kernels. Cycles
  per avoided miss came out below the predicted 3–4 range.
- **Not tested:** the 63% Amdahl bound, and the 768-entry second-level TLB
  explanation.
