if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" source)
extract_between("${source}"
    "\tvoid DrawVRRenderScaleModeTooltip()"
    "\tvoid DrawVRStartupNativeFallbackStatus("
    "render_scale_menu_under_test.h")
