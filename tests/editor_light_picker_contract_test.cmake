if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

file(READ "${PROJECT_ROOT}/src/Api/EditorDevBenchBridge.cpp" _bridge)
file(READ "${PROJECT_ROOT}/src/Api/EditorService.cpp" _service)
file(READ "${PROJECT_ROOT}/src/CSEditor/EditorWindow.cpp" _editor)
file(READ "${PROJECT_ROOT}/src/CSEditor/EditorWindow.h" _editor_header)
file(READ "${PROJECT_ROOT}/src/CSEditor/LightPicker.cpp" _picker)
file(READ "${PROJECT_ROOT}/src/Features/VR/Input.cpp" _vr_input)
file(READ "${PROJECT_ROOT}/src/Menu/OverlayRenderer.cpp" _overlay)

foreach(_action IN ITEMS open_light_editor begin_light_pick cancel_light_pick)
    string(FIND "${_bridge}" "if (a_action == \"${_action}\")" _parse_position)
    string(FIND "${_bridge}" "\"${_action}\", " _registry_middle_position)
    string(FIND "${_bridge}" "\"${_action}\" })" _registry_last_position)
    string(FIND "${_bridge}" "\"${_action}\"]" _schema_last_position)
    string(FIND "${_bridge}" "\"${_action}\",\"" _schema_middle_position)
    if(_parse_position EQUAL -1 OR
       (_registry_middle_position EQUAL -1 AND _registry_last_position EQUAL -1))
        message(FATAL_ERROR "Editor DevBench action is not parsed and registered: ${_action}")
    endif()
    if(_schema_last_position EQUAL -1 AND _schema_middle_position EQUAL -1)
        message(FATAL_ERROR "Editor DevBench schema is missing: ${_action}")
    endif()
endforeach()

foreach(_snapshot_field IN ITEMS
    "\"lightEditor\""
    "\"selected\""
    "\"enabled\""
    "\"viewportVisible\""
    "\"pickerActive\""
    "\"deferredWorkPending\""
)
    string(FIND "${_bridge}" "${_snapshot_field}" _snapshot_position)
    if(_snapshot_position EQUAL -1)
        message(FATAL_ERROR "Editor DevBench snapshot is missing: ${_snapshot_field}")
    endif()
endforeach()

string(FIND "${_overlay}" "processInputEventQueue();" _input_position)
string(FIND "${_overlay}" "editorWindow->UpdateOpenState();" _transition_position)
string(FIND "${_overlay}" "editorWindow->AdvanceLightEditorDeferredWork();" _deferred_position)
string(FIND "${_overlay}" "auto drawableOverlays = CollectDrawableFeatureOverlays(menu);" _collect_position)
string(FIND "${_overlay}" "if (ShouldSkipRendering(menu, !drawableOverlays.empty()))" _skip_position)
if(_input_position EQUAL -1 OR _deferred_position LESS _input_position OR
   _transition_position LESS _deferred_position OR _collect_position LESS _transition_position OR
   _skip_position LESS _collect_position)
    message(FATAL_ERROR "Editor lifecycle work must run once after input and before overlay skip")
endif()

string(REGEX MATCHALL
    "AdvanceLightEditorDeferredWork\\(\\)"
    _deferred_calls
    "${_overlay}"
)
list(LENGTH _deferred_calls _deferred_call_count)
if(NOT _deferred_call_count EQUAL 1)
    message(FATAL_ERROR "Overlay must advance deferred editor work exactly once")
endif()

string(FIND "${_editor}" "void EditorWindow::UpdateOpenState()" _open_state_position)
string(FIND "${_editor}" "void EditorWindow::AdvanceLightEditorDeferredWork()" _advance_function_position)
string(FIND "${_editor}" "lightEditor.TickDeferredWork();" _tick_position)
if(_open_state_position EQUAL -1 OR _advance_function_position LESS _open_state_position OR
   _tick_position LESS _advance_function_position)
    message(FATAL_ERROR "UpdateOpenState must remain transition-only")
endif()

string(FIND "${_editor}" "viewportImageRect = {};" _clear_position)
string(FIND "${_editor}" ".min = imageMin" _publish_position)
string(FIND "${_editor}" "const bool visible = Util::BeginWithRoundedClose(\"Viewport\"" _begin_position)
string(FIND "${_editor}" "if (!visible)" _hidden_position)
string(FIND "${_editor}" "!std::isfinite(availableSpace.x)" _available_space_position)
string(FIND "${_editor}" "tempTexture->desc.Width > 0 && tempTexture->desc.Height > 0" _render_size_position)
string(FIND "${_editor_header}" "return open && settings.showViewport && viewportImageRect.valid;" _visible_contract_position)
string(FIND "${_editor}" "lightEditor.GatherLights();" _gather_position)
string(FIND "${_editor}" "ShowObjectsWindow();" _objects_position)
string(FIND "${_editor}" "ShowViewportWindow();" _viewport_position)
string(FIND "${_editor}" "lightEditor.UpdatePicker();" _picker_position)
string(FIND "${_editor}" "ShowWidgetWindow();" _widgets_position)
if(_clear_position EQUAL -1 OR _publish_position LESS _clear_position OR
   _begin_position EQUAL -1 OR _hidden_position LESS _begin_position OR
   _available_space_position LESS _hidden_position OR _publish_position LESS _available_space_position OR
   _render_size_position LESS _available_space_position OR _publish_position LESS _render_size_position OR
   _visible_contract_position EQUAL -1 OR
   _gather_position EQUAL -1 OR _objects_position LESS _gather_position OR
   _viewport_position EQUAL -1 OR _picker_position LESS _viewport_position OR
   _widgets_position LESS _picker_position)
    message(FATAL_ERROR "Light gathering and viewport readiness must precede same-frame picking")
endif()

foreach(_viewport_contract IN ITEMS
    ".min = imageMin"
    ".max = ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y)"
    ".renderSize = ImVec2("
    ".valid = true"
)
    string(FIND "${_editor}" "${_viewport_contract}" _viewport_contract_position)
    if(_viewport_contract_position EQUAL -1)
        message(FATAL_ERROR "Viewport image mapping is incomplete: ${_viewport_contract}")
    endif()
endforeach()

foreach(_service_contract IN ITEMS
    "MutationAction::kOpenLightEditor"
    "MutationAction::kBeginLightPick"
    "MutationAction::kCancelLightPick"
    "lightDeferredWorkPending"
    "lightViewportVisible"
)
    string(FIND "${_service}" "${_service_contract}" _service_position)
    if(_service_position EQUAL -1)
        message(FATAL_ERROR "Editor service light-picker contract is missing: ${_service_contract}")
    endif()
endforeach()

string(REGEX MATCHALL
    "SupportsMutationAction\\(a_capabilities, mutation.action\\)"
    _capability_gates
    "${_service}"
)
list(LENGTH _capability_gates _capability_gate_count)
if(NOT _capability_gate_count EQUAL 2)
    message(FATAL_ERROR "Editor preflight and execute must both enforce interface capabilities")
endif()

foreach(_vr_click_owner_contract IN ITEMS
    "RecordQueuedWandClickOwner(logicalButton, eventController);"
    "const int targetFrame = ImGui::GetFrameCount() + 1;"
    "!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || click.frame != ImGui::GetFrameCount()"
)
    string(FIND "${_vr_input}" "${_vr_click_owner_contract}" _vr_click_owner_position)
    if(_vr_click_owner_position EQUAL -1)
        message(FATAL_ERROR "VR click ownership is not bounded to its consuming ImGui frame: ${_vr_click_owner_contract}")
    endif()
endforeach()

string(FIND "${_overlay}" "globals::features::vr.DiscardQueuedImGuiClickOwners();" _discard_click_owner_position)
if(_discard_click_owner_position EQUAL -1)
    message(FATAL_ERROR "Discarded ImGui events must also discard queued VR click provenance")
endif()

foreach(_picker_input_contract IN ITEMS
    "GetImGuiLeftClickWandController()"
    "GetVRPointerSource(clickController)"
    "escapePressed && ImGui::IsKeyDown(ImGuiKey_Escape)"
)
    string(FIND "${_picker}" "${_picker_input_contract}" _picker_input_position)
    if(_picker_input_position EQUAL -1)
        message(FATAL_ERROR "Light picker input provenance contract is missing: ${_picker_input_contract}")
    endif()
endforeach()

message(STATUS "Editor Light Picker lifecycle and DevBench contract is coherent")
