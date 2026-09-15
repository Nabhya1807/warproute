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
//
//   The kpep_event field layout below is also this project's, NOT the gist's.
//   The gist declares `u8 number` at 0x2c and is_fixed at 0x2f, but as3.plist
//   has event numbers above 255 (L2_TLB_MISS_DATA = 1035). Offsets were read
//   from `dyld_info -disassemble` of kperfdata on Darwin 24.6.0:
//     kpep_event_name/description/errata/alias load 0x00/0x08/0x10/0x18,
//     add_event_internal reads fallback at 0x20, counters_mask (u32) at 0x28,
//     and tests bit 0 of the byte at 0x30 to take the fixed-counter path,
//     kpep_config_kpc loads the event number with `ldrh [ev, #0x2c]`.
//   Like the rest of this file, this can break on any OS update.
// =============================================================================

#include "counters.hpp"

#include <dlfcn.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace warproute {
namespace {

// ---- Borrowed declarations (ibireme gist, see PROVENANCE) -------------------

constexpr std::uint32_t KPC_CLASS_FIXED_MASK = 1u << 0;
constexpr std::uint32_t KPC_CLASS_CONFIGURABLE_MASK = 1u << 1;
constexpr std::size_t KPC_MAX_COUNTERS = 32;

struct kpep_db;
struct kpep_config;

// Layout from disassembly, see PROVENANCE. Only name, number and flags are
// read; nothing here is ever written.
struct kpep_event {
  const char* name;
  const char* description;
  const char* errata;
  const char* alias;
  const char* fallback;
  std::uint32_t counters_mask;
  std::uint16_t number;
  std::uint8_t umask;
  std::uint8_t reserved;
  std::uint8_t flags;  // bit 0: fixed counter
};
static_assert(offsetof(kpep_event, counters_mask) == 0x28, "kpep_event layout");
static_assert(offsetof(kpep_event, number) == 0x2c, "kpep_event layout");
static_assert(offsetof(kpep_event, flags) == 0x30, "kpep_event layout");

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

bool g_loaded = false;
bool g_active = false;
kpep_db* g_db = nullptr;
kpep_config* g_cfg = nullptr;
// Index i is the i-th caller-supplied event: g_map[i] is its counter slot.
std::vector<std::string> g_names;
std::size_t g_map[KPC_MAX_COUNTERS] = {};
// Index into g_names of FIXED_CYCLES / FIXED_INSTRUCTIONS, or -1 if absent.
long g_cycles_idx = -1;
long g_insns_idx = -1;

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
  return counters_init({"FIXED_CYCLES", "FIXED_INSTRUCTIONS", "INST_ALL"});
}

bool counters_init(const std::vector<std::string>& events) {
  if (g_active) {
    if (events == g_names) return true;
    std::fprintf(stderr, "FAIL counters_init: already active with a different "
                         "event list; call counters_shutdown() first\n");
    return false;
  }
  if (events.empty() || events.size() > KPC_MAX_COUNTERS) {
    std::fprintf(stderr, "FAIL counters_init: %zu events, need 1..%zu\n",
                 events.size(), KPC_MAX_COUNTERS);
    return false;
  }
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

  const std::size_t n = events.size();
  std::vector<kpep_event*> evs(n, nullptr);
  // kpep's canonical name for each requested event, before add_event can swap
  // a fixed event for its fallback.
  std::vector<const char*> canon(n, nullptr);
  bool any_fixed = false;
  bool any_configurable = false;
  for (std::size_t i = 0; i < n; i++) {
    const char* name = events[i].c_str();
    if ((ret = kpep_db_event(g_db, name, &evs[i]))) {
      std::fprintf(stderr, "FAIL event %s not in db ret=%d\n", name, ret);
      release_config();
      return false;
    }
    canon[i] = evs[i]->name;
    std::uint32_t err = 0;
    if ((ret = kpep_config_add_event(g_cfg, &evs[i], 0, &err))) {
      std::fprintf(stderr, "FAIL kpep_config_add_event %s ret=%d err=0x%x\n",
                   name, ret, err);
      // err is a bitmask of already-added events (by add order, which is the
      // caller's order) occupying every counter this event could use.
      for (std::size_t j = 0; j < i && j < 32; j++) {
        if (err & (1u << j)) {
          std::fprintf(stderr, "FAIL   %s conflicts with %s\n", name,
                       events[j].c_str());
        }
      }
      release_config();
      return false;
    }
    if (evs[i]->flags & 1u) {
      any_fixed = true;
    } else {
      any_configurable = true;
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
      (any_fixed ? KPC_CLASS_FIXED_MASK : 0u) |
      (any_configurable ? KPC_CLASS_CONFIGURABLE_MASK : 0u);
  if ((classes & want_classes) != want_classes || reg_count == 0) {
    std::fprintf(stderr,
                 "FAIL kpep_config_kpc_classes: classes=0x%x reg_count=%zu, "
                 "expected classes & 0x%x == 0x%x and reg_count > 0\n",
                 classes, reg_count, want_classes, want_classes);
    release_config();
    return false;
  }
  for (std::size_t i = 0; i < n; i++) {
    if (g_map[i] >= KPC_MAX_COUNTERS) {
      std::fprintf(stderr, "FAIL kpc map: %s -> %zu out of range (max %zu)\n",
                   events[i].c_str(), g_map[i], KPC_MAX_COUNTERS);
      release_config();
      return false;
    }
  }
  for (std::size_t i = 0; i < n; i++) {
    const bool fell_back = evs[i]->name && canon[i] &&
                           std::strcmp(evs[i]->name, canon[i]) != 0;
    if (evs[i]->flags & 1u) {
      std::fprintf(stderr, "event %-26s number=fixed slot=%zu",
                   events[i].c_str(), g_map[i]);
    } else {
      std::fprintf(stderr, "event %-26s number=%-5u slot=%zu",
                   events[i].c_str(), (unsigned)evs[i]->number, g_map[i]);
    }
    if (fell_back) std::fprintf(stderr, " (fallback %s)", evs[i]->name);
    std::fprintf(stderr, "\n");
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

  // Match on the canonical requested name, so aliases ("Cycles") count and a
  // fallback (FIXED_INSTRUCTIONS -> INST_ALL) still fills the named field.
  g_cycles_idx = -1;
  g_insns_idx = -1;
  for (std::size_t i = 0; i < n; i++) {
    if (!canon[i]) continue;
    if (g_cycles_idx < 0 && std::strcmp(canon[i], "FIXED_CYCLES") == 0) {
      g_cycles_idx = static_cast<long>(i);
    } else if (g_insns_idx < 0 &&
               std::strcmp(canon[i], "FIXED_INSTRUCTIONS") == 0) {
      g_insns_idx = static_cast<long>(i);
    }
  }

  g_names = events;
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
    r.events.assign(g_names.size(), 0);
    return r;
  }
  // Allocate after the kpc read. In a before/after pair, the 'before'
  // reading's allocation still falls inside the measured delta.
  r.events.resize(g_names.size());
  for (std::size_t i = 0; i < g_names.size(); i++) r.events[i] = buf[g_map[i]];
  if (g_cycles_idx >= 0) {
    r.cycles = r.events[static_cast<std::size_t>(g_cycles_idx)];
  }
  if (g_insns_idx >= 0) {
    r.instructions = r.events[static_cast<std::size_t>(g_insns_idx)];
  }
  return r;
}

void counters_shutdown() {
  if (!g_active) return;
  stop_counting();
  release_config();
  g_names.clear();
  g_cycles_idx = -1;
  g_insns_idx = -1;
  g_active = false;
}

}  // namespace warproute
