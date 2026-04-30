option(NEOSPICE_DEBUG_COMPARE "Print margin diagnostics from compare functions" OFF)
if(NEOSPICE_DEBUG_COMPARE)
    add_compile_definitions(NEOSPICE_DEBUG_COMPARE)
endif()

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NGSPICE REQUIRED ngspice)

add_executable(neospice_tests
    unit/test_smoke.cpp
    unit/test_matrix.cpp
    unit/test_amd.cpp
    unit/test_btf.cpp
    unit/test_neo_solver.cpp
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
    unit/test_parser_mosfet.cpp
    unit/test_transient.cpp
    unit/test_ac.cpp
    unit/test_ngspice_runner.cpp
    unit/test_comparator.cpp
    unit/test_oscillator_comparator.cpp
    unit/test_api.cpp
    unit/test_sim_status.cpp
    unit/test_sim_status_enriched.cpp
    unit/test_raw_writer.cpp
    unit/test_ngspice_compare.cpp
    unit/test_bsim4v7_ucb_setup.cpp
    unit/test_bsim4v7_shim_load_fields.cpp
    unit/test_device_state_interface.cpp
    unit/test_bsim4v7_shim_matrix.cpp
    unit/test_bsim4v7_ucb_load.cpp
    unit/test_circuit_state_allocation.cpp
    unit/test_circuit_state_rotation.cpp
    unit/test_newton_init_mode_flip.cpp
    unit/test_bsim4v7_niintegrate.cpp
    unit/test_sim_options_temp_plumbing.cpp
    unit/test_internal_nodes.cpp
    unit/test_bsim4v7_ac.cpp
    unit/test_bsim4v7_trunc.cpp
    unit/test_bsim4v7_cvtest.cpp
    unit/test_bsim4v7_query.cpp
    unit/test_vcvs.cpp
    unit/test_vccs.cpp
    unit/test_ccvs.cpp
    unit/test_cccs.cpp
    unit/test_dc_sweep.cpp
    unit/test_save_filter.cpp
    unit/test_expression.cpp
    unit/test_expression_random.cpp
    unit/test_subcircuit.cpp
    unit/test_subcircuit_expand.cpp
    unit/test_include.cpp
    unit/test_lib.cpp
    unit/test_pdk_integration.cpp
    unit/test_bjt.cpp
    unit/test_coupled_inductor.cpp
    unit/test_tline.cpp
    unit/test_ltra.cpp
    unit/test_jfet.cpp
    unit/test_switch.cpp
    unit/test_noise.cpp
    unit/test_noise_flicker.cpp
    unit/test_noise_temp.cpp
    unit/test_bjt_jfet_noise.cpp
    unit/test_bsim4v7_noise.cpp
    unit/test_fourier.cpp
    unit/test_measure.cpp
    unit/test_output.cpp
    unit/test_options.cpp
    unit/test_nonlinear_sources.cpp
    unit/test_asrc.cpp
    unit/test_ths4131.cpp
    unit/test_tf.cpp
    unit/test_sens.cpp
    unit/test_pz.cpp
    unit/test_step.cpp
    unit/test_ringing.cpp
    unit/test_circuit_state.cpp
    unit/test_circuit_builder.cpp
    unit/test_introspection.cpp
    unit/test_handles.cpp
    unit/test_result_handles.cpp
    unit/test_circuit_typed.cpp
    unit/test_kicad_lm358_ns.cpp
    unit/test_kicad_bc547a.cpp
    unit/test_kicad_opa1632.cpp
    framework/ngspice_runner.cpp
    framework/comparator.cpp
)

target_link_libraries(neospice_tests PRIVATE
    gtest_main
    neospice_lib
    ${NGSPICE_LIBRARIES}
)

target_include_directories(neospice_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${NGSPICE_INCLUDE_DIRS}
)

target_compile_definitions(neospice_tests PRIVATE
    TEST_CIRCUITS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/circuits"
)

gtest_discover_tests(neospice_tests)

# Per-device comparison tests
add_subdirectory(devices/asrc)
add_subdirectory(devices/bjt)
add_subdirectory(devices/bsim3)
add_subdirectory(devices/bsim4v7)
add_subdirectory(devices/dio)
add_subdirectory(devices/hfet1)
add_subdirectory(devices/hfet2)
add_subdirectory(devices/ltra)
add_subdirectory(devices/mes)
add_subdirectory(devices/mos1)
add_subdirectory(devices/jfet2)
add_subdirectory(devices/mos3)
add_subdirectory(devices/mos9)
add_subdirectory(devices/vbic)
add_subdirectory(devices/bsim3v32)
add_subdirectory(devices/hisim2)
add_subdirectory(devices/hisimhv)
add_subdirectory(devices/bsimsoi)

# Performance benchmarks (requires: apt install libngspice0-dev)
add_executable(bench_ths4131
    bench/bench_ths4131.cpp
)

target_link_libraries(bench_ths4131 PRIVATE neospice_lib ${NGSPICE_LIBRARIES})
target_include_directories(bench_ths4131 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${NGSPICE_INCLUDE_DIRS})
target_compile_definitions(bench_ths4131 PRIVATE
    TEST_CIRCUITS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/circuits"
)

add_executable(bench_comprehensive
    bench/bench_comprehensive.cpp
)
target_link_libraries(bench_comprehensive PRIVATE neospice_lib ${NGSPICE_LIBRARIES})
target_include_directories(bench_comprehensive PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${NGSPICE_INCLUDE_DIRS})
target_compile_definitions(bench_comprehensive PRIVATE
    TEST_CIRCUITS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/circuits"
)

add_executable(bench_neo_solver
    bench/bench_neo_solver.cpp
)
target_link_libraries(bench_neo_solver PRIVATE neospice_lib)
target_include_directories(bench_neo_solver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

