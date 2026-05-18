add_library(bsim3v32_obj OBJECT
    bsim3v32_setup.cpp
    bsim3v32_load.cpp
    bsim3v32_temp.cpp
    bsim3v32_check.cpp
    bsim3v32_param.cpp
    bsim3v32_mpar.cpp
    bsim3v32_devsup.cpp
    bsim3v32_shim.cpp
    bsim3v32_device.cpp
    bsim3v32_model_card.cpp
    bsim3v32_factory.cpp
)
target_include_directories(bsim3v32_obj PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include)
