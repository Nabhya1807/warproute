#include "system_info.hpp"

#include <cstdint>
#include <sstream>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <unistd.h>

#include <fstream>
#endif

#if defined(WARPROUTE_HAVE_CUDA)
#include <cuda_runtime.h>
#endif

namespace warproute {

#if defined(__APPLE__)
namespace {

bool sysctl_u64(const char* name, std::uint64_t& out) {
  std::size_t size = sizeof(out);
  return sysctlbyname(name, &out, &size, nullptr, 0) == 0;
}

bool sysctl_i32(const char* name, int& out) {
  std::size_t size = sizeof(out);
  return sysctlbyname(name, &out, &size, nullptr, 0) == 0;
}

std::string sysctl_str(const char* name) {
  std::size_t size = 0;
  if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) {
    return {};
  }
  std::string value(size, '\0');
  if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0) {
    return {};
  }
  if (!value.empty() && value.back() == '\0') {
    value.pop_back();
  }
  return value;
}

CoreClusterInfo read_perflevel(const char* prefix) {
  CoreClusterInfo cluster;
  int cores = 0;
  std::uint64_t l1 = 0;
  std::uint64_t l2 = 0;
  if (sysctl_i32((std::string(prefix) + ".physicalcpu").c_str(), cores)) {
    cluster.physical_cores = cores;
  }
  if (sysctl_u64((std::string(prefix) + ".l1dcachesize").c_str(), l1)) {
    cluster.l1d_cache_size = l1;
  }
  if (sysctl_u64((std::string(prefix) + ".l2cachesize").c_str(), l2)) {
    cluster.l2_cache_size = l2;
  }
  return cluster;
}

}  // namespace
#elif defined(__linux__)
namespace {

std::string read_cpu_brand_linux() {
  std::ifstream cpuinfo("/proc/cpuinfo");
  std::string line;
  while (std::getline(cpuinfo, line)) {
    if (line.rfind("model name", 0) == 0) {
      const auto colon = line.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      const auto start = line.find_first_not_of(" \t", colon + 1);
      return start == std::string::npos ? std::string{} : line.substr(start);
    }
  }
  return {};
}

}  // namespace
#endif

#if defined(WARPROUTE_HAVE_CUDA)
namespace {

GpuInfo query_gpu() {
  GpuInfo gpu;
  int device_count = 0;
  if (cudaGetDeviceCount(&device_count) != cudaSuccess || device_count == 0) {
    return gpu;
  }

  cudaDeviceProp props{};
  if (cudaGetDeviceProperties(&props, 0) != cudaSuccess) {
    return gpu;
  }

  gpu.present = true;
  gpu.name = props.name;
  gpu.sm_count = props.multiProcessorCount;
  gpu.compute_capability_major = props.major;
  gpu.compute_capability_minor = props.minor;
  return gpu;
}

}  // namespace
#endif

SystemInfo query() {
  SystemInfo info;

#if defined(__APPLE__)
  info.cpu_brand = sysctl_str("machdep.cpu.brand_string");

  std::uint64_t cache_line = 0;
  if (sysctl_u64("hw.cachelinesize", cache_line)) {
    info.cache_line_size = cache_line;
  }

  // Apple Silicon: perflevel0 is the P-core cluster, perflevel1 is E-core.
  // On single-cluster Macs (e.g. Intel) perflevel1 simply won't resolve and
  // e_cores is left at its default (physical_cores == 0).
  info.p_cores = read_perflevel("hw.perflevel0");
  info.e_cores = read_perflevel("hw.perflevel1");
#elif defined(__linux__)
  info.cpu_brand = read_cpu_brand_linux();

  const long cores = sysconf(_SC_NPROCESSORS_ONLN);
  if (cores > 0) {
    info.p_cores.physical_cores = static_cast<int>(cores);
  }

#if defined(_SC_LEVEL1_DCACHE_SIZE)
  const long l1 = sysconf(_SC_LEVEL1_DCACHE_SIZE);
  if (l1 > 0) {
    info.p_cores.l1d_cache_size = static_cast<std::size_t>(l1);
  }
#endif
#if defined(_SC_LEVEL2_CACHE_SIZE)
  const long l2 = sysconf(_SC_LEVEL2_CACHE_SIZE);
  if (l2 > 0) {
    info.p_cores.l2_cache_size = static_cast<std::size_t>(l2);
  }
#endif
#if defined(_SC_LEVEL1_DCACHE_LINESIZE)
  const long line_size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
  if (line_size > 0) {
    info.cache_line_size = static_cast<std::size_t>(line_size);
  }
#endif
  // Linux doesn't expose a portable P-core/E-core split, so e_cores stays
  // default (not reported) here.
#endif

#if defined(WARPROUTE_HAVE_CUDA)
  info.gpu = query_gpu();
#endif

  return info;
}

std::string format(const SystemInfo& info) {
  std::ostringstream out;
  out << "CPU: " << (info.cpu_brand.empty() ? "unknown" : info.cpu_brand) << '\n';
  out << "Cache line: " << info.cache_line_size << " bytes\n";

  auto print_cluster = [&out](const char* label, const CoreClusterInfo& cluster) {
    out << label << ": ";
    if (cluster.physical_cores == 0) {
      out << "not reported\n";
      return;
    }
    out << cluster.physical_cores << " cores, L1d " << (cluster.l1d_cache_size / 1024)
        << " KiB, L2 " << (cluster.l2_cache_size / 1024) << " KiB\n";
  };
  print_cluster("P-cores", info.p_cores);
  print_cluster("E-cores", info.e_cores);

  out << "GPU: ";
  if (info.gpu.present) {
    out << info.gpu.name << ", " << info.gpu.sm_count << " SMs, compute capability "
        << info.gpu.compute_capability_major << '.' << info.gpu.compute_capability_minor << '\n';
  } else {
    out << "absent\n";
  }

  return out.str();
}

}  // namespace warproute
