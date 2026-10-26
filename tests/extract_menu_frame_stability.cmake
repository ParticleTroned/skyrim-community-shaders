include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Utils/UI.h" _ui)
extract_between("${_ui}" "constexpr float kBaselineFontSize" "/** Draws a checkbox" "menu_ui_scale_under_test.h")
file(READ "${PROJECT_ROOT}/src/Menu/ThemeManager.cpp" _theme)
extract_between("${_theme}"
    "void ThemeManager::SetupImGuiStyle(const Menu& menu)"
    "void ThemeManager::ForceApplyDefaultTheme()"
    "menu_theme_style_under_test.h"
)
file(READ "${PROJECT_ROOT}/src/Menu/HomePageRenderer.cpp" _home)
extract_between("${_home}"
    "void HomePageRenderer::RenderFirstTimeSetupDialog()"
    "\tauto menu = Menu::GetSingleton();"
    "menu_welcome_layout_under_test.h"
)
extract_between("${_home}"
    "bool HomePageRenderer::ShouldShowFirstTimeSetup()"
    "bool HomePageRenderer::TryCompleteFirstTimeSetupFromInput("
    "menu_welcome_admission_under_test.h"
)
file(READ "${PROJECT_ROOT}/src/Hooks.cpp" _hooks)
extract_between("${_hooks}" "struct IDXGISwapChain_Present" "decltype(&CreateDXGIFactory)" "menu_present_under_test.h")
file(READ "${PROJECT_ROOT}/src/Menu.cpp" _menu)
extract_between("${_menu}" "void Menu::UpdateIdleCameraSuppression()" "bool Menu::HasClosedMenuOverlay()" "menu_idle_camera_under_test.h")
extract_between("${_menu}" "\tstatic bool menuWasOffsetForTopStatusWindow" "\tconst bool lockVRMenuToCanvas" "menu_status_layout_under_test.h")
extract_between("${_menu}" "void Menu::ApplyPendingLayoutReset()" "void Menu::DrawOverlay()" "menu_recovery_under_test.h")
extract_between("${_menu}" "\tbool ClampSettingsWindowToCanvas(" "\tfloat GetVRSettingsWindowAspect()" "menu_canvas_bounds_under_test.h")
extract_between("${_menu}" "\tconst bool lockVRMenuToCanvas" "\tif (contentsVisible)" "menu_window_geometry_under_test.h")
extract_between("${_menu}" "\t\t\tstatic const std::vector<InputCombo> resetShortcut" "\t\t\tconst bool wasCapturingHotkey" "menu_recovery_shortcut_under_test.h")
extract_between("${_menu}" "\tif (layoutResetKeyHeld)" "\t// Solves the alt+tab stuck issue" "menu_recovery_focus_under_test.h")
file(READ "${PROJECT_ROOT}/src/Menu/FeatureListRenderer.cpp" _features)
extract_between("${_features}" "\tconstexpr float sidebarTextScale" "\tconstexpr float featureDescriptionScale" "menu_sidebar_font_under_test.h")
file(READ "${PROJECT_ROOT}/src/Menu/MenuHeaderRenderer.cpp" _header)
extract_between("${_header}" "\tfloat GetHeaderIconSize()" "\tfloat GetSteamVRResizeHandleInset" "menu_header_scale_under_test.h")
file(READ "${PROJECT_ROOT}/src/Menu/OverlayRenderer.cpp" _overlay)
if(NOT _menu MATCHES "ProcessInputEventQueue\\(\\);[\r\n\t ]+UpdateIdleCameraSuppression\\(\\);")
    message(FATAL_ERROR "Idle-camera ownership must follow input processing before any rendering early return")
endif()
extract_between("${_overlay}" "\tconstexpr const char* kTopStatusWindows" "\tImVec2 FitSizeToAspect(" "menu_status_visibility_under_test.h")
string(REGEX MATCHALL "ImGui::Begin\\(\"ShaderCompilationInfo\", nullptr, kShaderStatusWindowFlags\\)" _passive_shader_windows "${_overlay}")
list(LENGTH _passive_shader_windows _passive_shader_window_count)
if(NOT _passive_shader_window_count EQUAL 3)
    message(FATAL_ERROR "Compilation, error and RenderDoc status windows must all use passive status flags")
endif()
extract_between("${_overlay}" "\tbool IsHiddenDesktopOverlayDrawList(" "\tbool BuildScaledDesktopDrawData(" "menu_desktop_status_under_test.h")
extract_between("${_overlay}" "float OverlayRenderer::GetTopStatusWindowBottom(" "bool OverlayRenderer::MoveWindowBelowShaderCompilationStatus(" "menu_status_reservation_under_test.h")
extract_between("${_overlay}" "\tconst float yPos = GetTopStatusWindowBottom(" "\tImGui::SetNextWindowPos(ImVec2(pos, yPos));" "menu_shader_status_stack_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/UnifiedWater.cpp" _water)
extract_between("${_water}" "\tconst float yPos = OverlayRenderer::GetTopStatusWindowBottom(" "\tconst auto snapshot =" "menu_water_status_stack_under_test.h")
extract_between("${_overlay}" "bool OverlayRenderer::MoveWindowBelowShaderCompilationStatus(" "void OverlayRenderer::HandleVRSetup()" "menu_status_overlap_under_test.h")
