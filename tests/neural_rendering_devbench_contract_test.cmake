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
    _character_header_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterRendering.h"
)
set(
    _character_source_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp"
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
    "${_character_header_path}"
    "${_character_source_path}"
    "${_character_category_shader_path}"
    "${_character_mask_shader_path}"
    "${_lighting_shader_path}"
    "${_grass_shader_path}"
    "${_effect_shader_path}"
    "${_distant_tree_shader_path}"
    "${_sky_shader_path}"
    "${_deferred_composite_shader_path}"
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
file(READ "${_character_header_path}" _character_header)
file(READ "${_character_source_path}" _character_source)
file(READ "${_character_category_shader_path}" _character_category_shader)
file(READ "${_character_mask_shader_path}" _character_mask_shader)
file(READ "${_lighting_shader_path}" _lighting_shader)
file(READ "${_grass_shader_path}" _grass_shader)
file(READ "${_effect_shader_path}" _effect_shader)
file(READ "${_distant_tree_shader_path}" _distant_tree_shader)
file(READ "${_sky_shader_path}" _sky_shader)
file(READ "${_deferred_composite_shader_path}" _deferred_composite_shader)
file(READ "${_character_doc_path}" _character_doc)
file(READ "${_d3d12_interop_source_path}" _d3d12_interop_source)
file(READ "${_pipeline_policy_path}" _pipeline_policy)
set(
    _source_contract_text
    "${_upscaling}\n${_upscaling_header}\n${_deferred}\n${_subsurface_header}\n${_subsurface_source}\n${_streamline}\n${_streamline_header}\n${_renderer_header}\n${_renderer_source}\n${_character_header}\n${_character_source}\n${_character_category_shader}\n${_character_mask_shader}\n${_lighting_shader}\n${_grass_shader}\n${_effect_shader}\n${_distant_tree_shader}\n${_sky_shader}\n${_deferred_composite_shader}\n${_character_doc}\n${_d3d12_interop_source}\n${_pipeline_policy}"
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
    [[{ "apiVersion", 8 }]]
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
    [[nr_status returns the API-v8 NR runtime]]
    [[{ "neuralRendering", NeuralRenderingStatusJson(a_upscaling) }]]
    [[{ "characterRendering", CharacterRenderingStatusJson(a_upscaling) }]]
    [[{ "visualMasking", {]]
    [[{ "implemented", snapshot.visualMaskImplemented }]]
    [[{ "providerValidated", snapshot.visualMaskProviderValidated }]]
    [[{ "status", "experimental_private_contract" }]]
    [[{ "publicProductSemanticsDescribed", true }]]
    [[{ "exactBindingContractPublished", false }]]
    [[{ "computeRoi", {]]
    [[{ "inferenceRestrictedToRois", false }]]
    [[{ "maskCoveragePercent", eye.maskCoverageReady ? json(eye.maskCoveragePercent) : json(nullptr) }]]
    [[{ "droppedCharacterActors", eye.droppedCharacterRegions }]]
    [[{ "observationCapacityDrops", snapshot.observationCapacityDrops }]]
    [[{ "currentCategoryObservations", {]]
    [[{ "currentClassificationRejections", {]]
    [[{ "classificationRejections", {]]
    [[{ "categoryCaptureAttempts", snapshot.categoryCaptureAttempts }]]
    [[{ "categoryCaptureSuccesses", snapshot.categoryCaptureSuccesses }]]
    [[{ "categoryCaptureFailures", snapshot.categoryCaptureFailures }]]
    [[{ "categoryCaptureEmptyBypasses", snapshot.categoryCaptureEmptyBypasses }]]
    [[{ "categoryCaptureReady", snapshot.categoryCaptureReady }]]
    [[{ "categoryCaptureEmpty", snapshot.categoryCaptureEmpty }]]
    [[{ "projectedFaceActors", eye.visibleFaces }]]
    [[{ "eligibleCharacterActors", eye.visibleCharacterRegions }]]
    [[{ "mergedEligibilityRegions", eye.mergedRegions }]]
    [[{ "maskCoverageSampleIntervalFrames", 30 }]]
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
    [[{ "missingPreparedFeatureSlotMask", missingPreparedFeatureSlots }]]
    [[{ "unexpectedFeatureSlotMask", unexpectedFeatureSlots }]]
    [[{ "correlationScope", featureTimingIsStereoPair ? "stereo_pair" : (featureTimingIsEyeSample ? "eye_sample" : "invalid") }]]
    [[{ "coversPreparedStereoPair", featureTimingIsStereoPair && lastFeatureSlotMask == preparedCharacterSlotMask }]]
    [[{ "matchesPreparedCharacterMask", featureTimingMatchesPreparedMask }]]
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
    [[(!logicalEmptyCapture && !plan.regions.empty());]]
    [[DXGI_FORMAT_R16G16B16A16_UNORM]]
    [[DXGI_FORMAT_R8_UNORM]]
    [[constexpr std::uint32_t kCoverageSampleInterval = 30;]]
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
    [[PreserveCharacterDirectCommitBaselines(directCommit)]]
    [[ResolveCharacterCompositeInputs(]]
    [[const bool directCommit = params.front().neuralDirectCommit;]]
    [[params.back().neuralDirectCommit != directCommit]]
    [[params.neuralDirectCommit = neuralDirectCommit;]]
    [[EvaluateTemporalAdmission(]]
    [[BuildNeuralTemporalAdmission(]]
    [[ObserveNeuralTemporalAdmission(]]
    [[neuralTemporalAdmission.admitted &&]]
    [[timingFenceValue > lastCompletedTimingFenceValue_]]
    [[mergeOverlapsToFixedPoint();]]
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

foreach(_tuple_contract IN ITEMS
    [[float4 Encode(float inverseVertexAo, uint category, float opacity)]]
    [[saturate(inverseVertexAo),]]
    [[EncodeCategory(category),]]
    [[saturate(opacity))]]
    [[const uint2 code = uint2(round(saturate(encodedValue.yz) * 65535.0));]]
    [[const bool exactX = code.x == 0u || code.x == 65535u;]]
    [[const bool exactY = code.y == 0u || code.y == 65535u;]]
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
    [[Texture2D<unorm float4> AuthoredTuple : register(t0);]]
    [[Texture2D<float> AuthoredDepth : register(t1);]]
    [[Texture2D<float> CurrentDepth : register(t2);]]
    [[RWTexture2D<unorm float> ControlMask : register(u0);]]
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

foreach(_capture_contract IN ITEMS
    [[ID3D11ShaderResourceView* a_depthSource,]]
    [[state_->EnsureCategoryCapture(a_device, categoryDesc)]]
    [[state_->EnsureDepthCapture(]]
    [[state_->capturedCategories_.Get(), a_categorySource]]
    [[state_->capturedDepth_.Get(), depthTexture.Get()]]
    [[ComPtr<ID3D11Texture2D> capturedDepth_;]]
    [[ComPtr<ID3D11ShaderResourceView> capturedDepthSrv_;]]
    [[.authoredDepthIdentity = authoredDepthIdentity,]]
    [[.currentDepthIdentity = currentDepthIdentity,]]
    [[state_->capturedDepthSrv_.Get(),]]
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

string(FIND
    "${_deferred}"
    [[DXGI_FORMAT_R16G16B16A16_UNORM]]
    _vr_tuple_format_position
)
string(FIND
    "${_character_source}"
    [[textureDesc.Format = DXGI_FORMAT_R8_UNORM;]]
    _provider_mask_format_position
)
if(_vr_tuple_format_position EQUAL -1 OR _provider_mask_format_position EQUAL -1)
    message(FATAL_ERROR
        "VR category provenance must use RGBA16 UNORM and the provider mask R8 UNORM"
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

string(FIND "${_subsurface_source}" "Flag::kSkinned" _generic_skinned_position)
if(NOT _generic_skinned_position EQUAL -1)
    message(FATAL_ERROR "Generic skinned geometry must not be classified as character skin")
endif()

foreach(_admission_contract IN ITEMS
    [[actor.nearestFaceDistanceUnits]]
    [[actor.faces.empty()]]
    [[for (const auto& sizeRect : faceRects)]]
    [[stereoMaximumSize < a_args.settings.minimumFacePixelSize]]
    [[std::array<std::map<std::uint32_t, HeldRegion>, 4> heldRegions_]]
    [[MergeRegions(result.regions, a_args.settings.maximumRoiRegions);]]
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

string(FIND
    "${_deferred}"
    ".CaptureAuthoredCategories("
    _character_category_capture
)
string(FIND
    "${_deferred}"
    "terrainBlending.RenderTerrainBlendingPasses();"
    _terrain_blending_replay
)
string(FIND
    "${_deferred}"
    "func(This, RenderFlags);"
    _blended_decal_draw
)
if(_character_category_capture EQUAL -1 OR _terrain_blending_replay EQUAL -1 OR
    _blended_decal_draw EQUAL -1 OR
    NOT _character_category_capture LESS _terrain_blending_replay OR
    NOT _terrain_blending_replay LESS _blended_decal_draw)
    message(FATAL_ERROR
        "Character categories must be frozen before terrain replay and decals"
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
    characterFeatherRadius
    characterFeatherDepthThreshold
    characterRoiMode
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
    [["nr_character_roi_mode_unknown"]]
    [["nr_character_debug_view_unknown"]]
    [["nr_character_mask_test_mode_type_invalid"]]
    [["nr_character_mask_test_mode_unknown"]]
    [[const bool requiredAutoMask =]]
    [["nr_control_mask_mode_conflict"]]
    [[NeuralRendering::CharacterRendering::Instance().Reset();]]
    [[{ "characterStateReset", true }]]
    [=["characterRoiMode":{"type":"string","enum":["auto","disabled"]}]=]
    [=["characterDebugView":{"type":"string","enum":["off","character_mask","roi_rectangles","dlss5_output"]}]=]
    [=["characterMaskTestMode":{"type":"string","enum":["authored","force_zero","force_one","force_half","invert_authored"]}]=]
    [[{ "maskTestMode", GetCharacterMaskTestModeName(maskTestMode) }]]
    [[{ "characterVisualIsolationEnabled", settings.neuralCharacterVisualIsolationEnabled }]]
    [[{ "composite", ProfileTimerJson("Upscaling::DLSS5CharacterComposite") }]]
    [[{ "baselineCopy", ProfileTimerJson("Upscaling::DLSS5CharacterBaselineCopy") }]]
    [[{ "providerDeclared", false }]]
    [[{ "value", "0..1" }]]
    [[const bool runtimeSettingsChanged =]]
    [[const bool characterDebugViewChanged =]]
    [[!runtimeSettingsChanged ||]]
    [[{ "historyResetRequested", runtimeSettingsChanged }]]
    [["character_debug_view"]]
    [[Debug-view-only changes are applied without a history reset.]]
    [[{ "finalVisibilityClassificationGuaranteed", false }]]
    [[{ "upscaledCenterCommitPolicy", "configured_commit_lane_preserved" }]]
	[[{ "directCommitCompositeBaseline", "one_normal_dlss_center_copy" }]]
	[[{ "controlMaskPresent", snapshot.controlMaskPresent }]]
	[[{ "controlMaskCopies", snapshot.counters.controlMaskCopies }]]
	[[Character profiling reports baseline-copy, mask-generation, Feature 18 evaluation, and composite costs separately so Direct plus isolation overhead remains attributable.]]
    [[{ "multiSparseSupported", false }]]
    [[{ "privateSingleSubrectCandidate", true }]]
    [[{ "privateSingleSubrectEnabled", false }]]
    [[{ "privateSingleSubrectValidation", "pending_contract_and_timing_validation" }]]
    [[Multi/sparse compute ROI is unsupported. One private single-subrect candidate exists but remains disabled pending contract validation and timing; nonempty character masks use the normal center evaluation extent, while an empty Authored stereo pair bypasses Feature 18 and its category copy.]]
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
