set(_pointer_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/vr-menu-pointer")
set(_pointer_test_headers
    "${_pointer_test_dir}/accepted_draw_geometry_match_under_test.h"
    "${_pointer_test_dir}/vr_menu_pointer_under_test.h"
    "${_pointer_test_dir}/vr_menu_pointer_visibility_under_test.h"
    "${_pointer_test_dir}/vr_menu_pointer_presentation_under_test.h"
)
add_custom_command(
    OUTPUT ${_pointer_test_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_pointer_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_vr_menu_pointer.cmake"
    DEPENDS
        src/Api/AcceptedDrawService.cpp
        src/Features/Upscaling.cpp
        tests/extract_source_region.cmake
        tests/extract_vr_menu_pointer.cmake
    VERBATIM
)
add_controller_test(
    vr_menu_pointer_test
    VRMenuPointer
    tests/vr_menu_pointer_test.cpp
)
target_sources(vr_menu_pointer_test PRIVATE ${_pointer_test_headers})
target_include_directories(vr_menu_pointer_test PRIVATE "${_pointer_test_dir}")

add_d3d_shader_test(
    vr_menu_pointer_capture_test
    VRMenuPointerCapture
    tests/vr_menu_pointer_capture_test.cpp
)
target_sources(
    vr_menu_pointer_capture_test
    PRIVATE src/Features/Upscaling/VRMenuPointerOverlay.cpp
)
target_include_directories(
    vr_menu_pointer_capture_test
    PRIVATE "${_pointer_test_dir}"
)
target_compile_definitions(
    vr_menu_pointer_capture_test
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
target_link_libraries(
    vr_menu_pointer_capture_test
    PRIVATE runtimeobject.lib
)
set_tests_properties(VRMenuPointerCapture PROPERTIES TIMEOUT 30)
