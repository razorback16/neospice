add_library(vbic_obj OBJECT
    vbic_setup.cpp
    vbic_load.cpp
    vbic_temp.cpp
    vbic_param.cpp
    vbic_mpar.cpp
    vbic_devsup.cpp
    vbic_shim.cpp
    vbic_device.cpp
)
target_include_directories(vbic_obj PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
