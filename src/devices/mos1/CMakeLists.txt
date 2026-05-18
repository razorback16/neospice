add_library(mos1_obj OBJECT
    mos1_setup.cpp
    mos1_load.cpp
    mos1_temp.cpp
    mos1_param.cpp
    mos1_mpar.cpp
    mos1_devsup.cpp
    mos1_shim.cpp
    mos1_device.cpp
    mos1_model_card.cpp
    mos1_factory.cpp
)
target_include_directories(mos1_obj PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include)
