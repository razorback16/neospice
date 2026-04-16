cmake_minimum_required(VERSION 3.20)
project(cudaspice LANGUAGES C CXX)

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
find_path(SLEEF_INCLUDE_DIR sleef.h
    PATHS /usr/include/x86_64-linux-gnu
    REQUIRED
)
find_library(SLEEF_LIBRARY
    NAMES sleef
    PATHS /usr/lib/x86_64-linux-gnu
    REQUIRED
)

# Optional OpenMP
find_package(OpenMP)

add_subdirectory(src)

option(CUDASPICE_BUILD_TESTS "Build tests" ON)
if(CUDASPICE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
