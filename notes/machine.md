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
