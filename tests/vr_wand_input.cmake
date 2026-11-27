set(_wand_input_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/vr-wand-input")
set(_wand_input_test_headers
    "${_wand_input_test_dir}/vr_wand_cursor_ownership_under_test.h"
    "${_wand_input_test_dir}/vr_wand_selection_under_test.h"
)
add_custom_command(
    OUTPUT ${_wand_input_test_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_wand_input_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_vr_wand_input.cmake"
    DEPENDS
        src/Features/VR/Input.cpp
        src/Features/VR/WandPointing.cpp
        tests/extract_source_region.cmake
        tests/extract_vr_wand_input.cmake
    VERBATIM
)
add_controller_test(vr_wand_input_test VRWandInput tests/vr_wand_input_test.cpp)
target_sources(vr_wand_input_test PRIVATE ${_wand_input_test_headers})
target_include_directories(vr_wand_input_test PRIVATE "${_wand_input_test_dir}")
target_link_libraries(vr_wand_input_test PRIVATE imgui::imgui)
