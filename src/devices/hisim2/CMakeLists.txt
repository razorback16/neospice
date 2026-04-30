add_library(hisim2_obj OBJECT
    hisim2_setup.cpp
    hisim2_load.cpp
    hisim2_temp.cpp
    hisim2_mpar.cpp
    hisim2_param.cpp
    hisim2_eval.cpp
    hisim2_devsup.cpp
    hisim2_shim.cpp
    hisim2_device.cpp
    hisim2_model_card.cpp
    hisim2_factory.cpp
)
target_include_directories(hisim2_obj PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
