add_library(jfet2_obj OBJECT
    jfet2_setup.cpp
    jfet2_load.cpp
    jfet2_temp.cpp
    jfet2_param.cpp
    jfet2_mpar.cpp
    jfet2_devsup.cpp
    jfet2_psmodel.cpp
    jfet2_shim.cpp
    jfet2_device.cpp
    jfet2_model_card.cpp
)
target_include_directories(jfet2_obj PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
