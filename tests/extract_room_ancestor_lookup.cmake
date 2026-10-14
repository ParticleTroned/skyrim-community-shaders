if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix.cpp" _source)
extract_between(
    "${_source}" "\tvoid ClearStrictLightData("
    "\n\tbool IsNearWhiteTint(" "room_ancestor_clear_under_test.h"
)
extract_between(
    "${_source}" "RE::NiNode* GetParentRoomNode("
    "\nconst LightLimitFix::SceneLightSnapshot* LightLimitFix::GetSceneLightSnapshot("
    "room_ancestor_lookup_under_test.h"
)
