cmake_minimum_required(VERSION 3.20)
project(warproute LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()


include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mcpu=apple-m3" WARPROUTE_HAS_MCPU_APPLE_M3)

set(WARPROUTE_RELEASE_FLAGS -O3)
if(WARPROUTE_HAS_MCPU_APPLE_M3)
  list(APPEND WARPROUTE_RELEASE_FLAGS -mcpu=apple-m3)
endif()


find_package(CUDAToolkit QUIET)

add_executable(warproute
  src/main.cpp
  src/system_info.cpp
  src/timer.cpp
  src/probe_tlb.cpp)
target_include_directories(warproute PRIVATE include)
target_compile_options(warproute PRIVATE $<$<CONFIG:Release>:${WARPROUTE_RELEASE_FLAGS}>)

add_executable(probe
  src/probe_main.cpp
  src/timer.cpp
  src/probe.cpp
  src/system_info.cpp
  kernels/saxpy_cpu.cpp
  kernels/sgemm_cpu.cpp
  src/probe_tlb.cpp)
target_include_directories(probe PRIVATE include)
target_compile_options(probe PRIVATE $<$<CONFIG:Release>:${WARPROUTE_RELEASE_FLAGS}>)

target_link_libraries(probe PRIVATE "-framework Accelerate")


add_executable(counters
  src/counters_main.cpp
  src/counters.cpp)
target_include_directories(counters PRIVATE include)
target_compile_options(counters PRIVATE $<$<CONFIG:Release>:${WARPROUTE_RELEASE_FLAGS}>)
target_link_libraries(counters PRIVATE ${CMAKE_DL_LIBS})


add_executable(sgemm_counters
  src/sgemm_counters_main.cpp
  src/counters.cpp
  src/timer.cpp
  kernels/sgemm_cpu.cpp)
target_include_directories(sgemm_counters PRIVATE include)
target_compile_options(sgemm_counters PRIVATE $<$<CONFIG:Release>:${WARPROUTE_RELEASE_FLAGS}>)
target_link_libraries(sgemm_counters PRIVATE ${CMAKE_DL_LIBS} "-framework Accelerate")

if(CUDAToolkit_FOUND)
  target_compile_definitions(warproute PRIVATE WARPROUTE_HAVE_CUDA)
  target_link_libraries(warproute PRIVATE CUDA::cudart)
endif()

enable_testing()
add_executable(tests tests/test_main.cpp)
target_compile_options(tests PRIVATE $<$<CONFIG:Release>:${WARPROUTE_RELEASE_FLAGS}>)
add_test(NAME tests COMMAND tests)