set(_llf_shader_lifecycle_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/llf-shader-lifecycle")
set(_llf_shader_lifecycle_headers)
foreach(_header IN ITEMS lifecycle status device relatch draw)
    list(APPEND _llf_shader_lifecycle_headers
        "${_llf_shader_lifecycle_dir}/llf_shader_${_header}_under_test.h")
endforeach()
add_custom_command(
    OUTPUT ${_llf_shader_lifecycle_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_llf_shader_lifecycle_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_light_limit_fix_shader_lifecycle.cmake"
    DEPENDS
        src/Features/LightLimitFix.cpp
        tests/extract_light_limit_fix_shader_lifecycle.cmake
        tests/extract_source_region.cmake
    VERBATIM
)
add_d3d_shader_test(
    light_limit_fix_shader_lifecycle_test LightLimitFixShaderLifecycle
    tests/light_limit_fix_shader_lifecycle_test.cpp
)
target_sources(light_limit_fix_shader_lifecycle_test PRIVATE ${_llf_shader_lifecycle_headers})
target_include_directories(light_limit_fix_shader_lifecycle_test PRIVATE "${_llf_shader_lifecycle_dir}")
target_link_libraries(light_limit_fix_shader_lifecycle_test PRIVATE spdlog::spdlog xxHash::xxhash windowsapp.lib)
target_compile_definitions(light_limit_fix_shader_lifecycle_test PRIVATE NOMINMAX)
