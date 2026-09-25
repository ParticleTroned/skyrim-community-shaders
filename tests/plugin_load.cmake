set(_plugin_load_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/plugin-load")
add_custom_command(
    OUTPUT "${_plugin_load_test_dir}/plugin_load_under_test.h"
        "${_plugin_load_test_dir}/commonlib_trampoline_under_test.h"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_plugin_load_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_plugin_load.cmake"
    DEPENDS src/XSEPlugin.cpp tests/extract_plugin_load.cmake
        extern/CommonLibSSE-NG/src/SKSE/API.cpp
    VERBATIM
)
add_controller_test(plugin_load_test PluginLoad tests/plugin_load_test.cpp)
target_sources(
    plugin_load_test
    PRIVATE "${_plugin_load_test_dir}/plugin_load_under_test.h"
        "${_plugin_load_test_dir}/commonlib_trampoline_under_test.h"
)
target_include_directories(plugin_load_test PRIVATE "${_plugin_load_test_dir}")
