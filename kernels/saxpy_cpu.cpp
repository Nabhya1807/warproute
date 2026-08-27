#include "saxpy_cpu.hpp"
#include <functional> 

namespace warproute{
    static std:: vector<float> x,y,y_original;
    static size_t n;
    static const float a = 2.0f;


    void saxpy_setup(size_t N) {
        n=N;
        x.resize(N);
        for(size_t i=0; i<n; i++){
            x[i]=1.0f;
        }
        y.resize(N);
        for(size_t j=0; j<n; j++){
            y[j]=3.0f;
        }
        y_original=y; 
    }
    void saxpy_run(slayout config){
        for(size_t i=0; i<n; i++){
            y[i]=a * x[i]+y[i];
        }

    }
    bool saxpy_verify(){
        for(size_t i=0; i<n;i++){
            float exp = a * x[i] + y_original[i];
            if (std::fabs(y[i] - exp) > 1e-4f) {
                return false;
            }
        }
        return true;

    }
    ulayout make_saxpy(){
        ulayout z;
        z.name= "SAXPY";
        z.setup = saxpy_setup;
        z.run = saxpy_run;
        z.verify= saxpy_verify; 
        return z; 
    }
}

