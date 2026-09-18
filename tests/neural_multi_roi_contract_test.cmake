cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(_upscaling_header_path "${PROJECT_ROOT}/src/Features/Upscaling.h")
set(_upscaling_source_path "${PROJECT_ROOT}/src/Features/Upscaling.cpp")
set(_character_settings_json_path "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterSettingsJson.h")
set(
    _renderer_header_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.h"
)
set(
    _character_header_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterRendering.h"
)
set(
    _character_source_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp"
)
set(
    _renderer_source_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.cpp"
)
set(
    _runtime_header_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Runtime.h"
)
set(
    _devbench_bridge_path
    "${PROJECT_ROOT}/src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp"
)
foreach(_required_path IN ITEMS
    "${_upscaling_header_path}"
    "${_upscaling_source_path}"
    "${_character_settings_json_path}"
    "${_renderer_header_path}"
    "${_character_header_path}"
    "${_character_source_path}"
    "${_renderer_source_path}"
    "${_runtime_header_path}"
    "${_devbench_bridge_path}"
)
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR "Required multi-ROI contract input is missing: ${_required_path}")
    endif()
endforeach()

file(READ "${_upscaling_header_path}" _upscaling_header)
file(READ "${_upscaling_source_path}" _upscaling_source)
file(READ "${_character_settings_json_path}" _character_settings_json)
file(READ "${_renderer_header_path}" _renderer_header)
file(READ "${_character_header_path}" _character_header)
file(READ "${_character_source_path}" _character_source)
file(READ "${_renderer_source_path}" _renderer_source)
file(READ "${_runtime_header_path}" _runtime_header)
file(READ "${_devbench_bridge_path}" _devbench_bridge)

# Early reduction must be optional and identify the exact immutable source.
foreach(_early_bounds_contract IN ITEMS
    [[state_->QueueEarlyMaskBounds(a_device, a_context, evidence.get());]]
    [[state_->earlyMaskCaptureSerial_ = state_->AllocatePreparedContentSerial();]]
    [[readback.captureSerial == earlyMaskCaptureSerial_]]
    [[readback.frame == a_args.sourceWorldFrame && readback.frame == capturedFrame_]]
    [[readback.categories == GetEnabledCharacterCategoryMask(a_args.settings)]]
    [[PollCharacterMaskBounds(a_args.context, selected->query.Get(), selected->staging.Get(),]]
    [[MapEarlyCharacterMaskBounds(]]
    [[if (!usedEarlyBounds && a_args.settings.multiRoi)]]
    [[Increment(state_->snapshot_.earlyMaskBounds.geometryFallbacks);]]
)
    string(FIND "${_character_source}" "${_early_bounds_contract}" _early_bounds_position)
    if(_early_bounds_position EQUAL -1)
        message(FATAL_ERROR "Early GPU bounds freshness/fallback contract missing: ${_early_bounds_contract}")
    endif()
endforeach()

set(
    _contract_text
    "${_upscaling_header}\n${_upscaling_source}\n${_renderer_header}\n${_character_header}\n${_character_source}\n${_renderer_source}\n${_runtime_header}"
)

string(APPEND _contract_text "\n${_character_settings_json}")

foreach(_settings_contract IN ITEMS
    [[bool neuralCharacterMultiRoiEnabled = false;]]
    [[bool neuralCharacterMultiRoiSavingsGateEnabled = true;]]
    [[visit("neuralCharacterMultiRoiSavingsGateEnabled", settings.neuralCharacterMultiRoiSavingsGateEnabled, policy.multiRoiSavingsGate);]]
    [[settings.neuralCharacterMultiRoiSavingsGateEnabled = true;]]
    [[o_json.erase("neuralCharacterMultiRoiSavingsGateEnabled");]]
    [[add(a_settings.neuralCharacterMultiRoiSavingsGateEnabled);]]
    [[add(a_settings.multiRoiSavingsGate);]]
    [[visit("neuralCharacterMultiRoiEnabled", settings.neuralCharacterMultiRoiEnabled, policy.multiRoi);]]
    [[NeuralRendering::ReadUpscalingCharacterSettingsJson(a_json, parsed);]]
    [[NeuralRendering::WriteUpscalingCharacterSettingsJson(a_json, a_settings);]]
    [[settings.neuralCharacterMultiRoiEnabled = false;]]
    [[o_json.erase("neuralCharacterMultiRoiEnabled");]]
    [[NeuralRendering::GetUpscalingCharacterSettings(a_settings)]]
    [[add(a_settings.neuralCharacterMultiRoiEnabled);]]
)
    string(FIND "${_contract_text}" "${_settings_contract}"
        _settings_contract_position)
    if(_settings_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI persistence/default/key contract is missing: ${_settings_contract}"
        )
    endif()
endforeach()

foreach(_telemetry_contract IN ITEMS
    [[rendererSnapshot.performance.lastFeatureLogicalEyeCount]]
    [=[attributedPreparation->computeRegionCounts[featureSlot]]=]
    [["physicalSlotMask"]]
    [["logicalSlotMask"]]
    [["computeRegionCount"]]
    [["computeRegions"]]
    [["multiRoiPixels"]]
    [["multiRoiFallback"]]
    [["multiRoiReason"]]
    [["multiRoiDiagnostics"]]
    [["singleFallback"]]
    [["candidateRegions"]]
    [["requiredSavingPixels"]]
    [["passesActiveSavingsGate"]]
    [[std::string_view{ "experimentalMultiRoiSavingsGate" }]]
    [[!parseBoolean("experimentalMultiRoiSavingsGate", a_request.experimentalMultiRoiSavingsGate)]]
    [[requestedSettings.neuralCharacterMultiRoiSavingsGateEnabled = *request.experimentalMultiRoiSavingsGate;]]
    [[previousSettings.neuralCharacterMultiRoiSavingsGateEnabled != requestedSettings.neuralCharacterMultiRoiSavingsGateEnabled]]
    [["experimentalMultiRoiSavingsGate":{"type":"boolean"]]
    [[{ "maskRoiStatus", eye.maskRoiStatus }]]
    [[{ "maskRoiCurrentFrame", eye.maskRoiCurrentFrame }]]
    [[{ "maskRoiGpuProvenEmpty", eye.maskRoiGpuProvenEmpty }]]
    [[{ "maskRoiOccupiedTiles", eye.maskRoiOccupiedTiles }]]
    [[{ "maskRoiRequiredSubrect", {]]
    [[{ "valid", eye.maskRoiRequiredSubrect.IsValid() }]]
    [[{ "maskRoiReadbackWaitMs", eye.maskRoiReadbackWaitMs }]]
    [[{ "maskRoiLastFailure", eye.maskRoiLastFailure }]]
    [[{ "maskRoiLastFailureResult", eye.maskRoiLastFailureResult }]]
    [[{ "maskRoiLastFailureWaitMs", eye.maskRoiLastFailureWaitMs }]]
    [[{ "maskRoiReadbackAttempts", eye.maskRoiReadbackAttempts }]]
    [[{ "maskRoiReadbackSuccesses", eye.maskRoiReadbackSuccesses }]]
    [[{ "maskRoiReadbackFallbacks", eye.maskRoiReadbackFallbacks }]]
)
    string(FIND "${_devbench_bridge}" "${_telemetry_contract}"
        _telemetry_contract_position)
    if(_telemetry_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI DevBench attribution contract is missing: ${_telemetry_contract}"
        )
    endif()
endforeach()

foreach(_mask_roi_snapshot_contract IN ITEMS
    [[std::string maskRoiStatus = "disabled";]]
    [[bool maskRoiCurrentFrame = false;]]
    [[bool maskRoiGpuProvenEmpty = false;]]
    [[std::uint32_t maskRoiOccupiedTiles = 0;]]
    [[ComputeSubrect maskRoiRequiredSubrect{};]]
    [[double maskRoiReadbackWaitMs = 0.0;]]
    [[std::uint64_t maskRoiReadbackAttempts = 0;]]
    [[std::uint64_t maskRoiReadbackSuccesses = 0;]]
    [[std::uint64_t maskRoiReadbackFallbacks = 0;]]
)
    string(FIND "${_character_header}" "${_mask_roi_snapshot_contract}" _mask_roi_snapshot_position)
    if(_mask_roi_snapshot_position EQUAL -1)
        message(FATAL_ERROR "Current prepared-mask ROI snapshot contract is missing: ${_mask_roi_snapshot_contract}")
    endif()
endforeach()

foreach(_prepare_contract IN ITEMS
    [[CharacterComputeRegionPlan computeRegions{};]]
    [[a_args.computeRegions = {};]]
    [[a_args.computeRegions = result.computeRegions;]]
    [[GetPreparedComputeRegions(]]
    [[FindPreparedSlot(]]
    [[FinalizePreparedMasks(]]
)
    string(FIND "${_contract_text}" "${_prepare_contract}"
        _prepare_contract_position)
    if(_prepare_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI prepare/result threading contract is missing: ${_prepare_contract}"
        )
    endif()
endforeach()

# ROI preparation must not reintroduce a GPU queue drain. Delayed diagnostic
# sampling retains its existing single nonblocking poll contract separately.
foreach(_blocking_roi_token IN ITEMS
    [[ReadCharacterMaskBounds(]]
    [[QueueCurrentMaskBounds(]]
    [[ResolveCurrentMaskBounds(]]
    [[deferMaskRoiReadback]]
    [[->Flush(]]
    [[Sleep(]]
    [[YieldProcessor(]]
)
    string(FIND "${_character_source}" "${_blocking_roi_token}" _blocking_position)
    if(NOT _blocking_position EQUAL -1)
        message(FATAL_ERROR "ROI preparation must not wait on GPU bounds: ${_blocking_roi_token}")
    endif()
endforeach()

foreach(_execution_contract IN ITEMS
    [[static constexpr std::size_t kFeatureSlotCount = 8;]]
    [[PhysicalRegionFeatureSlot(logical.featureSlot, region)]]
    [[physical.computeSubrect = plan.regions[region];]]
    [[resources[index].historyKey.regionIdentity = regionIdentities[index];]]
    [[.logicalEyeCount = logicalEyeCount,]]
    [[AggregateRegionEvaluationMask(]]
)
    string(FIND "${_contract_text}" "${_execution_contract}"
        _execution_contract_position)
    if(_execution_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI physical-evaluation contract is missing: ${_execution_contract}"
        )
    endif()
endforeach()

string(REGEX REPLACE "[\r\n\t ]+" " "
    _character_source_normalized "${_character_source}")
foreach(_history_contract IN ITEMS
    [[void InvalidatePreparedMasks(bool a_preserveMultiRoiHistory = false)]]
    [[if (!a_preserveMultiRoiHistory) { slot.stableMultiRoi = {}; slot.stableMaskRoi = {}; }]]
    [[state_->InvalidatePreparedMasks(true);]]
    [[if (slot.multiRoiPolicyKey != key.settings) { slot.stableMultiRoi = {};]]
)
    string(FIND "${_character_source_normalized}" "${_history_contract}"
        _history_contract_position)
    if(_history_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI history must survive ordinary capture and reset for policy changes: ${_history_contract}"
        )
    endif()
endforeach()

foreach(_copy_contract IN ITEMS
    [[bool CopyNeuralOutputRegions(]]
    [[a_regions.count > 2u]]
    [[ContainsComputeSubrect(a_enclosure, region)]]
    [[GetPreparedComputeRegions(]]
    [[computeSubrect, computeRegions);]]
    [[preparedSubrect, preparedRegions);]]
    [[computeSubrect, neuralArgs[eye].computeRegions))]]
)
    string(FIND "${_upscaling_source}" "${_copy_contract}"
        _copy_contract_position)
    if(_copy_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI defined-output copy contract is missing: ${_copy_contract}"
        )
    endif()
endforeach()

string(REGEX MATCHALL [[CopyNeuralOutputRegions\(]]
    _copy_helper_occurrences "${_upscaling_source}")
list(LENGTH _copy_helper_occurrences _copy_helper_occurrence_count)
if(NOT _copy_helper_occurrence_count EQUAL 3)
    message(FATAL_ERROR
        "Expected one exact-region copy helper plus the ordinary-center and shared-float staged-output callers; found ${_copy_helper_occurrence_count} occurrences"
    )
endif()

foreach(_ui_contract IN ITEMS
    [["Experimental Multi-ROI"]]
    [["Multi-ROI Savings Gate"]]
    [[&settings.neuralCharacterMultiRoiSavingsGateEnabled]]
    [[&settings.neuralCharacterMultiRoiEnabled]]
    [[Uses at most two persistent Feature 18 regions per eye]]
    [[Multi-ROI uses separate feature instances.]]
    [[current-source GPU face/skin/hair category bounds when ready]]
    [[Early GPU bounds are read without waiting]]
    [[Geometry is only a fallback when matching GPU evidence is unavailable]]
    [[stale masks cannot exclude new pixels]]
    [[65,536-pixel reserve for its extra invocation plus 25%]]
    [[This is not native sparse-ROI support.]]
    [[Compare summed planned pixels, not the enclosing rectangle]]
)
    string(FIND "${_upscaling_source}" "${_ui_contract}"
        _ui_contract_position)
    if(_ui_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI user-facing contract is missing: ${_ui_contract}"
        )
    endif()
endforeach()

string(FIND "${_upscaling_source}"
    [[bool Upscaling::HandleNeuralRenderingSettingsTransition(]]
    _settings_transition_begin)
string(FIND "${_upscaling_source}"
    [[Upscaling::GetLatchedNeuralRenderingInsertionPoint()]]
    _settings_transition_end)
if(_settings_transition_begin EQUAL -1 OR _settings_transition_end EQUAL -1 OR
   _settings_transition_end LESS_EQUAL _settings_transition_begin)
    message(FATAL_ERROR "Unable to isolate the Neural Rendering settings transition")
endif()
math(EXPR _settings_transition_length
    "${_settings_transition_end} - ${_settings_transition_begin}")
string(SUBSTRING "${_upscaling_source}" ${_settings_transition_begin}
    ${_settings_transition_length} _settings_transition)
string(REGEX REPLACE "[\r\n\t ]+" " "
    _settings_transition_normalized "${_settings_transition}")

foreach(_transition_contract IN ITEMS
    [[const bool multiRoiChanged = a_previousSettings.neuralCharacterMultiRoiEnabled != settings.neuralCharacterMultiRoiEnabled;]]
    [[if (!multiRoiChanged && HasSameNeuralRenderingSettingsKey(a_previousSettings, settings))]]
    [[RequestHistoryReset();]]
    [[NeuralRendering::RequiresBackendRetirement(]]
    [[multiRoiChanged, insertionPointChanged,]]
    [[neuralRenderer.Reset();]]
)
    string(FIND "${_settings_transition_normalized}" "${_transition_contract}"
        _transition_contract_position)
    if(_transition_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI reset-on-toggle contract is missing: ${_transition_contract}"
        )
    endif()
endforeach()

string(FIND "${_settings_transition_normalized}"
    [[const bool multiRoiChanged =]] _multi_roi_changed_position)
string(FIND "${_settings_transition_normalized}"
    [[if (!multiRoiChanged && HasSameNeuralRenderingSettingsKey]]
    _same_key_early_return_position)
string(FIND "${_settings_transition_normalized}"
    [[neuralRenderer.Reset();]]
    _renderer_reset_position)
if(NOT _multi_roi_changed_position LESS _same_key_early_return_position OR
   NOT _same_key_early_return_position LESS _renderer_reset_position)
    message(FATAL_ERROR
        "Multi-ROI toggle detection must precede early returns and force renderer retirement"
    )
endif()

message(STATUS "Neural multi-ROI contract passed")
