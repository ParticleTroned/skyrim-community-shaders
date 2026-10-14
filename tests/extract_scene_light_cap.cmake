if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix.cpp" _source)

extract_between(
    "${_source}" "static constexpr uint MAX_LIGHTS ="
    "\n" "scene_light_cap_constant_under_test.h"
)
extract_between(
    "${_source}" "\tauto addRoom ="
    "\n\t{\n\t\tCS_PROFILE_CPU_SCOPE(\"LightLimitFix::ParticleLightsCPU\");"
    "scene_light_cap_under_test.h"
)
extract_between(
    "${_source}" "void LightLimitFix::DataLoaded()"
    "\nvoid LightLimitFix::ClearShaderCache()"
    "scene_light_cap_native_limit_under_test.h"
)
