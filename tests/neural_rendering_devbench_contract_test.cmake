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
set(_deferred_path "${PROJECT_ROOT}/src/Deferred.cpp")
set(
    _subsurface_header_path
    "${PROJECT_ROOT}/src/Features/SubsurfaceScattering.h"
)
set(
    _subsurface_source_path
    "${PROJECT_ROOT}/src/Features/SubsurfaceScattering.cpp"
)
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
    _runtime_header_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Runtime.h"
)
set(
    _runtime_source_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Runtime.cpp"
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
    _character_region_policy_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterRegionPolicy.h"
)
set(
    _character_category_shader_path
    "${PROJECT_ROOT}/package/Shaders/Common/CharacterCategoryMask.hlsli"
)
set(
    _character_mask_shader_path
    "${PROJECT_ROOT}/package/Shaders/DLSS5CharacterMaskCS.hlsl"
)
set(_lighting_shader_path "${PROJECT_ROOT}/package/Shaders/Lighting.hlsl")
set(_grass_shader_path "${PROJECT_ROOT}/package/Shaders/RunGrass.hlsl")
set(_effect_shader_path "${PROJECT_ROOT}/package/Shaders/Effect.hlsl")
set(
    _distant_tree_shader_path
    "${PROJECT_ROOT}/package/Shaders/DistantTree.hlsl"
)
set(_sky_shader_path "${PROJECT_ROOT}/package/Shaders/Sky.hlsl")
set(
    _deferred_composite_shader_path
    "${PROJECT_ROOT}/package/Shaders/DeferredCompositeCS.hlsl"
)
set(
    _foveated_center_blend_shader_path
    "${PROJECT_ROOT}/features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl"
)
set(
    _character_doc_path
    "${PROJECT_ROOT}/docs/development/dlss5-character-neural-rendering.md"
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
    "${_deferred_path}"
    "${_subsurface_header_path}"
    "${_subsurface_source_path}"
    "${_streamline_path}"
    "${_streamline_header_path}"
    "${_renderer_header_path}"
    "${_renderer_source_path}"
    "${_runtime_header_path}"
    "${_runtime_source_path}"
    "${_character_header_path}"
    "${_character_source_path}"
    "${_character_region_policy_path}"
    "${_character_category_shader_path}"
    "${_character_mask_shader_path}"
    "${_lighting_shader_path}"
    "${_grass_shader_path}"
    "${_effect_shader_path}"
    "${_distant_tree_shader_path}"
    "${_sky_shader_path}"
    "${_deferred_composite_shader_path}"
    "${_foveated_center_blend_shader_path}"
    "${_character_doc_path}"
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
file(READ "${_deferred_path}" _deferred)
file(READ "${_subsurface_header_path}" _subsurface_header)
file(READ "${_subsurface_source_path}" _subsurface_source)
file(READ "${_streamline_path}" _streamline)
file(READ "${_streamline_header_path}" _streamline_header)
file(READ "${_renderer_header_path}" _renderer_header)
file(READ "${_renderer_source_path}" _renderer_source)
file(READ "${_runtime_header_path}" _runtime_header)
file(READ "${_runtime_source_path}" _runtime_source)
file(READ "${_character_header_path}" _character_header)
file(READ "${_character_source_path}" _character_source)
file(READ "${_character_region_policy_path}" _character_region_policy)
file(READ "${_character_category_shader_path}" _character_category_shader)
file(READ "${_character_mask_shader_path}" _character_mask_shader)
file(READ "${_lighting_shader_path}" _lighting_shader)
file(READ "${_grass_shader_path}" _grass_shader)
file(READ "${_effect_shader_path}" _effect_shader)
file(READ "${_distant_tree_shader_path}" _distant_tree_shader)
file(READ "${_sky_shader_path}" _sky_shader)
file(READ "${_deferred_composite_shader_path}" _deferred_composite_shader)
file(READ "${_foveated_center_blend_shader_path}" _foveated_center_blend_shader)
file(READ "${_character_doc_path}" _character_doc)
file(READ "${_d3d12_interop_source_path}" _d3d12_interop_source)
file(READ "${_pipeline_policy_path}" _pipeline_policy)
set(
    _source_contract_text
    "${_upscaling}\n${_upscaling_header}\n${_deferred}\n${_subsurface_header}\n${_subsurface_source}\n${_streamline}\n${_streamline_header}\n${_renderer_header}\n${_renderer_source}\n${_runtime_header}\n${_runtime_source}\n${_character_header}\n${_character_source}\n${_character_region_policy}\n${_character_category_shader}\n${_character_mask_shader}\n${_lighting_shader}\n${_grass_shader}\n${_effect_shader}\n${_distant_tree_shader}\n${_sky_shader}\n${_deferred_composite_shader}\n${_foveated_center_blend_shader}\n${_character_doc}\n${_d3d12_interop_source}\n${_pipeline_policy}"
)

foreach(_selection_composite_contract IN ITEMS
    [[CharacterMask.SampleLevel(LinearSampler, centerUV, 0)]]
    [[centerColor = lerp(baselineColor, centerColor, characterWeight);]]
)
    string(FIND
        "${_foveated_center_blend_shader}"
        "${_selection_composite_contract}"
        _selection_composite_position
    )
    if(_selection_composite_position EQUAL -1)
        message(FATAL_ERROR
            "Character selection composite contract is missing: ${_selection_composite_contract}"
        )
    endif()
endforeach()

foreach(_forbidden_character_selection_contract IN ITEMS
    [[CharacterMask.SampleLevel(LinearSampler, centerUV, 0) * 255.0]]
    [[PreserveCharacterDirectCommitBaselines(]]
    [[a_args.controlMask = result.controlMask]]
)
    string(FIND
        "${_source_contract_text}"
        "${_forbidden_character_selection_contract}"
        _forbidden_character_selection_position
    )
    if(NOT _forbidden_character_selection_position EQUAL -1)
        message(FATAL_ERROR
            "Obsolete character selection path remains: ${_forbidden_character_selection_contract}"
        )
    endif()
endforeach()

foreach(_transient_setting IN ITEMS
    neuralCharacterDebugView
    neuralCharacterMaskTestMode
)
    string(FIND
        "${_upscaling}"
        "OP(${_transient_setting})"
        _transient_setting_serialized
    )
    if(NOT _transient_setting_serialized EQUAL -1)
        message(FATAL_ERROR
            "Developer-only character control must not persist: ${_transient_setting}"
        )
    endif()
endforeach()
foreach(_transient_reset_contract IN ITEMS
    [[settings.neuralCharacterDebugView = static_cast<uint>(]]
    [[NeuralRendering::CharacterDebugView::Off);]]
    [[settings.neuralCharacterMaskTestMode = static_cast<uint>(]]
    [[NeuralRendering::CharacterMaskTestMode::Authored);]]
)
    string(FIND
        "${_upscaling}"
        "${_transient_reset_contract}"
        _transient_reset_position
    )
    if(_transient_reset_position EQUAL -1)
        message(FATAL_ERROR
            "Transient character control load reset is missing: ${_transient_reset_contract}"
        )
    endif()
endforeach()
foreach(_unsupported_roi_token IN ITEMS
    CharacterRoiMode
    neuralCharacterRoiMode
    characterRoiMode
)
    string(FIND
        "${_upscaling}\n${_upscaling_header}\n${_bridge}\n${_character_header}\n${_character_source}"
        "${_unsupported_roi_token}"
        _unsupported_roi_token_position
    )
    if(NOT _unsupported_roi_token_position EQUAL -1)
        message(FATAL_ERROR
            "Unsupported compute-ROI mode is still mutable: ${_unsupported_roi_token}"
        )
    endif()
endforeach()

function(_extract_upscaling_section _begin_marker _end_marker _output_variable)
    string(FIND "${_upscaling}" "${_begin_marker}" _section_begin)
    string(FIND "${_upscaling}" "${_end_marker}" _section_end)
    if(_section_begin EQUAL -1 OR _section_end EQUAL -1 OR
        _section_end LESS_EQUAL _section_begin)
        message(FATAL_ERROR
            "Unable to isolate Neural Rendering source section: ${_begin_marker}"
        )
    endif()
    math(EXPR _section_length "${_section_end} - ${_section_begin}")
    string(SUBSTRING
        "${_upscaling}"
        ${_section_begin}
        ${_section_length}
        _section
    )
    string(REGEX REPLACE "[\r\n\t ]+" " " _section_normalized "${_section}")
    set(${_output_variable} "${_section_normalized}" PARENT_SCOPE)
endfunction()

_extract_upscaling_section(
    [[bool Upscaling::PrepareSubmitNeuralFloatResources(]]
    [[ID3D11Resource* Upscaling::GetSubmitNeuralFloatEvaluationOutput(]]
    _submit_float_resource_section
)
_extract_upscaling_section(
    [[ID3D11Resource* Upscaling::GetSubmitNeuralFloatEvaluationOutput(]]
    [[bool Upscaling::CommitSubmitNeuralFloatOutput(]]
    _submit_float_output_section
)
_extract_upscaling_section(
    [[bool Upscaling::CommitSubmitNeuralFloatOutput(]]
    [[bool Upscaling::EnsureFoveatedDepthGuideSRV(]]
    _submit_float_commit_section
)
_extract_upscaling_section(
    [[bool Upscaling::DispatchSingleFoveatedVendorEye(]]
    [[bool Upscaling::ApplyFinalLdrNeuralStereo(]]
    _upscaled_center_section
)
_extract_upscaling_section(
    [[bool Upscaling::ApplyFinalLdrNeuralStereo(]]
    [[void Upscaling::ApplyMainFinalLdrNeuralStereo()]]
    _final_ldr_section
)
_extract_upscaling_section(
    [[bool Upscaling::DispatchFoveatedVendorEyeComposite(]]
    [[bool Upscaling::DispatchFoveatedVendorUpscaling(]]
    _upscaled_center_composite_section
)

foreach(_submit_float_header_contract IN ITEMS
    "eastl::unique_ptr<Texture2D> submitNeuralFloatColorIn[2]"
    "eastl::unique_ptr<Texture2D> submitNeuralFloatColorOut[2]"
    "eastl::unique_ptr<Texture2D> submitNeuralFloatStagedOut[2]"
)
    string(FIND
        "${_upscaling_header}"
        "${_submit_float_header_contract}"
        _submit_float_header_position
    )
    if(_submit_float_header_position EQUAL -1)
        message(FATAL_ERROR
            "Submit float resource declaration is missing: ${_submit_float_header_contract}"
        )
    endif()
endforeach()

foreach(_submit_float_resource_contract IN ITEMS
    [[constexpr DXGI_FORMAT neuralFormat = DXGI_FORMAT_R11G11B10_FLOAT]]
    "submitNeuralFloatColorIn[eyeIndex]"
    "submitNeuralFloatColorOut[eyeIndex]"
    "submitNeuralFloatStagedOut[eyeIndex]"
    [[texture->desc.Format == neuralFormat]]
)
    string(FIND
        "${_submit_float_resource_section}"
        "${_submit_float_resource_contract}"
        _submit_float_resource_position
    )
    if(_submit_float_resource_position EQUAL -1)
        message(FATAL_ERROR
            "Fixed submit float resource contract is missing: ${_submit_float_resource_contract}"
        )
    endif()
endforeach()
string(REGEX MATCHALL
    [[neuralFormat\)]]
    _submit_float_format_bindings
    "${_submit_float_resource_section}"
)
list(LENGTH _submit_float_format_bindings _submit_float_format_binding_count)
if(_submit_float_format_binding_count LESS 3)
    message(FATAL_ERROR
        "Every submit Neural input/output resource must use the fixed float format"
    )
endif()

string(FIND
    "${_submit_float_output_section}"
    "directCommit ? submitNeuralFloatColorOut[eyeIndex] : submitNeuralFloatStagedOut[eyeIndex]"
    _submit_float_output_selection
)
if(_submit_float_output_selection EQUAL -1)
    message(FATAL_ERROR
        "Direct and staged submit NR evaluations no longer select their float outputs"
    )
endif()

foreach(_submit_float_commit_contract IN ITEMS
    [[if (directCommit) return true]]
    [[globals::d3d::context->CopyResource( submitNeuralFloatColorOut[eyeIndex]->resource.get(), submitNeuralFloatStagedOut[eyeIndex]->resource.get())]]
)
    string(FIND
        "${_submit_float_commit_section}"
        "${_submit_float_commit_contract}"
        _submit_float_commit_position
    )
    if(_submit_float_commit_position EQUAL -1)
        message(FATAL_ERROR
            "Submit float commit contract is missing: ${_submit_float_commit_contract}"
        )
    endif()
endforeach()

foreach(_upscaled_center_float_contract IN ITEMS
    [[const bool submitStageDLSSCenter = a_upscaleMethod == UpscaleMethod::kDLSS && dlssViewportRole == Streamline::DLSSViewportRole::SubmitStageFoveatedCenter]]
    [[const bool useSubmitNeuralFloatBridge = submitStageDLSSCenter && neuralRenderingRequested && !NeuralRendering::RunsBeforeDlss()]]
    [[args.insertionPoint = NeuralRendering::InsertionPoint::UpscaledCenter]]
    [[DispatchSubmitStageColorRegion( foveatedCenterColorOut[eyeIndex]->srv.get(), submitNeuralFloatColorIn[eyeIndex]->uav.get()]]
    [[neuralColorInput = submitNeuralFloatColorIn[eyeIndex]->resource.get()]]
    [[return CommitSubmitNeuralFloatOutput( eyeIndex, directNeuralCommit)]]
    [[useSubmitNeuralFloatBridge && neuralAppliedForComposite]]
)
    string(FIND
        "${_upscaled_center_section}"
        "${_upscaled_center_float_contract}"
        _upscaled_center_float_position
    )
    if(_upscaled_center_float_position EQUAL -1)
        message(FATAL_ERROR
            "Upscaled-centre submit float contract is missing: ${_upscaled_center_float_contract}"
        )
    endif()
endforeach()

foreach(_upscaled_center_composite_contract IN ITEMS
    [[if (params.centerAlreadyPrepared)]]
    [[const bool useSubmitNeuralFloatOutput = params.dlssViewportRole == Streamline::DLSSViewportRole::SubmitStageFoveatedCenter && neuralResult && neuralResult->applied]]
    [[centerSRV = submitNeuralFloatColorOut[eyeIndex]->srv.get()]]
    [[ResolveSubmitCharacterCompositeInputs(]]
    [[DispatchFoveatedBlendPass( centerSRV, outputColorUAV]]
)
    string(FIND
        "${_upscaled_center_composite_section}"
        "${_upscaled_center_composite_contract}"
        _upscaled_center_composite_position
    )
    if(_upscaled_center_composite_position EQUAL -1)
        message(FATAL_ERROR
            "Prepared-centre submit float composite is missing: ${_upscaled_center_composite_contract}"
        )
    endif()
endforeach()

foreach(_final_ldr_float_contract IN ITEMS
    [[const bool useSubmitNeuralFloatBridge = a_role == NeuralStereoRouteRole::Submit]]
    [[args.insertionPoint = NeuralRendering::InsertionPoint::FinalLdrPreUi]]
    [[("Upscale_NeuralFinalLdr_ColorIn_" + suffix).c_str(), targetUavDescs[eye].Format]]
    [[DispatchSubmitStageColorRegion( neuralFinalLdrColorIn[eye]->srv.get(), submitNeuralFloatColorIn[eye]->uav.get()]]
    [[args.colorInput = useSubmitNeuralFloatBridge ? submitNeuralFloatColorIn[eye]->resource.get() : neuralFinalLdrColorIn[eye]->resource.get()]]
    [[CommitSubmitNeuralFloatOutput(eye, directCommit)]]
    [[useSubmitNeuralFloatBridge ? submitNeuralFloatColorOut[eye]->srv.get() : neuralFinalLdrColorOut[eye]->srv.get()]]
)
    string(FIND
        "${_final_ldr_section}"
        "${_final_ldr_float_contract}"
        _final_ldr_float_position
    )
    if(_final_ldr_float_position EQUAL -1)
        message(FATAL_ERROR
            "Final-LDR submit float contract is missing: ${_final_ldr_float_contract}"
        )
    endif()
endforeach()

string(FIND
    "${_final_ldr_section}"
    [[context->CopySubresourceRegion( a_targets[eye].resource, a_targets[eye].subresource]]
    _final_ldr_rollback_begin
)
if(_final_ldr_rollback_begin EQUAL -1)
    message(FATAL_ERROR "Final-LDR rollback no longer targets the original output")
endif()
string(SUBSTRING
    "${_final_ldr_section}"
    ${_final_ldr_rollback_begin}
    -1
    _final_ldr_rollback_tail
)
string(FIND "${_final_ldr_rollback_tail}" [[);]] _final_ldr_rollback_end)
string(FIND
    "${_final_ldr_rollback_tail}"
    [[neuralFinalLdrColorIn[eye]->resource.get(), 0u, &originalCenterBox]]
    _final_ldr_rollback_source
)
if(_final_ldr_rollback_end EQUAL -1 OR _final_ldr_rollback_source EQUAL -1 OR
    NOT _final_ldr_rollback_source LESS _final_ldr_rollback_end)
    message(FATAL_ERROR
        "Final-LDR failure must restore the target from its same-format input snapshot"
    )
endif()

string(REGEX MATCHALL
    [[PrepareSubmitNeuralFloatResources\(]]
    _submit_float_prepare_sites
    "${_upscaling}"
)
list(LENGTH _submit_float_prepare_sites _submit_float_prepare_site_count)
if(NOT _submit_float_prepare_site_count EQUAL 3)
    message(FATAL_ERROR
        "Submit float resources must be prepared only by the helper and two insertion points"
    )
endif()

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
if(NOT _descriptor_all_of_length EQUAL 4)
    message(FATAL_ERROR "DevBench schema must retain NR and foveation conditional contracts")
endif()
string(JSON
    _descriptor_cycle_required
    GET
    "${_descriptor_json}"
    inputSchema
    allOf
    2
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
    1
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
    3
    then
    properties
    valueIndex
    maximum
)
if(NOT _descriptor_two_value_maximum EQUAL 1)
    message(FATAL_ERROR "Two-value foveation controls must reject valueIndex 2")
endif()

foreach(_status_contract IN ITEMS
    [[{ "apiVersion", 9 }]]
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
    [[{ "lastFeatureFrameId", snapshot.performance.lastFeatureFrameId }]]
    [[{ "lastFeatureEvaluationCount", snapshot.performance.lastFeatureEvaluationCount }]]
    [[{ "lastFeatureSlotMask", snapshot.performance.lastFeatureSlotMask }]]
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
    [[{ "hardMenuBlocked", route.hardMenuBlocked }]]
    [[{ "lateMenuCompositeReady", route.lateMenuCompositeReady }]]
    [[{ "csOverlayOpen", route.csOverlayOpen }]]
    [[{ "menuContinuityAllowed", route.menuContinuityAllowed }]]
    [[{ "gamePaused", temporalAdmission.gamePaused }]]
    [[{ "pausedSubmitContinuityAllowed", temporalAdmission.pausedSubmitContinuityAllowed }]]
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
    [[nr_status returns the API-v9 NR runtime]]
    [[{ "neuralRendering", NeuralRenderingStatusJson(a_upscaling) }]]
    [[{ "characterRendering", CharacterRenderingStatusJson(a_upscaling) }]]
    [[{ "visualMasking", {]]
    [[{ "implemented", snapshot.visualMaskImplemented }]]
    [[{ "providerValidated", snapshot.visualMaskProviderValidated }]]
    [[{ "status", "csx_output_composite" }]]
    [[{ "publicProductSemanticsDescribed", true }]]
    [[{ "exactBindingContractPublished", false }]]
    [[{ "computeRoi", {]]
    [[{ "inferenceRestrictedToRois", false }]]
    [[{ "maskCoveragePercent", eye.maskCoverageReady ? json(eye.maskCoveragePercent) : json(nullptr) }]]
    [[{ "authoredCategoryPixels", {]]
    [[{ "visibleCategoryPixels", {]]
    [[{ "visibilityRejectedPixels", eye.maskCoverageReady ? json(eye.visibilityRejectedPixels) : json(nullptr) }]]
    [[{ "depthCoordinates", {]]
    [[{ "zeroCoverageBypassRequested", eye.zeroCoverageBypassRequested }]]
    [[{ "zeroCoverageBypassResolved", eye.zeroCoverageBypassResolved }]]
    [[{ "zeroCoverageBypassedFeature18", eye.zeroCoverageBypassed }]]
    [[{ "feature18EvaluationSucceeded", eye.feature18EvaluationSucceeded }]]
    [[{ "zeroCoverageCpuProven", eye.zeroCoverageCpuProven }]]
    [[{ "zeroCoverageSampleReused", eye.zeroCoverageSampleReused }]]
    [[{ "feature18Disposition", NeuralRendering::GetCharacterFeature18DispositionName(eye.feature18Disposition) }]]
    [[{ "maskCoverageSampleAgeFrames",]]
    [[{ "maskCoverageMatchesCurrentPolicy", eye.maskCoverageMatchesCurrentPolicy }]]
    [[{ "authoredCategoryPixelCountSpace", "active_eye_input_pixels_before_visibility" }]]
    [[{ "visibleCategoryPixelCountSpace", "feature18_evaluation_pixels_after_visibility_inside_eligibility" }]]
    [[{ "droppedCharacterActors", eye.droppedCharacterRegions }]]
    [[{ "observationCapacityDrops", snapshot.observationCapacityDrops }]]
    [[{ "currentCategoryObservations", {]]
    [[{ "currentClassificationRejections", {]]
    [[{ "classificationRejections", {]]
    [[{ "categoryCaptureAttempts", snapshot.categoryCaptureAttempts }]]
    [[{ "categoryCaptureSuccesses", snapshot.categoryCaptureSuccesses }]]
    [[{ "categoryCaptureFailures", snapshot.categoryCaptureFailures }]]
    [[{ "categoryCaptureEmptyBypasses", snapshot.categoryCaptureEmptyBypasses }]]
    [[{ "categoryCaptureReuses", snapshot.categoryCaptureReuses }]]
    [[{ "categoryCaptureReady", snapshot.categoryCaptureReady }]]
    [[{ "categoryCaptureEmpty", snapshot.categoryCaptureEmpty }]]
    [[{ "provenEmptyFeatureBypassRequests", snapshot.provenEmptyFeatureBypassRequests }]]
    [[{ "measuredZeroCoverageBypassRequests", snapshot.measuredZeroCoverageBypassRequests }]]
    [[{ "provenEmptyFeatureBypasses", snapshot.provenEmptyFeatureBypasses }]]
    [[{ "measuredZeroCoverageBypasses", snapshot.measuredZeroCoverageBypasses }]]
    [[{ "currentBypassRequestedSlotMask", currentPreparationFound ? currentPreparation->bypassRequestedSlotMask : 0u }]]
    [[{ "currentResolutionRecordedSlotMask", currentPreparationFound ? currentPreparation->resolutionRecordedSlotMask : 0u }]]
    [[{ "currentEvaluatedSlotMask", currentPreparationFound ? currentPreparation->evaluatedSlotMask : 0u }]]
    [[{ "currentSuccessfulSlotMask", currentPreparationFound ? currentPreparation->successfulSlotMask : 0u }]]
    [[{ "currentBypassedSlotMask", currentPreparationFound ? currentPreparation->bypassedSlotMask : 0u }]]
    [[{ "currentAbortedSlotMask", currentPreparationFound ? currentPreparation->abortedSlotMask : 0u }]]
    [[{ "eligibleFaceActors", eye.visibleFaces }]]
    [[{ "eligibleCharacterActors", eye.visibleCharacterRegions }]]
    [[{ "mergedEligibilityRegions", eye.mergedRegions }]]
    [[{ "fullEyeEligibilityFallback", eye.fullEyeEligibilityFallback }]]
    [[{ "authoredMaskCoverageSampleIntervalFrames", NeuralRendering::CharacterPolicy::kCoverageSampleIntervalFrames }]]
    [[{ "forcedMaskCoverageSampleIntervalFrames", NeuralRendering::CharacterPolicy::kCoverageSampleIntervalFrames }]]
    [[{ "zeroCoverageReuseFrames", NeuralRendering::CharacterPolicy::kZeroCoverageReuseFrames }]]
    [[{ "configured", visualIsolationConfigured }]]
    [[{ "active", visualIsolationEffective }]]
    [[{ "vrAttachmentFormat", "R8G8_UNORM" }]]
    [[{ "additionalVrBytesPerPixel", 0 }]]
    [[{ "alphaTestAndBlend", snapshot.currentClassificationRejections[2] }]]
    [[ProfileTimerJson("Upscaling::DLSS5CharacterCategoryCapture")]]
    [[ProfileTimerJson("Upscaling::DLSS5CharacterMask")]]
    [[ProfileTimerJson("Upscaling::DLSS5CharacterRoiSetup")]]
    [[{ "lastFeature18GpuSample", {]]
    [[{ "frame", rendererSnapshot.performance.lastFeatureFrameId != std::numeric_limits<std::uint32_t>::max() ? json(rendererSnapshot.performance.lastFeatureFrameId) : json(nullptr) }]]
    [[{ "gpuMicroseconds", rendererSnapshot.performance.lastFeatureGpuMicroseconds }]]
    [[{ "pixelCount", rendererSnapshot.performance.lastFeaturePixelCount }]]
    [[{ "evaluationCount", rendererSnapshot.performance.lastFeatureEvaluationCount }]]
    [[{ "slotMask", lastFeatureSlotMask }]]
    [[{ "preparedCharacterSlotMask", preparedCharacterSlotMask }]]
    [[{ "evaluationRequiredCharacterSlotMask", evaluationRequiredCharacterSlotMask }]]
    [[{ "bypassRequestedCharacterSlotMask", bypassRequestedCharacterSlotMask }]]
    [[{ "resolutionRecordedCharacterSlotMask", resolutionRecordedCharacterSlotMask }]]
    [[{ "evaluatedCharacterSlotMask", evaluatedCharacterSlotMask }]]
    [[{ "successfulCharacterSlotMask", successfulCharacterSlotMask }]]
    [[{ "bypassedCharacterSlotMask", bypassedCharacterSlotMask }]]
    [[{ "abortedCharacterSlotMask", abortedCharacterSlotMask }]]
    [[{ "expectedFeatureSlotMask", expectedFeatureSlotMask }]]
    [[{ "preparedFrameFound", attributedPreparationFound }]]
    [[{ "missingPreparedFeatureSlotMask", missingPreparedFeatureSlots }]]
    [[{ "missingExpectedFeatureSlotMask", missingExpectedFeatureSlots }]]
    [[{ "unexpectedFeatureSlotMask", unexpectedFeatureSlots }]]
    [[{ "correlationScope", featureTimingIsStereoPair ? "stereo_pair" : (featureTimingIsEyeSample ? "eye_sample" : "invalid") }]]
    [[{ "coversPreparedStereoPair", featureTimingIsStereoPair && lastFeatureSlotMask == expectedFeatureSlotMask }]]
    [[{ "matchesPreparedCharacterMask", featureTimingMatchesPreparedMask }]]
    [[{ "developerModeRequired", false }]]
    [[{ "streamlineLogLevelAffectsAdmission", false }]]
    [[{ "allowUnlistedPatchedOrUnsignedRuntime", true }]]
    [[{ "reason", "D3D11 profile components and asynchronous D3D12 Feature 18 samples are exposed separately; no uncorrelated sum is reported" }]]
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

string(FIND
    "${_upscaling}"
    [[bool Upscaling::SubmitVRUpscaledFrame(]]
    _submit_stage_begin
)
string(FIND
    "${_upscaling}"
    [[bool Upscaling::TryReplaceVanillaDynamicResolutionUpsample(]]
    _submit_stage_end
)
if(_submit_stage_begin EQUAL -1 OR _submit_stage_end EQUAL -1 OR
    _submit_stage_end LESS_EQUAL _submit_stage_begin)
    message(FATAL_ERROR "Unable to isolate the submit-stage menu admission contract")
endif()
math(EXPR _submit_stage_length "${_submit_stage_end} - ${_submit_stage_begin}")
string(SUBSTRING
    "${_upscaling}"
    ${_submit_stage_begin}
    ${_submit_stage_length}
    _submit_stage
)
string(REGEX REPLACE "[\r\n\t ]+" " " _submit_stage_normalized "${_submit_stage}")

string(FIND
    "${_submit_stage}"
    [[IsNeuralRenderingMenuSuppressed()]]
    _broad_submit_menu_suppression
)
if(NOT _broad_submit_menu_suppression EQUAL -1)
    message(FATAL_ERROR
        "Submit NR must not inherit the broad main-route menu suppression policy"
    )
endif()

string(FIND
    "${_submit_stage}"
    [[PreserveCharacterDirectCommitBaselines(directCommit)]]
    _submit_float_baseline_copy
)
if(NOT _submit_float_baseline_copy EQUAL -1)
    message(FATAL_ERROR
        "Submit float NR must preserve normal DLSS without allocating the non-float baseline lane"
    )
endif()

foreach(_submit_menu_contract IN ITEMS
    [[const bool hardMenuBlocked =]]
    [[IsMainMenuContextActive()]]
    [[IsVRLoadingPresentationContextActive(state)]]
    [[IsSaveLoadTransitionContextActive()]]
    [[IsVRLoadingSubmitProtectionContextActive(*this, state)]]
    [[const bool lateMenuCompositeReady =]]
    [[const bool csOverlayOpen =]]
    [[const bool menuContinuityAllowed =]]
    [[.menuContextActive = hardMenuBlocked]]
    [[.pausedSubmitContinuityAllowed = menuContinuityAllowed]]
)
    string(FIND
        "${_submit_stage_normalized}"
        "${_submit_menu_contract}"
        _submit_menu_contract_position
    )
    if(_submit_menu_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Submit-stage menu continuity contract is missing: ${_submit_menu_contract}"
        )
    endif()
endforeach()

foreach(_named_menu_decision IN ITEMS
    hardMenuBlocked
    lateMenuCompositeReady
    menuContinuityAllowed
    neuralMenuContinuityDispatch
    foveatedRequested
)
    string(FIND
        "${_submit_stage_normalized}"
        "const bool ${_named_menu_decision} ="
        _named_menu_decision_position
    )
    if(_named_menu_decision_position EQUAL -1)
        message(FATAL_ERROR
            "Submit-stage menu decision is missing: ${_named_menu_decision}"
        )
    endif()
    string(SUBSTRING
        "${_submit_stage_normalized}"
        ${_named_menu_decision_position}
        -1
        _named_menu_decision_tail
    )
    string(FIND "${_named_menu_decision_tail}" ";" _named_menu_decision_length)
    if(_named_menu_decision_length EQUAL -1)
        message(FATAL_ERROR
            "Submit-stage menu decision has no terminator: ${_named_menu_decision}"
        )
    endif()
    string(SUBSTRING
        "${_named_menu_decision_tail}"
        0
        ${_named_menu_decision_length}
        _${_named_menu_decision}_expression
    )
endforeach()

string(FIND
    "${_hardMenuBlocked_expression}"
    [[IsMainMenuContextActive() || IsVRLoadingPresentationContextActive(state) || IsSaveLoadTransitionContextActive() || IsVRLoadingSubmitProtectionContextActive(*this, state)]]
    _hard_menu_or_chain_position
)
if(_hard_menu_or_chain_position EQUAL -1)
    message(FATAL_ERROR
        "Submit hard-menu predicate must retain the four fail-closed contexts"
    )
endif()

foreach(_hard_menu_term IN ITEMS
    [[IsMainMenuContextActive()]]
    [[IsVRLoadingPresentationContextActive(state)]]
    [[IsSaveLoadTransitionContextActive()]]
    [[IsVRLoadingSubmitProtectionContextActive(*this, state)]]
)
    string(FIND
        "${_hardMenuBlocked_expression}"
        "${_hard_menu_term}"
        _hard_menu_term_position
    )
    if(_hard_menu_term_position EQUAL -1)
        message(FATAL_ERROR
            "Submit hard-menu predicate is missing: ${_hard_menu_term}"
        )
    endif()
endforeach()

string(FIND
    "${_lateMenuCompositeReady_expression}"
    [[submitStageMenuFinalCompositeRequested]]
    _late_composite_source_position
)
if(_late_composite_source_position EQUAL -1)
    message(FATAL_ERROR
        "Late menu readiness must come from the sealed final-composite decision"
    )
endif()

foreach(_continuity_term IN ITEMS
    [[!hardMenuBlocked]]
    [[!currentMenuPresentationContext]]
    [[lateMenuCompositeReady && !csOverlayOpen]]
)
    string(FIND
        "${_menuContinuityAllowed_expression}"
        "${_continuity_term}"
        _continuity_term_position
    )
    if(_continuity_term_position EQUAL -1)
        message(FATAL_ERROR
            "Submit menu-continuity predicate is missing: ${_continuity_term}"
        )
    endif()
endforeach()

foreach(_neural_menu_dispatch_term IN ITEMS
    [[sceneFeatureMenuPauseContext]]
    [[neuralSubmitBaseEligible]]
    [[menuContinuityAllowed]]
)
    string(FIND
        "${_neuralMenuContinuityDispatch_expression}"
        "${_neural_menu_dispatch_term}"
        _neural_menu_dispatch_term_position
    )
    if(_neural_menu_dispatch_term_position EQUAL -1)
        message(FATAL_ERROR
            "Safe Neural menu dispatch is missing: ${_neural_menu_dispatch_term}"
        )
    endif()
endforeach()

foreach(_foveated_menu_term IN ITEMS
    [[!sceneFeatureMenuPauseContext]]
    [[foveatedMaskVisualizationPreview]]
    [[neuralMenuContinuityDispatch]]
)
    string(FIND
        "${_foveatedRequested_expression}"
        "${_foveated_menu_term}"
        _foveated_menu_term_position
    )
    if(_foveated_menu_term_position EQUAL -1)
        message(FATAL_ERROR
            "Foveated menu dispatch contract is missing: ${_foveated_menu_term}"
        )
    endif()
endforeach()

string(FIND
    "${_submit_stage}"
    [[const bool lateMenuCompositeReady =]]
    _late_menu_ready_position
)
string(FIND
    "${_submit_stage}"
    [[const bool foveatedRequested =]]
    _foveated_requested_position
)
if(_late_menu_ready_position EQUAL -1 OR _foveated_requested_position EQUAL -1 OR
    NOT _late_menu_ready_position LESS _foveated_requested_position)
    message(FATAL_ERROR
        "Late menu-composite readiness must participate in the foveated submit decision"
    )
endif()

string(FIND
    "${_upscaling}"
    [[void Upscaling::NotifyVRMenuPresentationContextChange(]]
    _menu_notification_begin
)
string(FIND
    "${_upscaling}"
    [[bool Upscaling::SealVRMenuFrameTransaction(]]
    _menu_notification_end
)
if(_menu_notification_begin EQUAL -1 OR _menu_notification_end EQUAL -1 OR
    _menu_notification_end LESS_EQUAL _menu_notification_begin)
    message(FATAL_ERROR "Unable to isolate the VR menu-change notification")
endif()
math(EXPR
    _menu_notification_length
    "${_menu_notification_end} - ${_menu_notification_begin}"
)
string(SUBSTRING
    "${_upscaling}"
    ${_menu_notification_begin}
    ${_menu_notification_length}
    _menu_notification
)
string(FIND
    "${_menu_notification}"
    [[g_neuralMenuQueryEpoch.fetch_add(]]
    _menu_epoch_advance_position
)
if(_menu_epoch_advance_position EQUAL -1)
    message(FATAL_ERROR "Menu changes must invalidate retained submit-pair identity")
endif()
string(FIND
    "${_menu_notification}"
    [[RequestHistoryReset(]]
    _unconditional_menu_history_reset_position
)
if(NOT _unconditional_menu_history_reset_position EQUAL -1)
    message(FATAL_ERROR
        "Ordinary menu notifications must not reset NR history outside temporal admission"
    )
endif()

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
    [[static std::once_flag installed;]]
    [[std::call_once(installed,]]
    [[CaptureAuthoredCategories(]]
    [[CS_PROFILE_SCOPE("Upscaling::DLSS5CharacterCategoryCapture")]]
    [[capturedFrame_ != a_args.frameId]]
    [[const bool logicalEmptyCapture = state_->capturedCategoriesEmpty_;]]
    [[if (logicalEmptyCapture) {]]
    [[state_->ClearMask(]]
    [[slot.requiresEvaluation =]]
    [[const bool cpuProvenEmpty =]]
    [[const bool measuredZero =]]
    [[state_->HasFreshZeroAuthoredCoverageSample(]]
    [[UsesAuthoredMask(a_args.settings.maskTestMode)]]
    [[const bool samplePending = std::ranges::any_of(]]
    [[sourceEyeWidth = state_->capturedEyeWidth_;]]
    [[state_->capturedEyeWidth_ != a_args.viewportCrop.fullInput.width]]
    [[state_->capturedHeight_ != a_args.viewportCrop.fullInput.height]]
    [[state_->RecordPreparedFrame(]]
    [[a_args.viewportCrop.input.right > sourceEyeWidth]]
    [[if (!characterEvaluationRequired)]]
    [[DXGI_FORMAT_R8G8_UNORM]]
    [[DXGI_FORMAT_R8_UNORM]]
    [[inline constexpr std::uint32_t kCoverageSampleIntervalFrames = 30;]]
    [[inline constexpr std::uint32_t kZeroCoverageReuseFrames = 4;]]
    [[CharacterCategoryMask::Encode]]
    [[CharacterCategoryMask::DecodeCategory]]
    [[CharacterCategoryMask::DecodeInverseVertexAo]]
    [[IsCharacterNeuralRenderingRouteRequested()]]
    [[NeuralRendering::CharacterRendering::Instance().Invalidate();]]
    [[const Upscaling::Settings defaults{};]]
    [[a_json.value(#name, defaults.name)]]
    [[OP(neuralRenderingInsertionPoint)]]
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
    [[IsOrderedStereoFeatureSlotPair(left.featureSlot, right.featureSlot)]]
    [[generation, insertion point, feature mode]]
    [[a_neuralRouteAllowed]]
    [[if (!compositeCenter)]]
    [[slEvaluateFeature(sl::kFeatureDLSS,]]
    [[dlssPassTelemetryFrames.GetOrCreate(]]
    [[evaluationSucceededFeatureSlotMask]]
    [[RecordNeuralPassTelemetry(]]
    [[PrepareCharacterSelectionMask(]]
    [[a_args.tuning.useAutoMask = true;]]
    [[!UsesCharacterVisualIsolation(settings) &&]]
    [[state_->capturedEnabledCategoryMask_ == a_enabledCategoryMask]]
    [[Increment(state_->snapshot_.categoryCaptureReuses);]]
    [[plan.fullEyeEligibilityFallback = true;]]
    [[(logicalEmptyCapture || plan.regions.empty());]]
    [[plan.regions.empty() && plan.projectionUncertain]]
    [[IsCharacterMaterialCandidate(]]
    [[classificationCache.try_emplace(]]
    [[ResolveCharacterCompositeInputs(]]
    [[const bool directCommit = params.front().neuralDirectCommit;]]
    [[params.back().neuralDirectCommit != directCommit]]
    [[params.neuralDirectCommit = neuralDirectCommit;]]
    [[EvaluateTemporalAdmission(]]
    [[BuildNeuralTemporalAdmission(]]
    [[ObserveNeuralTemporalAdmission(]]
    [[neuralTemporalAdmission.admitted &&]]
    [[timingFenceValue > lastCompletedTimingFenceValue_]]
    [[CharacterRegionPolicy::MergeAndLimit(]]
    [[RefreshProjectedActors(a_args);]]
    [[if (!currentlyProjected.contains(it->first))]]
    [[.currentDepthIdentity = currentDepthIdentity,]]
    [[.captureJitterX = state_->capturedJitterX_,]]
    [[.captureJitterY = state_->capturedJitterY_,]]
    [[capturedDepthSrv_.Get(),]]
    [[std::array<ID3D11ShaderResourceView*, 3> srvs{]]
    [[a_authoredDepthSource,]]
    [[a_args.depthGuide,]]
    [[(a_args.featureSlot & 1u) != a_args.eyeIndex]]
    [[slot.requiresEvaluation = true;]]
    [[neuralEvaluation.pairBypassed,]]
    [[for (uint32_t stereoEye = 0; stereoEye < neuralResults.size(); ++stereoEye)]]
)
    string(FIND "${_source_contract_text}" "${_source_contract}" _source_position)
    if(_source_position EQUAL -1)
        message(FATAL_ERROR
            "Neural Rendering source contract is missing: ${_source_contract}"
        )
    endif()
endforeach()

string(FIND "${_runtime_source}" "bool Runtime::Initialize(" _runtime_initialize_begin)
string(FIND "${_runtime_source}" "bool Runtime::Execute(" _runtime_execute_begin)
string(FIND "${_runtime_source}" "bool Runtime::ResetFeatureLocked(" _runtime_reset_begin)
string(FIND "${_runtime_source}" "bool Runtime::ShutdownLocked()" _runtime_shutdown_begin)
string(FIND "${_runtime_source}" "void Runtime::AbandonLocked()" _runtime_abandon_begin)
if(_runtime_initialize_begin EQUAL -1 OR _runtime_execute_begin EQUAL -1 OR
    _runtime_reset_begin EQUAL -1 OR _runtime_shutdown_begin EQUAL -1 OR
    _runtime_abandon_begin EQUAL -1)
    message(FATAL_ERROR "NR runtime lifecycle functions could not be isolated")
endif()
if(NOT _runtime_initialize_begin LESS _runtime_execute_begin OR
    NOT _runtime_execute_begin LESS _runtime_reset_begin OR
    NOT _runtime_reset_begin LESS _runtime_shutdown_begin OR
    NOT _runtime_shutdown_begin LESS _runtime_abandon_begin)
    message(FATAL_ERROR "NR runtime lifecycle functions are out of order")
endif()
math(EXPR _runtime_initialize_length
    "${_runtime_execute_begin} - ${_runtime_initialize_begin}"
)
math(EXPR _runtime_execute_length
    "${_runtime_reset_begin} - ${_runtime_execute_begin}"
)
string(FIND "${_runtime_source}" "bool Runtime::ResetFeatures()"
    _runtime_reset_end)
if(_runtime_reset_end EQUAL -1 OR
    NOT _runtime_reset_begin LESS _runtime_reset_end)
    message(FATAL_ERROR "NR feature-reset function could not be isolated")
endif()
math(EXPR _runtime_reset_length
    "${_runtime_reset_end} - ${_runtime_reset_begin}"
)
math(EXPR _runtime_shutdown_length
    "${_runtime_abandon_begin} - ${_runtime_shutdown_begin}"
)
string(SUBSTRING "${_runtime_source}" ${_runtime_initialize_begin}
    ${_runtime_initialize_length} _runtime_initialize)
string(SUBSTRING "${_runtime_source}" ${_runtime_execute_begin}
    ${_runtime_execute_length} _runtime_execute)
string(SUBSTRING "${_runtime_source}" ${_runtime_reset_begin}
    ${_runtime_reset_length} _runtime_reset)
string(SUBSTRING "${_runtime_source}" ${_runtime_shutdown_begin}
    ${_runtime_shutdown_length} _runtime_shutdown)

foreach(_runtime_cache_contract IN ITEMS
    [[RuntimeExports runtimeExports_{};]]
    [[ParameterCoreExports parameterCoreExports_{};]]
    [[reinterpret_cast<InitD3D12>(runtimeExports_.initialize)]]
    [[reinterpret_cast<CreateFeature>(runtimeExports_.createFeature)]]
    [[reinterpret_cast<EvaluateFeature>(runtimeExports_.evaluateFeature)]]
    [[reinterpret_cast<ReleaseFeature>(runtimeExports_.releaseFeature)]]
    [[reinterpret_cast<ShutdownD3D12>(runtimeExports_.shutdown)]]
    [[parameterCoreExports_.destroyParameters]]
    [[InterlockedCompareExchangePointer(]]
    [[HMODULE retainedCallerModule = nullptr;]]
    [[active->retainedCallerModule]]
    [[ReadImportSlot(g_pathProxySlot)]]
    [[GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS]]
    [[GET_MODULE_HANDLE_EX_FLAG_PIN]]
	[[ImportSlotExchangeResult]]
	[[exchange.protectionRestored]]
	[[exchange.rolledBack]]
    [[runtimeExports_ = {]]
    [[.initialize = requiredExports[0],]]
    [[parameterCoreExports_ = {]]
    [[.allocateParameters = reinterpret_cast<void*>(]]
)
    string(FIND
        "${_runtime_header}\n${_runtime_source}"
        "${_runtime_cache_contract}"
        _runtime_cache_position
    )
    if(_runtime_cache_position EQUAL -1)
        message(FATAL_ERROR
            "NR runtime cached-export contract is missing: ${_runtime_cache_contract}"
        )
    endif()
endforeach()

string(FIND "${_runtime_source}"
    [[bool InstallRuntimeCallerPathHook(]] _hook_install_begin)
string(FIND "${_runtime_source}"
    [[bool UninstallRuntimeCallerPathHook(]] _hook_install_end)
if(_hook_install_begin EQUAL -1 OR _hook_install_end EQUAL -1 OR
    NOT _hook_install_begin LESS _hook_install_end)
    message(FATAL_ERROR "NR caller-path hook installation could not be isolated")
endif()
math(EXPR _hook_install_length "${_hook_install_end} - ${_hook_install_begin}")
string(SUBSTRING "${_runtime_source}" ${_hook_install_begin}
    ${_hook_install_length} _hook_install)
string(FIND "${_hook_install}"
    [[&invocation->retainedCallerModule]] _hook_pin_position)
string(FIND "${_hook_install}"
    [[ProtectAndExchangeImportSlot(]] _hook_exchange_position)
if(_hook_pin_position EQUAL -1 OR _hook_exchange_position EQUAL -1 OR
    NOT _hook_pin_position LESS _hook_exchange_position)
    message(FATAL_ERROR
        "NR caller module must be pinned before the vendor IAT is changed"
    )
endif()
string(FIND "${_runtime_source}" [[callerModulePinned]] _late_hook_pin)
if(NOT _late_hook_pin EQUAL -1)
    message(FATAL_ERROR
        "NR caller-path safety must not rely on a fallible teardown-time pin"
    )
endif()

string(REGEX MATCHALL
    "InstallRuntimeCallerPathHook\\("
    _runtime_hook_installs
    "${_runtime_source}"
)
list(LENGTH _runtime_hook_installs _runtime_hook_install_count)
if(NOT _runtime_hook_install_count EQUAL 2)
    message(FATAL_ERROR
        "NR caller-path hook must have one definition and one lifetime install"
    )
endif()
string(FIND "${_runtime_source}" "RuntimeCallerPathScope" _old_runtime_scope)
if(NOT _old_runtime_scope EQUAL -1)
    message(FATAL_ERROR "NR runtime must not reinstall its IAT proxy per NGX call")
endif()

foreach(_initialize_contract IN ITEMS
    [[InstallRuntimeCallerPathHook(]]
    [[RuntimeCallerPathCall pathProxy(runtime);]]
	[[lastPathProxyInstalled_ = pathProxy.IsInstalled();]]
    [[initialize(]]
    [[UninstallRuntimeCallerPathHook(runtime)]]
)
    string(FIND "${_runtime_initialize}" "${_initialize_contract}"
        _initialize_contract_position)
    if(_initialize_contract_position EQUAL -1)
        message(FATAL_ERROR
            "NR initialization hook contract is missing: ${_initialize_contract}"
        )
    endif()
endforeach()
string(FIND "${_runtime_initialize}" "InstallRuntimeCallerPathHook("
    _initialize_install_position)
string(FIND "${_runtime_initialize}" "const auto initializeResult = initialize("
    _initialize_call_position)
if(NOT _initialize_install_position LESS _initialize_call_position)
    message(FATAL_ERROR "NR caller-path hook must precede NGX initialization")
endif()

foreach(_execute_contract IN ITEMS
    [[RuntimeCallerPathCall pathProxy(runtime);]]
    [[runtimeExports_.createFeature]]
    [[runtimeExports_.evaluateFeature]]
)
    string(FIND "${_runtime_execute}" "${_execute_contract}"
        _execute_contract_position)
    if(_execute_contract_position EQUAL -1)
        message(FATAL_ERROR
            "NR Execute cached-hook contract is missing: ${_execute_contract}"
        )
    endif()
endforeach()
foreach(_execute_forbidden IN ITEMS
    [[InstallRuntimeCallerPathHook(]]
    [[UninstallRuntimeCallerPathHook(]]
    [[GetProcAddress(]]
)
    string(FIND "${_runtime_execute}" "${_execute_forbidden}"
        _execute_forbidden_position)
    if(NOT _execute_forbidden_position EQUAL -1)
        message(FATAL_ERROR
            "NR Execute must not perform runtime hook/export setup: ${_execute_forbidden}"
        )
    endif()
endforeach()

foreach(_reset_contract IN ITEMS
    [[RuntimeCallerPathCall pathProxy(runtime);]]
    [[runtimeExports_.releaseFeature]]
)
    string(FIND "${_runtime_reset}" "${_reset_contract}"
        _reset_contract_position)
    if(_reset_contract_position EQUAL -1)
        message(FATAL_ERROR
            "NR feature-reset cached-hook contract is missing: ${_reset_contract}"
        )
    endif()
endforeach()
string(FIND "${_runtime_reset}" "GetProcAddress(" _reset_get_proc)
if(NOT _reset_get_proc EQUAL -1)
    message(FATAL_ERROR "NR feature reset must use its cached release export")
endif()

string(FIND "${_runtime_shutdown}" "shutdown(static_cast<ID3D12Device*>(device_))"
    _runtime_shutdown_call)
string(FIND "${_runtime_shutdown}" "UninstallRuntimeCallerPathHook(runtime)"
    _runtime_uninstall_call)
string(FIND "${_runtime_shutdown}" "FreeLibrary(runtime)"
    _runtime_free_call)
if(_runtime_shutdown_call EQUAL -1 OR _runtime_uninstall_call EQUAL -1 OR
    _runtime_free_call EQUAL -1 OR
    NOT _runtime_shutdown_call LESS _runtime_uninstall_call OR
    NOT _runtime_uninstall_call LESS _runtime_free_call)
    message(FATAL_ERROR
        "NR shutdown must call NGX shutdown, restore its IAT, then unload"
    )
endif()
string(FIND "${_runtime_source}" [[void Runtime::AbandonLocked() noexcept]]
    _runtime_abandon_contract)
string(FIND "${_runtime_source}" [[pathProxyRestored = UninstallRuntimeCallerPathHook(]]
    _runtime_abandon_restore)
if(_runtime_abandon_contract EQUAL -1 OR _runtime_abandon_restore EQUAL -1)
    message(FATAL_ERROR "Unsafe NR abandon must attempt IAT restoration")
endif()

foreach(_readback_contract IN ITEMS
    [[D3D11_ASYNC_GETDATA_DONOTFLUSH]]
    [[D3D11_MAP_FLAG_DO_NOT_WAIT]]
    [[if (queryResult == S_FALSE)]]
    [[if (mapResult == DXGI_ERROR_WAS_STILL_DRAWING)]]
    [[Increment(snapshot_.readbackDrops);]]
    [[if (lastReadbackPollFrame_ == a_frame)]]
    [[readback.serial >= slot.maskCoverageSerial]]
    [[const bool samplePending = std::ranges::any_of(]]
    [[const bool measurementDue =]]
    [[a_slot.lastCoverageRequestPolicyKey != samplingPolicyKey]]
    [[periodicSampleDue]]
    [[coverageReadback->serial = AllocateCoverageSerial();]]
    [[CharacterPolicy::kCoverageSampleIntervalFrames]]
)
    string(FIND "${_character_source}" "${_readback_contract}"
        _readback_contract_position)
    if(_readback_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Asynchronous character-mask readback contract is missing: ${_readback_contract}"
        )
    endif()
endforeach()
foreach(_bounded_zero_contract IN ITEMS
    [[a_slot.maskDiagnosticKey != a_diagnosticKey]]
    [[a_frame - a_slot.maskCoverageFrame]]
    [[CharacterPolicy::kZeroCoverageReuseFrames]]
    [[IsCategoryEnabled(characterCategory, a_settings)]]
    [[a_slot.authoredCategoryPixels[category - 1u] != 0]]
    [[add(a_plan.eligibilitySignature);]]
    [[const bool cpuProvenEmpty =]]
    [[const bool measuredZero =]]
    [[slot.zeroCoverageBypassed = false;]]
    [[Increment(state_->snapshot_.provenEmptyFeatureBypassRequests);]]
    [[Increment(state_->snapshot_.measuredZeroCoverageBypassRequests);]]
)
    string(FIND "${_character_source}" "${_bounded_zero_contract}"
        _bounded_zero_position)
    if(_bounded_zero_position EQUAL -1)
        message(FATAL_ERROR
            "Bounded zero-mask bypass contract is missing: ${_bounded_zero_contract}"
        )
    endif()
endforeach()

string(FIND "${_character_source}"
    "std::uint64_t BuildCoverageSamplingPolicyKey(" _sampling_policy_begin)
string(FIND "${_character_source}"
    "std::uint64_t BuildDiagnosticKey(" _sampling_policy_end)
if(_sampling_policy_begin EQUAL -1 OR _sampling_policy_end EQUAL -1 OR
    NOT _sampling_policy_begin LESS _sampling_policy_end)
    message(FATAL_ERROR "Character coverage sampling policy could not be isolated")
endif()
math(EXPR _sampling_policy_length
    "${_sampling_policy_end} - ${_sampling_policy_begin}"
)
string(SUBSTRING "${_character_source}" ${_sampling_policy_begin}
    ${_sampling_policy_length} _sampling_policy)
foreach(_dynamic_sampling_token IN ITEMS
    [[a_plan]]
    [[eligibilitySignature]]
    [[capturedJitterX_]]
    [[capturedJitterY_]]
    [[roiRectangles]]
)
    string(FIND "${_sampling_policy}" "${_dynamic_sampling_token}"
        _dynamic_sampling_position)
    if(NOT _dynamic_sampling_position EQUAL -1)
        message(FATAL_ERROR
            "Coverage cadence policy contains dynamic frame content: ${_dynamic_sampling_token}"
        )
    endif()
endforeach()

string(FIND "${_character_source}"
    "std::uint64_t BuildDiagnosticKey(" _diagnostic_key_begin)
string(FIND "${_character_source}"
    "bool HasFreshZeroAuthoredCoverageSample(" _diagnostic_key_end)
if(_diagnostic_key_begin EQUAL -1 OR _diagnostic_key_end EQUAL -1 OR
    NOT _diagnostic_key_begin LESS _diagnostic_key_end)
    message(FATAL_ERROR "Character zero-proof compatibility key could not be isolated")
endif()
math(EXPR _diagnostic_key_length
    "${_diagnostic_key_end} - ${_diagnostic_key_begin}"
)
string(SUBSTRING "${_character_source}" ${_diagnostic_key_begin}
    ${_diagnostic_key_length} _diagnostic_key)
foreach(_diagnostic_key_contract IN ITEMS
    [[BuildCoverageSamplingPolicyKey(]]
    [[add(a_plan.eligibilitySignature);]]
)
    string(FIND "${_diagnostic_key}" "${_diagnostic_key_contract}"
        _diagnostic_key_position)
    if(_diagnostic_key_position EQUAL -1)
        message(FATAL_ERROR
            "Character zero-proof compatibility key is missing: ${_diagnostic_key_contract}"
        )
    endif()
endforeach()
foreach(_dynamic_diagnostic_token IN ITEMS
    [[capturedJitterX_]]
    [[capturedJitterY_]]
    [[roiRectangles]]
)
    string(FIND "${_diagnostic_key}" "${_dynamic_diagnostic_token}"
        _dynamic_diagnostic_position)
    if(NOT _dynamic_diagnostic_position EQUAL -1)
        message(FATAL_ERROR
            "Zero-proof compatibility key contains unstable geometry: ${_dynamic_diagnostic_token}"
        )
    endif()
endforeach()
string(FIND "${_character_source}" "bool CharacterRendering::PrepareMask("
    _prepare_mask_begin)
string(FIND "${_character_source}"
    "void CharacterRendering::ResolveFeature18Disposition("
    _prepare_mask_end)
if(_prepare_mask_begin EQUAL -1 OR _prepare_mask_end EQUAL -1 OR
    NOT _prepare_mask_begin LESS _prepare_mask_end)
    message(FATAL_ERROR "Character mask preparation function could not be isolated")
endif()

string(FIND "${_character_source}"
    "void CharacterRendering::ResolveFeature18Disposition("
    _resolve_disposition_begin)
string(FIND "${_character_source}" "void CharacterRendering::Reset()"
    _resolve_disposition_end)
if(_resolve_disposition_begin EQUAL -1 OR _resolve_disposition_end EQUAL -1 OR
    NOT _resolve_disposition_begin LESS _resolve_disposition_end)
    message(FATAL_ERROR "Character Feature 18 disposition function could not be isolated")
endif()
math(EXPR _resolve_disposition_length
    "${_resolve_disposition_end} - ${_resolve_disposition_begin}"
)
string(SUBSTRING "${_character_source}" ${_resolve_disposition_begin}
    ${_resolve_disposition_length} _resolve_disposition)
foreach(_disposition_contract IN ITEMS
    [[a_evaluatedFeatureSlotMask & unresolvedMask]]
    [[a_successfulFeatureSlotMask & evaluatedMask]]
    [[a_bypassedFeatureSlotMask & unresolvedMask & ~evaluatedMask]]
    [[preparedFrame->resolutionRecordedSlotMask |= slotBit;]]
    [[preparedFrame->evaluatedSlotMask |= slotBit;]]
    [[preparedFrame->successfulSlotMask |= slotBit;]]
    [[preparedFrame->bypassedSlotMask |= slotBit;]]
    [[preparedFrame->abortedSlotMask |= slotBit;]]
    [[Increment(state_->snapshot_.provenEmptyFeatureBypasses);]]
    [[Increment(state_->snapshot_.measuredZeroCoverageBypasses);]]
)
    string(FIND "${_resolve_disposition}" "${_disposition_contract}"
        _disposition_contract_position)
    if(_disposition_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Character Feature 18 disposition contract is missing: ${_disposition_contract}"
        )
    endif()
endforeach()
foreach(_disposition_call_contract IN ITEMS
    [[std::array<bool, 2> evaluatedEyes{};]]
    [[std::array<bool, 2> successfulEyes{};]]
    [[successfulEyes.fill(true);]]
    [[ResolveAbortedCharacterFeature18Preparations(]]
)
    string(FIND "${_upscaling}" "${_disposition_call_contract}"
        _disposition_call_position)
    if(_disposition_call_position EQUAL -1)
        message(FATAL_ERROR
            "Character Feature 18 outcome call-site contract is missing: ${_disposition_call_contract}"
        )
    endif()
endforeach()

math(EXPR _prepare_mask_length "${_prepare_mask_end} - ${_prepare_mask_begin}")
string(SUBSTRING "${_character_source}" ${_prepare_mask_begin}
    ${_prepare_mask_length} _prepare_mask)
string(FIND "${_prepare_mask}"
    [[Increment(state_->snapshot_.provenEmptyFeatureBypasses);]]
    _premature_proven_bypass)
string(FIND "${_prepare_mask}"
    [[Increment(state_->snapshot_.measuredZeroCoverageBypasses);]]
    _premature_measured_bypass)
if(NOT _premature_proven_bypass EQUAL -1 OR
    NOT _premature_measured_bypass EQUAL -1)
    message(FATAL_ERROR
        "Mask preparation must report zero requests, not completed Feature 18 skips"
    )
endif()
string(FIND "${_prepare_mask}" "state_->Dispatch(" _prepare_mask_dispatch)
string(FIND "${_prepare_mask}" "const bool measuredZero ="
    _prepare_mask_measured_zero)
if(_prepare_mask_dispatch EQUAL -1 OR _prepare_mask_measured_zero EQUAL -1 OR
    NOT _prepare_mask_dispatch LESS _prepare_mask_measured_zero)
    message(FATAL_ERROR
        "A reused zero sample must never suppress current-frame mask generation"
    )
endif()

foreach(_tuple_contract IN ITEMS
    [[float4 Encode(float inverseVertexAo, uint category, float opacity)]]
    [[saturate(inverseVertexAo),]]
    [[EncodeCategory(category),]]
    [[0.0,]]
    [[saturate(opacity))]]
    [[const uint code = uint(round(saturate(encodedValue.y) * 255.0));]]
    [[if (code == 85u)]]
    [[if (code == 170u)]]
    [[return code == 255u ? 3u : 0u;]]
    [[return encodedValue.x;]]
)
    string(FIND
        "${_character_category_shader}"
        "${_tuple_contract}"
        _tuple_contract_position
    )
    if(_tuple_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Character provenance tuple contract is missing: ${_tuple_contract}"
        )
    endif()
endforeach()

foreach(_mask_shader_contract IN ITEMS
    [[Texture2D<unorm float2> AuthoredTuple : register(t0);]]
    [[Texture2D<float> AuthoredDepth : register(t1);]]
    [[Texture2D<float> CurrentDepth : register(t2);]]
    [[RWTexture2D<unorm float> CharacterSelectionMask : register(u0);]]
    [[RWByteAddressBuffer DiagnosticCounters : register(u1);]]
    [[groupshared uint GroupCounters[8];]]
    [[const uint2 inputSize = uint2(SourceCrop.w, Options.x);]]
    [[measureCoverage && all(dispatchThreadId.xy < inputSize)]]
    [[ReadAuthoredCategory(int2(dispatchThreadId.xy))]]
    [[AuthoredFacePixels);]]
    [[CountCategory(centerCategory, VisibleFacePixels);]]
    [[const bool visibilityRejectionEnabled = VisibilityOptions.y >= 0.5;]]
    [[return currentDepth + tolerance >= authoredDepth;]]
    [[CharacterCategoryMask::DecodeCategory(]]
    [[IsAuthoredSurfaceVisible(sourcePixel, centerDepth)]]
    [[if (centerVisible && Options.w != 0 && Options.z != 0 && mask < 1.0)]]
)
    string(FIND
        "${_character_mask_shader}"
        "${_mask_shader_contract}"
        _mask_shader_contract_position
    )
    if(_mask_shader_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Character mask depth/provenance contract is missing: ${_mask_shader_contract}"
        )
    endif()
endforeach()
foreach(_coverage_dispatch_contract IN ITEMS
    [[const auto dispatchWidth = std::max(]]
    [[a_args.outputWidth, a_args.viewportCrop.input.Width());]]
    [[const auto dispatchHeight = std::max(]]
    [[a_args.outputHeight, a_args.viewportCrop.input.Height());]]
)
    string(FIND "${_character_source}" "${_coverage_dispatch_contract}"
        _coverage_dispatch_position)
    if(_coverage_dispatch_position EQUAL -1)
        message(FATAL_ERROR
            "Character coverage dispatch does not span input and output domains: ${_coverage_dispatch_contract}"
        )
    endif()
endforeach()

string(FIND "${_character_mask_shader}" "bool IsAuthoredSurfaceVisible("
    _visibility_function_begin)
string(FIND "${_character_mask_shader}" "void CountCategory("
    _visibility_function_end)
if(_visibility_function_begin EQUAL -1 OR _visibility_function_end EQUAL -1 OR
    NOT _visibility_function_begin LESS _visibility_function_end)
    message(FATAL_ERROR "Character visibility function could not be isolated")
endif()
math(EXPR _visibility_function_length
    "${_visibility_function_end} - ${_visibility_function_begin}"
)
string(SUBSTRING "${_character_mask_shader}" ${_visibility_function_begin}
    ${_visibility_function_length} _visibility_function)
set(_visibility_previous_position -1)
foreach(_visibility_contract IN ITEMS
    [[if (!visibilityRejectionEnabled && !depthFeatherEnabled)]]
    [[currentDepth = LinearizeDepth(ReadCurrentDepth(localSourcePixel));]]
    [[const float authoredDepth = LinearizeDepth(ReadAuthoredDepth(localSourcePixel));]]
    [[const float tolerance = max(]]
    [[return currentDepth + tolerance >= authoredDepth;]]
)
    string(FIND "${_visibility_function}" "${_visibility_contract}"
        _visibility_position)
    if(_visibility_position EQUAL -1 OR
        NOT _visibility_previous_position LESS _visibility_position)
        message(FATAL_ERROR
            "Directional character visibility contract is missing or out of order: ${_visibility_contract}"
        )
    endif()
    set(_visibility_previous_position ${_visibility_position})
endforeach()
string(FIND "${_visibility_function}"
    "currentDepth = LinearizeDepth(ReadCurrentDepth(localSourcePixel));"
    _visibility_current_depth)
string(FIND "${_visibility_function}"
    "const float authoredDepth = LinearizeDepth(ReadAuthoredDepth(localSourcePixel));"
    _visibility_authored_depth)
math(EXPR _visibility_bypass_length
    "${_visibility_authored_depth} - ${_visibility_current_depth}"
)
string(SUBSTRING "${_visibility_function}" ${_visibility_current_depth}
    ${_visibility_bypass_length} _visibility_bypass)
foreach(_visibility_bypass_contract IN ITEMS
    [[if (!visibilityRejectionEnabled)]]
    [[return true;]]
)
    string(FIND "${_visibility_bypass}" "${_visibility_bypass_contract}"
        _visibility_bypass_position)
    if(_visibility_bypass_position EQUAL -1)
        message(FATAL_ERROR
            "Disabling visibility rejection must retain current depth for feathering: ${_visibility_bypass_contract}"
        )
    endif()
endforeach()
foreach(_visibility_forbidden IN ITEMS
    [[abs(authoredDepth - currentDepth) <= tolerance]]
    [[authoredDepth + tolerance >= currentDepth]]
)
    string(FIND "${_visibility_function}" "${_visibility_forbidden}"
        _visibility_forbidden_position)
    if(NOT _visibility_forbidden_position EQUAL -1)
        message(FATAL_ERROR
            "Character visibility restored an invalid depth comparison: ${_visibility_forbidden}"
        )
    endif()
endforeach()

foreach(_capture_contract IN ITEMS
    [[ID3D11ShaderResourceView* a_depthSource,]]
    [[std::uint32_t a_sourceEyeWidth,]]
    [[std::uint32_t a_sourceHeight,]]
    [[const auto activeStereoWidth =]]
    [[static_cast<std::uint64_t>(a_sourceEyeWidth) * 2u;]]
    [[categoryCaptureDesc.Width = static_cast<UINT>(activeStereoWidth);]]
    [[categoryCaptureDesc.Height = a_sourceHeight;]]
    [[depthCaptureDesc.Width = static_cast<UINT>(activeStereoWidth);]]
    [[depthCaptureDesc.Height = a_sourceHeight;]]
    [[state_->EnsureCategoryCapture(a_device, categoryCaptureDesc)]]
    [[state_->EnsureDepthCapture(]]
    [[a_context->CopySubresourceRegion(]]
    [[state_->capturedCategories_.Get(), 0, 0, 0, 0,]]
    [[state_->capturedDepth_.Get(), 0, 0, 0, 0,]]
    [[a_categorySource, 0, &activeStereoBox);]]
    [[depthTexture.Get(), 0, &activeStereoBox);]]
    [[ComPtr<ID3D11Texture2D> capturedDepth_;]]
    [[ComPtr<ID3D11ShaderResourceView> capturedDepthSrv_;]]
    [[.authoredDepthIdentity = authoredDepthIdentity,]]
    [[.currentDepthIdentity = currentDepthIdentity,]]
    [[state_->capturedDepthSrv_.Get(),]]
    [[state_->capturedEyeWidth_ = a_sourceEyeWidth;]]
    [[state_->capturedHeight_ = a_sourceHeight;]]
    [[static_cast<std::uint64_t>(sourceEyeWidth) * 2u != sourceDesc.Width]]
    [[state_->capturedHeight_ != sourceDesc.Height]]
    [[constants.sourceCrop[0] = a_args.eyeIndex * a_sourceEyeWidth;]]
)
    string(FIND
        "${_character_source}\n${_character_header}"
        "${_capture_contract}"
        _capture_contract_position
    )
    if(_capture_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Frozen/current character-depth contract is missing: ${_capture_contract}"
        )
    endif()
endforeach()

foreach(_forbidden_full_capture IN ITEMS
    [[a_context->CopyResource(
					state_->capturedCategories_.Get(), a_categorySource);]]
    [[a_context->CopyResource(
					state_->capturedDepth_.Get(), depthTexture.Get());]]
    [[sourceDesc.Width / 2u]]
)
    string(FIND
        "${_character_source}"
        "${_forbidden_full_capture}"
        _forbidden_full_capture_position
    )
    if(NOT _forbidden_full_capture_position EQUAL -1)
        message(FATAL_ERROR
            "Character capture must use its exact packed logical stereo extent: ${_forbidden_full_capture}"
        )
    endif()
endforeach()

string(FIND
    "${_deferred}"
    [[DXGI_FORMAT_R8G8_UNORM]]
    _vr_tuple_format_position
)
string(FIND
    "${_character_source}"
    [[textureDesc.Format = DXGI_FORMAT_R8_UNORM;]]
    _selection_mask_format_position
)
if(_vr_tuple_format_position EQUAL -1 OR _selection_mask_format_position EQUAL -1)
    message(FATAL_ERROR
        "VR category provenance must use RG8 UNORM and the CSX selection mask R8 UNORM"
    )
endif()

string(FIND
    "${_character_source}"
    [[void InvalidatePreparedSlot(]]
    _slot_invalidation_start
)
if(_slot_invalidation_start EQUAL -1)
    message(FATAL_ERROR "Per-slot character-mask invalidation is missing")
endif()
string(SUBSTRING
    "${_character_source}"
    ${_slot_invalidation_start}
    1200
    _slot_invalidation_body
)
foreach(_slot_invalidation_contract IN ITEMS
    [[if (a_featureSlot < slots_.size())]]
    [[slot.prepared = false;]]
    [[slot.requiresEvaluation = true;]]
    [[slot.prepareKey = {};]]
    [[lastSlotForEye_[a_eyeIndex] = 4;]]
    [[snapshot_.eyes[a_eyeIndex] = {};]]
)
    string(FIND
        "${_slot_invalidation_body}"
        "${_slot_invalidation_contract}"
        _slot_invalidation_contract_position
    )
    if(_slot_invalidation_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Per-slot character-mask invalidation is not fail closed: ${_slot_invalidation_contract}"
        )
    endif()
endforeach()
string(REGEX MATCHALL
    "InvalidatePreparedSlot\\("
    _slot_invalidation_sites
    "${_character_source}"
)
list(LENGTH _slot_invalidation_sites _slot_invalidation_site_count)
if(_slot_invalidation_site_count LESS 4)
    message(FATAL_ERROR
        "Preparation failures and both exception paths must invalidate only the affected slot"
    )
endif()

string(FIND
    "${_character_source}"
    [[bool CharacterRendering::PrepareMask(]]
    _prepare_mask_start
)
if(_prepare_mask_start EQUAL -1)
    message(FATAL_ERROR "Character-mask preparation entry point is missing")
endif()
string(SUBSTRING
    "${_character_source}"
    ${_prepare_mask_start}
    -1
    _prepare_mask_tail
)
string(FIND
    "${_prepare_mask_tail}"
    [[void CharacterRendering::Reset() noexcept]]
    _prepare_mask_length
)
if(_prepare_mask_length EQUAL -1)
    message(FATAL_ERROR "Character-mask preparation boundary is missing")
endif()
string(SUBSTRING
    "${_prepare_mask_tail}"
    0
    ${_prepare_mask_length}
    _prepare_mask_body
)
string(REGEX MATCHALL
    "InvalidatePreparedSlot\\("
    _prepare_failure_invalidation_sites
    "${_prepare_mask_body}"
)
list(LENGTH
    _prepare_failure_invalidation_sites
    _prepare_failure_invalidation_site_count
)
if(_prepare_failure_invalidation_site_count LESS 3)
    message(FATAL_ERROR
        "Character-mask validation failures and both exception paths must invalidate the affected slot"
    )
endif()

function(_require_vr_deferred_producer
    _variable_name
    _label
    _opacity_contract
    _minimum_writes
)
    set(_shader_text "${${_variable_name}}")
    string(FIND "${_shader_text}" [[SV_Target7]] _target7_position)
    string(FIND
        "${_shader_text}"
        [[CharacterCategoryMask::Encode(]]
        _category_write_position
    )
    string(FIND
        "${_shader_text}"
        "${_opacity_contract}"
        _opacity_position
    )
    string(REGEX MATCHALL
        "CharacterCategoryMask::Encode\\("
        _category_write_sites
        "${_shader_text}"
    )
    list(LENGTH _category_write_sites _category_write_count)
    if(_target7_position EQUAL -1 OR _category_write_position EQUAL -1 OR
        _opacity_position EQUAL -1 OR
        _category_write_count LESS _minimum_writes)
        message(FATAL_ERROR
            "${_label} must write the VR deferred category tuple to target 7 and preserve output opacity"
        )
    endif()
endfunction()

_require_vr_deferred_producer(
    _lighting_shader
    "Lighting"
    [[characterCategory, psout.Diffuse.w);]]
    1
)
_require_vr_deferred_producer(
    _grass_shader
    "RunGrass"
    [[0, psout.Diffuse.w);]]
    2
)
_require_vr_deferred_producer(
    _effect_shader
    "Effect"
    [[0u, psout.Diffuse.w);]]
    1
)
_require_vr_deferred_producer(
    _distant_tree_shader
    "DistantTree"
    [[0u, psout.Diffuse.w);]]
    1
)
_require_vr_deferred_producer(
    _sky_shader
    "Sky"
    [[0u, psout.Color.w);]]
    1
)

foreach(_classification_contract IN ITEMS
    [[alphaProperty->GetAlphaBlending()]]
    [[alphaProperty->GetAlphaTesting()]]
    [[CharacterClassificationRejection::AlphaTestAndBlend]]
    [[a_property->flags.any(Flag::kFace)]]
    [[RE::BSShaderMaterial::Feature::kFaceGen]]
    [[a_property->flags.any(Flag::kFaceGenRGBTint)]]
    [[RE::BSShaderMaterial::Feature::kFaceGenRGBTint]]
    [[a_actor->GetFaceNodeSkinned()]]
    [[case AncestryResult::Descendant:]]
    [[case AncestryResult::NotDescendant:]]
    [[a_property->flags.any(Flag::kHairTint)]]
    [[RE::BSShaderMaterial::Feature::kHairTint]]
    [[if (actor && actor->IsPlayerRef())]]
    [[} else if (actor) {]]
    [[CharacterCategory::Skin]]
    [[CharacterCategory::Hair]]
)
    string(FIND
        "${_subsurface_source}"
        "${_classification_contract}"
        _classification_position
    )
    if(_classification_position EQUAL -1)
        message(FATAL_ERROR
            "Character material classification contract is missing: ${_classification_contract}"
        )
    endif()
endforeach()

string(FIND "${_character_header}"
    "[[nodiscard]] bool ObserveGeometry(" _observation_acceptance_api)
string(FIND "${_subsurface_source}"
    "void SubsurfaceScattering::BSLightingShader_SetupSkin("
    _setup_skin_begin)
string(FIND "${_subsurface_source}"
    "void SubsurfaceScattering::Hooks::BSLightingShader_SetupGeometry::thunk("
    _setup_skin_end)
if(_observation_acceptance_api EQUAL -1 OR _setup_skin_begin EQUAL -1 OR
    _setup_skin_end EQUAL -1 OR NOT _setup_skin_begin LESS _setup_skin_end)
    message(FATAL_ERROR
        "Character semantic-ID observation acceptance contract is missing"
    )
endif()
math(EXPR _setup_skin_length "${_setup_skin_end} - ${_setup_skin_begin}")
string(SUBSTRING "${_subsurface_source}" ${_setup_skin_begin}
    ${_setup_skin_length} _setup_skin)
string(FIND "${_setup_skin}"
    "entry->second.accepted =" _accepted_observation)
string(FIND "${_setup_skin}"
    "static_cast<uint>(entry->second.category) << 8;" _semantic_descriptor_write)
if(_accepted_observation EQUAL -1 OR _semantic_descriptor_write EQUAL -1 OR
    NOT _accepted_observation LESS _semantic_descriptor_write)
    message(FATAL_ERROR
        "Character category IDs must be emitted only after observation acceptance"
    )
endif()

string(FIND "${_subsurface_source}" "Flag::kSkinned" _generic_skinned_position)
if(NOT _generic_skinned_position EQUAL -1)
    message(FATAL_ERROR "Generic skinned geometry must not be classified as character skin")
endif()

string(FIND "${_character_source}"
    "void CharacterRendering::ObserveClassificationRejection("
    _rejection_observer_begin)
string(FIND "${_character_source}"
    "bool CharacterRendering::CaptureAuthoredCategories("
    _rejection_observer_end)
if(_rejection_observer_begin EQUAL -1 OR _rejection_observer_end EQUAL -1 OR
    NOT _rejection_observer_begin LESS _rejection_observer_end)
    message(FATAL_ERROR "Character rejection observer could not be isolated")
endif()
math(EXPR _rejection_observer_length
    "${_rejection_observer_end} - ${_rejection_observer_begin}"
)
string(SUBSTRING "${_character_source}" ${_rejection_observer_begin}
    ${_rejection_observer_length} _rejection_observer)
string(FIND "${_rejection_observer}"
    "state_->RecordClassificationRejection(a_frame, index);"
    _atomic_rejection_record)
string(FIND "${_rejection_observer}"
    "std::scoped_lock lock(state_->mutex_);"
    _shared_rejection_lock)
if(_atomic_rejection_record EQUAL -1 OR NOT _shared_rejection_lock EQUAL -1)
    message(FATAL_ERROR
        "Rejected actor draws must not serialize on the character resource mutex"
    )
endif()
foreach(_rejection_counter_contract IN ITEMS
    [[std::atomic<std::uint32_t> rejectionFrame_]]
    [[currentClassificationRejections_{};]]
    [[classificationRejections_{};]]
    [[AtomicIncrement(currentClassificationRejections_[a_index]);]]
    [[AtomicIncrement(classificationRejections_[a_index]);]]
    [[state_->PublishClassificationRejections(snapshot);]]
)
    string(FIND "${_character_source}" "${_rejection_counter_contract}"
        _rejection_counter_position)
    if(_rejection_counter_position EQUAL -1)
        message(FATAL_ERROR
            "Character rejection diagnostic contract is missing: ${_rejection_counter_contract}"
        )
    endif()
endforeach()

foreach(_admission_contract IN ITEMS
    [[actor.nearestFaceDistanceUnits]]
    [[actor.nearestSelectedDistanceUnits]]
    [[RefreshProjectedActors(a_args);]]
    [[if (!actorRect.IsValid())]]
    [[actor.selectedRects[a_args.eyeIndex ^ 1u].IsValid()]]
    [[for (const auto& stereoAnchor : stereoAnchors)]]
    [[stereoMaximumSize >= a_args.settings.minimumFacePixelSize]]
    [[std::array<std::map<std::uint32_t, HeldRegion>, 4> heldRegions_]]
    [[CharacterRegionPolicy::IsWithinHoldWindow(]]
    [[if (!currentlyProjected.contains(it->first))]]
    [[CharacterRegionPolicy::MergeAndLimit(]]
)
    string(FIND
        "${_character_source}"
        "${_admission_contract}"
        _admission_position
    )
    if(_admission_position EQUAL -1)
        message(FATAL_ERROR
            "Character stereo admission contract is missing: ${_admission_contract}"
        )
    endif()
endforeach()

string(FIND "${_deferred}"
    "void Deferred::Hooks::Main_RenderWorld_BlendedDecals::thunk("
    _blended_decal_thunk_begin)
string(FIND "${_deferred}"
    "void Deferred::Hooks::BSCubeMapCamera_RenderCubemap::thunk("
    _blended_decal_thunk_end)
if(_blended_decal_thunk_begin EQUAL -1 OR _blended_decal_thunk_end EQUAL -1 OR
    NOT _blended_decal_thunk_begin LESS _blended_decal_thunk_end)
    message(FATAL_ERROR "Deferred blended-decal hook could not be isolated")
endif()
math(EXPR _blended_decal_thunk_length
    "${_blended_decal_thunk_end} - ${_blended_decal_thunk_begin}"
)
string(SUBSTRING "${_deferred}" ${_blended_decal_thunk_begin}
    ${_blended_decal_thunk_length} _blended_decal_thunk)
string(FIND "${_blended_decal_thunk}"
    "terrainBlending.RenderTerrainBlendingPasses();"
    _terrain_blending_replay)
string(FIND "${_blended_decal_thunk}"
    ".CaptureAuthoredCategories("
    _character_category_capture)
string(FIND "${_blended_decal_thunk}"
    "func(This, RenderFlags);"
    _blended_decal_draw)
string(FIND "${_blended_decal_thunk}"
    "deferred->EndDeferred();"
    _end_deferred)
if(_terrain_blending_replay EQUAL -1 OR _character_category_capture EQUAL -1 OR
    _blended_decal_draw EQUAL -1 OR _end_deferred EQUAL -1 OR
    NOT _terrain_blending_replay LESS _character_category_capture OR
    NOT _character_category_capture LESS _blended_decal_draw OR
    NOT _blended_decal_draw LESS _end_deferred)
    message(FATAL_ERROR
        "Character IDs and depth must be frozen after terrain and before blended decals"
    )
endif()
foreach(_deferred_capture_contract IN ITEMS
    [[deferred && deferred->deferredPass]]
    [[upscaling.GetRuntimeFoveatedRegionDimensions(]]
    [[categorySource.texture,]]
    [[depthSource.depthSRV,]]
    [[inputWidthPerEye,]]
    [[inputHeight,]]
)
    string(FIND "${_blended_decal_thunk}" "${_deferred_capture_contract}"
        _deferred_capture_position)
    if(_deferred_capture_position EQUAL -1)
        message(FATAL_ERROR
            "Deferred character capture contract is missing: ${_deferred_capture_contract}"
        )
    endif()
endforeach()
string(REGEX MATCHALL "\\.CaptureAuthoredCategories\\("
    _character_capture_sites "${_blended_decal_thunk}")
list(LENGTH _character_capture_sites _character_capture_site_count)
if(NOT _character_capture_site_count EQUAL 1)
    message(FATAL_ERROR "Deferred character capture must occur exactly once")
endif()
string(REGEX MATCHALL "\\.CaptureAuthoredCategories\\("
    _global_character_capture_sites "${_deferred}")
list(LENGTH _global_character_capture_sites
    _global_character_capture_site_count)
if(NOT _global_character_capture_site_count EQUAL 1)
    message(FATAL_ERROR
        "Deferred character capture must have exactly one global call site"
    )
endif()

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
    [["insertion_point"]]
    [[const bool resetAttempted =]]
    [[(enableStateChanged || insertionPointChanged);]]
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

set(_character_configuration_fields
    characterEnabled
    characterVisualIsolationEnabled
    characterFaces
    characterSkin
    characterHair
    characterFaceStrength
    characterSkinStrength
    characterHairStrength
    characterMaximumDistanceMeters
    characterMinimumFacePixelSize
    characterRoiMargin
    characterMaximumRoiRegions
    characterRoiHoldFrames
    characterDepthAwareFeather
    characterVisibilityDepthTest
    characterFeatherRadius
    characterFeatherDepthThreshold
    characterDebugView
    characterMaskTestMode
)
foreach(_character_field IN LISTS _character_configuration_fields)
    string(REGEX MATCHALL
        "${_character_field}"
        _character_field_sites
        "${_bridge}"
    )
    list(LENGTH _character_field_sites _character_field_site_count)
    if(_character_field_site_count LESS 3)
        message(FATAL_ERROR
            "Character NR DevBench field is not fully wired: ${_character_field}"
        )
    endif()
endforeach()

foreach(_character_contract IN ITEMS
    [[TryValidateActionFields(]]
    [["nr_request_field_unknown"]]
    [["nr_character_number_type_invalid"]]
    [["nr_character_number_non_finite"]]
    [["nr_character_number_out_of_range"]]
    [["nr_character_integer_type_invalid"]]
    [["nr_character_integer_out_of_range"]]
    [["nr_character_debug_view_unknown"]]
    [["nr_character_mask_test_mode_type_invalid"]]
    [["nr_character_mask_test_mode_unknown"]]
    [[const bool requiredAutoMask =]]
    [["nr_automatic_mask_required"]]
    [[NeuralRendering::CharacterRendering::Instance().Reset();]]
    [[{ "characterStateReset", true }]]
    [=["characterDebugView":{"type":"string","enum":["off","character_mask","roi_rectangles","dlss5_output"]}]=]
    [=["characterMaskTestMode":{"type":"string","enum":["authored","force_zero","force_one","force_half","invert_authored","authored_without_visibility_depth"]}]=]
    [[{ "maskTestMode", GetCharacterMaskTestModeName(maskTestMode) }]]
    [[{ "characterVisualIsolationEnabled", settings.neuralCharacterVisualIsolationEnabled }]]
    [[{ "composite", ProfileTimerJson("Upscaling::DLSS5CharacterComposite") }]]
    [[{ "providerDeclared", false }]]
    [[{ "value", "0..1" }]]
    [[const bool runtimeSettingsChanged =]]
    [[const bool characterDebugViewChanged =]]
    [[!runtimeSettingsChanged ||]]
    [[{ "historyResetRequested", runtimeSettingsChanged }]]
    [["character_debug_view"]]
    [[Debug-view-only changes are applied without a history reset.]]
    [[{ "finalVisibilityClassificationGuaranteed", false }]]
	[[{ "requestedOutputCommit", settings.neuralRenderingDirectCommit ? "direct" : "staged" }]]
	[[{ "effectiveUpscaledCenterOutputCommit", mainCenterCommitForcedStaged ? "staged" : (settings.neuralRenderingDirectCommit ? "direct" : "staged") }]]
	[[{ "mainCenterCommitForcedStaged", mainCenterCommitForcedStaged }]]
	[[{ "mainCenterBaseline", "existing_normal_dlss_center" }]]
	[[{ "submitFloatBaseline", "existing_normal_dlss_center" }]]
	[[{ "controlMaskPresent", snapshot.controlMaskPresent }]]
	[[{ "useAutoMask", snapshot.useAutoMask }]]
	[[{ "feature18UseAutoMask", rendererSnapshot.useAutoMask }]]
	[[{ "feature18ControlMaskPresent", rendererSnapshot.controlMaskPresent }]]
	[[{ "feature18InvocationMatchesEvidenceFrame", rendererSnapshot.frameId == observedFrame }]]
	[[{ "controlMaskCopies", snapshot.counters.controlMaskCopies }]]
	[[Character diagnostics count authored face, skin, and hair pixels across the active low-resolution eye input]]
	[[final-LDR and submit routes already preserve a separate baseline.]]
    [[{ "multiSparseSupported", false }]]
    [[{ "privateSingleSubrectCandidate", true }]]
    [[{ "privateSingleSubrectEnabled", false }]]
    [[{ "privateSingleSubrectValidation", "pending_contract_and_timing_validation" }]]
    [[Feature 18 bypasses same-frame CPU-proven empty masks and bounded fresh raw-authored GPU-zero samples]]
    [[const bool characterVisualIsolationChanged =]]
    [[{ "characterVisualIsolationChanged", characterVisualIsolationChanged }]]
)
    string(FIND "${_bridge}" "${_character_contract}" _character_contract_position)
    if(_character_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Character NR DevBench contract is missing: ${_character_contract}"
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
