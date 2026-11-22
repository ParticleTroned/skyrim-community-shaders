set(_vs_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/volumetric-shadows")
set(_vs_test_headers
    "${_vs_test_dir}/volumetric_shadows_helpers.h"
    "${_vs_test_dir}/volumetric_shadows_resources.h"
    "${_vs_test_dir}/volumetric_shadows_under_test.h"
)
add_custom_command(
    OUTPUT ${_vs_test_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_vs_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_volumetric_shadows.cmake"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/src/Features/VolumetricShadows.cpp"
        "${PROJECT_SOURCE_DIR}/src/Features/VolumetricShadows.h"
        "${PROJECT_SOURCE_DIR}/src/Utils/D3D.cpp"
        "${PROJECT_SOURCE_DIR}/tests/extract_source_region.cmake"
        "${PROJECT_SOURCE_DIR}/tests/extract_volumetric_shadows.cmake"
    VERBATIM
)
add_d3d_shader_test(volumetric_shadows_test VolumetricShadows tests/volumetric_shadows_test.cpp)
target_sources(volumetric_shadows_test PRIVATE ${_vs_test_headers})
target_include_directories(volumetric_shadows_test PRIVATE "${_vs_test_dir}")
target_link_libraries(volumetric_shadows_test PRIVATE windowsapp.lib)
target_compile_definitions(volumetric_shadows_test PRIVATE NOMINMAX)
set_tests_properties(VolumetricShadows PROPERTIES TIMEOUT 180)

set(_lighting_shadow_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/lighting-shadow-sources")
set(_lighting_shadow_shader "${_lighting_shadow_dir}/lighting_shadow_sources.hlsl")
add_custom_command(
    OUTPUT "${_lighting_shadow_shader}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_lighting_shadow_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_lighting_shadow_sources.cmake"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/package/Shaders/Lighting.hlsl"
        "${PROJECT_SOURCE_DIR}/package/Shaders/Common/LightingFlags.hlsli"
        "${PROJECT_SOURCE_DIR}/package/Shaders/Common/Permutation.hlsli"
        "${PROJECT_SOURCE_DIR}/features/Light Limit Fix/Shaders/LightLimitFix/Common.hlsli"
        "${PROJECT_SOURCE_DIR}/features/Light Limit Fix/Shaders/LightLimitFix/LightLimitFix.hlsli"
        "${PROJECT_SOURCE_DIR}/tests/extract_source_region.cmake"
        "${PROJECT_SOURCE_DIR}/tests/extract_lighting_shadow_sources.cmake"
        "${PROJECT_SOURCE_DIR}/tests/lighting_shadow_sources.hlsl.in"
    VERBATIM
)
add_d3d_shader_test(lighting_shadow_sources_test LightingShadowSources tests/lighting_shadow_sources_test.cpp)
set_source_files_properties("${_lighting_shadow_shader}" PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(lighting_shadow_sources_test PRIVATE "${_lighting_shadow_shader}")
target_compile_definitions(lighting_shadow_sources_test PRIVATE
    LIGHTING_SHADOW_TEST_SHADER=L"${_lighting_shadow_shader}" NOMINMAX)
set_tests_properties(LightingShadowSources PROPERTIES TIMEOUT 180)

add_d3d_shader_test(contact_shadow_membership_test ContactShadowMembership tests/contact_shadow_membership_test.cpp)
target_compile_definitions(contact_shadow_membership_test PRIVATE NOMINMAX)
set_tests_properties(ContactShadowMembership PROPERTIES TIMEOUT 180)
