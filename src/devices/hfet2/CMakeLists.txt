add_library(hfet2_obj OBJECT
    hfet2_setup.cpp
    hfet2_load.cpp
    hfet2_temp.cpp
    hfet2_param.cpp
    hfet2_mpar.cpp
    hfet2_devsup.cpp
    hfet2_shim.cpp
    hfet2_device.cpp
    hfet2_model_card.cpp
    hfet2_factory.cpp
)
target_include_directories(hfet2_obj PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include)
