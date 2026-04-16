add_library(cudaspice_lib
    core/types.cpp
    core/matrix.cpp
    core/klu_solver.cpp
    devices/resistor.cpp
    devices/vsource.cpp
    devices/isource.cpp
    devices/capacitor.cpp
    devices/inductor.cpp
    devices/diode.cpp
)

target_include_directories(cudaspice_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(cudaspice_lib PRIVATE
    ${SLEEF_INCLUDE_DIR}
)

target_link_libraries(cudaspice_lib PUBLIC
    SuiteSparse::KLU
    SuiteSparse::AMD
    SuiteSparse::COLAMD
    SuiteSparse::BTF
    SuiteSparse::SuiteSparseConfig
    PkgConfig::OPENBLAS
    ${SLEEF_LIBRARY}
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(cudaspice_lib PUBLIC OpenMP::OpenMP_CXX)
endif()
