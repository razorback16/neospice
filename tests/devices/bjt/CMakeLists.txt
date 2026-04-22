add_executable(test_bjt_compare
    test_bjt_compare.cpp
    ${CMAKE_SOURCE_DIR}/tests/framework/ngspice_runner.cpp
    ${CMAKE_SOURCE_DIR}/tests/framework/comparator.cpp
)

target_link_libraries(test_bjt_compare PRIVATE gtest_main neospice_lib)
target_include_directories(test_bjt_compare PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(test_bjt_compare PRIVATE
    NGSPICE_BINARY="/usr/bin/ngspice"
    TEST_CIRCUITS_DIR="${CMAKE_SOURCE_DIR}/tests/circuits"
)

gtest_discover_tests(test_bjt_compare)
