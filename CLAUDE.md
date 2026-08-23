# WarpRoute
C++17 CPU-GPU autotuner. macOS arm64 (Apple M3 Pro), Apple clang 16.
Build: cmake -B build && cmake --build build

## Rules
- Do NOT modify src/timer.cpp, include/timer.hpp, src/probe.cpp, or kernel inner loops. I write those by hand.
- No new dependencies. C++17 stdlib only (CUDA later, guarded).
- Never use -march=native. This is arm64; use -mcpu=apple-m3.
- No CUDA on this machine. All GPU code must be behind find_package(CUDAToolkit QUIET).
- Every benchmark config must pass verify() before its timing is reported.
- Small changes, explained. I need to defend this code in interviews.
