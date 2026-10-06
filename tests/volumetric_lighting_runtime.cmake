set(_vl_runtime_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/volumetric-runtime")
add_custom_command(
    OUTPUT "${_vl_runtime_dir}/runtime.h" "${_vl_runtime_dir}/vl_runtime_settings.h"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_FILE=${_vl_runtime_dir}/runtime.h" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_volumetric_lighting_runtime.cmake"
    DEPENDS
        src/Features/VolumetricLighting.cpp src/Features/VolumetricLighting.h
        tests/extract_source_region.cmake tests/extract_volumetric_lighting_runtime.cmake
    VERBATIM
)
add_controller_test(volumetric_lighting_runtime_test VolumetricLightingRuntime tests/volumetric_lighting_runtime_test.cpp)
set_tests_properties(VolumetricLightingRuntime PROPERTIES TIMEOUT 30)
target_sources(volumetric_lighting_runtime_test PRIVATE "${_vl_runtime_dir}/runtime.h" "${_vl_runtime_dir}/vl_runtime_settings.h")
target_include_directories(volumetric_lighting_runtime_test PRIVATE "${_vl_runtime_dir}")
