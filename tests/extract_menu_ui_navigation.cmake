if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/MenuDevBenchBridge.cpp" source)
extract_between("${source}" "\t\t\t\tconst auto location = MenuUI::SettingsNavigation::Resolve(featureName, tab);"
    "\n\t\t\t});" "menu_ui_navigation_under_test.h")

extract_between("${source}" "\t\tif (action == \"feature_settings\") {"
    "\n\t\tif (action == \"restore_defaults\")" "feature_settings_action_under_test.h")
