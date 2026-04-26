cmake_minimum_required(VERSION 3.20)
project(neospice LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# KLU (SuiteSparse)
find_package(KLU REQUIRED)
find_package(AMD REQUIRED)
find_package(COLAMD REQUIRED)
find_package(BTF REQUIRED)
find_package(SuiteSparse_config REQUIRED)

# OpenBLAS via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENBLAS REQUIRED IMPORTED_TARGET openblas)

# SLEEF
find_path(SLEEF_INCLUDE_DIR sleef.h REQUIRED)
find_library(SLEEF_LIBRARY NAMES sleef REQUIRED)

# Optional OpenMP
find_package(OpenMP)

add_subdirectory(src)

option(NEOSPICE_BUILD_TESTS "Build tests" ON)
if(NEOSPICE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

add_executable(neospice cli/main.cpp)
target_link_libraries(neospice PRIVATE neospice_lib)
