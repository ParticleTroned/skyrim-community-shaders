set(_menu_resize_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/menu-resize")
set(_menu_resize_test_headers
    "${_menu_resize_test_dir}/menu_resize_helpers_under_test.h"
    "${_menu_resize_test_dir}/menu_resize_under_test.h"
    "${_menu_resize_test_dir}/menu_resize_flags_under_test.h"
    "${_menu_resize_test_dir}/menu_resize_dispatch_under_test.h"
)
add_custom_command(
    OUTPUT ${_menu_resize_test_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_menu_resize_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_menu_resize.cmake"
    DEPENDS
        src/Menu/MenuHeaderRenderer.cpp
        src/Menu.cpp
        tests/extract_source_region.cmake
        tests/extract_menu_resize.cmake
    VERBATIM
)
add_controller_test(menu_resize_test MenuResize tests/menu_resize_test.cpp)
target_sources(menu_resize_test PRIVATE ${_menu_resize_test_headers})
target_include_directories(menu_resize_test PRIVATE "${_menu_resize_test_dir}")
target_link_libraries(menu_resize_test PRIVATE imgui::imgui)
