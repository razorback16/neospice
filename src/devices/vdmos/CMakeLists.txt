add_library(vdmos_obj OBJECT
    vdmos_setup.cpp
    vdmos_load.cpp
    vdmos_temp.cpp
    vdmos_param.cpp
    vdmos_mpar.cpp
    vdmos_devsup.cpp
    vdmos_shim.cpp
    vdmos_device.cpp
    vdmos_model_card.cpp
    vdmos_factory.cpp
)
target_include_directories(vdmos_obj PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
