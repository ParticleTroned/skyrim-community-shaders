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
    "${_upscaling}"
    "bool IsExplicitVRMenuPresentationContextActive()\n\t{"
    "\n\tuint64_t EncodeVRMenuBridgeTraceState("
    "vr_explicit_menu_context_under_test.h"
)
extract_between(
    "${_upscaling}"
    "const auto requiresNeuralMenuLayer = [&]() {"
    "\n\tconst bool csOverlayOpen ="
    "vr_neural_menu_requirement_under_test.h"
)
extract_between(
    "${_upscaling}"
    "const bool sourceMenuContextActive = menuWorldUIBridge ?"
    "\n\t// Gameplay prompts must not arm menu tails"
    "vr_menu_capture_owner_under_test.h"
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

# Exercise the exact production admission path before any GPU calls.
extract_between("${_upscaling}"
    "void Upscaling::PresentVRMenuDesktopMirror("
    "\n\twinrt::com_ptr<ID3D11Texture2D> backBuffer;"
    "vr_menu_desktop_admission_under_test.h"
)
file(READ "${OUTPUT_DIRECTORY}/vr_menu_desktop_admission_under_test.h" admission)
string(REPLACE "void Upscaling::PresentVRMenuDesktopMirror(IDXGISwapChain* a_swapChain)"
    "bool Upscaling::CanPresentDesktopLayer(void* a_swapChain)" admission "${admission}")
string(REPLACE "return;" "return false;" admission "${admission}")
file(WRITE "${OUTPUT_DIRECTORY}/vr_menu_desktop_admission_under_test.h" "${admission}\nreturn true;\n}\n")
