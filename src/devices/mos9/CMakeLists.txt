add_library(mos9_obj OBJECT
    mos9_setup.cpp
    mos9_load.cpp
    mos9_temp.cpp
    mos9_param.cpp
    mos9_mpar.cpp
    mos9_devsup.cpp
    mos9_shim.cpp
    mos9_device.cpp
    mos9_model_card.cpp
)
target_include_directories(mos9_obj PUBLIC ${CMAKE_SOURCE_DIR}/src)
