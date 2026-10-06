add_executable(neospice_wasm bindings.cpp)
target_link_libraries(neospice_wasm PRIVATE neospice_lib)
# The library's exception handlers must also survive compilation.
add_compile_options(-fexceptions)
target_compile_options(neospice_lib PRIVATE -fexceptions)
foreach(device IN LISTS NEOSPICE_DEVICE_NAMES)
    target_compile_options(${device}_obj PRIVATE -fexceptions)
endforeach()
target_compile_options(neospice_wasm PRIVATE -fexceptions)
target_link_options(neospice_wasm PRIVATE
    --bind --no-entry -fexceptions
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createNeospiceModule
    -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web,worker,node
    -sFILESYSTEM=1 -sDISABLE_EXCEPTION_CATCHING=0
)
set_target_properties(neospice_wasm PROPERTIES OUTPUT_NAME neospice SUFFIX .mjs)
foreach(asset neospice-api.mjs neospice-api.d.mts index.html demo.mjs worker.mjs)
    configure_file(${asset} ${asset} COPYONLY)
endforeach()
