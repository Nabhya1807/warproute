#pragma once
#include <cstddef>

namespace warproute {

void probe_tlb_reach(std::size_t page_size, std::size_t cache_line_size);

}  // namespace warproute