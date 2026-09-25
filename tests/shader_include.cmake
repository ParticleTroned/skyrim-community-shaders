set(_shader_include_test_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shader-include"
)
set(_tracking_include_header
    "${_shader_include_test_dir}/tracking_include_under_test.h"
)
add_custom_command(
    OUTPUT "${_tracking_include_header}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_shader_include_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_shader_include.cmake"
    DEPENDS
        src/ShaderCache.cpp
        tests/extract_shader_include.cmake
        tests/extract_source_region.cmake
    VERBATIM
)
add_controller_test(
    shader_include_test
    ShaderInclude
    tests/shader_include_test.cpp
)
target_sources(shader_include_test PRIVATE "${_tracking_include_header}")
target_include_directories(
    shader_include_test
    PRIVATE "${_shader_include_test_dir}"
)
target_link_libraries(shader_include_test PRIVATE d3dcompiler.lib)
set_tests_properties(ShaderInclude PROPERTIES TIMEOUT 120)

foreach(
    _target
    IN
    ITEMS
        shader_include_test
        ambient_balance_shader_test
        skylighting_probe_slice_test
        motion_sharpening_runtime_compile_test
        volumetric_lighting_composite_test
)
    target_sources(${_target} PRIVATE src/Utils/ShaderInclude.cpp)
    target_link_libraries(${_target} PRIVATE spdlog::spdlog)
endforeach()
