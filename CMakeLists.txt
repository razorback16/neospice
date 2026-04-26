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

# Python bindings (opt-in)
option(NEOSPICE_BUILD_PYTHON "Build Python bindings" OFF)
if(NEOSPICE_BUILD_PYTHON)
    set_target_properties(neospice_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)
    foreach(dev bsim4v7 bsim3 bjt jfet jfet2 dio mos1 mos3 mos9
                vbic asrc hfet1 hfet2 bsim3v32 hisim2 hisimhv bsimsoi)
        set_target_properties(${dev}_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endforeach()

    find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
    find_package(nanobind CONFIG REQUIRED)
    add_subdirectory(python)
endif()
