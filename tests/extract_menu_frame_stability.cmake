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
extract_between("${_menu}" "\tstatic bool menuWasOffsetForTopStatusWindow" "\tconst bool lockVRMenuToCanvas" "menu_status_layout_under_test.h")
file(READ "${PROJECT_ROOT}/src/Menu/OverlayRenderer.cpp" _overlay)
extract_between("${_overlay}" "bool OverlayRenderer::MoveWindowBelowShaderCompilationStatus(" "void OverlayRenderer::HandleVRSetup()" "menu_status_overlap_under_test.h")
