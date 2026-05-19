cmake_minimum_required(VERSION 3.20)
project(neospice LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# OpenBLAS via pkg-config
find_package(PkgConfig REQUIRED)
if(APPLE)
    # Homebrew installs openblas as keg-only; add its pkg-config path
    execute_process(
        COMMAND brew --prefix openblas
        OUTPUT_VARIABLE _OPENBLAS_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_OPENBLAS_PREFIX)
        set(ENV{PKG_CONFIG_PATH} "${_OPENBLAS_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()

endif()
pkg_check_modules(OPENBLAS REQUIRED IMPORTED_TARGET openblas)

# ngspice (for comparison tests and benchmarks)
# Prefer locally-built libngspice from scripts/build-libngspice-mac.sh
set(_LOCAL_NGSPICE "${PROJECT_SOURCE_DIR}/third_party/libngspice")
if(EXISTS "${_LOCAL_NGSPICE}/include/ngspice/sharedspice.h")
    set(NGSPICE_FOUND TRUE)
    set(NGSPICE_INCLUDE_DIRS "${_LOCAL_NGSPICE}/include" CACHE PATH "ngspice include dirs")
    set(NGSPICE_LIBRARY_DIRS "${_LOCAL_NGSPICE}/lib" CACHE PATH "ngspice library dirs")
    set(NGSPICE_LIBRARIES "ngspice" CACHE STRING "ngspice libraries")
    list(APPEND CMAKE_BUILD_RPATH "${_LOCAL_NGSPICE}/lib")
else()
    pkg_check_modules(NGSPICE REQUIRED ngspice)
endif()

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

option(NEOSPICE_BUILD_CLI "Build neospice CLI executable" ON)
if(NEOSPICE_BUILD_CLI)
    add_executable(neospice cli/main.cpp)
    target_link_libraries(neospice PRIVATE neospice_lib)
endif()

# Python bindings (opt-in)
option(NEOSPICE_BUILD_PYTHON "Build Python bindings" OFF)
if(NEOSPICE_BUILD_PYTHON)
    set_target_properties(neospice_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)
    foreach(_dev IN LISTS NEOSPICE_DEVICE_NAMES)
        set_target_properties(${_dev}_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endforeach()

    find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
    find_package(nanobind CONFIG REQUIRED)
    add_subdirectory(python)
endif()
