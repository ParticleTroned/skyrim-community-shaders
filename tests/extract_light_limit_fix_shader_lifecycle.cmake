if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix.cpp" _source)
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix.h" _header)
extract_between(
    "${_header}" "\tstatic constexpr std::array<const wchar_t*, 2> kComputeShaderPaths{" "\n\t/** @brief Read or stage"
    "llf_shader_paths_under_test.h"
)
extract_between(
    "${_source}" "void LightLimitFix::ClearShaderCache()"
    "\nfloat LightLimitFix::CalculateLightDistance(" "llf_shader_lifecycle_under_test.h"
)
extract_between(
    "${_source}" "bool LightLimitFix::IsPerformanceCostMeasurementReady() const"
    "\njson LightLimitFix::CapturePerformanceSettingsState(" "llf_shader_status_under_test.h"
)
# Exercise device retention before rebuilding render-target buffers.
extract_between(
    "${_source}" "void LightLimitFix::SetupResources()"
    "\n\t{\n\t\tD3D11_BUFFER_DESC sbDesc{};" "llf_shader_device_under_test.h"
)
file(APPEND "${OUTPUT_DIRECTORY}/llf_shader_device_under_test.h" "\n\t(void)clusterCount;\n}\n")
extract_between(
    "${_source}" "void LightLimitFix::SetupRenderTargetResources()"
    "\nvoid LightLimitFix::Reset()" "llf_shader_relatch_under_test.h"
)
# Exercise draw eligibility before the GPU resource mutations.
extract_between(
    "${_source}" "void LightLimitFix::UpdateStructure()"
    "\n\tif (globals::game::cameraNear)" "llf_shader_draw_under_test.h"
)
file(APPEND "${OUTPUT_DIRECTORY}/llf_shader_draw_under_test.h" "\n\t++readyDraws;\n}\n")
