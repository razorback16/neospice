add_library(bsim4v7_obj OBJECT
    bsim4v7_setup.cpp
    bsim4v7_load.cpp
    bsim4v7_temp.cpp
    bsim4v7_check.cpp
    bsim4v7_mpar.cpp
    bsim4v7_param.cpp
    bsim4v7_geo.cpp
    bsim4v7_devsup.cpp
    bsim4v7_shim.cpp
    bsim4v7_device.cpp
)
target_include_directories(bsim4v7_obj PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
