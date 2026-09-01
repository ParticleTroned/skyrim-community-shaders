if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Api/AcceptedDrawService.cpp" _service)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _upscaling)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_between(
    "${_service}"
    "const void* GetCurrentAcceptedDrawGeometry("
    "SuppressAcceptedDraw::SuppressAcceptedDraw()"
    "accepted_draw_geometry_match_under_test.h"
)
extract_between(
    "${_upscaling}"
    "bool Upscaling::TryCaptureVRMenuPointerDraw("
    "ID3D11ShaderResourceView* Upscaling::GetCurrentVRMenuPointerOverlay("
    "vr_menu_pointer_under_test.h"
)
extract_between(
    "${_upscaling}"
    "bool IsVRMenuPointerVisible("
    "\n}\n\nbool Upscaling::TryCaptureVRMenuPointerDraw("
    "vr_menu_pointer_visibility_under_test.h"
)
extract_between(
    "${_upscaling}"
    "ID3D11ShaderResourceView* Upscaling::GetCurrentVRMenuPointerOverlay("
    "bool Upscaling::ShouldTraceVRMenuBridgeDrawOperation("
    "vr_menu_pointer_presentation_under_test.h"
)
