// =============================================================================
// PROVENANCE
//
// Borrowed (NOT this project's work):
//   The kperf / kpc / kpep function-pointer declarations in the "Borrowed
//   declarations" block below, the three kpc class/limit constants, and the
//   opaque kpep_db / kpep_config / kpep_event types. These are reverse
//   engineered signatures for Apple's PRIVATE, undocumented frameworks
//     /System/Library/PrivateFrameworks/kperf.framework
//     /System/Library/PrivateFrameworks/kperfdata.framework
//   taken from ibireme's public gist (kpc_demo.c, by YaoYuan, released into
//   the public domain under unlicense.org):
//     https://gist.github.com/ibireme/173517c208c7dc333ba962c1f0d67d12
//   The call sequence (force_all_ctrs -> kpep db/config -> kpc_set_config ->
//   set_counting -> get_thread_counters) also follows that demo. Apple does
//   not document or guarantee any of this; it can break on any OS update.
//
// This project's own code:
//   Everything else: the dlopen/dlsym loader and FAIL diagnostics (ported from
//   ~/scratch/pmu-spike/pmu_probe.c), the init/read/shutdown state machine,
//   cleanup on partial failure, and the CounterReading API in counters.hpp.
// =============================================================================

#include "counters.hpp"

#include <dlfcn.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace warproute {
namespace {

// ---- Borrowed declarations (ibireme gist, see PROVENANCE) -------------------

constexpr std::uint32_t KPC_CLASS_FIXED_MASK = 1u << 0;
constexpr std::uint32_t KPC_CLASS_CONFIGURABLE_MASK = 1u << 1;
constexpr std::size_t KPC_MAX_COUNTERS = 32;

struct kpep_db;
struct kpep_config;
struct kpep_event;

// kperf.framework
int (*kpc_force_all_ctrs_get)(int* val);
int (*kpc_force_all_ctrs_set)(int val);
int (*kpc_set_config)(std::uint32_t classes, std::uint64_t* config);
int (*kpc_set_counting)(std::uint32_t classes);
int (*kpc_set_thread_counting)(std::uint32_t classes);
int (*kpc_get_thread_counters)(std::uint32_t tid, std::uint32_t count,
                               std::uint64_t* buf);
// kperfdata.framework
int (*kpep_db_create)(const char* name, kpep_db** db);
void (*kpep_db_free)(kpep_db* db);
int (*kpep_db_event)(kpep_db* db, const char* name, kpep_event** ev);
int (*kpep_config_create)(kpep_db* db, kpep_config** cfg);
void (*kpep_config_free)(kpep_config* cfg);
int (*kpep_config_force_counters)(kpep_config* cfg);
int (*kpep_config_add_event)(kpep_config* cfg, kpep_event** ev,
                             std::uint32_t flag, std::uint32_t* err);
int (*kpep_config_kpc_classes)(kpep_config* cfg, std::uint32_t* classes);
int (*kpep_config_kpc_count)(kpep_config* cfg, std::size_t* count);
int (*kpep_config_kpc_map)(kpep_config* cfg, std::size_t* buf,
                           std::size_t size);
int (*kpep_config_kpc)(kpep_config* cfg, std::uint64_t* buf, std::size_t size);

// ---- End borrowed declarations ----------------------------------------------

// Order matters: index i here is map[i] below and a field of CounterReading.
const char* const kEvents[3] = {"FIXED_CYCLES", "FIXED_INSTRUCTIONS",
                                "INST_ALL"};

bool g_loaded = false;
bool g_active = false;
kpep_db* g_db = nullptr;
kpep_config* g_cfg = nullptr;
std::size_t g_map[KPC_MAX_COUNTERS] = {};

#define WARPROUTE_SYM(h, name)                                                \
  do {                                                                        \
    name = reinterpret_cast<decltype(name)>(dlsym(h, #name));                 \
    if (!name) {                                                              \
      std::fprintf(stderr, "FAIL dlsym %s: %s\n", #name, dlerror());          \
      return false;                                                           \
    }                                                                         \
  } while (0)

bool load_frameworks() {
  if (g_loaded) return true;
  void* k = dlopen("/System/Library/PrivateFrameworks/kperf.framework/kperf",
                   RTLD_LAZY);
  if (!k) {
    std::fprintf(stderr, "FAIL dlopen kperf: %s\n", dlerror());
    return false;
  }
  void* d = dlopen(
      "/System/Library/PrivateFrameworks/kperfdata.framework/kperfdata",
      RTLD_LAZY);
  if (!d) {
    std::fprintf(stderr, "FAIL dlopen kperfdata: %s\n", dlerror());
    return false;
  }
  WARPROUTE_SYM(k, kpc_force_all_ctrs_get);
  WARPROUTE_SYM(k, kpc_force_all_ctrs_set);
  WARPROUTE_SYM(k, kpc_set_config);
  WARPROUTE_SYM(k, kpc_set_counting);
  WARPROUTE_SYM(k, kpc_set_thread_counting);
  WARPROUTE_SYM(k, kpc_get_thread_counters);
  WARPROUTE_SYM(d, kpep_db_create);
  WARPROUTE_SYM(d, kpep_db_free);
  WARPROUTE_SYM(d, kpep_db_event);
  WARPROUTE_SYM(d, kpep_config_create);
  WARPROUTE_SYM(d, kpep_config_free);
  WARPROUTE_SYM(d, kpep_config_force_counters);
  WARPROUTE_SYM(d, kpep_config_add_event);
  WARPROUTE_SYM(d, kpep_config_kpc_classes);
  WARPROUTE_SYM(d, kpep_config_kpc_count);
  WARPROUTE_SYM(d, kpep_config_kpc_map);
  WARPROUTE_SYM(d, kpep_config_kpc);
  g_loaded = true;
  return true;
}

#undef WARPROUTE_SYM

void stop_counting() {
  kpc_set_counting(0);
  kpc_set_thread_counting(0);
  kpc_force_all_ctrs_set(0);
}

void release_config() {
  if (g_cfg) kpep_config_free(g_cfg);
  if (g_db) kpep_db_free(g_db);
  g_cfg = nullptr;
  g_db = nullptr;
}

}  // namespace

bool counters_init() {
  if (g_active) return true;
  if (!load_frameworks()) return false;

  int ret = 0;
  int force = 0;
  if ((ret = kpc_force_all_ctrs_get(&force))) {
    std::fprintf(stderr,
                 "FAIL kpc_force_all_ctrs_get ret=%d errno=%d (%s) -> needs "
                 "root\n",
                 ret, errno, std::strerror(errno));
    return false;
  }

  if ((ret = kpep_db_create(nullptr, &g_db))) {
    std::fprintf(stderr, "FAIL kpep_db_create ret=%d\n", ret);
    release_config();
    return false;
  }
  if ((ret = kpep_config_create(g_db, &g_cfg))) {
    std::fprintf(stderr, "FAIL kpep_config_create ret=%d\n", ret);
    release_config();
    return false;
  }
  if ((ret = kpep_config_force_counters(g_cfg))) {
    std::fprintf(stderr, "FAIL kpep_config_force_counters ret=%d\n", ret);
    release_config();
    return false;
  }

  for (int i = 0; i < 3; i++) {
    kpep_event* ev = nullptr;
    if ((ret = kpep_db_event(g_db, kEvents[i], &ev))) {
      std::fprintf(stderr, "FAIL event %s not in db ret=%d\n", kEvents[i],
                   ret);
      release_config();
      return false;
    }
    std::uint32_t err = 0;
    if ((ret = kpep_config_add_event(g_cfg, &ev, 0, &err))) {
      std::fprintf(stderr, "FAIL kpep_config_add_event %s ret=%d err=%u\n",
                   kEvents[i], ret, err);
      release_config();
      return false;
    }
  }

  std::uint32_t classes = 0;
  std::size_t reg_count = 0;
  std::uint64_t regs[KPC_MAX_COUNTERS] = {};
  if ((ret = kpep_config_kpc_classes(g_cfg, &classes)) ||
      (ret = kpep_config_kpc_count(g_cfg, &reg_count)) ||
      (ret = kpep_config_kpc_map(g_cfg, g_map, sizeof(g_map))) ||
      (ret = kpep_config_kpc(g_cfg, regs, sizeof(regs)))) {
    std::fprintf(stderr, "FAIL building kpc config ret=%d\n", ret);
    release_config();
    return false;
  }

  const std::uint32_t want_classes =
      KPC_CLASS_FIXED_MASK | KPC_CLASS_CONFIGURABLE_MASK;
  if ((classes & want_classes) != want_classes || reg_count == 0) {
    std::fprintf(stderr,
                 "FAIL kpep_config_kpc_classes: classes=0x%x reg_count=%zu, "
                 "expected classes & 0x%x == 0x%x and reg_count > 0\n",
                 classes, reg_count, want_classes, want_classes);
    release_config();
    return false;
  }
  for (int i = 0; i < 3; i++) {
    if (g_map[i] >= KPC_MAX_COUNTERS) {
      std::fprintf(stderr, "FAIL kpc map: %s -> %zu out of range (max %zu)\n",
                   kEvents[i], g_map[i], KPC_MAX_COUNTERS);
      release_config();
      return false;
    }
  }

  if ((ret = kpc_force_all_ctrs_set(1))) {
    std::fprintf(stderr, "FAIL kpc_force_all_ctrs_set(1) ret=%d errno=%d (%s)\n",
                 ret, errno, std::strerror(errno));
    release_config();
    return false;
  }
  if ((ret = kpc_set_config(classes, regs))) {
    std::fprintf(stderr, "FAIL kpc_set_config ret=%d errno=%d (%s)\n", ret,
                 errno, std::strerror(errno));
    stop_counting();
    release_config();
    return false;
  }
  if ((ret = kpc_set_counting(classes)) ||
      (ret = kpc_set_thread_counting(classes))) {
    std::fprintf(stderr, "FAIL kpc_set_(thread_)counting ret=%d errno=%d (%s)\n",
                 ret, errno, std::strerror(errno));
    stop_counting();
    release_config();
    return false;
  }

  g_active = true;
  return true;
}

CounterReading counters_read() {
  CounterReading r;
  if (!g_active) return r;
  std::uint64_t buf[KPC_MAX_COUNTERS] = {};
  int ret = kpc_get_thread_counters(0, KPC_MAX_COUNTERS, buf);
  if (ret) {
    std::fprintf(stderr, "FAIL kpc_get_thread_counters ret=%d errno=%d\n", ret,
                 errno);
    return r;
  }
  r.cycles = buf[g_map[0]];
  r.instructions = buf[g_map[1]];
  r.inst_all = buf[g_map[2]];
  return r;
}

void counters_shutdown() {
  if (!g_active) return;
  stop_counting();
  release_config();
  g_active = false;
}

}  // namespace warproute
