cmake_minimum_required(VERSION 3.15)
project(shmfeeder)

add_library(shmfeeder INTERFACE)

target_include_directories(shmfeeder INTERFACE
    include
)

target_compile_features(shmfeeder INTERFACE cxx_std_17)

# Option to build examples, OFF by default
option(SHMFEEDER_BUILD_EXAMPLES "Build examples" OFF)

if(SHMFEEDER_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
