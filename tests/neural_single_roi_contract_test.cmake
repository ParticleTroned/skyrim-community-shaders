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
        message(FATAL_ERROR "Required single-ROI contract input is missing: ${_required_path}")
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

# GPU-only support is optional; CPU geometry alone determines the crop.
foreach(_early_bounds_contract IN ITEMS
    [[state_->QueueEarlyMaskBounds(a_device, a_context, evidence.get());]]
    [[state_->earlyMaskCaptureSerial_ = state_->AllocatePreparedContentSerial();]]
    [[readback.captureSerial == earlyMaskCaptureSerial_]]
    [[readback.frame == a_args.sourceWorldFrame && readback.frame == capturedFrame_]]
    [[readback.categories == GetEnabledCharacterCategoryMask(a_args.settings)]]
    [[if (a_gpuMaskSupport)]]
    [[if (!provenEmpty && !fullOutputMask)]]
    [[Util::ReadGeometryBounds(*a_geometry, state_->geometryBoundsBudget_, a_selectedCategory, refined)]]
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
    [[NeuralRendering::ReadUpscalingCharacterSettingsJson(a_json, parsed);]]
    [[NeuralRendering::WriteUpscalingCharacterSettingsJson(a_json, a_settings);]]
    [[NeuralRendering::GetUpscalingCharacterSettings(a_settings)]]
)
    string(FIND "${_contract_text}" "${_settings_contract}"
        _settings_contract_position)
    if(_settings_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Single-ROI persistence/default/key contract is missing: ${_settings_contract}"
        )
    endif()
endforeach()

foreach(_telemetry_contract IN ITEMS
    [[rendererSnapshot.performance.lastFeatureLogicalEyeCount]]
    [[rendererSnapshot.performance.lastExecutionPlan]]
    [[executionPlan->physicalSlotMask & NeuralRendering::RegionRouteMask(a_logicalMask)]]
    [[NeuralRendering::LogicalRegionMask(lastFeatureSlotMask)]]
    [["physicalSlotMask"]]
    [["logicalSlotMask"]]
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
            "Single-ROI DevBench attribution contract is missing: ${_telemetry_contract}"
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
    [[a_args.roi = result.roi;]]
    [[a_batchArgs[eye].roi = maskResults[eye].roi;]]
    [[a_result.roi = slot.roi;]]
    [[evidence->roi = slot.roi;]]
    [[GetPreparedSelection(]]
    [[FindPreparedSlot(]]
    [[FinalizePreparedMasks(]]
)
    string(FIND "${_contract_text}" "${_prepare_contract}"
        _prepare_contract_position)
    if(_prepare_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Single-ROI prepare/result threading contract is missing: ${_prepare_contract}"
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
    [[static constexpr std::size_t kFeatureSlotCount = kLogicalFeatureSlotCount;]]
    [[if (args.featureSlot >= kLogicalFeatureSlotCount)]]
    [[GetRoiDescriptorViolation(a_resources.roi, provider,]]
    [[region.roi = resource.roi;]]
    [[a_resources.nativeLayout = BuildNativeEvaluationLayout(]]
    [[region.nativeLayout = resource.nativeLayout;]]
    [[const NativeEvaluationLayout& a_layout,]]
    [[.outputWidth = a_resources.nativeLayout.output.backing.width,]]
    [[observation.rect = resources[index].roi.ownedOutput;]]
    [[resources[index].roi.ownedOutput);]]
    [[resources[index].historyKey.regionIdentity = 0;]]
    [[.logicalEyeCount = logicalEyeCount,]]
)
    string(FIND "${_contract_text}" "${_execution_contract}"
        _execution_contract_position)
    if(_execution_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Single-ROI physical-evaluation contract is missing: ${_execution_contract}"
        )
    endif()
endforeach()

string(REGEX REPLACE "[\r\n\t ]+" " "
    _character_source_normalized "${_character_source}")
foreach(_history_contract IN ITEMS
    [[void InvalidatePreparedMasks(bool a_preserveRoiHistory = false)]]
    [[if (!a_preserveRoiHistory) { slot.stableComputeSubrect = {}; }]]
    [[state_->InvalidatePreparedMasks(true);]]
    [[if (slot.roiPolicyKey != key.settings) { slot.stableComputeSubrect = {};]]
)
    string(FIND "${_character_source_normalized}" "${_history_contract}"
        _history_contract_position)
    if(_history_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Single-ROI history must survive ordinary capture and reset for policy changes: ${_history_contract}"
        )
    endif()
endforeach()

foreach(_copy_contract IN ITEMS
    [[bool CopyNeuralOutputSubrect(]]
    [[!a_subrect.Fits(a_width, a_height)]]
    [[computeSubrect);]]
    [[rect.outputWidth, rect.outputHeight, preparedSubrect);]]
    [[GetPreparedSelection(]]
)
    string(FIND "${_upscaling_source}" "${_copy_contract}"
        _copy_contract_position)
    if(_copy_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Single-ROI defined-output copy contract is missing: ${_copy_contract}"
        )
    endif()
endforeach()

string(REGEX MATCHALL [[CopyNeuralOutputSubrect\(]]
    _copy_helper_occurrences "${_upscaling_source}")
list(LENGTH _copy_helper_occurrences _copy_helper_occurrence_count)
if(NOT _copy_helper_occurrence_count EQUAL 3)
    message(FATAL_ERROR
        "Expected one exact-region copy helper plus the ordinary-center and shared-float staged-output callers; found ${_copy_helper_occurrence_count} occurrences"
    )
endif()

foreach(_ui_contract IN ITEMS
    [[DrawNeuralRenderingCropControl(showDiagnostics);]]
    [[ImGui::SeparatorText("Actor selection");]]
    [[ImGui::Checkbox("Actors only", &settings.neuralCharacterRenderingEnabled);]]
    [[Uncropped processes the complete input with the same actor mask.]]
)
    string(FIND "${_upscaling_source}" "${_ui_contract}" _ui_contract_position)
    if(_ui_contract_position EQUAL -1)
        message(FATAL_ERROR "Actor NR single-ROI menu contract is missing: ${_ui_contract}")
    endif()
endforeach()


# A single ROI is intrinsic, with no selectable legacy execution modes.


string(FIND "${_upscaling_source}" [[ImGui::TreeNodeEx("ROI and edge settings"]] _ordinary_roi_begin)
string(FIND "${_upscaling_source}" [["Actor diagnostics and experiments"]] _diagnostic_roi_begin)
if(_ordinary_roi_begin LESS 0 OR _diagnostic_roi_begin LESS_EQUAL _ordinary_roi_begin)
    message(FATAL_ERROR "Ordinary ROI controls must precede the Debug-only diagnostic section")
endif()
math(EXPR _ordinary_roi_length "${_diagnostic_roi_begin} - ${_ordinary_roi_begin}")
string(SUBSTRING "${_upscaling_source}" ${_ordinary_roi_begin} ${_ordinary_roi_length} _ordinary_roi_controls)
foreach(_ordinary_roi_control IN ITEMS
    [["Minimum Face Size"]]
    [["Adaptive ROI Performance"]]
    [["Eligibility Margin"]]
    [["Eligibility Hold"]]
    [["Depth-aware Edge Feather"]]
    [["Edge Radius"]]
    [["Relative Depth Threshold"]]
)
    string(FIND "${_ordinary_roi_controls}" "${_ordinary_roi_control}" _ordinary_roi_position)
    if(_ordinary_roi_position LESS 0)
        message(FATAL_ERROR "Ordinary ROI control must be available at Info: ${_ordinary_roi_control}")
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
    [[if (!fovChanged && HasSameNeuralRenderingSettingsKey(a_previousSettings, settings))]]
    [[RequestHistoryReset();]]
    [[NeuralRendering::RequiresBackendRetirement(]]
    [[a_previousSettings.neuralRenderingEnabled != settings.neuralRenderingEnabled, insertionPointChanged, previousInsertionPoint != currentInsertionPoint || a_previousSettings.neuralCharacterProviderBlending != settings.neuralCharacterProviderBlending,]]
    [[neuralRenderer.Reset();]]
)
    string(FIND "${_settings_transition_normalized}" "${_transition_contract}"
        _transition_contract_position)
    if(_transition_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Single-ROI reset-on-toggle contract is missing: ${_transition_contract}"
        )
    endif()
endforeach()

string(FIND "${_settings_transition_normalized}"
    [[if (!fovChanged && HasSameNeuralRenderingSettingsKey]]
    _same_key_early_return_position)
string(FIND "${_settings_transition_normalized}"
    [[neuralRenderer.Reset();]]
    _renderer_reset_position)
if(NOT _same_key_early_return_position LESS _renderer_reset_position)
    message(FATAL_ERROR "Unchanged settings must preserve native backend ownership")
endif()

message(STATUS "Actor NR intrinsic single-ROI and output contract passed")
