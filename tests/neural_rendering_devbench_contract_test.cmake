cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(
    _bridge_path
    "${PROJECT_ROOT}/src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp"
)
set(
    _experiment_doc_path
    "${PROJECT_ROOT}/docs/development/dlss-neural-rendering-experiments.md"
)
set(_upscaling_path "${PROJECT_ROOT}/src/Features/Upscaling.cpp")
set(_upscaling_header_path "${PROJECT_ROOT}/src/Features/Upscaling.h")
set(_streamline_path "${PROJECT_ROOT}/src/Features/Upscaling/Streamline.cpp")
set(_streamline_header_path "${PROJECT_ROOT}/src/Features/Upscaling/Streamline.h")
set(
    _renderer_header_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.h"
)
set(
    _renderer_source_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.cpp"
)
set(
    _d3d12_interop_source_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/D3D12Interop.cpp"
)
set(
    _pipeline_policy_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/PipelinePolicy.h"
)
foreach(_required_path IN ITEMS
    "${_bridge_path}"
    "${_experiment_doc_path}"
    "${_upscaling_path}"
    "${_upscaling_header_path}"
    "${_streamline_path}"
    "${_streamline_header_path}"
    "${_renderer_header_path}"
    "${_renderer_source_path}"
    "${_d3d12_interop_source_path}"
    "${_pipeline_policy_path}"
)
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR "Required Neural Rendering contract input is missing: ${_required_path}")
    endif()
endforeach()

file(READ "${_bridge_path}" _bridge)
file(READ "${_experiment_doc_path}" _experiment_doc)
file(READ "${_upscaling_path}" _upscaling)
file(READ "${_upscaling_header_path}" _upscaling_header)
file(READ "${_streamline_path}" _streamline)
file(READ "${_streamline_header_path}" _streamline_header)
file(READ "${_renderer_header_path}" _renderer_header)
file(READ "${_renderer_source_path}" _renderer_source)
file(READ "${_d3d12_interop_source_path}" _d3d12_interop_source)
file(READ "${_pipeline_policy_path}" _pipeline_policy)
set(
    _source_contract_text
    "${_upscaling}\n${_upscaling_header}\n${_streamline}\n${_streamline_header}\n${_renderer_header}\n${_renderer_source}\n${_d3d12_interop_source}\n${_pipeline_policy}"
)

string(FIND
    "${_bridge}"
    "static constexpr const char* descriptor ="
    _descriptor_declaration
)
if(_descriptor_declaration EQUAL -1)
    message(FATAL_ERROR "DevBench descriptor declaration is missing")
endif()
string(SUBSTRING "${_bridge}" ${_descriptor_declaration} -1 _descriptor_tail)
string(FIND "${_descriptor_tail}" [[R"(]] _descriptor_raw_start)
if(_descriptor_raw_start EQUAL -1)
    message(FATAL_ERROR "DevBench descriptor raw string is missing")
endif()
math(EXPR _descriptor_json_start "${_descriptor_raw_start} + 3")
string(SUBSTRING
    "${_descriptor_tail}"
    ${_descriptor_json_start}
    -1
    _descriptor_json_tail
)
string(FIND "${_descriptor_json_tail}" [[)";]] _descriptor_json_length)
if(_descriptor_json_length EQUAL -1)
    message(FATAL_ERROR "DevBench descriptor terminator is missing")
endif()
string(SUBSTRING
    "${_descriptor_json_tail}"
    0
    ${_descriptor_json_length}
    _descriptor_json
)
string(JSON
    _descriptor_schema_type
    ERROR_VARIABLE _descriptor_json_error
    TYPE
    "${_descriptor_json}"
    inputSchema
)
if(_descriptor_json_error)
    message(FATAL_ERROR
        "DevBench descriptor is not valid JSON: ${_descriptor_json_error}"
    )
endif()
if(NOT _descriptor_schema_type STREQUAL "OBJECT")
    message(FATAL_ERROR "DevBench inputSchema must be an object")
endif()
string(JSON
    _descriptor_all_of_length
    LENGTH
    "${_descriptor_json}"
    inputSchema
    allOf
)
if(NOT _descriptor_all_of_length EQUAL 3)
    message(FATAL_ERROR "DevBench foveation schema must retain three conditional contracts")
endif()
string(JSON
    _descriptor_cycle_required
    GET
    "${_descriptor_json}"
    inputSchema
    allOf
    1
    then
    required
    0
)
if(NOT _descriptor_cycle_required STREQUAL "control")
    message(FATAL_ERROR "foveation_cycle schema must require control")
endif()
string(JSON
    _descriptor_configure_field_count
    LENGTH
    "${_descriptor_json}"
    inputSchema
    allOf
    0
    then
    anyOf
)
if(NOT _descriptor_configure_field_count EQUAL 17)
    message(FATAL_ERROR "foveation_configure schema must require at least one of 17 controls")
endif()
string(JSON
    _descriptor_two_value_maximum
    GET
    "${_descriptor_json}"
    inputSchema
    allOf
    2
    then
    properties
    valueIndex
    maximum
)
if(NOT _descriptor_two_value_maximum EQUAL 1)
    message(FATAL_ERROR "Two-value foveation controls must reject valueIndex 2")
endif()

foreach(_status_contract IN ITEMS
    [[{ "apiVersion", 7 }]]
    [[{ "implementationMatrix", NeuralImplementationMatrixJson() }]]
    [[{ "insertionPointMatrix", NeuralInsertionPointMatrixJson() }]]
    [[{ "selectedInsertionPoint", NeuralInsertionPointJson(insertionPoint) }]]
    [[{ "insertionPoint", NeuralRendering::GetInsertionPointName(snapshot.insertionPoint) }]]
    [[{ "byInsertionPoint", NeuralInsertionPointPerformanceJson(snapshot.performance) }]]
    [[{ "invalidInsertionPointSamples", snapshot.performance.invalidInsertionPointSamples }]]
    [[{ "lastInsertionPoint", NeuralRendering::GetInsertionPointName(snapshot.performance.lastInsertionPoint) }]]
    [[{ "frame", snapshot.frameId }]]
    [[{ "submitCycleSource", "submit_entry" }]]
    [[{ "eyeMaskSemantics", {]]
    [[{ "hdrClassification",]]
    [[{ "featureUpscaling", snapshot.featureUpscaling }]]
    [[{ "interopInitializations", snapshot.counters.interopInitializations }]]
    [[{ "callerHistoryResets", snapshot.counters.callerHistoryResets }]]
    [[{ "discontinuousHistoryResets", snapshot.counters.discontinuousHistoryResets }]]
    [[{ "quarantinedBypasses", snapshot.counters.quarantinedBypasses }]]
    [[{ "mainStereoCommandSubmissions", snapshot.performance.mainStereoCommandSubmissions }]]
    [[{ "submitStereoCommandSubmissions", snapshot.performance.submitStereoCommandSubmissions }]]
    [[{ "lastFeatureGpuMicroseconds", snapshot.performance.lastFeatureGpuMicroseconds }]]
    [[{ "lastFeaturePixelCount", snapshot.performance.lastFeaturePixelCount }]]
    [[{ "lastFeatureEvaluationCount", snapshot.performance.lastFeatureEvaluationCount }]]
    [[{ "unexpectedPassEyeMask", route.unexpectedPassEyeMask }]]
    [[{ "dlssEvaluationAttempts", route.dlssEvaluationAttemptCount[eye] }]]
    [[{ "dlssEvaluationSuccesses", route.dlssEvaluationSuccessCount[eye] }]]
    [[{ "feature18EvaluationAttempts", route.featureEvaluationAttemptCount[eye] }]]
    [[{ "feature18EvaluationSuccesses", route.featureEvaluationSuccessCount[eye] }]]
    [[{ "centerBlendAttempts", route.centerBlendAttemptCount[eye] }]]
    [[{ "centerBlendSuccesses", route.centerBlendSuccessCount[eye] }]]
    [[{ "lateNeuralBlendAttempts", route.lateNeuralBlendAttemptCount[eye] }]]
    [[{ "lateNeuralBlendSuccesses", route.lateNeuralBlendSuccessCount[eye] }]]
    [[{ "unexpectedPassCountDetected", (route.unexpectedPassEyeMask & eyeBit) != 0 }]]
    [[{ "knownMenuContext", temporalAdmission.menuContextActive }]]
    [[{ "gamePaused", temporalAdmission.gamePaused }]]
    [[{ "worldFrameStateAvailable", temporalAdmission.worldFrameStateAvailable }]]
    [[{ "worldFrameStarted", temporalAdmission.worldFrameStarted }]]
    [[{ "worldFrameCompleted", temporalAdmission.worldFrameCompleted }]]
    [[{ "temporalSourceFresh", temporalAdmission.temporalSourceFresh }]]
    [[{ "temporalAdmission", {]]
    [[{ "admitted", temporalAdmission.admitted }]]
    [[{ "blockReason", NeuralRendering::GetTemporalAdmissionBlockReasonName(temporalAdmission.blockReason) }]]
    [[{ "currentFrame", temporalAdmission.currentFrame }]]
    [[{ "lastWorldRenderFrame", temporalAdmission.lastWorldRenderFrame }]]
    [[{ "lastCompletedWorldRenderFrame", temporalAdmission.lastCompletedWorldRenderFrame }]]
    [[nr_status returns the API-v7 NR runtime]]
    [[{ "neuralRendering", NeuralRenderingStatusJson(a_upscaling) }]]
    [[{ "foveation", FoveationStatusJson(a_upscaling) }]]
    [[{ "plan", FoveatedPlanJson(a_upscaling, activeProfile) }]]
    [[{ "observedFrame", observedFrame }]]
    [[{ "currentWorkFrame", std::move(currentWorkFrameJson) }]]
    [[{ "measurementSafeFromFrame", std::move(measurementSafeFromFrame) }]]
    [[{ "matchesRequestedSettings", FoveatedPlanMatchesSettings(]]
    [[{ "finalLdrNeuralSupportRequested", finalLdrNeuralSupportRequested }]]
    [[{ "finalLdrNeuralSupportLatched", finalLdrNeuralSupportLatched }]]
    [[{ "visibleOutput", FoveatedRectJson(a_eye.visibleOutput) }]]
    [[{ "output", FoveatedRectJson(a_eye.output) }]]
    [[{ "input", FoveatedRectJson(a_eye.input) }]]
    [[{ "sourceOffset", { { "x", sourceOffsetX }, { "y", sourceOffsetY } } }]]
)
    string(FIND "${_bridge}" "${_status_contract}" _status_position)
    if(_status_position EQUAL -1)
        message(FATAL_ERROR
            "Neural Rendering DevBench status contract is missing: ${_status_contract}"
        )
    endif()
endforeach()

foreach(_source_contract IN ITEMS
    [[neuralPrepared = true;]]
    [[submitStageNeuralStereoState.outputsReady = false;]]
    [[bool IsNeuralRenderingMenuSuppressed()]]
    [[neuralTemporalAdmissionLatch.compare_exchange_weak(]]
    [[phase == NeuralCenterPhase::Resolve ?]]
    [[neuralPairApplied = neuralPairApplied &&]]
    [[args.frameId =]]
    [[ApplySequentialStereo(]]
    [[evaluationAttemptedFeatureSlotMask]]
    [[IsSequentialFrame(]]
    [[activeFeatureSlot_]]
    [[ActiveFeatureSlotOrLocked(]]
    [[neuralRenderingInsertionPoint,]]
    [[uint neuralRenderingInsertionPoint =]]
    [[settings.neuralRenderingInsertionPoint = static_cast<uint>(]]
    [[o_json.erase("neuralRenderingInsertionPoint");]]
    [[a_settings.neuralRenderingInsertionPoint]]
    [[const bool insertionPointChanged =]]
    [[neuralInsertionPointTransitionFrame = globals::state ?]]
    [[mainFinalLdrNeuralState = {};]]
    [[mainFinalLdrPresentationState = {};]]
    [[IsNeuralRenderingInsertionTransitionBlocked()]]
    [[args.insertionPoint = NeuralRendering::InsertionPoint::UpscaledCenter;]]
    [[NeuralRendering::InsertionPoint::FinalLdrPreUi;]]
    [[.insertionPoint = a_args.front().insertionPoint,]]
    [[left.insertionPoint == right.insertionPoint]]
    [[generation, insertion point, feature mode]]
    [[a_neuralRouteAllowed]]
    [[if (!compositeCenter)]]
    [[slEvaluateFeature(sl::kFeatureDLSS,]]
    [[dlssPassTelemetryFrames.GetOrCreate(]]
    [[evaluationSucceededFeatureSlotMask]]
    [[RecordNeuralPassTelemetry(]]
    [[EvaluateTemporalAdmission(]]
    [[BuildNeuralTemporalAdmission(]]
    [[ObserveNeuralTemporalAdmission(]]
    [[neuralTemporalAdmission.admitted &&]]
    [[timingFenceValue > lastCompletedTimingFenceValue_]]
)
    string(FIND "${_source_contract_text}" "${_source_contract}" _source_position)
    if(_source_position EQUAL -1)
        message(FATAL_ERROR
            "Neural Rendering source contract is missing: ${_source_contract}"
        )
    endif()
endforeach()

foreach(_foveation_semantic_contract IN ITEMS
    [[double a_minimum,]]
    [[double a_maximum,]]
    [[constexpr double kManualOffsetRequestMin = -0.3;]]
    [[constexpr double kManualOffsetRequestMax = 0.3;]]
    [[constexpr double kBlendFeatherRequestMax = 0.1;]]
    [[constexpr double kPeripheryTAAOuterScaleRequestMin = 0.3;]]
    [[requestedSettings.periphery_taa_outer_scale <]]
    [[requestedSettings.periphery_taa_center_area)]]
    [[AppendDistinctFoveationCycleValue(]]
    [[const std::size_t valueCount = a_values.size();]]
    [[a_upscaling.GetRuntimeResolutionWorkFrame();]]
    [[latchedFrame != currentWorkFrame]]
)
    string(FIND "${_bridge}" "${_foveation_semantic_contract}" _semantic_position)
    if(_semantic_position EQUAL -1)
        message(FATAL_ERROR
            "Foveation DevBench semantic contract is missing: ${_foveation_semantic_contract}"
        )
    endif()
endforeach()

string(REGEX MATCHALL
    "NeuralPhysicalPass::CenterBlend,"
    _center_blend_telemetry_sites
    "${_upscaling}"
)
list(LENGTH _center_blend_telemetry_sites _center_blend_telemetry_site_count)
if(NOT _center_blend_telemetry_site_count EQUAL 4)
    message(FATAL_ERROR
        "Both normal-centre blend boundaries must record one attempt and one success"
    )
endif()

string(REGEX MATCHALL
    "NeuralPhysicalPass::LateNeuralBlend,"
    _late_blend_telemetry_sites
    "${_upscaling}"
)
list(LENGTH _late_blend_telemetry_sites _late_blend_telemetry_site_count)
if(NOT _late_blend_telemetry_site_count EQUAL 2)
    message(FATAL_ERROR
        "The final-LDR blend boundary must record one attempt and one success"
    )
endif()

string(FIND
    "${_upscaling}"
    "void Upscaling::MenuManagerDrawInterfaceStartHook::thunk"
    _main_hook_start
)
if(_main_hook_start EQUAL -1)
    message(FATAL_ERROR "Main final-LDR presentation hook is missing")
endif()
string(SUBSTRING "${_upscaling}" ${_main_hook_start} 9000 _main_hook)
string(FIND "${_main_hook}" "upscaling.ApplyMainFinalLdrNeuralStereo();" _main_apply)
string(FIND "${_main_hook}" "upscaling.BeginVRMenuDrawInterface();" _menu_begin)
string(FIND "${_main_hook}" "func(a1);" _menu_draw)
string(FIND "${_main_hook}" "upscaling.FinalizeMainFinalLdrNeuralPresentation();" _mask_finalize)
if(_main_apply EQUAL -1 OR _menu_begin EQUAL -1 OR _menu_draw EQUAL -1 OR
    _mask_finalize EQUAL -1 OR NOT _main_apply LESS _menu_begin OR
    NOT _menu_begin LESS _menu_draw OR NOT _menu_draw LESS _mask_finalize)
    message(FATAL_ERROR
        "Main final-LDR ordering must remain NR, UI draw, then HMD mask repair"
    )
endif()

string(FIND
    "${_upscaling}"
    "if (!prepareSubmitStageSceneEyeOutput("
    _submit_scene_prepare
)
string(FIND
    "${_upscaling}"
    "neuralPairApplied = ApplyFinalLdrNeuralStereo("
    _submit_neural_apply
)
string(FIND
    "${_upscaling}"
    "if (!finalizeSubmitStagePresentationEyeOutput("
    _submit_presentation_finalize
)
if(_submit_scene_prepare EQUAL -1 OR _submit_neural_apply EQUAL -1 OR
    _submit_presentation_finalize EQUAL -1 OR
    NOT _submit_scene_prepare LESS _submit_neural_apply OR
    NOT _submit_neural_apply LESS _submit_presentation_finalize)
    message(FATAL_ERROR
        "Submit final-LDR ordering must remain scene output, NR, then UI/HMD presentation"
    )
endif()

string(REGEX MATCHALL
    "SetActiveFeatureSlotLocked\\(a_args\\[1\\]\\.featureSlot\\);"
    _stereo_preflight_slot_attributions
    "${_renderer_source}"
)
list(LENGTH _stereo_preflight_slot_attributions _stereo_preflight_slot_count)
if(_stereo_preflight_slot_count LESS 2)
    message(FATAL_ERROR
        "Stereo preflight exceptions do not preserve the active right-eye slot"
    )
endif()

foreach(_forbidden_source_contract IN ITEMS
    [[(void)compositeCenter;]]
    [[feature18Evaluations]]
    [[centerBlends]]
    [[lateNeuralBlends]]
    [[ObserveNeuralRenderingMenuSuppression(]]
    [[neuralPrepared = neuralSubmitted && neuralBatchArgs;]]
    [[WasFeatureEvaluated(]]
    [[evaluatedFeatureSlotMask]]
    [[observedEyeMask]]
    [[provenEyeMask]]
    [[state_->snapshot_.featureSlot < Runtime::kFeatureSlotCount]]
)
    string(FIND
        "${_source_contract_text}"
        "${_forbidden_source_contract}"
        _forbidden_source_position
    )
    if(NOT _forbidden_source_position EQUAL -1)
        message(FATAL_ERROR
            "Stale Neural Rendering source contract remains: ${_forbidden_source_contract}"
        )
    endif()
endforeach()

foreach(_cycle_contract IN ITEMS
    [[if (action == "nr_cycle_modes")]]
    [["nr_matrix_index_invalid"]]
    [[{ "selectedLane", NeuralImplementationJson(]]
    [[{ "executionClaimed", false }]]
    [[{ "implementationMatrix", NeuralImplementationMatrixJson() }]]
    [["nr_cycle_modes","nr_reset"]]
    [["matrixIndex":{"type":"integer","minimum":0,"maximum":3}]]
)
    string(FIND "${_bridge}" "${_cycle_contract}" _cycle_position)
    if(_cycle_position EQUAL -1)
        message(FATAL_ERROR
            "Neural Rendering matrix-cycle contract is missing: ${_cycle_contract}"
        )
    endif()
endforeach()

foreach(_foveation_action_contract IN ITEMS
    [[if (action == "foveation_configure")]]
    [[if (action == "foveation_cycle")]]
    [[TryParseFoveationConfiguration(]]
    [[TryParseFoveationCycleRequest(]]
    [[ApplyFoveationConfiguration(]]
    [[ApplyFoveationCycle(]]
    [[a_upscaling.InvalidateFrameScopedUpscalingState();]]
    [[a_upscaling.RequestHistoryReset();]]
    [["foveation_configure","foveation_cycle"]]
    [["foveation_configure_empty"]]
    [["foveation_request_field_unknown"]]
    [["foveation_outer_scale_below_center"]]
    [["foveation_cycle_index_out_of_range"]]
    [=["required":["control"]]=]
    [=["propertyNames":{"enum":["action","control","valueIndex"]}]=]
    [=["anyOf":[{"required":["foveatedEnabled"]}]=]
    [["then":{"properties":{"valueIndex":{"maximum":1}}}]]
)
    string(FIND "${_bridge}" "${_foveation_action_contract}" _foveation_action_position)
    if(_foveation_action_position EQUAL -1)
        message(FATAL_ERROR
            "Foveation DevBench action contract is missing: ${_foveation_action_contract}"
        )
    endif()
endforeach()

set(_foveation_configuration_fields
    foveatedEnabled
    peripheryTaaEnabled
    centerOrigin
    horizontalAnchor
    fovOnlyCenterScale
    peripheryTaaCenterScale
    peripheryTaaOuterScale
    centerHorizontalScale
    leftEyeOffsetX
    leftEyeOffsetY
    rightEyeOffsetX
    rightEyeOffsetY
    fovOnlyBlendFeather
    peripheryTaaBlendFeather
    neuralFinalLdrBlendFeather
    reconstructionGuardBandPixels
    maskVisualization
)
foreach(_foveation_field IN LISTS _foveation_configuration_fields)
    string(FIND "${_bridge}" "\"${_foveation_field}\"" _foveation_field_position)
    if(_foveation_field_position EQUAL -1)
        message(FATAL_ERROR
            "Foveation DevBench configuration field is missing: ${_foveation_field}"
        )
    endif()
endforeach()

set(_foveation_cycle_controls
    master
    periphery_taa
    center_origin
    horizontal_anchor
    fov_only_center_scale
    periphery_taa_center_scale
    periphery_taa_outer_scale
    center_horizontal_scale
    left_eye_offset_x
    left_eye_offset_y
    right_eye_offset_x
    right_eye_offset_y
    fov_only_blend_feather
    periphery_taa_blend_feather
    neural_final_ldr_blend_feather
    reconstruction_guard_band_pixels
    mask_visualization
)
foreach(_foveation_control IN LISTS _foveation_cycle_controls)
    string(FIND "${_bridge}" "\"${_foveation_control}\"" _foveation_control_position)
    if(_foveation_control_position EQUAL -1)
        message(FATAL_ERROR
            "Foveation DevBench cycle control is missing: ${_foveation_control}"
        )
    endif()
endforeach()

foreach(_outer_scale_contract IN ITEMS
    [[std::optional<float> peripheryTaaOuterScale;]]
    [[TryParseFoveationFloat(a_args, "peripheryTaaOuterScale",]]
    [[a_settings.periphery_taa_outer_scale = *a_request.peripheryTaaOuterScale;]]
    [[a_left.periphery_taa_outer_scale == a_right.periphery_taa_outer_scale]]
    [[case FoveationCycleControl::PeripheryTAAOuterScale:]]
    [[FoveationCycleControl::PeripheryTAAOuterScale, "periphery_taa_outer_scale"]]
    [[request.peripheryTaaOuterScale = selected;]]
    [[{ "peripheryTaaOuterScale", settings.periphery_taa_outer_scale }]]
    [["peripheryTaaOuterScale":{"type":"number","minimum":0.3,"maximum":1.0}]]
)
    string(FIND "${_bridge}" "${_outer_scale_contract}" _outer_scale_position)
    if(_outer_scale_position EQUAL -1)
        message(FATAL_ERROR
            "Foveation outer-scale contract is missing: ${_outer_scale_contract}"
        )
    endif()
endforeach()

foreach(_main_thread_claim_contract IN ITEMS
    [[enum class TaskClaim : uint8_t]]
    [[TaskClaim::Pending]]
    [[TaskClaim::Running]]
    [[TaskClaim::Cancelled]]
    [[claim->compare_exchange_strong(]]
    [[future.wait_for(kMainThreadCompletionGrace)]]
    [[{ "mainThreadTaskClaimed", true }]]
    [[{ "mutationOutcome", "indeterminate" }]]
)
    string(FIND "${_bridge}" "${_main_thread_claim_contract}" _claim_position)
    if(_claim_position EQUAL -1)
        message(FATAL_ERROR
            "Main-thread mutation claim contract is missing: ${_main_thread_claim_contract}"
        )
    endif()
endforeach()

string(REGEX MATCHALL
    "EnsureFoveationMutationEnvelope\\("
    _foveation_envelope_sites
    "${_bridge}"
)
list(LENGTH _foveation_envelope_sites _foveation_envelope_site_count)
if(_foveation_envelope_site_count LESS 3)
    message(FATAL_ERROR
        "Both foveation actions must preserve their mutation response envelope"
    )
endif()

foreach(_insertion_point_contract IN ITEMS
    [[std::optional<NeuralRendering::InsertionPoint> insertionPoint;]]
    [[NeuralRendering::ParseInsertionPointName(requested)]]
    [[requestedSettings.neuralRenderingInsertionPoint =]]
    [[{ "insertionPointChanged", insertionPointChanged }]]
    [[insertionPointChanged ? "insertion_point"]]
    [[const bool resetAttempted =]]
    [[enableStateChanged || insertionPointChanged;]]
    [["insertionPoint":{"type":"string","enum":["upscaled_center","final_ldr_pre_ui"]}]]
)
    string(FIND "${_bridge}" "${_insertion_point_contract}" _insertion_point_position)
    if(_insertion_point_position EQUAL -1)
        message(FATAL_ERROR
            "Neural Rendering insertion-point contract is missing: ${_insertion_point_contract}"
        )
    endif()
endforeach()

foreach(_validation_contract IN ITEMS
    [["nr_preset_type_invalid"]]
    [["nr_preset_out_of_range"]]
    [["nr_tuning_type_invalid"]]
    [["nr_tuning_non_finite"]]
    [["nr_tuning_out_of_range"]]
    [["nr_style_type_invalid"]]
    [["nr_style_out_of_range"]]
    [["nr_insertion_point_type_invalid"]]
    [["nr_insertion_point_unknown"]]
)
    string(FIND "${_bridge}" "${_validation_contract}" _validation_position)
    if(_validation_position EQUAL -1)
        message(FATAL_ERROR
            "Neural Rendering request validation is missing: ${_validation_contract}"
        )
    endif()
endforeach()

foreach(_forbidden_clamp IN ITEMS
    [[std::min<uint64_t>(preset->get<uint64_t>(), 4u)]]
    [[std::clamp<int64_t>(preset->get<int64_t>(), 0, 4)]]
    [[std::min<uint64_t>(style->get<uint64_t>(), 3u)]]
    [[std::clamp<int64_t>(style->get<int64_t>(), 0, 3)]]
    [[std::clamp(requested, 0.0, 2.0)]]
)
    string(FIND "${_bridge}" "${_forbidden_clamp}" _clamp_position)
    if(NOT _clamp_position EQUAL -1)
        message(FATAL_ERROR
            "Neural Rendering request validation still clamps: ${_forbidden_clamp}"
        )
    endif()
endforeach()

string(FIND
    "${_experiment_doc}"
    "rejected rather than silently clamped"
    _documentation_position
)
if(_documentation_position EQUAL -1)
    message(FATAL_ERROR "Neural Rendering validation behavior is undocumented")
endif()

message(STATUS "Neural Rendering DevBench contract passed")
