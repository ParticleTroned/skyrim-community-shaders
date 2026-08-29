if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

file(READ "${PROJECT_ROOT}/src/Features/VR.cpp" _vr_source)

function(assert_section_contains _start_marker _end_marker _required_text _surface)
    string(FIND "${_vr_source}" "${_start_marker}" _start)
    string(FIND "${_vr_source}" "${_end_marker}" _end)
    if(_start EQUAL -1 OR _end EQUAL -1 OR _end LESS_EQUAL _start)
        message(FATAL_ERROR "Unable to inspect the ${_surface} menu surface")
    endif()

    math(EXPR _length "${_end} - ${_start}")
    string(SUBSTRING "${_vr_source}" ${_start} ${_length} _section)
    string(FIND "${_section}" "${_required_text}" _required_position)
    if(_required_position EQUAL -1)
        message(FATAL_ERROR
            "${_surface} does not expose the shared menu layout toggle"
        )
    endif()
endfunction()

assert_section_contains(
    "void VR::DrawEssentialSettings()"
    "json VR::CapturePerformanceSettingsState() const"
    "DrawMenuLayoutUnlockSetting();"
    "VR Essentials UI"
)
assert_section_contains(
    "if (ImGui::CollapsingHeader(\"Menu Settings\")) {"
    "ImGui::SliderFloat(\"Mouse Speed\""
    "DrawMenuLayoutUnlockSetting();"
    "VR Advanced UI"
)

foreach(_required_behavior IN ITEMS
    "ImGui::Checkbox(\"Unlock Menu Position and Size\""
    "vr.settings.UnlockMenuPositionAndSize"
    "vr.SetMenuLayoutUnlocked(layoutUnlocked)"
)
    string(FIND "${_vr_source}" "${_required_behavior}" _behavior_position)
    if(_behavior_position EQUAL -1)
        message(FATAL_ERROR
            "Shared menu layout toggle behavior is missing: ${_required_behavior}"
        )
    endif()
endforeach()

message(STATUS "VR menu layout toggle is exposed in Essentials and Advanced UI")
