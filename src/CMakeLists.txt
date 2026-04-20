add_subdirectory(devices/bsim4v7)
add_subdirectory(devices/bjt)
add_subdirectory(devices/jfet)
add_subdirectory(devices/dio)
add_subdirectory(devices/vbic)

add_library(neospice_lib
    core/types.cpp
    core/matrix.cpp
    core/klu_solver.cpp
    core/circuit.cpp
    core/newton.cpp
    core/convergence.cpp
    core/dc.cpp
    core/transient.cpp
    core/ac.cpp
    core/noise.cpp
    core/fourier.cpp
    core/measure.cpp
    core/timestep.cpp
    devices/resistor.cpp
    devices/switch.cpp
    devices/vsource.cpp
    devices/isource.cpp
    devices/vcvs.cpp
    devices/vccs.cpp
    devices/vcvs_nonlinear.cpp
    devices/vccs_nonlinear.cpp
    devices/ccvs.cpp
    devices/cccs.cpp
    devices/capacitor.cpp
    devices/inductor.cpp
    devices/coupled_inductor.cpp
    devices/tline.cpp

    parser/tokenizer.cpp
    parser/expression.cpp
    parser/model_cards.cpp
    parser/subcircuit_expand.cpp
    parser/netlist_parser.cpp
    api/neospice.cpp
    output/raw_writer.cpp
    output/output.cpp
    $<TARGET_OBJECTS:bsim4v7_obj>
    $<TARGET_OBJECTS:bjt_obj>
    $<TARGET_OBJECTS:jfet_obj>
    $<TARGET_OBJECTS:dio_obj>
    $<TARGET_OBJECTS:vbic_obj>
)

target_include_directories(neospice_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(neospice_lib PRIVATE
    ${SLEEF_INCLUDE_DIR}
)

target_link_libraries(neospice_lib PUBLIC
    SuiteSparse::KLU
    SuiteSparse::AMD
    SuiteSparse::COLAMD
    SuiteSparse::BTF
    SuiteSparse::SuiteSparseConfig
    PkgConfig::OPENBLAS
    ${SLEEF_LIBRARY}
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(neospice_lib PUBLIC OpenMP::OpenMP_CXX)
endif()
