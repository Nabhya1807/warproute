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



