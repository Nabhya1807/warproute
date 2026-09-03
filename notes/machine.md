# Machine under test

## Apple M3 Pro (primary dev machine)
- macOS 24.6.0 (Darwin), arm64
- Apple clang 16.0.0
- Build flags: -O3 only. `-mcpu=apple-m3` is REJECTED by clang 16 even on M3
  hardware; CMake flag check correctly falls back. Retry `-mcpu=native` later.

### Published / OS-reported (ground truth for validation)
| Parameter | P-cores | E-cores |
|---|---|---|
| Physical cores | 6 | 6 |
| L1d | 128 KiB | 64 KiB |
| L2 (shared per cluster) | 16 MiB | 4 MiB |

- Cache line size: 128 bytes (note: 2x the 64 B typical on x86)
- GPU: absent (no CUDA on macOS — all GPU work runs on Colab)

### Day-2 predictions to test
The pointer-chase probe should show:
- latency cliff #1 near 128 KiB  -> L1d capacity (P-core)
- latency cliff #2 near 16 MiB   -> L2 capacity
- stride sweep flattening at 128 B -> line size
If cliff #1 lands near 64 KiB instead, the run landed on an E-core and the
QoS hint is not working.

### Day-6 prediction (blocked SGEMM)
Blocked SGEMM keeps 3 tiles live. With float (4 B):
- 32x32 tile = 4 KiB; 3 tiles = 12 KiB  -> fits 128 KiB L1 easily
- 64x64 tile = 16 KiB; 3 tiles = 48 KiB -> still fits
- 128x128 tile = 64 KiB; 3 tiles = 192 KiB -> exceeds L1, expect a drop
Predicted best tile: 64x64. Verify on day 6.

## Colab T4 (GPU runs)
- Fill in after first Colab session: SM count, compute capability, CUDA version.




## Day 3 results — SAXPY (single-threaded, sequential access)
y[i] = a*x[i] + y[i]. All sizes passed verify().

elements     bytes      time         GB/s
1024         12 KB      83 ns        148.0
2048         24 KB      167 ns       147.2
4096         48 KB      333 ns       147.6
8192         96 KB      625 ns       157.3
16384        192 KB     1646 ns      119.4   <- L1 boundary crossed (192 KB > 128 KB L1)
32768        384 KB     4208 ns      93.4
65536        768 KB     8291 ns      94.9
131072       1536 KB    16480 ns     95.4
262144       3072 KB    32896 ns     95.6
524288       6144 KB    66084 ns     95.2
1048576      12288 KB   229188 ns    54.9    <- anomaly, see surprises.md
2097152      24576 KB   270166 ns    93.1
4194304      49152 KB   558770 ns    90.1
8388608      98304 KB   1144400 ns   88.0
16777216     196608 KB  2184250 ns   92.2
33554432     393216 KB  5089310 ns   79.1    <- L2 boundary crossed (384 MB > 16 MB L2)
67108864     786432 KB  10321500 ns  78.0

Peak (L1, sequential): ~150-157 GB/s
Plateau (L2, sequential): ~90-96 GB/s
Steady state (DRAM, sequential): ~78-80 GB/s

Comparison to day-2 pointer chase (random access):
- Same two boundaries (128 KB, 16 MB) show up in both experiments.
- Pointer chase (random, defeats prefetch) shows sharp cliffs: L1->L2 is
  a 4x latency jump, L2->DRAM is a 4.4x jump.
- SAXPY (sequential, exploits prefetch+burst) shows much gentler bends:
  L1->L2 bandwidth drops ~35%, L2->DRAM drops ~15%.
- Interpretation: sequential streaming gets DRAM row-buffer hits and
  hardware prefetching that random access cannot get. Same hardware,
  same boundaries, very different curve shape depending on access pattern.

## Day 4 results — L1d associativity (conflict-miss sweep)

Method: K addresses spaced exactly one full L1d capacity apart (131072 bytes
= 16384 size_t slots), so all K share the same set index but differ in tag.
Linked into one shuffled cycle, chased 1M hops. Sweep K upward; latency stays
at L1-hit level while K <= ways, spikes once K exceeds ways.

Stride derived from measured hw.perflevel0.l1dcachesize, not hardcoded.

K     ns/hop     iqr
1     0.382      0.0001    <- DEGENERATE, see surprises.md
2     1.548      0.059
3     1.544      0.042
4     1.546      0.115
5     1.556      0.037
6     1.550      0.018
7     1.535      0.011
8     1.542      0.031     <- last value at L1-hit latency
9     3.527      1.098     <- JUMP, 2.3x
10    4.529      0.442
11    4.942      0.318
12    5.184      0.135
13    6.035      0.680
14    6.613      0.982
15    6.927      0.118
16    7.204      0.390
17    6.975      0.543
18    6.969      0.120
19    7.114      0.353
20    7.030      0.053
21    8.239      0.390
22    9.775      0.063
23    10.356     0.305
24    11.034     0.351
25    10.935     0.661
26    10.528     0.831
27    10.236     1.256
28    10.946     0.595
29    11.006     0.251
30    11.130     0.304
31    10.928     0.211
32    11.244     0.241

RESULT: L1d is 8-WAY SET-ASSOCIATIVE.
  K=2..8 flat at 1.535-1.556 ns (spread of 0.02 ns across seven values)
  K=9 jumps to 3.53 ns -- boundary is unambiguous
  Not direct-mapped: K=2 would have spiked immediately if 1-way.

Full L1d structure, now completely determined by measurement:
  capacity     128 KB      (day 2)
  block size   128 bytes   (sysctl; day-3 stride sweep still to confirm)
  total blocks 128 KB / 128 B = 1024
  ways         8           (day 4)
  sets         1024 / 8 = 128
  check: 128 sets x 8 ways x 128 B = 131072 B = 128 KB  OK

Note: this measures L1d only. L1i associativity is not probeable with a
data-access pointer chase and is out of scope for this project.

Cross-check against day 2: the K=16-20 plateau sits at ~7.0 ns, close to the
day-2 measured L2 hit latency of ~6.1 ns. Consistent -- once L1 is fully
thrashing, every access is an L1 miss served by L2, so latency converges on
L2 hit time. Two independent experiments agreeing.
## Day 5 results — TLB reach (page-strided pointer chase)

Method: one pointer slot per page, slots linked into a single shuffled
cycle so every hop lands on a distinct page and consumes a distinct TLB
entry. Slot offset within each page advances by one cache line per page
((n * line_size) % page_size) so slots spread across L1 sets rather than
colliding in one -- without this the sweep measures associativity, not
TLB reach.

Page size read from hw.pagesize (16384 B), not hardcoded.
200,000 hops per measurement, median of 10 runs after 3 warmups.

Fine sweep, run 3 (cleanest of three; runs 1-2 noisier in the tail):

pages   bytes      ns/hop
96      1.5 MB     1.122
112     1.8 MB     1.010
128     2.0 MB     1.010
144     2.4 MB     1.047
160     2.6 MB     1.104   <- last value at L1-dTLB-hit latency
192     3.1 MB     2.800   <- JUMP, 2.5x
224     3.7 MB     2.644
256     4.2 MB     2.861
320     5.2 MB     2.775
384     6.3 MB     2.831
448     7.3 MB     2.763
512     8.4 MB     2.749
768     12.6 MB    2.811   <- second plateau ends here
1024    16.8 MB    4.362   <- second climb begins

Coarse sweep tail (4-8192 pages): 2048 -> 6.749, 4096 -> 15.335,
8192 -> 17.631.

RESULT: two-level TLB.
  L1 dTLB  ~160 entries, 2.5 MB reach, ~1.03 ns
  L2 TLB   covers to ~768 pages (12 MB), ~2.8 ns
  full page walk beyond that, climbing to 17.6 ns at 8192 pages

Knee at 160/192 reproduced across all three runs. Second plateau
(192-768) held 2.64-2.86 ns across a 4x range of page counts in run 3 --
flat enough to be a real second structure, not a transition.

Neither TLB level is published for Apple Silicon.

Cross-check against day 2: L1 region (4 KB - 128 KB) spans at most 8
pages, so TLB pressure cannot exist there. That is why the L1 plateau is
genuinely flat while the L2 plateau is not -- TLB reach (2.5 MB) sits
INSIDE the L2 range (16 MB), so the last 6x of the L2 sweep pays
translation cost on top of data cost.
