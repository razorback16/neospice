add_library(hfet1_obj OBJECT
    hfet1_setup.cpp
    hfet1_load.cpp
    hfet1_temp.cpp
    hfet1_param.cpp
    hfet1_mpar.cpp
    hfet1_devsup.cpp
    hfet1_shim.cpp
    hfet1_device.cpp
    hfet1_model_card.cpp
    hfet1_factory.cpp
)
target_include_directories(hfet1_obj PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include)
