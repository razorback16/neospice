nanobind_add_module(_core bindings.cpp)
target_link_libraries(_core PRIVATE neospice_lib)
install(TARGETS _core DESTINATION neospice)
