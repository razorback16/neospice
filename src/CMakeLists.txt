# Auto-discover device subdirectories (each must define <dirname>_obj OBJECT library)
file(GLOB _device_dirs RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/devices
     ${CMAKE_CURRENT_SOURCE_DIR}/devices/*/CMakeLists.txt)
set(NEOSPICE_DEVICE_OBJECTS "")
set(NEOSPICE_DEVICE_NAMES "")
foreach(_cmake_file IN LISTS _device_dirs)
    get_filename_component(_dev_dir ${_cmake_file} DIRECTORY)
    add_subdirectory(devices/${_dev_dir})
    target_include_directories(${_dev_dir}_obj PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/include
    )
    list(APPEND NEOSPICE_DEVICE_OBJECTS $<TARGET_OBJECTS:${_dev_dir}_obj>)
    list(APPEND NEOSPICE_DEVICE_NAMES ${_dev_dir})
endforeach()
set(NEOSPICE_DEVICE_NAMES "${NEOSPICE_DEVICE_NAMES}" PARENT_SCOPE)

add_library(neospice_lib
    core/types.cpp
    core/matrix.cpp
    core/amd.cpp
    core/btf.cpp
    core/matching.cpp
    core/neo_solver.cpp
    core/circuit.cpp
    core/newton.cpp
    core/convergence.cpp
    core/dc.cpp
    core/topology.cpp
    core/tf.cpp
    core/sens.cpp
    core/transient.cpp
    core/ac.cpp
    core/noise.cpp
    core/pz.cpp
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
    devices/ccvs_nonlinear.cpp
    devices/cccs_nonlinear.cpp
    devices/capacitor.cpp
    devices/inductor.cpp
    devices/coupled_inductor.cpp
    devices/tline.cpp
    devices/ltra.cpp
    devices/device_registry.cpp
    devices/mosfet_common.cpp
    devices/bjt_common.cpp
    devices/jfet_common.cpp
    devices/hfet_common.cpp

    parser/tokenizer.cpp
    parser/expression.cpp
    parser/model_cards.cpp
    parser/subcircuit_expand.cpp
    parser/netlist_parser.cpp
    api/neospice.cpp
    api/circuit_typed.cpp
    api/circuit_include.cpp
    api/measure_utils.cpp
    output/raw_writer.cpp
    output/output.cpp
    ${NEOSPICE_DEVICE_OBJECTS}
)

target_include_directories(neospice_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
)

target_include_directories(neospice_lib PRIVATE
    ${SLEEF_INCLUDE_DIR}
)

target_link_libraries(neospice_lib PUBLIC
    PkgConfig::OPENBLAS
    ${SLEEF_LIBRARY}
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(neospice_lib PUBLIC OpenMP::OpenMP_CXX)
endif()
