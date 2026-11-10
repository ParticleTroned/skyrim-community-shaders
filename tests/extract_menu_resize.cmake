include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Menu/OverlayRenderer.cpp" _overlay)
extract_between("${_overlay}"
    "\tbool ComputeDesktopMirrorTransform("
    "\tvoid DrawShaderCompilationFailures("
    "menu_mirror_under_test.h"
)
file(READ "${PROJECT_ROOT}/src/Menu/MenuHeaderRenderer.cpp" _header)
extract_between("${_header}"
    "\tfloat GetSteamVRResizeHandleSize(float a_uiScale)"
    "\tfloat GetSteamVRHeaderRightInset(float a_uiScale)"
    "menu_resize_helpers_under_test.h"
)
extract_between("${_header}"
    "void MenuHeaderRenderer::RenderSteamVRResizeHandles("
    "void MenuHeaderRenderer::RenderStableHeader("
    "menu_resize_under_test.h"
)
file(READ "${PROJECT_ROOT}/src/Menu.cpp" _menu)
extract_between("${_menu}"
    "\tbool ClampSettingsWindowToCanvas("
    "\tfloat GetVRSettingsWindowAspect()"
    "menu_canvas_under_test.h"
)
extract_between("${_menu}"
    "\t// Determine window flags based on docking state"
    "\tconst bool contentsVisible"
    "menu_resize_flags_under_test.h"
)
extract_between("${_menu}"
    "\t\tif (showSteamVRWindowControls) {"
    "\n\t}\n\tImGui::End();"
    "menu_resize_dispatch_under_test.h"
)
