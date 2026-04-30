add_library(mes_obj OBJECT
    mes_setup.cpp
    mes_load.cpp
    mes_temp.cpp
    mes_param.cpp
    mes_mpar.cpp
    mes_devsup.cpp
    mes_shim.cpp
    mes_device.cpp
    mes_model_card.cpp
    mes_factory.cpp
)
target_include_directories(mes_obj PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
