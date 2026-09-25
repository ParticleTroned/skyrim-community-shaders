set(_menu_lifetime_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/vr-menu-layer-lifetime"
)
set(_menu_lifetime_headers
    "${_menu_lifetime_dir}/vr_map_event_state_under_test.h"
    "${_menu_lifetime_dir}/vr_map_context_under_test.h"
    "${_menu_lifetime_dir}/vr_non_loading_menu_context_under_test.h"
    "${_menu_lifetime_dir}/vr_map_initial_state_under_test.h"
    "${_menu_lifetime_dir}/vr_known_menu_context_under_test.h"
    "${_menu_lifetime_dir}/vr_map_presentation_context_under_test.h"
    "${_menu_lifetime_dir}/vr_menu_transaction_under_test.h"
    "${_menu_lifetime_dir}/vr_menu_begin_frame_under_test.h"
    "${_menu_lifetime_dir}/vr_menu_layer_lifetime_under_test.h"
)
add_custom_command(
    OUTPUT ${_menu_lifetime_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_menu_lifetime_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_vr_menu_layer_lifetime.cmake"
    DEPENDS
        src/Features/Upscaling.cpp
        src/Features/Upscaling.h
        tests/extract_source_region.cmake
        tests/extract_vr_menu_layer_lifetime.cmake
    VERBATIM
)
add_controller_test(
    vr_menu_layer_lifetime_test
    VRMenuLayerLifetime
    tests/vr_menu_layer_lifetime_test.cpp
)
target_sources(vr_menu_layer_lifetime_test PRIVATE ${_menu_lifetime_headers})
target_include_directories(
    vr_menu_layer_lifetime_test
    PRIVATE "${_menu_lifetime_dir}"
)
