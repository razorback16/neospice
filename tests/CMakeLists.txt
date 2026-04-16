include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(cudaspice_tests
    unit/test_smoke.cpp
    unit/test_matrix.cpp
)

target_link_libraries(cudaspice_tests PRIVATE
    gtest_main
    cudaspice_lib
)

target_compile_definitions(cudaspice_tests PRIVATE
    NGSPICE_BINARY="/usr/bin/ngspice"
    TEST_CIRCUITS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/circuits"
)

gtest_discover_tests(cudaspice_tests)
