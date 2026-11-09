set(_vl_depth_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/volumetric-depth")
add_custom_command(
    OUTPUT
        "${_vl_depth_dir}/depth.h"
        "${_vl_depth_dir}/volumetric_depth_methods.h"
        "${_vl_depth_dir}/volumetric_depth_hook.h"
        "${_vl_depth_dir}/volumetric_depth_apply.h"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_FILE=${_vl_depth_dir}/depth.h" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_volumetric_lighting_depth.cmake"
    DEPENDS
        src/Features/VolumetricLighting.cpp src/Hooks.cpp
        tests/extract_source_region.cmake tests/extract_volumetric_lighting_depth.cmake
    VERBATIM
)
add_d3d_shader_test(volumetric_lighting_depth_test VolumetricLightingDepth tests/volumetric_lighting_depth_test.cpp)
target_sources(volumetric_lighting_depth_test PRIVATE "${_vl_depth_dir}/depth.h")
target_include_directories(volumetric_lighting_depth_test PRIVATE "${_vl_depth_dir}")
