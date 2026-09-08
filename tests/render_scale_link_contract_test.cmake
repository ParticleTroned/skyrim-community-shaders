cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

file(READ "${PROJECT_ROOT}/src/Features/Upscaling.h" _header)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _source)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/VRRenderScaleModePolicy.h" _policy)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp" _bridge)
foreach(_text IN ITEMS _header _source _policy _bridge)
    string(REGEX REPLACE "[\r\n\t ]+" " " ${_text} "${${_text}}")
endforeach()

function(require_contract _text _needle _label)
    string(FIND "${_text}" "${_needle}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Render Scale link ${_label} contract missing: ${_needle}")
    endif()
endfunction()

function(forbid_contract _text _needle _label)
    string(FIND "${_text}" "${_needle}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR "Render Scale link must not alter ${_label}: ${_needle}")
    endif()
endfunction()

function(section _text _begin _end _result)
    string(FIND "${_text}" "${_begin}" _begin_position)
    if(_begin_position EQUAL -1)
        message(FATAL_ERROR "Cannot isolate Render Scale link contract section: ${_begin}")
    endif()
    string(SUBSTRING "${_text}" ${_begin_position} -1 _tail)
    string(FIND "${_tail}" "${_end}" _length)
    if(_length LESS_EQUAL 0)
        message(FATAL_ERROR "Cannot find end of Render Scale link contract section: ${_begin}")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_length} _contents)
    set(${_result} "${_contents}" PARENT_SCOPE)
endfunction()

require_contract("${_header}" [[bool renderScaleLinkedToUpscaling = true;]] "default")
require_contract("${_header}" [[bool ApplyCSMenuUpscalingTransition(]] "transition acceptance result")
require_contract("${_source}" [[OP(renderScaleLinkedToUpscaling)]] "JSON persistence")
section("${_source}" [[void ResetVRSpecificUpscalingSettings(]]
    [[void StripVRSpecificUpscalingSettings(]] _non_vr_reset)
require_contract("${_non_vr_reset}" [[settings.renderScaleLinkedToUpscaling = false;]] "non-VR reset")
require_contract("${_source}" [[o_json.erase("renderScaleLinkedToUpscaling");]] "non-VR serialization")
section("${_source}" [[void Upscaling::SaveSettings(]]
    [[void Upscaling::LoadSettings(]] _save)
require_contract("${_save}"
    [[const auto desiredProfile = GetPendingVRRenderScaleDesiredProfile();]]
    "save reads accepted pending selection")
require_contract("${_save}"
    [[if (desiredProfile.pending) { o_json["renderScaleMode"] = desiredProfile.renderScaleModePreference ? 1u : 0u; o_json["qualityMode"] = desiredProfile.qualityMode; o_json["dlssPreset"] = desiredProfile.dlssPreset; }]]
    "pending selection is serialized without replacing physical settings")
section("${_save}" [[if (IsVRRuntimeActive())]]
    [[if (!IsVRRuntimeActive())]] _vr_save)
require_contract("${_vr_save}" [[GetPendingVRRenderScaleDesiredProfile()]] "pending-save VR scope")
section("${_save}" [[if (desiredProfile.pending)]]
    [[o_json.erase("perfMode");]] _pending_save)
forbid_contract("${_pending_save}" [[settings.]] "live settings during pending serialization")
foreach(_mutation IN ITEMS
    [[ApplyCSMenuUpscalingTransition(]]
    [[ApplyPendingPerfModeRenderTargetRecreate(]]
    [[QueueVRRenderScaleRequest(]]
    [[RequestPerfModeRenderTargetRecreate(]]
    [[ClearPendingVRUpscalingTransition(]]
    [[SetPerfModeRequested(]]
    [[GetVRRenderScalePreferenceForSelection(]]
)
    forbid_contract("${_save}" "${_mutation}" "transition state while saving")
endforeach()
section("${_source}" [[void Upscaling::LoadSettings(]]
    [[bool Upscaling::HandleNeuralRenderingSettingsTransition(]] _load)
forbid_contract("${_load}" [[renderScaleLinkedToUpscaling]] "legacy settings load")
forbid_contract("${_load}" [[GetVRRenderScalePreferenceForSelection]] "legacy settings load")

section("${_policy}" [[constexpr State Resolve(]] [[}; }]] _explicit_policy)
forbid_contract("${_explicit_policy}" [[a_linkedToUpscaling]] "explicit preference resolution")
require_contract("${_explicit_policy}" [[a_methodEligible && a_requestedPreference]] "durable explicit preference")
require_contract("${_explicit_policy}" [[.enabled = preference && a_qualityEligible]] "native-AA physical gating")

section("${_source}"
    [[bool Upscaling::GetVRRenderScalePreferenceForSelection(]]
    [[bool Upscaling::GetVRRenderScaleModeRequested()]] _selection)
require_contract("${_selection}" [[VRRenderScaleModePolicy::ResolveSelectionPreference(]] "shared selection policy")
require_contract("${_selection}" [[IsRenderScaleMethodEligible(a_targetMethod)]] "method eligibility")
require_contract("${_selection}" [[settings.renderScaleLinkedToUpscaling]] "link input")
require_contract("${_selection}" [[GetVRRenderScaleModePreference()]] "remembered preference input")

section("${_source}" [[void Upscaling::DrawVRRenderScaleLinkSetting(]]
    [[void Upscaling::DrawSettings()]] _checkbox)
require_contract("${_checkbox}"
    [[ImGui::Checkbox("Link Render Scale to DLSS/FSR Upscaling", &linked)]] "exact UI name")
require_contract("${_checkbox}"
    [[a_upscaleMethod, true, GetEffectiveUpscalingQualityMode(), GetEffectiveDLSSPreset(), "render-scale link enabled")]]
    "link enabling preserves selected quality")
forbid_contract("${_checkbox}" [[kDefaultRenderScaleQualityMode]] "native-AA quality on link enable")
require_contract("${_checkbox}" [[settings.renderScaleLinkedToUpscaling = true;]] "accepted link enable")

section("${_source}" [[void Upscaling::DrawSettings()]]
    [[void Upscaling::DrawPerformanceSettings(]] _full_menu)
section("${_source}" [[void Upscaling::DrawPerformanceSettings(]]
    [[void Upscaling::DrawEssentialSettings()]] _performance_menu)
foreach(_menu IN ITEMS _full_menu _performance_menu)
    require_contract("${${_menu}}"
        [[GetVRRenderScalePreferenceForSelection(selectedUpscaleChoice.method)]] "${_menu} method selection")
    require_contract("${${_menu}}"
        [[GetVRRenderScalePreferenceForSelection(upscaleMethod)]] "${_menu} quality selection")
    require_contract("${${_menu}}" [[DrawVRRenderScaleLinkSetting(upscaleMethod);]] "${_menu} shared checkbox")
    string(REGEX MATCH
        [[if \(ApplyCSMenuUpscalingTransition\([^;{}]*&& !enableRenderScaleMode\) \{ settings.renderScaleLinkedToUpscaling = false;]]
        _accepted_manual_off "${${_menu}}")
    if(NOT _accepted_manual_off)
        message(FATAL_ERROR "${_menu} must unlink only after an accepted explicit Render Scale-off transition")
    endif()
endforeach()

section("${_source}" [[bool Upscaling::ApplyCSMenuUpscalingTransition(]]
    [[void Upscaling::SetVRUpscalingTransitionProfile(]] _transition)
forbid_contract("${_transition}" [[renderScaleLinkedToUpscaling]] "explicit transition requests")
forbid_contract("${_transition}" [[GetVRRenderScalePreferenceForSelection]] "explicit transition requests")
require_contract("${_transition}"
    [[if (ApplyOpenCompositeUpscalingBlocker(true)) return false;]] "blocked transition rejection")
require_contract("${_transition}"
    [[if (stageVRUpscalingChange && !ShouldAcceptVRUpscalingTransitionRequest(*this, a_origin)) return false;]]
    "transition admission rejection")
require_contract("${_transition}"
    [[if (QueueVRRenderScaleRequest(targetMethod, targetRenderScalePreference, qualityMode, dlssPreset, a_origin) == 0) return false;]]
    "queue rejection")
require_contract("${_transition}" [[return true;]] "accepted transition acknowledgement")

section("${_source}" [[VRFpsStabilizerTransitionTarget ResolveVRFpsStabilizerTransitionTarget(]]
    [[bool MatchesVRFpsStabilizerTransitionTarget(]] _stabilizer)
require_contract("${_stabilizer}"
    [[a_profile.hasRenderScaleMode ? a_profile.renderScaleMode :]] "explicit stabilizer precedence")
require_contract("${_stabilizer}"
    [[GetVRRenderScalePreferenceForSelection(target.method)]] "partial stabilizer profile fallback")
require_contract("${_stabilizer}"
    [[const bool selectionChanged = target.method != currentMethod || target.qualityMode != a_upscaling.GetEffectiveUpscalingQualityMode();]]
    "stabilizer link applies only to selection changes")
require_contract("${_stabilizer}"
    [[selectionChanged ? a_upscaling.GetVRRenderScalePreferenceForSelection(target.method) : a_upscaling.GetVRRenderScaleModePreference();]]
    "stabilizer preserves non-selection updates")

forbid_contract("${_bridge}" [[GetVRRenderScalePreferenceForSelection]] "explicit DevBench profiles")
forbid_contract("${_bridge}" [[ResolveSelectionPreference]] "explicit DevBench profiles")

message(STATUS "VR Render Scale link contract passed")
