# UCB BSIM 4.7.0 Z-port (Phase 1a scaffolding).
# Sources are added task-by-task. Not yet linked into the top-level library.
add_library(bsim4v7_obj OBJECT
    bsim4v7_shim.cpp
)
target_include_directories(bsim4v7_obj PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(bsim4v7_obj PUBLIC
    neospice_lib  # for NumericMatrix / SparsityBuilder
)
