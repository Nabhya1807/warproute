
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
