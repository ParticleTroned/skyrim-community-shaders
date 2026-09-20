set(_fov_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/fov-settings")
set(_fov_test_headers
    "${_fov_test_dir}/fov_under_test.h"
    "${_fov_test_dir}/fov_defaults.h"
)
add_custom_command(
    OUTPUT ${_fov_test_headers}
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/extract_fov_settings.py" --source-dir
        "${PROJECT_SOURCE_DIR}" --output-dir "${_fov_test_dir}"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/src/Features/Upscaling.cpp"
        "${PROJECT_SOURCE_DIR}/src/Features/ScreenSpaceGI.cpp"
        "${PROJECT_SOURCE_DIR}/src/Features/ScreenSpaceGI.h"
        "${PROJECT_SOURCE_DIR}/src/Features/ScreenSpaceShadows.cpp"
        "${PROJECT_SOURCE_DIR}/src/Features/ScreenSpaceShadows.h"
        "${PROJECT_SOURCE_DIR}/src/Features/VR.cpp"
        "${PROJECT_SOURCE_DIR}/src/MenuDevBenchBridge.cpp"
        "${PROJECT_SOURCE_DIR}/tests/extract_fov_settings.py"
        "${PROJECT_SOURCE_DIR}/tests/extract_adaptive_balance_toggle.py"
    VERBATIM
)
add_controller_test(fov_settings_test FovSettings tests/fov_settings_test.cpp)
target_sources(fov_settings_test PRIVATE ${_fov_test_headers})
target_include_directories(fov_settings_test PRIVATE "${_fov_test_dir}")
target_link_libraries(
    fov_settings_test
    PRIVATE imgui::imgui nlohmann_json::nlohmann_json
)
