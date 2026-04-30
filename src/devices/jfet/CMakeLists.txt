add_library(jfet_obj OBJECT
    jfet_setup.cpp
    jfet_load.cpp
    jfet_temp.cpp
    jfet_param.cpp
    jfet_mpar.cpp
    jfet_devsup.cpp
    jfet_shim.cpp
    jfet_device.cpp
)
target_include_directories(jfet_obj PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
