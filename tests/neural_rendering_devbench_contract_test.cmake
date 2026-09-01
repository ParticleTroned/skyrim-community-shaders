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
set(
    _renderer_header_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.h"
)
set(
    _renderer_source_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.cpp"
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
    "${_renderer_header_path}"
    "${_renderer_source_path}"
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
file(READ "${_renderer_header_path}" _renderer_header)
file(READ "${_renderer_source_path}" _renderer_source)
file(READ "${_pipeline_policy_path}" _pipeline_policy)
set(
    _source_contract_text
    "${_upscaling}\n${_upscaling_header}\n${_renderer_header}\n${_renderer_source}\n${_pipeline_policy}"
)

foreach(_status_contract IN ITEMS
    [[{ "apiVersion", 4 }]]
    [[{ "implementationMatrix", NeuralImplementationMatrixJson() }]]
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
)
    string(FIND "${_source_contract_text}" "${_source_contract}" _source_position)
    if(_source_position EQUAL -1)
        message(FATAL_ERROR
            "Neural Rendering source contract is missing: ${_source_contract}"
        )
    endif()
endforeach()

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

foreach(_validation_contract IN ITEMS
    [["nr_preset_type_invalid"]]
    [["nr_preset_out_of_range"]]
    [["nr_tuning_type_invalid"]]
    [["nr_tuning_non_finite"]]
    [["nr_tuning_out_of_range"]]
    [["nr_style_type_invalid"]]
    [["nr_style_out_of_range"]]
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
