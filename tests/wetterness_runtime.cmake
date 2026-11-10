set(_wetterness_runtime_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/wetterness-runtime")
set(_wetterness_runtime_headers)
foreach(part members state constants weather shader_settings packing sanitization reset activity update)
    list(APPEND _wetterness_runtime_headers "${_wetterness_runtime_dir}/wetterness_${part}.h")
endforeach()
add_custom_command(
    OUTPUT ${_wetterness_runtime_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_wetterness_runtime_dir}"
        -P "${PROJECT_SOURCE_DIR}/tests/extract_wetterness_runtime.cmake"
    DEPENDS src/Features/Wetterness.h src/Features/Wetterness.cpp
        tests/extract_wetterness_runtime.cmake tests/extract_source_region.cmake
    VERBATIM
)
add_controller_test(wetterness_runtime_test WetternessRuntime tests/wetterness_runtime_test.cpp)
target_sources(wetterness_runtime_test PRIVATE ${_wetterness_runtime_headers})
target_include_directories(wetterness_runtime_test PRIVATE "${_wetterness_runtime_dir}")
