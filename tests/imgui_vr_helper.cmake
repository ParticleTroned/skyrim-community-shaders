get_filename_component(
    _helper_source_root
    "${CMAKE_CURRENT_LIST_DIR}/.."
    ABSOLUTE
)
set(_helper_capture_generated
    "${CMAKE_CURRENT_BINARY_DIR}/generated/imgui_vr_helper"
)
set(_helper_capture_header
    "${_helper_capture_generated}/imgui_vr_helper_scene_capture_under_test.h"
)
add_custom_command(
    OUTPUT "${_helper_capture_header}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${_helper_source_root}"
        "-DOUTPUT_DIRECTORY=${_helper_capture_generated}" -P
        "${_helper_source_root}/tests/extract_imgui_vr_helper_scene_capture.cmake"
    DEPENDS
        "${_helper_source_root}/src/Features/VR/ImGuiVRHelperSceneCapture.cpp"
        "${_helper_source_root}/tests/extract_imgui_vr_helper_scene_capture.cmake"
    VERBATIM
)
add_custom_target(
    imgui_vr_helper_capture_extraction
    DEPENDS "${_helper_capture_header}"
)

foreach(_helper_contract scene_packet host_policy scene_capture)
    set(_helper_target "imgui_vr_helper_${_helper_contract}_test")
    set(_helper_source "tests/${_helper_target}.cpp")
    set(_helper_test "ImGuiVRHelper_${_helper_contract}")
    add_controller_test(${_helper_target} ${_helper_test} ${_helper_source})
    if(_helper_contract STREQUAL "scene_capture")
        add_dependencies(${_helper_target} imgui_vr_helper_capture_extraction)
        target_include_directories(
            ${_helper_target}
            PRIVATE "${_helper_capture_generated}"
        )
    endif()

    if(MSVC)
        set(_helper_fast_target "${_helper_target}_fast")
        add_controller_test(
            ${_helper_fast_target}
            "${_helper_test}_FastMath"
            ${_helper_source}
        )
        target_compile_options(${_helper_fast_target} PRIVATE /fp:fast)
        if(_helper_contract STREQUAL "scene_capture")
            add_dependencies(
                ${_helper_fast_target}
                imgui_vr_helper_capture_extraction
            )
            target_include_directories(
                ${_helper_fast_target}
                PRIVATE "${_helper_capture_generated}"
            )
        endif()
    endif()
endforeach()

set(_helper_lifecycle_header
    "${_helper_capture_generated}/imgui_vr_helper_render_lifecycle_under_test.h"
)
add_custom_command(
    OUTPUT "${_helper_lifecycle_header}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${_helper_source_root}"
        "-DOUTPUT_DIRECTORY=${_helper_capture_generated}" -P
        "${_helper_source_root}/tests/extract_imgui_vr_helper_render_lifecycle.cmake"
    DEPENDS
        "${_helper_source_root}/src/Hooks.cpp"
        "${_helper_source_root}/tests/extract_imgui_vr_helper_render_lifecycle.cmake"
    VERBATIM
)
add_custom_target(imgui_vr_helper_lifecycle_extraction DEPENDS "${_helper_lifecycle_header}")
add_controller_test(
    imgui_vr_helper_render_lifecycle_test
    ImGuiVRHelper_render_lifecycle
    tests/imgui_vr_helper_render_lifecycle_test.cpp
)
add_dependencies(imgui_vr_helper_render_lifecycle_test imgui_vr_helper_lifecycle_extraction)
target_include_directories(imgui_vr_helper_render_lifecycle_test PRIVATE "${_helper_capture_generated}")
