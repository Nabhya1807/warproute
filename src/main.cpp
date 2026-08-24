#include <iostream>
#include <string>

#include "system_info.hpp"

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "info") {
    std::cout << warproute::format(warproute::query());
    return 0;
  }

  std::cout << "warproute" << std::endl;
  return 0;
}
