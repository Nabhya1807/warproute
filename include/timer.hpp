#pragma once 
#include <functional>  // this is for std:: function 

namespace warproute{ 

struct Stats{ 
    double median_ns;
    double min_ns;
    double max_ns;
    double iqr_ns;
};
void set_high_qos(); // this is for using the P cores 
Stats run_n(const std::function<void()>& fn, int warmups =3, int reps =10); 


}