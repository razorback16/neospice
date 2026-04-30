add_library(bjt_obj OBJECT
    bjt_setup.cpp
    bjt_load.cpp
    bjt_temp.cpp
    bjt_param.cpp
    bjt_mpar.cpp
    bjt_devsup.cpp
    bjt_shim.cpp
    bjt_device.cpp
    bjt_model_card.cpp
    bjt_factory.cpp
)
target_include_directories(bjt_obj PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
