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
extract_between("${_utils}" "bool GetControllerWorldMatrix(" "\n}" "vr_menu_poses_under_test.h")

extract_between("${_header}" "static constexpr float kDefaultMenuScale" "// Default controller overlay offset values" "vr_menu_defaults_under_test.h")
extract_between("${_vr}" "bool VR::CanOpenMenuFromWorld() const" "bool VR::IsOverlayVisible() const" "vr_menu_world_admission_under_test.h")
extract_between("${_vr}" "bool VR::UseFixedWorldMenuPositioning() const" "void VR::SetMenuDistance(" "vr_menu_effective_under_test.h")
extract_between("${_vr}" "Vector3 VR::GetEffectiveHMDMenuOffset() const" "void VR::SetMenuLayoutUnlocked(" "vr_menu_offsets_under_test.h")
extract_between("${_vr}" "void VR::SetMenuLayoutUnlocked(" "void VR::UpdateVROverlayPosition()" "vr_menu_layout_lock_under_test.h")
extract_between("${_vr}" "\tconstexpr double kDesktopWindowStableCheckIntervalSecs" "\tconstexpr int kVRControllerBindingsVersion" "vr_menu_desktop_helpers_under_test.h")
extract_between("${_vr}" "void VR::UpdateMenuDesktopWindowManagement(" "void VR::SubmitCaptureIndicator(" "vr_menu_desktop_management_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/VR/OverlayDrag.cpp" _drag)
extract_between("${_drag}" "bool IsFiniteVector(" "bool TryApplyOffsetDelta(" "vr_menu_fixed_helpers_under_test.h")
extract_between("${_drag}" "bool MakeVerticalFacingTransform(" "\n}\n\nbool VR::GetGripPressed(" "vr_menu_fixed_facing_under_test.h")
file(READ "${OUTPUT_DIRECTORY}/vr_menu_fixed_facing_under_test.h" _facing)
file(APPEND "${OUTPUT_DIRECTORY}/vr_menu_fixed_helpers_under_test.h" "${_facing}")
extract_between("${_drag}" "void VR::RequestFixedWorldMenuReanchor()" "void VR::UpdateFixedWorldPositioning()" "vr_menu_fixed_update_under_test.h")
extract_between("${_drag}" "void VR::UpdateFixedWorldPositioning()" "\n}" "vr_menu_fixed_poll_under_test.h")
file(APPEND "${OUTPUT_DIRECTORY}/vr_menu_fixed_poll_under_test.h" "\n}\n")
