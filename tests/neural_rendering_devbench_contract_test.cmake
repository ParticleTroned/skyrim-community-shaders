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

foreach(_status_contract IN ITEMS
    [[{ "apiVersion", 6 }]]
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
    [[{ "neuralRendering", NeuralRenderingStatusJson(a_upscaling) }]]
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
    [[ObserveNeuralRenderingMenuSuppression(neuralMenuSuppressed);]]
    [[if (settings.neuralRenderingEnabled)
		RequestHistoryReset();]]
    [[if (settings.neuralRenderingEnabled && previous != a_suppressed)]]
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
    [[timingFenceValue > lastCompletedTimingFenceValue_]]
)
    string(FIND "${_source_contract_text}" "${_source_contract}" _source_position)
    if(_source_position EQUAL -1)
        message(FATAL_ERROR
            "Neural Rendering source contract is missing: ${_source_contract}"
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
