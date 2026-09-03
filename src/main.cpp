#include <iostream>
#include <string>

#include "system_info.hpp"
#include "probe_tlb.hpp"

int main(int argc, char** argv) {
  const std::string cmd = (argc > 1) ? argv[1] : "";
  const warproute::SystemInfo info = warproute::query();

  if (cmd == "info") {
    std::cout << warproute::format(info);
    return 0;
  } else if (cmd == "tlb") {
    warproute::probe_tlb_reach(info.page_size,info.cache_line_size);
    return 0;
  }

  std::cout << "warproute" << std::endl;
  return 0;
}