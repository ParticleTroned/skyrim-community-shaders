set(_vl_composite_test_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/volumetric-composite"
)
set(_vl_shared_header "${_vl_composite_test_dir}/shared_data_under_test.h")
set(_vl_runtime_headers
    "${_vl_composite_test_dir}/volumetric_lighting_settings_under_test.h"
    "${_vl_composite_test_dir}/volumetric_lighting_runtime_under_test.h"
)
add_custom_command(
    OUTPUT "${_vl_shared_header}" ${_vl_runtime_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_FILE=${_vl_shared_header}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_volumetric_lighting_shared_data.cmake"
    DEPENDS
        src/State.h
        src/State.cpp
        src/Features/VolumetricLighting.h
        src/Features/VolumetricLighting.cpp
        tests/extract_volumetric_lighting_shared_data.cmake
    VERBATIM
)
add_d3d_shader_test(
    volumetric_lighting_composite_test
    VolumetricLightingComposite
    tests/volumetric_lighting_composite_test.cpp
)
target_sources(
    volumetric_lighting_composite_test
    PRIVATE "${_vl_shared_header}" ${_vl_runtime_headers}
)
target_include_directories(
    volumetric_lighting_composite_test
    PRIVATE "${_vl_composite_test_dir}"
)
if(MSVC)
    target_compile_options(
        volumetric_lighting_tuning_policy_test
        PRIVATE "$<$<CONFIG:Release>:/fp:fast>"
    )
endif()
