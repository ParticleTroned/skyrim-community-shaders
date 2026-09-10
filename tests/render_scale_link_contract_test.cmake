cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

file(READ "${PROJECT_ROOT}/src/Features/Upscaling.h" _header)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _source)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/VRRenderScaleModePolicy.h" _policy)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp" _bridge)
file(READ "${PROJECT_ROOT}/src/Api/UpscalingService.cpp" _api)
file(READ "${PROJECT_ROOT}/include/VRAPI/CSpluginapi.h" _legacy_api)
foreach(_text IN ITEMS _header _source _policy _bridge _api _legacy_api)
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
require_contract("${_header}" [[UpscalingTransitionApplyResult ApplyCSMenuUpscalingTransition(]]
    "structured transition acceptance result")
section("${_source}" [[NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT( Upscaling::Settings,]]
    [[decltype(&D3D11CreateDeviceAndSwapChain)]] _serializer)
require_contract("${_serializer}" [[renderScaleLinkedToUpscaling,]] "JSON persistence")
section("${_source}" [[void ResetVRSpecificUpscalingSettings(]]
    [[void StripVRSpecificUpscalingSettings(]] _non_vr_reset)
require_contract("${_non_vr_reset}" [[settings.renderScaleLinkedToUpscaling = false;]] "non-VR reset")
require_contract("${_source}" [[o_json.erase("renderScaleLinkedToUpscaling");]] "non-VR serialization")
section("${_source}" [[void Upscaling::SaveSettings(]]
    [[void Upscaling::OnSettingsSaved()]] _save)
require_contract("${_save}"
    [[if (globals::game::isVR && HasPendingVRUpscalingTransition()) { Settings persistedSettings = CaptureEffectiveVRUpscalingSettings(*this); SanitizeUpscalingSettings(persistedSettings); o_json = persistedSettings; }]]
    "accepted pending selection is serialized from a detached settings copy")
section("${_source}" [[void ApplyVRUpscalingDesiredProfileToSettings(]]
    [[Upscaling::Settings CaptureEffectiveVRUpscalingSettings(]] _apply_desired)
require_contract("${_apply_desired}"
    [[a_settings.renderScaleMode = a_profile.renderScaleModePreference ? 1u : 0u;]]
    "captured settings retain quality-independent preference")
require_contract("${_apply_desired}"
    [[a_settings.perfMode = a_profile.perfModeEnabled ? 1u : 0u;]]
    "captured settings retain separate physical mode")
section("${_source}" [[Upscaling::Settings CaptureEffectiveVRUpscalingSettings(]]
    [[json Upscaling::CapturePerformanceSettingsState()]] _capture)
require_contract("${_capture}" [[Upscaling::Settings capturedSettings = a_upscaling.settings;]]
    "capture starts from a detached settings copy")
require_contract("${_capture}" [[if (!globals::game::isVR) return capturedSettings;]]
    "pending capture is VR-only")
require_contract("${_capture}" [[if (desiredProfile.HasPendingSettings())]]
    "capture includes accepted pending selections")
require_contract("${_capture}" [[ApplyVRUpscalingDesiredProfileToSettings( capturedSettings, desiredProfile);]]
    "capture applies the shared desired-profile serializer")
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
    [[void Upscaling::RestoreDefaultSettings()]] _load)
forbid_contract("${_load}" [[renderScaleLinkedToUpscaling]] "legacy settings load")
forbid_contract("${_load}" [[GetVRRenderScalePreferenceForSelection]] "legacy settings load")
section("${_source}" [[void ApplyLoadedVRUpscalingTransition(]]
    [[void Upscaling::LoadSettings(]] _loaded_transition)
require_contract("${_loaded_transition}"
    [[a_currentDesiredProfile.renderScaleModePreference != targetRenderScale.preference]]
    "reload detects preference changes even at native AA")
require_contract("${_loaded_transition}"
    [[a_upscaling.ApplyCSMenuUpscalingTransition( targetMethod, targetRenderScale.preference,]]
    "reload submits serialized preference without applying the link")
forbid_contract("${_loaded_transition}" [[GetVRRenderScalePreferenceForSelection]]
    "loaded explicit preference")

section("${_policy}" [[constexpr State Resolve(]] [[}; }]] _explicit_policy)
forbid_contract("${_explicit_policy}" [[a_linkedToUpscaling]] "explicit preference resolution")
require_contract("${_explicit_policy}" [[a_methodEligible && a_requestedPreference]] "durable explicit preference")
require_contract("${_explicit_policy}" [[.enabled = preference && a_qualityEligible]] "native-AA physical gating")
section("${_source}" [[void SanitizeUpscalingSettings(]]
    [[void ApplyLegacyFsr4RuntimeSelectionMigration(]] _sanitize)
require_contract("${_sanitize}"
    [[if (REL::Module::IsVR() && !IsRenderScaleQualityMode(settings.qualityMode)) { settings.perfMode = 0; }]]
    "native-AA sanitization suspends physical scaling without erasing preference")

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
require_contract("${_checkbox}" [[SetRenderScaleLinkedToUpscaling(linked)]]
    "UI uses the shared link setter")
forbid_contract("${_checkbox}" [[kDefaultRenderScaleQualityMode]] "native-AA quality on link enable")
section("${_source}" [[bool Upscaling::SetRenderScaleLinkedToUpscaling(]]
    [[void Upscaling::DrawVRRenderScaleLinkSetting(]] _link_setter)
require_contract("${_link_setter}"
    [[const auto desiredProfile = queuedSelection ? *queuedSelection : GetPendingVRRenderScaleDesiredProfile();]]
    "link enabling reads one complete pending selection")
require_contract("${_link_setter}"
    [[std::scoped_lock lock(pendingVRRenderScaleRequestMutex); auto selected = pendingVRRenderScaleRequest;]]
    "link selection snapshots both request slots under their existing mutex")
require_contract("${_link_setter}"
    [[if (deferredVRRenderScaleRequestAfterPhysicalRecovery && (!selected || IsVRRenderScaleRecoveryOrigin(selected->origin) || deferredVRRenderScaleRequestAfterPhysicalRecovery->requestID > selected->requestID)) { selected = deferredVRRenderScaleRequestAfterPhysicalRecovery; }]]
    "link selection retains deferred user intent over an internal recovery request")
require_contract("${_link_setter}" [[if (IsRenderScaleMethodEligible(desiredProfile.method))]]
    "link enabling does not replace a pending None/TAA selection")
require_contract("${_link_setter}"
    [[desiredProfile.method, true, desiredProfile.qualityMode, desiredProfile.dlssPreset,]]
    "link enabling retains the pending method, quality, and DLSS profile together")
require_contract("${_link_setter}" [[0, desiredProfile.fsr4RuntimeEnabled,]]
    "link enabling retains the pending FSR runtime selection")
forbid_contract("${_link_setter}" [[GetConfiguredUpscaleMethodForTransition()]]
    "pending method selection on link enable")
require_contract("${_link_setter}" [[if (!globals::game::isVR) return false;]]
    "link setter is VR-only")
require_contract("${_link_setter}"
    [[if (result.disposition == UpscalingTransitionApplyDisposition::Rejected) return false; } } settings.renderScaleLinkedToUpscaling = a_enabled; return true;]]
    "link preference changes only after accepted enabling; unlinking is preference-only")
forbid_contract("${_link_setter}" [[kDefaultRenderScaleQualityMode]] "native-AA quality on link enable")

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
        [[if \([^;{}]*\.disposition != UpscalingTransitionApplyDisposition::Rejected && !enableRenderScaleMode\) SetRenderScaleLinkedToUpscaling\(false\);]]
        _accepted_manual_off "${${_menu}}")
    if(NOT _accepted_manual_off)
        message(FATAL_ERROR "${_menu} must unlink only after an accepted explicit Render Scale-off transition")
    endif()
endforeach()

section("${_source}" [[Upscaling::UpscalingTransitionApplyResult Upscaling::ApplyCSMenuUpscalingTransition(]]
    [[void Upscaling::SetVRUpscalingTransitionProfile(]] _transition)
forbid_contract("${_transition}" [[renderScaleLinkedToUpscaling]] "explicit transition requests")
forbid_contract("${_transition}" [[GetVRRenderScalePreferenceForSelection]] "explicit transition requests")
require_contract("${_transition}"
    [[if (ApplyOpenCompositeUpscalingBlocker(true)) return { .disposition = UpscalingTransitionApplyDisposition::Rejected, .rejection = UpscalingTransitionApplyRejection::OpenComposite, };]]
    "blocked transition rejection")
require_contract("${_transition}"
    [[if (stageVRUpscalingChange && !ShouldAcceptVRUpscalingTransitionRequest(*this, a_origin)) { return { .disposition = UpscalingTransitionApplyDisposition::Rejected, .rejection = UpscalingTransitionApplyRejection::TransitionOwnership, }; }]]
    "transition admission rejection")
require_contract("${_transition}"
    [[if (!queueResult.Accepted()) { return { .disposition = UpscalingTransitionApplyDisposition::Rejected, .rejection = UpscalingTransitionApplyRejection::QueueRejected, }; }]]
    "queue rejection")
require_contract("${_transition}"
    [[.disposition = UpscalingTransitionApplyDisposition::Queued, .requestID = queueResult.requestID, .transitionEpoch = queueResult.transitionEpoch,]]
    "accepted transition retains queue identity")
require_contract("${_transition}"
    [[currentDesiredProfile.renderScaleModePreference == targetRenderScalePreference]]
    "native-AA preferences remain distinct during request coalescing")
require_contract("${_transition}"
    [[QueueVRRenderScaleRequest( targetMethod, targetRenderScalePreference,]]
    "transition queues durable intent rather than physical enablement")

section("${_source}"
    [[Upscaling::VRRenderScaleDesiredProfile Upscaling::GetPendingVRRenderScaleDesiredProfile()]]
    [[bool Upscaling::ResolvePendingVRUpscalingProviderSelection()]] _desired)
require_contract("${_desired}"
    [[profile.renderScaleModePreference = renderScale.preference; profile.renderScaleModeEnabled = renderScale.enabled;]]
    "current desired profile separates preference from physical enablement")
section("${_source}"
    [[Upscaling::VRRenderScaleRequestQueueResult Upscaling::QueueVRRenderScaleRequest(]]
    [[std::optional<Upscaling::VRRenderScaleDesiredProfile> Upscaling::TakePendingVRRenderScaleRequest()]] _queue)
require_contract("${_queue}" [[request.renderScaleModePreference = renderScale.preference;]]
    "immutable queue retains preference")
require_contract("${_queue}" [[request.renderScaleModeEnabled = renderScaleModeEnabled;]]
    "immutable queue retains physical mode separately")
forbid_contract("${_queue}" [[renderScaleLinkedToUpscaling]] "explicit queued preferences")
forbid_contract("${_queue}" [[GetVRRenderScalePreferenceForSelection]] "explicit queued preferences")
section("${_source}" [[void Upscaling::ApplyPendingVRUpscalingTransition()]]
    [[void Upscaling::FillMenuCameraMotionVectors()]] _consume)
require_contract("${_consume}"
    [[IsRenderScaleQualityMode(targetQualityMode), request.renderScaleModePreference);]]
    "consumer resolves immutable preference with the selected quality")
require_contract("${_consume}"
    [[const uint32_t requestedRenderScaleMode = targetRenderScalePreference ? 1u : 0u;]]
    "consumer stores durable preference at native AA")
require_contract("${_consume}" [[settings.renderScaleMode = requestedRenderScaleMode;]]
    "consumer commits durable preference")
forbid_contract("${_consume}" [[renderScaleLinkedToUpscaling]] "explicit consumed preferences")
forbid_contract("${_consume}" [[GetVRRenderScalePreferenceForSelection]] "explicit consumed preferences")

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
section("${_source}" [[bool MatchesVRFpsStabilizerTransitionTarget(]]
    [[bool HasCurrentVRRenderScaleControllerTarget(]] _stabilizer_match)
require_contract("${_stabilizer_match}" [[a_renderScaleMode == a_target.renderScaleMode]]
    "public native-AA profiles compare executable mode")
forbid_contract("${_stabilizer_match}" [[a_target.renderScaleModePreference]]
    "public API admission of native-AA profiles with suspended preference")
section("${_source}" [[void Upscaling::ApplyPendingVRFpsStabilizerLoadSync()]]
    [[bool Upscaling::IsPerfModePresentationActive()]] _stabilizer_sync)
require_contract("${_stabilizer_sync}"
    [[GetVRRenderScaleModePreference() == target.renderScaleModePreference && MatchesVRFpsStabilizerTransitionTarget( currentMethod, IsRenderScaleModeRequested(),]]
    "local load sync reconciles preference separately from public active mode")

forbid_contract("${_bridge}" [[GetVRRenderScalePreferenceForSelection]] "explicit DevBench profiles")
forbid_contract("${_bridge}" [[ResolveSelectionPreference]] "explicit DevBench profiles")
section("${_bridge}" [[if (action == "set_render_scale_link")]]
    [[if (action == "qualification_status")]] _devbench_setter)
require_contract("${_devbench_setter}"
    [[if (!a_args.contains("enabled") || !a_args["enabled"].is_boolean())]]
    "DevBench validates its explicit boolean")
require_contract("${_devbench_setter}"
    [[return RunOnMainThread([enabled = a_args["enabled"].get<bool>()]()]]
    "DevBench uses the main-thread settings boundary")
require_contract("${_devbench_setter}" [[if (!globals::game::isVR)]]
    "DevBench rejects non-VR sessions")
require_contract("${_devbench_setter}" [[!globals::state->IsDeveloperMode()]]
    "DevBench retains developer-mode admission")
require_contract("${_devbench_setter}" [[!upscaling.GetVRRenderScaleStressSessionSnapshot().active]]
    "DevBench requires an active capture")
require_contract("${_devbench_setter}" [[upscaling.SetRenderScaleLinkedToUpscaling(enabled)]]
    "DevBench uses the same admission-aware setter as the checkbox")
require_contract("${_devbench_setter}" [[{ "accepted", accepted }]]
    "DevBench reports admission rather than assuming physical completion")
require_contract("${_bridge}"
    [[descriptor["inputSchema"]["properties"]["action"]["enum"].push_back("set_render_scale_link");]]
    "DevBench publishes the new action in its schema")
require_contract("${_bridge}" [[set_render_scale_link requires boolean enabled, developer mode,]]
    "DevBench documents the action")
require_contract("${_bridge}" [[{ "rememberedPreference", a_upscaling.GetVRRenderScaleModePreference() }]]
    "DevBench reports durable intent separately from active scaling")

section("${_api}" [[Snapshot001 BuildSnapshot(]] [[output.effective = MakeProfile(]]
    _configured_api)
require_contract("${_configured_api}"
    [[const auto configuredMethod = upscaling.GetConfiguredUpscaleMethodForTransition();]]
    "configured API profile uses configured rather than pending method")
require_contract("${_configured_api}"
    [[VRRenderScaleModePolicy::Resolve( configuredMethod == Upscaling::UpscaleMethod::kDLSS || configuredMethod == Upscaling::UpscaleMethod::kFSR, upscaling.settings.qualityMode != 0, upscaling.settings.renderScaleMode != 0);]]
    "configured API profile gates native AA and non-vendor methods")
require_contract("${_configured_api}"
    [[output.configured = MakeProfile( configuredMethod, upscaling.settings.qualityMode, configuredRenderScale.enabled,]]
    "configured API profile exposes executable mode rather than suspended preference")
forbid_contract("${_configured_api}" [[GetVRRenderScaleModePreference()]]
    "configured API mode during native AA")
forbid_contract("${_configured_api}" [[GetPendingVRRenderScaleDesiredProfile()]]
    "configured API profile during pending transitions")

section("${_legacy_api}" [[inline void CSInterface001::SetUpscalePreset(]]
    [[inline bool CSInterface001::GetLightLimitFixContactShadowsEnabled()]] _legacy_preset)
require_contract("${_legacy_preset}"
    [[globals::game::isVR ? upscaling.GetVRRenderScaleModePreference() :]]
    "legacy preset edits preserve VR preference across native AA")
section("${_legacy_api}" [[inline void CSInterface001::SetDLSSProfile(]]
    [[inline bool CSInterface001::GetRenderAtUpscaleResEnabled()]] _legacy_profile)
require_contract("${_legacy_profile}"
    [[upscaling.ApplyCSMenuUpscalingTransition( upscaleMethod, upscaling.GetVRRenderScaleModePreference(),]]
    "legacy DLSS profile edits retain preference without enabling the link")
section("${_legacy_api}" [[inline void CSInterface001::SetUpscaleMethod(]]
    [[inline void CSInterface001::SetVRUpscalingTransitionProfileForMethod(]] _legacy_method)
require_contract("${_legacy_method}"
    [[globals::game::isVR ? upscaling.GetVRRenderScaleModePreference() :]]
    "legacy method edits preserve VR preference")
forbid_contract("${_legacy_api}" [[GetVRRenderScalePreferenceForSelection]]
    "legacy API explicit preferences")
forbid_contract("${_legacy_api}" [[ResolveSelectionPreference]]
    "legacy API explicit preferences")

message(STATUS "VR Render Scale link contract passed")
