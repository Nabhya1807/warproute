#include "probe.hpp"
#include <algorithm>
#include <random> 

namespace warproute{
    std::vector<size_t> build_chain(size_t n_slots){
        std::vector<size_t> initial(n_slots);
        for(size_t i=0; i<n_slots;i++){
            initial[i]=i;
        }
        std::mt19937 rng(12345); 
        std::shuffle(initial.begin(), initial.end(), rng);

        std::vector<size_t> behind(n_slots);
        for(size_t j=0; j<n_slots-1; j++){
            behind[initial[j]]=initial[j+1];
        }
        behind[initial[n_slots-1]]=initial[0];
        return behind;
    }

    size_t chase(const std::vector<size_t>& behind, size_t hops){
        size_t p=0;
        for(size_t i=0; i<hops;i++){
            p=behind[p];
        }
        return p;


    }
}
