#include "timer.hpp"

#include <algorithm> 
#include <chrono>
#include <vector>

#ifdef __APPLE__
#include <pthread.h>
#endif 

namespace warproute{
    using Clock = std::chrono::steady_clock; // aliasing 
    void set_high_qos(){
        #ifdef __APPLE__
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
        /*Now this is because apple has Peformance cores and E cores and we 
        have to only use the Performance cores and this QOS_CLASS_USER_INTERACTIVE
        gives the highest priority. 
        */ 
       #endif
    }
    static double time_once(const std::function<void()>& fn) { // static means in this file only 
        auto start = Clock::now();
        fn();
        auto end= Clock::now();
        return std::chrono::duration<double, std::nano>(end-start).count();
    }
    Stats run_n(const std::function<void()>& fn,int warmups, int reps){
        for(int i=0; i<warmups; i++){
            fn();
        }
        std::vector<double> store;
        for(int i=0; i<reps;i++){
            store.push_back(time_once(fn));
        }
        std::sort(store.begin(), store.end());

        size_t n = store.size();

        double median;
        if (n % 2 == 1) {
            median = store[n/2];
        } else {
            median = (store[n/2 - 1] + store[n/2]) / 2.0;
        }

        Stats s;
        s.median_ns = median;
        s.min_ns = store.front();
        s.max_ns = store.back();
        s.iqr_ns = store[(size_t)(0.75 * n)] - store[(size_t)(0.25 * n)];
        return s;
    }
        


}



