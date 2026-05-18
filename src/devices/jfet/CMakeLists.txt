add_library(jfet_obj OBJECT
    jfet_setup.cpp
    jfet_load.cpp
    jfet_temp.cpp
    jfet_param.cpp
    jfet_mpar.cpp
    jfet_devsup.cpp
    jfet_shim.cpp
    jfet_device.cpp
    jfet_model_card.cpp
    jfet_factory.cpp
)
target_include_directories(jfet_obj PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include)
