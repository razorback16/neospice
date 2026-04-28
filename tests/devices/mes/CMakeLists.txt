add_executable(test_mes_compare
    test_mes_compare.cpp
    ${CMAKE_SOURCE_DIR}/tests/framework/ngspice_runner.cpp
    ${CMAKE_SOURCE_DIR}/tests/framework/comparator.cpp
)

target_link_libraries(test_mes_compare PRIVATE gtest_main neospice_lib ${NGSPICE_LIBRARIES})
target_include_directories(test_mes_compare PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${NGSPICE_INCLUDE_DIRS})
target_compile_definitions(test_mes_compare PRIVATE
    TEST_CIRCUITS_DIR="${CMAKE_SOURCE_DIR}/tests/circuits"
)

gtest_discover_tests(test_mes_compare)
