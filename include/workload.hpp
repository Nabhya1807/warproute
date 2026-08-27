#pragma once
#include <functional>
#include <string>

namespace warproute{
    struct slayout{
        size_t thread_count=1 ;
        size_t b_size =256 ; 
        size_t t_size=32;
    };
    
    struct ulayout{
        std::string name;
        std::function<void(size_t)> setup;
        std::function<void(slayout)> run;
        std::function<bool()> verify; 
    };


}