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

add_d3d_shader_test(
    adaptive_balance_color_shader_test
    AdaptiveBalanceColorShader
    tests/adaptive_balance_color_shader_test.cpp
)
target_link_libraries(adaptive_balance_color_shader_test PRIVATE nlohmann_json::nlohmann_json)
set_tests_properties(AdaptiveBalanceColorShader PROPERTIES TIMEOUT 180)

set(_forward_composition_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/forward-reflection-composition"
)
set(_forward_composition_shader
    "${_forward_composition_dir}/forward_reflection_composition.hlsl"
)
add_custom_command(
    OUTPUT "${_forward_composition_shader}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_forward_composition_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_forward_reflection_composition.cmake"
    DEPENDS
        package/Shaders/Lighting.hlsl
        package/Shaders/ISSAOComposite.hlsl
        tests/extract_source_region.cmake
        tests/extract_forward_reflection_composition.cmake
        tests/forward_reflection_composition.hlsl.in
    VERBATIM
)
set_source_files_properties(
    "${_forward_composition_shader}"
    PROPERTIES HEADER_FILE_ONLY TRUE
)
target_sources(
    adaptive_balance_color_shader_test
    PRIVATE "${_forward_composition_shader}"
)
target_compile_definitions(
    adaptive_balance_color_shader_test
    PRIVATE FORWARD_COMPOSITION_TEST_SHADER=L"${_forward_composition_shader}"
)
add_test(
    NAME ForwardReflectionComposition
    COMMAND adaptive_balance_color_shader_test --reflections
)
set_tests_properties(
    ForwardReflectionComposition
    PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        TIMEOUT 60
        LABELS ControllerTests
)
add_test(
    NAME ForwardFogComposition
    COMMAND adaptive_balance_color_shader_test --fog
)
set_tests_properties(
    ForwardFogComposition
    PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        TIMEOUT 60
        LABELS ControllerTests
)

add_d3d_shader_test(
    foveated_blend_curve_shader_test
    FoveatedBlendCurveShader
    tests/foveated_blend_curve_shader_test.cpp
)

add_d3d_shader_test(
    foveated_mask_overlay_shader_test
    FoveatedMaskOverlayShader
    tests/foveated_mask_overlay_shader_test.cpp
)

set(_hair_indirect_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/hair-indirect")
set(_hair_indirect_shader "${_hair_indirect_dir}/hair_indirect.hlsl")
add_custom_command(
    OUTPUT "${_hair_indirect_shader}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_hair_indirect_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_hair_indirect.cmake"
    DEPENDS
        package/Shaders/Lighting.hlsl
        package/Shaders/Common/LightingEval.hlsli
        tests/extract_source_region.cmake
        tests/extract_hair_indirect.cmake
        tests/hair_indirect.hlsl.in
    VERBATIM
)
set_source_files_properties(
    "${_hair_indirect_shader}"
    PROPERTIES HEADER_FILE_ONLY TRUE
)
add_d3d_shader_test(
    hair_indirect_shader_test
    HairIndirectShader
    tests/hair_indirect_shader_test.cpp
)
target_sources(hair_indirect_shader_test PRIVATE "${_hair_indirect_shader}")
target_compile_definitions(
    hair_indirect_shader_test
    PRIVATE HAIR_INDIRECT_TEST_SHADER=L"${_hair_indirect_shader}"
)

foreach(
    _target
    IN
    ITEMS
        shader_include_test
        adaptive_balance_color_shader_test
        foveated_blend_curve_shader_test
        foveated_mask_overlay_shader_test
        hair_indirect_shader_test
        ambient_balance_shader_test
        pbr_grass_shader_test
        vr_depth_encode_shader_test
        skylighting_probe_slice_test
        motion_sharpening_runtime_compile_test
        volumetric_lighting_composite_test
)
    target_sources(${_target} PRIVATE src/Utils/ShaderInclude.cpp)
    target_link_libraries(${_target} PRIVATE spdlog::spdlog)
endforeach()
