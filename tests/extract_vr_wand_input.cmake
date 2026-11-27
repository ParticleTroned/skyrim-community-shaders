if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Features/VR/Input.cpp" _input)
file(READ "${PROJECT_ROOT}/src/Features/VR/WandPointing.cpp" _wand)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_between(
    "${_input}"
    "void VR::ProcessControllerInputForWandPointingPath("
    "void VR::ProcessControllerInputForMouseNavigationPath("
    "vr_wand_cursor_ownership_under_test.h"
)
extract_between(
    "${_wand}"
    "void VR::UpdateCursorFromWandPointing("
    "bool VR::UpdateWandPoseOwnershipSignal("
    "vr_wand_selection_under_test.h"
)
