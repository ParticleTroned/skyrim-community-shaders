if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix.h" _header)
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix.cpp" _source)
file(READ "${PROJECT_ROOT}/src/Utils/Game.h" _game_header)
file(READ "${PROJECT_ROOT}/src/Utils/Game.cpp" _game_source)
extract_between(
    "${_header}" "\tusing LightFlags = PointLightFlags::Flags;"
    "\n\tstruct ClusterAABB" "strict_light_data_under_test.h"
)
extract_between(
    "${_header}" "\tstruct alignas(16) StrictLightDataCB"
    "\n\tstruct CachedParticleLight" "strict_light_state_under_test.h"
)
extract_between(
    "${_source}" "\tbool SameStrictLightData("
    "\n\tvoid ClearStrictLightData(" "strict_light_comparison_under_test.h"
)
extract_between(
    "${_source}" "void LightLimitFix::BSLightingShader_SetupGeometry_After("
    "\nvoid LightLimitFix::SetLightPosition(" "strict_light_after_under_test.h"
)
extract_between(
    "${_game_header}" "\tclass FrameChecker"
    "\n\t/**" "strict_light_frame_checker_under_test.h"
)
extract_between(
    "${_game_source}" "\tbool FrameChecker::IsNewFrame()"
    "\n\tRE::BGSTextureSet* GetSeasonalSwap(" "strict_light_frame_read_under_test.h"
)
extract_between(
    "${_source}" "\t\thasUploadedStrictLightData = false;\n\t\tdelete strictLightDataCB;"
    "\n\t\tclusterBuildingCS.Reset();" "strict_light_retire_under_test.h"
)
extract_between(
    "${_source}" "\t\tif (!strictLightDataCB) {"
    "\n\t}\n}" "strict_light_create_under_test.h"
)
