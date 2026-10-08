set(_api_discovery_test_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/api-discovery"
)
set(_api_discovery_header
    "${_api_discovery_test_dir}/api_discovery_under_test.h"
)
add_custom_command(
    OUTPUT "${_api_discovery_header}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_api_discovery_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_api_discovery.cmake"
    DEPENDS src/XSEPlugin.cpp tests/extract_api_discovery.cmake
    VERBATIM
)
add_controller_test(api_discovery_lifecycle_test ApiDiscoveryLifecycle tests/api_discovery_lifecycle_test.cpp)
target_sources(api_discovery_lifecycle_test PRIVATE "${_api_discovery_header}")
target_include_directories(
    api_discovery_lifecycle_test
    PRIVATE "${_api_discovery_test_dir}"
)

set(_screenshot_settings_test_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/screenshot-settings"
)
set(_screenshot_settings_headers
    "${_screenshot_settings_test_dir}/screenshot_settings_types.h"
    "${_screenshot_settings_test_dir}/screenshot_settings_under_test.h"
    "${_screenshot_settings_test_dir}/screenshot_build_limits.h"
    "${_screenshot_settings_test_dir}/screenshot_build_admission.h"
)
add_custom_command(
    OUTPUT ${_screenshot_settings_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_screenshot_settings_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_screenshot_settings.cmake"
    DEPENDS
        src/Features/ScreenshotApi.cpp
        src/Features/ScreenshotApi.h
        src/Features/ScreenshotFeature.cpp
        src/Features/ScreenshotFeature.h
        tests/extract_screenshot_settings.cmake
    VERBATIM
)
add_custom_target(screenshot_settings_headers DEPENDS ${_screenshot_settings_headers})
foreach(_variant IN ITEMS production devbench)
    set(_target screenshot_settings_test)
    set(_test ScreenshotSettings)
    if(_variant STREQUAL "devbench")
        set(_target screenshot_settings_devbench_test)
        set(_test ScreenshotSettingsDevBench)
    endif()
    add_controller_test(${_target} ${_test} tests/screenshot_settings_test.cpp)
    add_dependencies(${_target} screenshot_settings_headers)
    target_include_directories(${_target} PRIVATE "${_screenshot_settings_test_dir}")
    target_link_libraries(${_target} PRIVATE nlohmann_json::nlohmann_json)
    # The extracted production path uses u8path with deprecation warnings disabled.
    target_compile_definitions(${_target} PRIVATE _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING)
    if(_variant STREQUAL "devbench")
        target_compile_definitions(${_target} PRIVATE DEVBENCH_BRIDGE_ENABLED)
    endif()
endforeach()
