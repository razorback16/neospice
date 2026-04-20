add_library(bsim3_obj OBJECT
    bsim3_setup.cpp
    bsim3_load.cpp
    bsim3_temp.cpp
    bsim3_check.cpp
    bsim3_mpar.cpp
    bsim3_param.cpp
    bsim3_devsup.cpp
    bsim3_shim.cpp
    bsim3_device.cpp
)
target_include_directories(bsim3_obj PUBLIC ${CMAKE_SOURCE_DIR}/src)
