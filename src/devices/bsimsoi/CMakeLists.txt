add_library(bsimsoi_obj OBJECT
    bsimsoi_setup.cpp
    bsimsoi_load.cpp
    bsimsoi_temp.cpp
    bsimsoi_check.cpp
    bsimsoi_mpar.cpp
    bsimsoi_param.cpp
    bsimsoi_devsup.cpp
    bsimsoi_shim.cpp
    bsimsoi_device.cpp
    bsimsoi_model_card.cpp
)
target_include_directories(bsimsoi_obj PUBLIC ${CMAKE_SOURCE_DIR}/src)
