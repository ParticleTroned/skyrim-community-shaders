set(_depth_encode_test_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/vr-depth-encode"
)
set(_depth_encode_headers)
foreach(_name IN ITEMS scope_exit identity texture_desc validation)
    list(
        APPEND _depth_encode_headers
        "${_depth_encode_test_dir}/depth_encode_${_name}.h"
    )
endforeach()
add_custom_command(
    OUTPUT ${_depth_encode_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_depth_encode_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_vr_depth_encode.cmake"
    DEPENDS
        src/Features/Upscaling.cpp
        tests/extract_vr_depth_encode.cmake
        tests/extract_source_region.cmake
    VERBATIM
)
add_d3d_shader_test(vr_depth_encode_shader_test VRDepthEncodeShader tests/vr_depth_encode_shader_test.cpp)
target_sources(vr_depth_encode_shader_test PRIVATE ${_depth_encode_headers})
target_include_directories(
    vr_depth_encode_shader_test
    PRIVATE "${_depth_encode_test_dir}"
)
