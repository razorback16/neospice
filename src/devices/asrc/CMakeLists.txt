add_library(asrc_obj OBJECT
    expression_ast.cpp
    asrc_device.cpp
)

target_include_directories(asrc_obj PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include
)
