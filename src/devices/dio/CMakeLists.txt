add_library(dio_obj OBJECT
    dio_setup.cpp
    dio_load.cpp
    dio_temp.cpp
    dio_param.cpp
    dio_mpar.cpp
    dio_devsup.cpp
    dio_shim.cpp
    dio_device.cpp
)
target_include_directories(dio_obj PUBLIC ${CMAKE_SOURCE_DIR}/src)
