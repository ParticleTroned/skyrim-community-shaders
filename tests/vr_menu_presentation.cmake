set(_vr_menu_presentation_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/vr-menu-presentation")
set(_vr_menu_presentation_headers)
foreach(_part IN ITEMS scale path route native_scale matrix_conversion native_relative defaults effective offsets layout_lock fixed_helpers fixed_update world_admission poses fixed_poll desktop_helpers desktop_management)
    list(APPEND _vr_menu_presentation_headers
        "${_vr_menu_presentation_dir}/vr_menu_${_part}_under_test.h")
endforeach()
add_custom_command(
    OUTPUT ${_vr_menu_presentation_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_vr_menu_presentation_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_vr_menu_presentation.cmake"
    DEPENDS
        src/Features/VR.h src/Features/VR.cpp src/Features/VR/OverlayDrag.cpp
        src/Utils/VRUtils.h src/Utils/VRUtils.cpp
        tests/extract_source_region.cmake tests/extract_vr_menu_presentation.cmake
    VERBATIM
)
add_custom_target(vr_menu_presentation_sources DEPENDS ${_vr_menu_presentation_headers})
add_controller_test(vr_menu_presentation_test VRMenuPresentation tests/vr_menu_presentation_test.cpp)
add_dependencies(vr_menu_presentation_test vr_menu_presentation_sources)
target_include_directories(vr_menu_presentation_test PRIVATE
    "${_vr_menu_presentation_dir}"
    "${PROJECT_SOURCE_DIR}/extern/CommonLibSSE-NG/extern/openvr/headers")
target_link_libraries(vr_menu_presentation_test PRIVATE Microsoft::DirectXTK)

add_controller_test(vr_menu_desktop_window_test VRMenuDesktopWindow tests/vr_menu_desktop_window_test.cpp)
add_dependencies(vr_menu_desktop_window_test vr_menu_presentation_sources)
target_include_directories(vr_menu_desktop_window_test PRIVATE "${_vr_menu_presentation_dir}")
