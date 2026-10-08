if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _upscaling)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.h" _header)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_between(
    "${_upscaling}"
    "enum class VRMapMenuEventState : uint8_t"
    "\n\tstd::atomic_bool g_vrStatsMenuOpenFromEvent"
    "vr_map_event_state_under_test.h"
)
extract_between(
    "${_upscaling}"
    "bool IsMapMenuContextActive()"
    "\n\tbool IsNonLoadingVRGameMenuPresentationContextActive()"
    "vr_map_context_under_test.h"
)
extract_between(
    "${_upscaling}"
    "bool IsNonLoadingVRGameMenuPresentationContextActive()"
    "\n\tbool IsNonLoadingVRMenuPresentationContextActive()"
    "vr_non_loading_menu_context_under_test.h"
)
extract_between(
    "${_upscaling}"
    "const auto initialMapState = ui->IsMenuOpen(RE::MapMenu::MENU_NAME) ?"
    "\n\tg_vrStatsMenuOpenFromEvent.store(ui->IsMenuOpen"
    "vr_map_initial_state_under_test.h"
)
extract_between(
    "${_upscaling}"
    "bool IsKnownGameMenuContextActive()\n\t{"
    "\n\tbool IsCommunityShadersMenuOpen()"
    "vr_known_menu_context_under_test.h"
)
extract_between(
    "${_upscaling}"
    "bool Upscaling::IsVRMapMenuPresentationActive() const"
    "\nbool Upscaling::EnsureVRMapMenuUISupersampling()"
    "vr_map_presentation_context_under_test.h"
)
extract_between(
    "${_header}"
    "struct VRMenuFrameTransaction\n"
    "\n\tuint32_t vrMenuFinalCompositeFrame"
    "vr_menu_transaction_under_test.h"
)
extract_between(
    "${_upscaling}"
    "void Upscaling::BeginVRMenuFinalCompositeFrame("
    "void Upscaling::PoisonVRMenuFrameTransaction("
    "vr_menu_begin_frame_under_test.h"
)
extract_between(
    "${_upscaling}"
    "void Upscaling::ResetVRMenuDesktopEyePairState("
    "void Upscaling::BeginVRMenuDrawInterface("
    "vr_menu_layer_lifetime_under_test.h"
)
