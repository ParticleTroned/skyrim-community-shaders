set(_menu_frame_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/menu-frame")
set(_menu_frame_test_headers
    "${_menu_frame_test_dir}/menu_theme_style_under_test.h"
    "${_menu_frame_test_dir}/menu_welcome_layout_under_test.h"
    "${_menu_frame_test_dir}/menu_welcome_admission_under_test.h"
    "${_menu_frame_test_dir}/menu_ui_scale_under_test.h"
    "${_menu_frame_test_dir}/menu_present_under_test.h"
    "${_menu_frame_test_dir}/menu_status_layout_under_test.h"
    "${_menu_frame_test_dir}/menu_status_overlap_under_test.h"
    "${_menu_frame_test_dir}/menu_recovery_under_test.h"
    "${_menu_frame_test_dir}/menu_canvas_bounds_under_test.h"
    "${_menu_frame_test_dir}/menu_window_geometry_under_test.h"
    "${_menu_frame_test_dir}/menu_recovery_shortcut_under_test.h"
    "${_menu_frame_test_dir}/menu_recovery_focus_under_test.h"
    "${_menu_frame_test_dir}/menu_sidebar_font_under_test.h"
    "${_menu_frame_test_dir}/menu_header_scale_under_test.h"
    "${_menu_frame_test_dir}/menu_idle_camera_under_test.h"
    "${_menu_frame_test_dir}/menu_game_update_under_test.h"
    "${_menu_frame_test_dir}/menu_status_visibility_under_test.h"
    "${_menu_frame_test_dir}/menu_status_reservation_under_test.h"
    "${_menu_frame_test_dir}/menu_water_status_stack_under_test.h"
    "${_menu_frame_test_dir}/menu_shader_status_stack_under_test.h"
    "${_menu_frame_test_dir}/menu_desktop_status_under_test.h"
)
add_custom_command(
    OUTPUT ${_menu_frame_test_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_menu_frame_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_menu_frame_stability.cmake"
    DEPENDS
        src/Menu/ThemeManager.cpp
        src/Menu/HomePageRenderer.cpp
        src/Utils/UI.h
        src/Hooks.cpp
        src/Menu.cpp
        src/Menu/OverlayRenderer.cpp
        src/Features/UnifiedWater.cpp
        src/Menu/FeatureListRenderer.cpp
        src/Menu/MenuHeaderRenderer.cpp
        tests/extract_source_region.cmake
        tests/extract_menu_frame_stability.cmake
    VERBATIM
)
add_custom_target(menu_frame_test_sources DEPENDS ${_menu_frame_test_headers})
add_controller_test(menu_frame_stability_test MenuFrameStability tests/menu_frame_stability_test.cpp)
target_link_libraries(menu_frame_stability_test PRIVATE imgui::imgui)
add_controller_test(menu_present_test MenuPresent tests/menu_present_test.cpp)
add_controller_test(menu_layout_recovery_test MenuLayoutRecovery tests/menu_layout_recovery_test.cpp)
add_controller_test(menu_session_stability_test MenuSessionStability tests/menu_session_stability_test.cpp)
target_link_libraries(menu_layout_recovery_test PRIVATE imgui::imgui)
foreach(
    _menu_frame_test_target
    IN
    ITEMS menu_frame_stability_test menu_present_test menu_layout_recovery_test menu_session_stability_test
)
    add_dependencies(${_menu_frame_test_target} menu_frame_test_sources)
    target_include_directories(
        ${_menu_frame_test_target}
        PRIVATE "${_menu_frame_test_dir}"
    )
endforeach()
