if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

file(READ "${PROJECT_ROOT}/src/Features/VR.cpp" _vr_source)
file(READ "${PROJECT_ROOT}/src/Features/VR/InSceneOverlay.cpp" _in_scene_source)

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
    "bool CanConfigureMenuLayout()"
    "void DrawKeepDesktopWindowFocusedForVRMenuSetting();"
    "return REL::Module::IsVR();"
    "menu layout runtime gate"
)
assert_section_contains(
    "void VR::DrawEssentialSettings()"
    "json VR::CapturePerformanceSettingsState() const"
    "CanConfigureMenuLayout()"
    "VR Essentials UI"
)
assert_section_contains(
    "void VR::DrawEssentialSettings()"
    "json VR::CapturePerformanceSettingsState() const"
    "DrawMenuLayoutUnlockSetting();"
    "VR Essentials UI"
)
assert_section_contains(
    "if (!CanConfigureMenuLayout())"
    "ImGui::SliderFloat(\"Mouse Speed\""
    "DrawMenuLayoutUnlockSetting();"
    "VR Advanced UI"
)
assert_section_contains(
    "float VR::GetEffectiveMenuScale() const"
    "Vector3 VR::GetEffectiveHMDMenuOffset() const"
    "Config::kDefaultMenuScale"
    "effective headset scale policy"
)
assert_section_contains(
    "void VR::SetMenuLayoutUnlocked(bool a_unlocked)"
    "void VR::UpdateVROverlayPosition()"
    "savedUnlockedFixedWorldOverlayPosition = fixedWorldOverlayPosition"
    "menu layout relock transition"
)
assert_section_contains(
    "void VR::SetMenuLayoutUnlocked(bool a_unlocked)"
    "void VR::UpdateVROverlayPosition()"
    "fixedWorldOverlayPosition = savedUnlockedFixedWorldOverlayPosition"
    "menu layout unlock transition"
)

foreach(_required_behavior IN ITEMS
    "ImGui::Checkbox(\"Unlock Menu Position and Size\""
    "vr.settings.UnlockMenuPositionAndSize"
    "vr.SetMenuLayoutUnlocked(layoutUnlocked)"
    "float VR::GetEffectiveMenuScale() const"
)
    string(FIND "${_vr_source}" "${_required_behavior}" _behavior_position)
    if(_behavior_position EQUAL -1)
        message(FATAL_ERROR
            "Shared menu layout toggle behavior is missing: ${_required_behavior}"
        )
    endif()
endforeach()

string(FIND
    "${_in_scene_source}"
    "const float menuScale = GetEffectiveMenuScale();"
    _effective_in_scene_scale_position
)
if(_effective_in_scene_scale_position EQUAL -1)
    message(FATAL_ERROR "In-scene menu presentation bypasses effective scale")
endif()

foreach(_forbidden_scale_path IN ITEMS
    "baseWidth * settings.VRMenuScale"
    "CreateHMDOverlayScaleMatrix(settings.VRMenuScale)"
    "CreateOverlayScaleMatrix(settings.VRMenuScale)"
)
    string(FIND "${_vr_source}" "${_forbidden_scale_path}" _vr_scale_position)
    string(FIND
        "${_in_scene_source}"
        "${_forbidden_scale_path}"
        _in_scene_scale_position
    )
    if(NOT _vr_scale_position EQUAL -1 OR NOT _in_scene_scale_position EQUAL -1)
        message(FATAL_ERROR
            "Menu presentation bypasses effective scale: ${_forbidden_scale_path}"
        )
    endif()
endforeach()

message(STATUS "VR menu layout UI and effective policy are coherent")
