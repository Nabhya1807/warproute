#include "probe_tlb.hpp"
#include "timer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace warproute {

static volatile void* tlb_sink;
static void* tlb_base = nullptr;

static void** build_page_chain(std::size_t page_count,
                               std::size_t page_size,
                               std::size_t cache_line_size) {
  void* base = nullptr;
  if (posix_memalign(&base, page_size, page_size * page_count) != 0) {
    return nullptr;
  }
  tlb_base = base;

  std::vector<std::size_t> page(page_count);
  for (std::size_t i = 0; i < page_count; i++) {
    page[i] = i;
  }
  std::mt19937 rng(12345);
  std::shuffle(page.begin(), page.end(), rng);

  for(size_t j=0; j<page_count;j++){
    size_t n =page[j];
    size_t m    = page[(j+1) % page_count];
    void** slot_this= (void **)((char*)base +n*page_size + (n*cache_line_size)%page_size);
    void** slot_next= (void **)((char*)base +m*page_size + (m*cache_line_size)%page_size);
    *slot_this= (void*) slot_next;
  }

  return (void**)((char*)base + page[0]*page_size + (page[0]*cache_line_size)%page_size);
}

static void free_page_chain(void** chain) {
  (void)chain;
  std::free(tlb_base);
  tlb_base = nullptr;
}

void probe_tlb_reach(std::size_t page_size, std::size_t cache_line_size) {
  std::printf("pages,bytes,ns_per_hop\n");
  const std::size_t sizes[] = {96, 112, 128, 132, 136, 142,  144, 160, 192, 224, 256,
                             320, 384, 448, 512, 768, 1024};

  for (std::size_t pages : sizes) {
    void** chain = build_page_chain(pages, page_size, cache_line_size);
    if (!chain) {
      std::fprintf(stderr, "allocation failed at %zu pages\n", pages);
      break;
    }

    const std::size_t hops = 200000;

    Stats s = run_n([&]() {
      void** p = chain;
      for (std::size_t i = 0; i < hops; i++) {
        p = (void**)*p;
        asm volatile("" : "+r"(p));
      }
      tlb_sink = p;
    });

    double ns_per_hop = s.median_ns / (double)hops;
    std::printf("%zu,%zu,%.3f\n", pages, pages * page_size, ns_per_hop);
    std::fflush(stdout);

    free_page_chain(chain);
  }
}

}  