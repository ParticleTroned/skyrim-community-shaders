set(_vl_blur_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/volumetric-blur")
set(_vl_blur_test_header "${_vl_blur_test_dir}/volumetric_blur_under_test.h")
add_custom_command(
    OUTPUT "${_vl_blur_test_header}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_FILE=${_vl_blur_test_header}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_volumetric_lighting_blur.cmake"
    DEPENDS
        src/Features/VolumetricLighting.cpp
        tests/extract_volumetric_lighting_blur.cmake
    VERBATIM
)
add_controller_test(
    volumetric_lighting_blur_test
    VolumetricLightingBlur
    tests/volumetric_lighting_blur_test.cpp
)
target_sources(volumetric_lighting_blur_test PRIVATE "${_vl_blur_test_header}")
target_include_directories(
    volumetric_lighting_blur_test
    PRIVATE "${_vl_blur_test_dir}"
)
if(MSVC)
    target_compile_options(
        volumetric_lighting_blur_test
        PRIVATE "$<$<CONFIG:Release>:/fp:fast>"
    )
endif()
