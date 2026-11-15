set(_vanilla_ssao_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/vanilla-ssao")
add_custom_command(
    OUTPUT "${_vanilla_ssao_dir}/vanilla_ssao.h"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_vanilla_ssao_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_vanilla_ssao.cmake"
    DEPENDS
        src/Features/ScreenSpaceGI.cpp tests/extract_vanilla_ssao.cmake
        tests/extract_source_region.cmake
    VERBATIM
)
add_controller_test(vanilla_ssao_test VanillaSSAO tests/vanilla_ssao_test.cpp)
target_sources(vanilla_ssao_test PRIVATE "${_vanilla_ssao_dir}/vanilla_ssao.h")
target_include_directories(vanilla_ssao_test PRIVATE "${_vanilla_ssao_dir}")
