include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(neospice_tests
    unit/test_smoke.cpp
    unit/test_matrix.cpp
    unit/test_klu_solver.cpp
    unit/test_resistor.cpp
    unit/test_vsource.cpp
    unit/test_isource.cpp
    unit/test_capacitor.cpp
    unit/test_inductor.cpp
    unit/test_gear.cpp
    unit/test_timestep.cpp
    unit/test_diode.cpp
    unit/test_circuit.cpp
    unit/test_dc.cpp
    unit/test_convergence.cpp
    unit/test_tokenizer.cpp
    unit/test_parser.cpp
    unit/test_transient.cpp
    unit/test_ac.cpp
    unit/test_ngspice_runner.cpp
    unit/test_comparator.cpp
    unit/test_api.cpp
    unit/test_raw_writer.cpp
    unit/test_ngspice_compare.cpp
    unit/test_bsim4v7_ucb_setup.cpp
    unit/test_bsim4v7_shim_load_fields.cpp
    unit/test_device_state_interface.cpp
    unit/test_bsim4v7_shim_matrix.cpp
    framework/ngspice_runner.cpp
    framework/comparator.cpp
    $<TARGET_OBJECTS:bsim4v7_obj>
)

target_link_libraries(neospice_tests PRIVATE
    gtest_main
    neospice_lib
)

target_include_directories(neospice_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(neospice_tests PRIVATE
    NGSPICE_BINARY="/usr/bin/ngspice"
    TEST_CIRCUITS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/circuits"
)

gtest_discover_tests(neospice_tests)
