set(_ui_regression_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/ui-regressions")
set(_ui_regression_headers)
foreach(name IN ITEMS wand_types wand_pose vr_runtime_state vr_trigger_release vr_scroll_types vr_scroll_refresh vr_scroll_poll vr_cursor_delta vr_scroll vr_input_release escape markers header_button docked_icon_size docked_header theme_save atomic_write filename blur_initialization blur_lifecycle)
    list(APPEND _ui_regression_headers "${_ui_regression_dir}/ui_${name}.h")
endforeach()
add_custom_command(
    OUTPUT ${_ui_regression_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_ui_regression_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_ui_regressions.cmake"
    DEPENDS src/Features/VR/WandPointing.cpp src/Features/VR/Input.cpp src/Menu.cpp
        src/CSEditor/EditorWindow.cpp src/Menu/MenuHeaderRenderer.cpp
        src/Menu/ThemeManager.cpp src/Menu/BackgroundBlur.cpp src/Utils/FileSystem.cpp src/Utils/VRUtils.cpp
        tests/extract_ui_regressions.cmake tests/extract_source_region.cmake
    VERBATIM
)
add_custom_target(ui_regression_sources DEPENDS ${_ui_regression_headers})
add_controller_test(ui_input_regression_test UIInputRegression tests/ui_input_regression_test.cpp)
add_controller_test(vr_menu_scroll_test VRMenuScroll tests/vr_menu_scroll_test.cpp)
add_controller_test(ui_persistence_regression_test UIPersistenceRegression tests/ui_persistence_regression_test.cpp)
foreach(target IN ITEMS ui_input_regression_test ui_persistence_regression_test vr_menu_scroll_test)
    add_dependencies(${target} ui_regression_sources)
    target_include_directories(${target} PRIVATE "${_ui_regression_dir}")
    target_link_libraries(${target} PRIVATE imgui::imgui nlohmann_json::nlohmann_json)
endforeach()
add_controller_test(device_context_state_test DeviceContextState tests/device_context_state_test.cpp)
target_link_libraries(device_context_state_test PRIVATE d3d11 dxguid runtimeobject)

add_controller_test(background_blur_initialization_test BackgroundBlurInitialization tests/background_blur_initialization_test.cpp)
add_dependencies(background_blur_initialization_test ui_regression_sources)
target_include_directories(background_blur_initialization_test PRIVATE "${_ui_regression_dir}")
target_compile_definitions(background_blur_initialization_test PRIVATE CSX_BLUR_SHADER_DIR="${PROJECT_SOURCE_DIR}/package/Shaders/Menu")
target_link_libraries(background_blur_initialization_test PRIVATE d3d11 d3dcompiler dxguid runtimeobject)
