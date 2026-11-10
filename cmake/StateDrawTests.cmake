set(_state_draw_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/state-draw")
set(_state_draw_headers "${_state_draw_test_dir}/state_draw_pending_under_test.h"
    "${_state_draw_test_dir}/state_draw_gate_under_test.h")
add_custom_command(OUTPUT ${_state_draw_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_state_draw_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_state_draw_pending.cmake"
    DEPENDS src/State.cpp src/Features/Upscaling.h
        tests/extract_source_region.cmake tests/extract_state_draw_pending.cmake
    VERBATIM)
add_controller_test(state_draw_pending_test StateDrawPending tests/state_draw_pending_test.cpp)
target_sources(state_draw_pending_test PRIVATE ${_state_draw_headers})
target_include_directories(state_draw_pending_test PRIVATE "${_state_draw_test_dir}")
