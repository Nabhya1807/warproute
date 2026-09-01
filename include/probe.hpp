#pragma once
#include <cstddef>
#include <vector>

namespace warproute {

std::vector<size_t> build_chain(size_t n_slots);
// builds the pointer chase chain 


size_t chase(const std::vector<size_t>& behind, size_t hops);
// builds the walk that will go through the chain 
std::vector<size_t> build_colliding_chain(size_t K, size_t set_stride);
}  