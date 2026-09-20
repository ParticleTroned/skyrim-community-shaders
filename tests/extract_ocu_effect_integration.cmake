if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

file(READ "${PROJECT_ROOT}/src/Features/ScreenSpaceGI.cpp" _ssgi)
file(READ "${PROJECT_ROOT}/src/ProfilerDevBenchBridge.cpp" _bridge)
file(READ "${PROJECT_ROOT}/src/Api/AcceptedDrawService.cpp" _draw_service)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")

include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")

extract_between(
    "${_ssgi}"
    "bool ScreenSpaceGI::CompileComputeShaders("
    "bool ScreenSpaceGI::ShadersOK()"
    "ocu_shader_batch_under_test.h"
)
extract_between(
    "${_ssgi}"
    "void ScreenSpaceGI::SetOCUEffectFoveationEnabled("
    "void ScreenSpaceGI::DrawOCUEffectFoveationSettings()"
    "ocu_setting_under_test.h"
)
extract_between(
    "${_bridge}"
    "json BuildOCUEffectFoveationResult("
    "json BuildProfilerResult("
    "ocu_devbench_under_test.h"
)
extract_between(
    "${_draw_service}"
    "void PublishAcceptedDraw("
    "const API* GetAcceptedDrawAPI()"
    "accepted_draw_publish_under_test.h"
)
