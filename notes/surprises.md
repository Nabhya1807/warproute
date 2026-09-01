
## Day 3 — SAXPY bandwidth dip at 1M elements (12 MB)
Measured 54.9 GB/s at N=1048576, vs 95.6 GB/s at N=524288 (below it) and
93.1 GB/s at N=2097152 (above it). Isolated dip -- neighbors are
consistent with the surrounding L2 plateau, and 12 MB isn't near any
known boundary (L1=128KB, L2=16MB).
Hypotheses: background process/OS scheduling interference during that
one run; thermal throttling; one-off page fault or allocation pattern
at that specific size.
Plan: re-run the full sweep once more. If the dip reproduces at the
same N, it's a real effect worth investigating further. If it doesn't,
treat it as measurement noise from that run.

## Day 4 — K=1 measures 0.38 ns/hop, BELOW L1 hit latency
K=1 builds a single self-referencing entry (chain[0] = 0), so the chase is
p = chain[0] repeated forever. Same address every hop, so: address never
changes, the value is served by load-forwarding rather than a real cache
access, and the dependency chain is trivial. 0.38 ns is roughly 1.5 clock
cycles -- that's loop overhead, not memory latency.
Not a data point. Excluded from analysis.
Tell: any measured latency FASTER than the fastest known cache level is a
methodology bug, not a discovery. Same failure family as compiler-eliminated
loops and short cycles in build_chain.
Action for the shipped tool: either start the sweep at K=2, or print K=1 with
an explicit "degenerate" label rather than hiding it.

## Day 4 — post-threshold curve ramps rather than steps
Expected a clean step from "all hits" to "all misses" at K=9. Instead the
curve climbs gradually (3.5 -> 4.5 -> 5.2 -> 6.0 -> 7.2) before plateauing
around 7 ns for K=16-20, then climbing again to ~11 ns by K=24.
Likely explanation: at K=9 the set is only slightly over capacity, so
depending on replacement policy and access order some accesses still hit.
The miss fraction grows with K rather than flipping to 100% instantly.
Not a problem for the measurement -- the K=8 to K=9 boundary is still sharp
and unambiguous -- but worth noting that the degradation is smooth.
