add_library(mos3_obj OBJECT
    mos3_setup.cpp
    mos3_load.cpp
    mos3_temp.cpp
    mos3_param.cpp
    mos3_mpar.cpp
    mos3_devsup.cpp
    mos3_shim.cpp
    mos3_device.cpp
    mos3_model_card.cpp
)
target_include_directories(mos3_obj PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
