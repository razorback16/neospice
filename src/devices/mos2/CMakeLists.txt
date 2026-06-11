add_library(mos2_obj OBJECT
    mos2_setup.cpp
    mos2_load.cpp
    mos2_temp.cpp
    mos2_param.cpp
    mos2_mpar.cpp
    mos2_devsup.cpp
    mos2_shim.cpp
    mos2_device.cpp
    mos2_model_card.cpp
)
target_include_directories(mos2_obj PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
