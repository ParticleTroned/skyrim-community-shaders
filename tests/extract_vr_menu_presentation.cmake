if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Features/VR.h" _header)
file(READ "${PROJECT_ROOT}/src/Features/VR.cpp" _vr)
file(READ "${PROJECT_ROOT}/src/Utils/VRUtils.h" _utils_header)
file(READ "${PROJECT_ROOT}/src/Utils/VRUtils.cpp" _utils)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_between("${_header}"
    "static constexpr int kOverlayWidth"
    "static constexpr float kDefaultMenuScale"
    "vr_menu_scale_under_test.h")
extract_between("${_header}"
    "enum class MenuOverlayPath"
    "MenuOverlayPath menuOverlayPath"
    "vr_menu_path_under_test.h")
extract_between("${_vr}"
    "bool VR::ShouldUseInSceneOverlay() const"
    "bool VR::CanOpenMenuFromWorld() const"
    "vr_menu_route_under_test.h")
extract_between("${_vr}"
    "void ScaleOverlayTransform("
    "HWND GetGameWindowHandle()"
    "vr_menu_native_scale_under_test.h")
extract_between("${_utils_header}"
    "inline Matrix HmdMatrix34ToMatrix("
    "inline vr::HmdMatrix34_t Float3x4ToHmdMatrix34("
    "vr_menu_matrix_conversion_under_test.h")
extract_between("${_utils}"
    "vr::HmdMatrix34_t CreateControllerOverlayTransform("
    "//============================================================================="
    "vr_menu_native_relative_under_test.h")
