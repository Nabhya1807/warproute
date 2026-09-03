#pragma once

#include <cstddef>
#include <string>

namespace warproute {

// One heterogeneous core cluster (e.g. Apple Silicon's P-core or E-core group).
struct CoreClusterInfo {
  int physical_cores = 0;
  std::size_t l1d_cache_size = 0;
  std::size_t l2_cache_size = 0;
};

struct GpuInfo {
  bool present = false;
  std::string name;
  int sm_count = 0;
  int compute_capability_major = 0;
  int compute_capability_minor = 0;
};

struct SystemInfo {
  std::string cpu_brand;
  std::size_t cache_line_size = 0;
  std::size_t page_size = 0;        
  CoreClusterInfo p_cores;
  CoreClusterInfo e_cores;
  GpuInfo gpu;
};

// Queries the host for CPU/cache/GPU topology. Fields that can't be
// determined on the current platform are left at their default value.
SystemInfo query();

std::string format(const SystemInfo& info);

}  // namespace warproute
