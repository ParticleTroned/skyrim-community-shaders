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
set(_hooks_path "${PROJECT_ROOT}/src/Hooks.cpp")
set(_character_authoring_path "${PROJECT_ROOT}/src/Utils/CharacterCategoryAuthoring.cpp")
set(_character_format_path "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterCategoryFormat.h")
set(_character_settings_path "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterSettings.h")
set(_character_settings_json_path "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterSettingsJson.h")
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
    _character_compute_subrect_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterComputeSubrect.h"
)
set(
    _character_actor_policy_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterActorPolicy.h"
)
set(
    _character_mask_work_policy_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterMaskWorkPolicy.h"
)
set(
    _character_category_shader_path
    "${PROJECT_ROOT}/package/Shaders/Common/CharacterCategoryMask.hlsli"
)
set(
    _character_mask_shader_path
    "${PROJECT_ROOT}/package/Shaders/DLSS5CharacterMaskCS.hlsl"
)
set(
    _character_capture_shader_path
    "${PROJECT_ROOT}/package/Shaders/DLSS5CharacterCaptureCS.hlsl"
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
    _copy_depth_guide_shader_path
    "${PROJECT_ROOT}/features/Upscaling/Shaders/Upscaling/NeuralRendering/CopyDepthGuideCS.hlsl"
)
set(
    _submit_stage_stretch_shader_path
    "${PROJECT_ROOT}/features/Upscaling/Shaders/Upscaling/SubmitStageStretchCS.hlsl"
)
set(
    _compute_subrect_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/ComputeSubrect.h"
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
set(
    _vr_render_scale_mode_policy_path
    "${PROJECT_ROOT}/src/Features/Upscaling/VRRenderScaleModePolicy.h"
)
foreach(_required_path IN ITEMS
    "${_bridge_path}"
    "${_experiment_doc_path}"
    "${_upscaling_path}"
    "${_upscaling_header_path}"
    "${_deferred_path}"
    "${_hooks_path}"
    "${_character_authoring_path}"
    "${_character_format_path}"
    "${_character_settings_path}"
    "${_character_settings_json_path}"
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
    "${_character_compute_subrect_path}"
    "${_character_actor_policy_path}"
    "${_character_mask_work_policy_path}"
    "${_character_category_shader_path}"
    "${_character_mask_shader_path}"
    "${_character_capture_shader_path}"
    "${_lighting_shader_path}"
    "${_grass_shader_path}"
    "${_effect_shader_path}"
    "${_distant_tree_shader_path}"
    "${_sky_shader_path}"
    "${_deferred_composite_shader_path}"
    "${_foveated_center_blend_shader_path}"
    "${_copy_depth_guide_shader_path}"
    "${_submit_stage_stretch_shader_path}"
    "${_compute_subrect_path}"
    "${_character_doc_path}"
    "${_d3d12_interop_source_path}"
    "${_pipeline_policy_path}"
    "${_vr_render_scale_mode_policy_path}"
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
file(READ "${_hooks_path}" _hooks)
file(READ "${_character_authoring_path}" _character_authoring)
file(READ "${_character_format_path}" _character_format)
file(READ "${_character_settings_path}" _character_settings)
file(READ "${_character_settings_json_path}" _character_settings_json)
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
file(READ "${_character_compute_subrect_path}" _character_compute_subrect)
file(READ "${_character_actor_policy_path}" _character_actor_policy)
file(READ "${_character_mask_work_policy_path}" _character_mask_work_policy)
file(READ "${_character_category_shader_path}" _character_category_shader)
file(READ "${_character_mask_shader_path}" _character_mask_shader)
file(READ "${_character_capture_shader_path}" _character_capture_shader)
file(READ "${_lighting_shader_path}" _lighting_shader)
file(READ "${_grass_shader_path}" _grass_shader)
file(READ "${_effect_shader_path}" _effect_shader)
file(READ "${_distant_tree_shader_path}" _distant_tree_shader)
file(READ "${_sky_shader_path}" _sky_shader)
file(READ "${_deferred_composite_shader_path}" _deferred_composite_shader)
file(READ "${_foveated_center_blend_shader_path}" _foveated_center_blend_shader)
file(READ "${_copy_depth_guide_shader_path}" _copy_depth_guide_shader)
file(READ "${_submit_stage_stretch_shader_path}" _submit_stage_stretch_shader)
file(READ "${_compute_subrect_path}" _compute_subrect)
file(READ "${_character_doc_path}" _character_doc)
file(READ "${_d3d12_interop_source_path}" _d3d12_interop_source)
file(READ "${_pipeline_policy_path}" _pipeline_policy)
file(READ "${_vr_render_scale_mode_policy_path}" _vr_render_scale_mode_policy)
set(
    _source_contract_text
    "${_upscaling}\n${_upscaling_header}\n${_deferred}\n${_subsurface_header}\n${_subsurface_source}\n${_streamline}\n${_streamline_header}\n${_renderer_header}\n${_renderer_source}\n${_runtime_header}\n${_runtime_source}\n${_compute_subrect}\n${_character_header}\n${_character_source}\n${_character_region_policy}\n${_character_compute_subrect}\n${_character_actor_policy}\n${_character_mask_work_policy}\n${_character_category_shader}\n${_character_mask_shader}\n${_character_capture_shader}\n${_lighting_shader}\n${_grass_shader}\n${_effect_shader}\n${_distant_tree_shader}\n${_sky_shader}\n${_deferred_composite_shader}\n${_foveated_center_blend_shader}\n${_copy_depth_guide_shader}\n${_submit_stage_stretch_shader}\n${_character_doc}\n${_d3d12_interop_source}\n${_pipeline_policy}\n${_vr_render_scale_mode_policy}"
)

string(APPEND _source_contract_text
    "\n${_hooks}\n${_character_authoring}\n${_character_format}\n${_character_settings}\n${_character_settings_json}")

foreach(_feature_mode_contract IN ITEMS
    [[ResolveFeatureUpscaling(]]
    [[args.featureUpscaling = *featureUpscaling;]]
    [[.featureUpscaling = a_args.featureUpscaling,]]
    [[Feature 18 upscaling mode does not match its guide-to-output geometry]]
    [[neuralFeatureUpscalingModeChanged]]
    [[NeuralRendering::CharacterRendering::Instance().Invalidate();]]
)
    string(FIND
        "${_source_contract_text}"
        "${_feature_mode_contract}"
        _feature_mode_contract_position
    )
    if(_feature_mode_contract_position EQUAL -1)
        message(FATAL_ERROR
            "DLSS/DLAA Neural Rendering mode contract is missing: ${_feature_mode_contract}"
        )
    endif()
endforeach()

string(REGEX MATCHALL
    "args\\.featureUpscaling = \\*featureUpscaling;"
    _feature_mode_builder_sites
    "${_upscaling}"
)
list(LENGTH _feature_mode_builder_sites _feature_mode_builder_site_count)
if(_feature_mode_builder_site_count LESS 2)
    message(FATAL_ERROR
        "Both Neural Rendering insertion paths must derive Feature 18 upscaling mode from geometry"
    )
endif()

string(FIND
    "${_upscaling}"
    [[args.featureUpscaling = true;]]
    _hard_coded_feature_upscaling_position
)
if(NOT _hard_coded_feature_upscaling_position EQUAL -1)
    message(FATAL_ERROR
        "Neural Rendering must not hard-code Feature 18 upscaling mode"
    )
endif()

foreach(_render_scale_preference_contract IN ITEMS
    [[renderScaleModePreference]]
    [[GetVRRenderScaleModePreference()]]
    [[VRRenderScaleModePolicy::Resolve(]]
    [[currentDesiredProfile.renderScaleModePreference != targetRenderScalePreference]]
)
    string(FIND
        "${_source_contract_text}"
        "${_render_scale_preference_contract}"
        _render_scale_preference_contract_position
    )
    if(_render_scale_preference_contract_position EQUAL -1)
        message(FATAL_ERROR
            "VR Render Scale preference contract is missing: ${_render_scale_preference_contract}"
        )
    endif()
endforeach()

foreach(_selection_composite_contract IN ITEMS
    [[CharacterMask.Load(sourcePos)]]
    [[BaselineCenterColor.Load(sourcePos)]]
    [[CenterColor.Load(sourcePos)]]
    [[centerColor = baselineColor;]]
    [[if (characterWeight > 0.0)]]
    [[centerColor = lerp(baselineColor, neuralColor, characterWeight);]]
    [[float4 CharacterMaskBounds;]]
    [[all(centerUV >= CharacterMaskBounds.xy)]]
    [[all(centerUV < CharacterMaskBounds.zw)]]
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

foreach(_selection_support_contract IN ITEMS
    [[float4 characterMaskBounds;]]
    [[static_assert(sizeof(FoveatedCenterBlendCB) == 96]]
    [[GetMaskSupportRect(characterMaskSRV, rect.outputWidth, rect.outputHeight)]]
    [[support.baseX * cbData.invSourceDim.x]]
    [[(support.baseX + support.width) * cbData.invSourceDim.x]]
    [[ExpandCharacterWorkRect(slot.maskWorkSubrect, a_width, a_height, 1)]]
    [[slot.uniformMaskValue == 0.0f ? ComputeSubrect{} : full]]
)
    string(FIND "${_source_contract_text}" "${_selection_support_contract}"
        _selection_support_position)
    if(_selection_support_position EQUAL -1)
        message(FATAL_ERROR
            "Character composite support-bounds contract is missing: ${_selection_support_contract}"
        )
    endif()
endforeach()

foreach(_mask_roi_dispatch_contract IN ITEMS
    [[static const uint EligibilityRegionCapacity = 16u;]]
    [[float4 EligibilityRectangles[EligibilityRegionCapacity];]]
    [[const float4 region = EligibilityRectangles[index];]]
    [[row_major float4x4 CameraProjInverse;]]
    [[const float4 viewPosition = mul(CameraProjInverse, clipPosition);]]
    [[length(viewPosition.xyz / viewPosition.w)]]
    [[uint4 DispatchRegion;]]
    [[const uint2 outputPixelId = dispatchThreadId.xy + DispatchRegion.xy;]]
    [[CharacterSelectionMask[outputPixelId] = mask;]]
    [[ClearUnorderedAccessViewFloat(]]
    [[a_slot.maskUav.Get(), clearMask.data())]]
    [[UnionCharacterWorkRects(]]
    [[a_slot.previousMaskWorkSubrect, a_slot.maskWorkSubrect)]]
    [[fullSurfaceDispatch ? 0u : dirtyRegion.baseX]]
    [[fullSurfaceDispatch ?]]
    [[dirtyRegion.width]]
    [[if (!a_slot.maskInitialized)]]
    [[a_slot.previousMaskWorkSubrect = a_slot.maskWorkSubrect;]]
)
    string(FIND "${_source_contract_text}" "${_mask_roi_dispatch_contract}"
        _mask_roi_dispatch_position)
    if(_mask_roi_dispatch_position EQUAL -1)
        message(FATAL_ERROR
            "Character-mask ROI dispatch contract is missing: ${_mask_roi_dispatch_contract}"
        )
    endif()
endforeach()

foreach(_roi_staging_contract IN ITEMS
    [[float2 OutputOffset;]]
    [[OutputTexture[outputPixelId] =]]
    [[region.baseX, region.baseY, 0,]]
    [[CopyNeuralOutputRegions(]]
    [[CommitSubmitNeuralFloatOutput(]]
)
    string(FIND "${_source_contract_text}" "${_roi_staging_contract}"
        _roi_staging_position)
    if(_roi_staging_position EQUAL -1)
        message(FATAL_ERROR
            "ROI-limited staging contract is missing: ${_roi_staging_contract}"
        )
    endif()
endforeach()

foreach(_dynamic_compute_roi_contract IN ITEMS
    [[BuildCharacterComputeSubrect(]]
    [[ResolveStableCharacterComputeSubrect(]]
    [[slot.computeSubrectGeneration != a_args.generation]]
    [[slot.computeSubrectCrop != a_args.viewportCrop]]
    [[slot.stableComputeSubrect = {};]]
    [[bounds = CharacterRegionPolicy::Union(bounds, region);]]
    [[a_args.computeSubrect = result.computeSubrect;]]
    [[MapComputeSubrect(]]
    [[parameters->Set("DLSSNR.ColorSubrectBaseX", colorSubrect.baseX);]]
    [[parameters->Set("DLSSNR.OutputSubrectWidth", outputSubrect.width);]]
    [[CopyTextureSubrect(]]
    [[Add(pixelCount, resources[index].outputSubrect.Area());]]
    [[RestrictVisibleOutputToComputeSubrect(]]
    [[const uint32_t evaluationEyeMask =]]
)
    string(FIND
        "${_source_contract_text}"
        "${_dynamic_compute_roi_contract}"
        _dynamic_compute_roi_position
    )
    if(_dynamic_compute_roi_position EQUAL -1)
        message(FATAL_ERROR
            "Dynamic character compute ROI contract is missing: ${_dynamic_compute_roi_contract}"
        )
    endif()
endforeach()

foreach(_stable_compute_roi_contract IN ITEMS
    [[kCharacterProviderRoiMinimumHeadroomPixels = 32]]
    [[kCharacterProviderRoiMaximumHeadroomPixels = 96]]
    [[kCharacterProviderRoiHistoryFrames = 60]]
    [[kCharacterProviderRoiShrinkDelayFrames = 30]]
    [[std::array<ComputeSubrect, kCharacterProviderRoiHistoryFrames> recentRequired{};]]
    [[if (!ContainsComputeSubrect(a_state.provider, a_required))]]
    [[a_state.provider = UnionCharacterComputeSubrect(]]
    [[recentBounds, a_state.recentRequired[index])]]
    [[recentCandidate.Area() <= maximumContractedArea]]
)
    string(FIND "${_character_compute_subrect}" "${_stable_compute_roi_contract}"
        _stable_compute_roi_position)
    if(_stable_compute_roi_position EQUAL -1)
        message(FATAL_ERROR
            "Bounded stable character compute ROI contract is missing: ${_stable_compute_roi_contract}"
        )
    endif()
endforeach()

foreach(_mask_work_rect_contract IN ITEMS
    [[UnionCharacterWorkRects(]]
    [[if (!a_left.IsValid())]]
    [[if (!a_right.IsValid())]]
    [[static_cast<std::uint64_t>(a_left.baseX) + a_left.width]]
    [[if (right > UINT32_MAX || bottom > UINT32_MAX)]]
    [[ExpandCharacterWorkRect(]]
    [[if (!a_rect.Fits(a_width, a_height))]]
)
    string(FIND "${_character_mask_work_policy}" "${_mask_work_rect_contract}"
        _mask_work_rect_position)
    if(_mask_work_rect_position EQUAL -1)
        message(FATAL_ERROR
            "Character mask dirty/support rectangle safety contract is missing: ${_mask_work_rect_contract}"
        )
    endif()
endforeach()

foreach(_roi_history_identity_contract IN ITEMS
    [[ComputeSubrect computeSubrect{};]]
    [[.computeSubrect = a_resources.outputSubrect,]]
    [[.insertionPoint = a_args.insertionPoint,]]
    [[.colorInputEpoch = colorConfiguration_.inputEpoch[static_cast<std::size_t>(a_args.insertionPoint)],]]
    [[slot.historyKey != resources[index].historyKey]]
)
    string(FIND
        "${_renderer_source}"
        "${_roi_history_identity_contract}"
        _roi_history_identity_position
    )
    if(_roi_history_identity_position EQUAL -1)
        message(FATAL_ERROR
            "Full Feature 18 ROI history identity is missing: ${_roi_history_identity_contract}"
        )
    endif()
endforeach()

foreach(_depth_compute_roi_contract IN ITEMS
    [[uint2 RoiOffset;]]
    [[uint2 RoiExtent;]]
    [[uint2 pixel = dispatchThreadId.xy + RoiOffset;]]
    [[OutputDepth[pixel] = SourceDepth.Load(uint3(pixel, 0));]]
)
    string(FIND
        "${_copy_depth_guide_shader}"
        "${_depth_compute_roi_contract}"
        _depth_compute_roi_position
    )
    if(_depth_compute_roi_position EQUAL -1)
        message(FATAL_ERROR
            "Depth-guide compute ROI contract is missing: ${_depth_compute_roi_contract}"
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
    [[bool CopyNeuralOutputRegions(]]
    [[bool IsDefaultFoveatedMaskGeometry(]]
    _region_output_copy_section
)
foreach(_region_output_contract IN ITEMS
    [[a_regions.count > 2u]]
    [[!a_enclosure.Fits(a_width, a_height)]]
    [[std::span<const NeuralRendering::ComputeSubrect>(a_regions.regions.data(), a_regions.count)]]
    [[std::span<const NeuralRendering::ComputeSubrect>(&a_enclosure, 1)]]
    [[!region.Fits(a_width, a_height)]]
    [[!NeuralRendering::ContainsComputeSubrect(a_enclosure, region)]]
    [[a_context->CopySubresourceRegion( a_destination, 0, region.baseX, region.baseY, 0, a_source, 0, &sourceBox)]]
)
    string(FIND "${_region_output_copy_section}" "${_region_output_contract}" _region_output_position)
    if(_region_output_position EQUAL -1)
        message(FATAL_ERROR "Produced-region staging contract is missing: ${_region_output_contract}")
    endif()
endforeach()
string(FIND "${_region_output_copy_section}" [[!NeuralRendering::ContainsComputeSubrect(]] _region_validation_position)
string(FIND "${_region_output_copy_section}" [[a_context->CopySubresourceRegion(]] _region_copy_position)
if(_region_copy_position LESS_EQUAL _region_validation_position)
    message(FATAL_ERROR "All staged regions must be validated before any copy is issued")
endif()
string(REGEX MATCHALL [[for \(const auto& region : regions\)]] _region_copy_loops "${_region_output_copy_section}")
list(LENGTH _region_copy_loops _region_copy_loop_count)
if(NOT _region_copy_loop_count EQUAL 2)
    message(FATAL_ERROR "Staging must validate every produced region separately before the copy loop")
endif()

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
    [[FidelityFX::UpscaleResult Upscaling::DispatchSingleFoveatedVendorEye(]]
    [[bool Upscaling::ApplyFinalLdrNeuralStereo(]]
    _upscaled_center_section
)
_extract_upscaling_section(
    [[bool Upscaling::ApplyFinalLdrNeuralStereo(]]
    [[void Upscaling::ApplyMainFinalLdrNeuralStereo()]]
    _final_ldr_section
)
_extract_upscaling_section(
    [[FidelityFX::UpscaleResult Upscaling::DispatchFoveatedVendorEyeComposite(]]
    [[FidelityFX::UpscaleResult Upscaling::DispatchFoveatedVendorUpscaling(]]
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
    [[return CopyNeuralOutputRegions( globals::d3d::context, submitNeuralFloatColorOut[eyeIndex]->resource.get(), submitNeuralFloatStagedOut[eyeIndex]->resource.get(), submitNeuralFloatColorOut[eyeIndex]->desc.Width, submitNeuralFloatColorOut[eyeIndex]->desc.Height, computeSubrect, computeRegions)]]
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
    [[const bool useSubmitNeuralFloatBridge = NeuralRendering::UsesSubmitNeuralFloatBridge( submitStageDLSSCenter, neuralRenderingRequested, GetNeuralRenderingArrangement())]]
    [[args.insertionPoint = NeuralRendering::InsertionPoint::UpscaledCenter]]
    [[DispatchSubmitStageColorRegion( foveatedCenterColorOut[eyeIndex]->srv.get(), submitNeuralFloatColorIn[eyeIndex]->uav.get()]]
    [[args.colorInput = submitNeuralFloatColorIn[eyeIndex]->resource.get()]]
    [[return CommitSubmitNeuralFloatOutput( eyeIndex, directNeuralCommit, preparedSubrect, preparedRegions)]]
    [[return CopyNeuralOutputRegions(]]
    [[rect.outputWidth, rect.outputHeight, preparedSubrect, preparedRegions)]]
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
    [[const bool useSubmitNeuralFloatOutput = NeuralRendering::UsesSubmitNeuralFloatBridge( params.dlssViewportRole == Streamline::DLSSViewportRole::SubmitStageFoveatedCenter, neuralResult && neuralResult->applied, GetNeuralRenderingArrangement())]]
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
    [[args.insertionPoint = NeuralRendering::InsertionPoint::FinalLdrPreUi]]
    [[("NeuralRendering::FinalLdrColorIn" + suffix).c_str(), targetUavDescs[eye].Format]]
    [[PrepareSubmitNeuralFloatResources( eye, target.resource, rect.outputWidth, rect.outputHeight, directCommit, false)]]
    [[DispatchSubmitStageColorRegion( neuralFinalLdrColorIn[eye]->srv.get(), submitNeuralFloatColorIn[eye]->uav.get()]]
    [[args.colorInput = submitNeuralFloatColorIn[eye]->resource.get()]]
    [[args.colorOutput = GetSubmitNeuralFloatEvaluationOutput(eye, directCommit)]]
    [[CommitSubmitNeuralFloatOutput( eye, directCommit, neuralArgs[eye].computeSubrect, neuralArgs[eye].computeRegions)]]
    [[DispatchFoveatedBlendPass( submitNeuralFloatColorOut[eye]->srv.get()]]
    [[uint32_t finalLdrColorMode = 1u]]
    [[switch (targetUavDescs[0].Format)]]
    [[case DXGI_FORMAT_R8G8B8A8_UNORM:]]
    [[finalLdrColorMode = 2u]]
    [[characterMaskOwners[eye].Get(), finalLdrColorMode, sharedFullResolutionFovMask && !FoveatedCommon::IsActiveCoverage(foveatedRectCache.centerScale))]]
)
    string(FIND
        "${_final_ldr_section}"
        "${_final_ldr_float_contract}"
        _final_ldr_float_position
    )
    if(_final_ldr_float_position EQUAL -1)
        message(FATAL_ERROR
            "Shared Main/Submit Final-LDR float contract is missing: ${_final_ldr_float_contract}"
        )
    endif()
endforeach()
foreach(_obsolete_final_ldr_contract IN ITEMS
    [[useSubmitNeuralFloatBridge]]
    [[neuralFinalLdrColorOut[]]
    [[neuralFinalLdrStagedOut[]]
)
    string(FIND "${_final_ldr_section}" "${_obsolete_final_ldr_contract}" _obsolete_final_ldr_position)
    if(NOT _obsolete_final_ldr_position EQUAL -1)
        message(FATAL_ERROR
            "Final-LDR must not retain a role-dependent presentation-format NR path: ${_obsolete_final_ldr_contract}"
        )
    endif()
endforeach()
foreach(_final_ldr_color_contract IN ITEMS
    [[uint32_t finalLdrColorMode = 0]]
    [[cbData.finalLdrColorMode = finalLdrColorMode]]
    [[offsetof(FoveatedCenterBlendCB, finalLdrColorMode) == 68]]
)
    string(FIND "${_source_contract_text}" "${_final_ldr_color_contract}" _final_ldr_color_position)
    if(_final_ldr_color_position EQUAL -1)
        message(FATAL_ERROR "Final-LDR blend-mode ABI/default is missing: ${_final_ldr_color_contract}")
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
    "constexpr const char* kNeuralRenderingDescriptor ="
    _descriptor_declaration
)
if(_descriptor_declaration EQUAL -1)
    message(FATAL_ERROR "DevBench descriptor declaration is missing")
endif()
string(SUBSTRING "${_bridge}" ${_descriptor_declaration} -1 _descriptor_tail)
string(FIND "${_descriptor_tail}" [[R"nr(]] _descriptor_raw_start)
if(_descriptor_raw_start EQUAL -1)
    message(FATAL_ERROR "DevBench descriptor raw string is missing")
endif()
math(EXPR _descriptor_json_start "${_descriptor_raw_start} + 5")
string(SUBSTRING
    "${_descriptor_tail}"
    ${_descriptor_json_start}
    -1
    _descriptor_json_tail
)
string(FIND "${_descriptor_json_tail}" [[)nr";]] _descriptor_json_length)
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
foreach(_fov_field IN ITEMS required available reason)
    string(JSON _fov_type GET "${_descriptor_json}"
        outputSchema properties neuralRendering properties fovPrerequisite properties ${_fov_field} type)
    if(_fov_field STREQUAL "reason")
        set(_expected_fov_type "string")
    else()
        set(_expected_fov_type "boolean")
    endif()
    if(NOT _fov_type STREQUAL _expected_fov_type)
        message(FATAL_ERROR "FOV prerequisite status must have a typed schema: ${_fov_field}")
    endif()
endforeach()
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
if(NOT _descriptor_configure_field_count EQUAL 13)
    message(FATAL_ERROR "foveation_configure schema must require at least one of 13 controls")
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
    [[{ "apiVersion", 10 }]]
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
    [[{ "sourceWorldFrame", eye.sourceWorldFrame }]]
    [[{ "sourceWorldFrame", snapshot.sourceWorldFrame }]]
    [[{ "knownMenuContext", temporalAdmission.menuContextActive }]]
    [[{ "hardMenuBlocked", route.hardMenuBlocked }]]
    [[{ "lateMenuCompositeReady", route.lateMenuCompositeReady }]]
    [[{ "csOverlayOpen", route.csOverlayOpen }]]
    [[{ "menuContinuityAllowed", route.menuContinuityAllowed }]]
    [[{ "gamePaused", temporalAdmission.gamePaused }]]
    [[{ "pausedContinuityAllowed", temporalAdmission.pausedContinuityAllowed }]]
    [[{ "pausedSubmitContinuityAllowed", temporalAdmission.pausedContinuityAllowed }]]
    [[{ "worldFrameStateAvailable", temporalAdmission.worldFrameStateAvailable }]]
    [[{ "worldFrameStarted", temporalAdmission.worldFrameStarted }]]
    [[{ "worldFrameCompleted", temporalAdmission.worldFrameCompleted }]]
    [[{ "retainedWorldFrame", temporalAdmission.retainedWorldFrame }]]
    [[{ "temporalSourceFresh", temporalAdmission.temporalSourceFresh }]]
    [[{ "temporalAdmission", {]]
    [[{ "admitted", temporalAdmission.admitted }]]
    [[{ "sourceWorldFrame", temporalAdmission.sourceWorldFrame }]]
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
    [[{ "dynamicCharacterSingleRectEnabled", dynamicCharacterSingleRectEnabled }]]
    [[{ "dynamicCharacterRoiEnabled", dynamicCharacterRoiEnabled }]]
    [[{ "dynamicCharacterMultiRegionEnabled", dynamicCharacterMultiRegionEnabled }]]
    [[{ "inferenceRestrictedToRois", dynamicCharacterRoiEnabled || privateSingleSubrectEnabled }]]
    [[{ "computeSubrect", {]]
    [[{ "computeSubrectPixels", eye.computeSubrectPixels }]]
    [[{ "computeSubrectCoveragePercent", eye.computeSubrectCoveragePercent }]]
    [[{ "maskCoveragePercent", eye.maskCoverageReady ? json(eye.maskCoveragePercent) : json(nullptr) }]]
    [[{ "authoredCategoryPixels", {]]
    [[{ "visibleCategoryPixels", {]]
    [[{ "visibilityRejectedPixels", eye.maskCoverageReady ? json(eye.visibilityRejectedPixels) : json(nullptr) }]]
    [[{ "distanceRejectedPixels", eye.maskCoverageReady ? json(eye.distanceRejectedPixels) : json(nullptr) }]]
    [[{ "providerRoiListEvidenceScope", "observed_feature18_parameter_abi" }]]
    [[{ "depthCoordinates", {]]
    [[{ "zeroCoverageBypassRequested", eye.zeroCoverageBypassRequested }]]
    [[{ "zeroCoverageBypassResolved", eye.zeroCoverageBypassResolved }]]
    [[{ "zeroCoverageBypassedFeature18", eye.zeroCoverageBypassed }]]
    [[{ "feature18EvaluationSucceeded", eye.feature18EvaluationSucceeded }]]
    [[{ "zeroCoverageCpuProven", eye.zeroCoverageCpuProven }]]
    [[{ "feature18Disposition", NeuralRendering::GetCharacterFeature18DispositionName(eye.feature18Disposition) }]]
    [[{ "maskCoverageSampleAgeFrames",]]
    [[{ "maskCoverageMatchesCurrentPolicy", eye.maskCoverageMatchesCurrentPolicy }]]
    [[{ "authoredCategoryPixelCountSpace", "active_eye_input_pixels_before_visibility" }]]
    [[{ "visibleCategoryPixelCountSpace", "feature18_evaluation_pixels_after_visibility_and_distance_inside_eligibility" }]]
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
    [[{ "provenEmptyFeatureBypasses", snapshot.provenEmptyFeatureBypasses }]]
    [[{ "currentBypassRequestedSlotMask", currentPreparationFound ? currentPreparation->bypassRequestedSlotMask : 0u }]]
    [[{ "currentResolutionRecordedSlotMask", currentPreparationFound ? currentPreparation->resolutionRecordedSlotMask : 0u }]]
    [[{ "currentEvaluatedSlotMask", currentPreparationFound ? currentPreparation->evaluatedSlotMask : 0u }]]
    [[{ "currentSuccessfulSlotMask", currentPreparationFound ? currentPreparation->successfulSlotMask : 0u }]]
    [[{ "currentBypassedSlotMask", currentPreparationFound ? currentPreparation->bypassedSlotMask : 0u }]]
    [[{ "currentAbortedSlotMask", currentPreparationFound ? currentPreparation->abortedSlotMask : 0u }]]
    [[{ "eligibleFaceActors", eye.visibleFaces }]]
    [[{ "eligibleCharacterActors", eye.visibleCharacterRegions }]]
    [[{ "selectedCharacterActors", eye.selectedCharacterRegions }]]
    [[{ "adaptivelyCulledCharacterActors", eye.adaptivelyCulledCharacterRegions }]]
    [[{ "mergedEligibilityRegions", eye.mergedRegions }]]
    [[{ "fullEyeEligibilityFallback", eye.fullEyeEligibilityFallback }]]
    [[{ "uncertainActors", eye.projectionUncertainActors }]]
    [[{ "clippedGeometry", eye.projectionClippedGeometry }]]
    [[NeuralRendering::CharacterProjectionReasonName(eye.projectionFallbackReason)]]
    [[{ "authoredMaskCoverageSampleIntervalFrames", NeuralRendering::CharacterPolicy::kCoverageSampleIntervalFrames }]]
    [[{ "forcedMaskCoverageSampleIntervalFrames", NeuralRendering::CharacterPolicy::kCoverageSampleIntervalFrames }]]
    [[{ "configured", visualIsolationConfigured }]]
    [[{ "active", visualIsolationEffective }]]
    [[{ "vrAttachmentFormat", "R16G16_UNORM" }]]
    [[{ "vrBytesPerPixel", 4 }]]
    [[{ "legacyBytesPerPixel", 2 }]]
    [[{ "additionalVrBytesPerPixel", 2 }]]
    [[{ "inverseVertexAoBits", 16 }]]
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
    [[{ "logicalEyeCount", lastFeatureLogicalEyeCount }]]
    [[lastFeatureLogicalEyeCount == 2u]]
    [[lastFeatureSlotMask == sampledScopeExpectedPhysicalSlotMask]]
    [[{ "coversPreparedStereoPair", featureTimingIsStereoPair && featureTimingMatchesPreparedMask && lastLogicalFeatureSlotMask == expectedFeatureSlotMask }]]
    [[{ "matchesPreparedCharacterMask", featureTimingMatchesPreparedMask }]]
    [[{ "developerModeRequired", false }]]
    [[{ "streamlineLogLevelAffectsAdmission", false }]]
    [[{ "allowUnlistedPatchedOrUnsignedRuntime", true }]]
    [[{ "reason", "D3D11 profile components and asynchronous D3D12 Feature 18 samples are exposed separately; no uncorrelated sum is reported" }]]
    [[{ "foveation", FoveationStatusJson(a_upscaling) }]]
    [[{ "fovPrerequisite", { { "required", fovRequired }, { "available", fovAvailable },]]
    [["fov_not_configured");]]
    [[nr_configure accepts valid settings even when FOV is unavailable]]
    [[{ "character", true }]]
    [[{ "stereoSubmission", globals::game::isVR }]]
    [[A/C and character ROI/Multi-ROI support SE/AE/VR]]
    [[{ "fullResolution", true }]]
    [[{ "reducedResolution", true }]]
    [[{ "foveated", globals::game::isVR }]]
    [[Reduced-resolution NR runs at the active render resolution before DLSS, which owns temporal reconstruction.]]
    [[eyeIndex < (globals::game::isVR ? 2u : 1u)]]
    [[{ "plan", FoveatedPlanJson(a_upscaling, activeProfile) }]]
    [[{ "observedFrame", observedFrame }]]
    [[{ "currentWorkFrame", std::move(currentWorkFrameJson) }]]
    [[{ "measurementSafeFromFrame", std::move(measurementSafeFromFrame) }]]
    [[{ "matchesRequestedSettings", FoveatedPlanMatchesSettings(]]
    [[{ "finalLdrNeuralSupportRequested", finalLdrNeuralSupportRequested }]]
    [[{ "finalLdrNeuralSupportLatched", finalLdrNeuralSupportLatched }]]
    [[{ "output", FoveatedRectJson(a_eye.output) }]]
    [[{ "input", FoveatedRectJson(a_eye.input) }]]
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
    [[IsNeuralRenderingMenuSuppressed(]]
    _broad_submit_menu_suppression
)
if(NOT _broad_submit_menu_suppression EQUAL -1)
    message(FATAL_ERROR
        "Submit NR must not inherit the broad main-route menu suppression policy"
    )
endif()

foreach(_obsolete_temporal_source_contract IN ITEMS
    [[IsNeuralRenderingMenuSuppressed(]]
    [[.pausedSubmitContinuityAllowed]]
)
    string(FIND
        "${_source_contract_text}"
        "${_obsolete_temporal_source_contract}"
        _obsolete_temporal_source_contract_position
    )
    if(NOT _obsolete_temporal_source_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Obsolete temporal-admission source contract remains: ${_obsolete_temporal_source_contract}"
        )
    endif()
endforeach()

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
    [[bool lateMenuCompositeReady =]]
    [[const bool csOverlayOpen =]]
    [[const bool menuContinuityAllowed =]]
    [[.menuContextActive = hardMenuBlocked]]
    [[.pausedContinuityAllowed = menuContinuityAllowed]]
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
    replayMenuContinuityAllowed
    menuContinuityAllowed
    neuralMenuContinuityDispatch
    foveatedRequested
)
    string(FIND
        "${_submit_stage_normalized}"
        "bool ${_named_menu_decision} ="
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
    "${_submit_stage_normalized}"
    [[if (lateMenuCompositeReady != submitStageMenuFinalCompositeRequested) neuralSubmitBaseEligible = false; lateMenuCompositeReady = submitStageMenuFinalCompositeRequested;]]
    _late_composite_source_position
)
if(_late_composite_source_position EQUAL -1)
    message(FATAL_ERROR
        "Late menu readiness must come from the sealed final-composite decision"
    )
endif()

foreach(_continuity_decision IN ITEMS menuContinuityAllowed replayMenuContinuityAllowed)
    foreach(_continuity_term IN ITEMS
        [[NeuralRendering::ResolveMenuContinuityAllowed(]]
        [[hardMenuBlocked]]
        [[requiresNeuralMenuLayer()]]
    )
        string(FIND
            "${_${_continuity_decision}_expression}"
            "${_continuity_term}"
            _continuity_term_position
        )
        if(_continuity_term_position EQUAL -1)
            message(FATAL_ERROR
                "${_continuity_decision} must use the live UI requirement: ${_continuity_term}"
            )
        endif()
    endforeach()
    string(FIND
        "${_${_continuity_decision}_expression}"
        [[currentMenuPresentationContext]]
        _continuity_tail_position
    )
    if(NOT _continuity_tail_position EQUAL -1)
        message(FATAL_ERROR
            "An empty menu-close tracking tail must not suppress NR"
        )
    endif()
endforeach()

foreach(_live_menu_layer_contract IN ITEMS
    [[return IsKnownGameMenuContextActive() || (vrMenuFrameTransaction.frame == currentFrame &&]]
    [[vrMenuFrameTransaction.menuLayerRequired ||]]
    [[vrMenuFrameTransaction.mapLayerRequired ||]]
    [[vrMenuFrameTransaction.recognizedOperations != 0 ||]]
    [[vrMenuFrameTransaction.OwnsPresentationWork()]]
    [[!cachedPairMenuContextMismatch && replayMenuContinuityAllowed &&]]
)
    string(FIND "${_submit_stage_normalized}" "${_live_menu_layer_contract}"
        _live_menu_layer_position)
    if(_live_menu_layer_position EQUAL -1)
        message(FATAL_ERROR
            "Live or pending UI must remain protected on fresh and cached NR pairs: ${_live_menu_layer_contract}"
        )
    endif()
endforeach()

file(READ "${PROJECT_ROOT}/src/State.cpp" _state_source)
file(READ "${PROJECT_ROOT}/src/XSEPlugin.cpp" _plugin_source)
string(REGEX REPLACE "[\r\n\t ]+" " " _state_source_normalized "${_state_source}")
string(FIND "${_upscaling}"
    [[a_state->pendingPostLoadRuntimeReset || a_state->IsWorldLoadTransitionActive()]]
    _world_load_render_gate_position)
if(_world_load_render_gate_position EQUAL -1)
    message(FATAL_ERROR "NR/FOV render gating must distinguish world loading from save-only persistence")
endif()
string(REGEX REPLACE "[\r\n\t ]+" " " _upscaling_normalized "${_upscaling}")
string(FIND "${_upscaling_normalized}"
    [[return IsSaveLoadTransitionContextActive(a_state) || (a_state && a_state->IsSaveLoadSafeModeActive());]]
    _save_load_mutation_guard_position)
if(_save_load_mutation_guard_position EQUAL -1)
    message(FATAL_ERROR "Mutations must stay guarded during both actual world loading and save-only persistence")
endif()
string(FIND "${_state_source_normalized}"
    [[const bool engineSaveLoadActive = engineSignals.RequiresPersistenceGuard();]]
    _save_persistence_guard_position)
if(_save_persistence_guard_position EQUAL -1)
    message(FATAL_ERROR "Ordinary saving must retain disk persistence protection")
endif()
string(FIND "${_plugin_source}" [[case SKSE::MessagingInterface::kSaveGame:]] _save_event_begin)
string(FIND "${_plugin_source}" [[case SKSE::MessagingInterface::kPostLoadGame:]] _save_event_end)
if(_save_event_begin EQUAL -1 OR _save_event_end LESS_EQUAL _save_event_begin)
    message(FATAL_ERROR "Unable to isolate the ordinary save notification")
endif()
math(EXPR _save_event_length "${_save_event_end} - ${_save_event_begin}")
string(SUBSTRING "${_plugin_source}" ${_save_event_begin} ${_save_event_length} _save_event)
string(FIND "${_save_event}" [[NotifyOrdinarySave(frame)]] _save_only_guard_position)
string(FIND "${_save_event}" [[ExtendSaveLoadSafeMode(]] _save_world_guard_position)
string(FIND "${_state_source_normalized}"
    [[void State::NotifyOrdinarySave(uint32_t a_currentFrame) { ExtendSaveGamePersistenceSafeMode(a_currentFrame, kSaveLoadSafeModeGraceFrames); }]]
    _save_notification_guard_position)
if(_save_only_guard_position EQUAL -1 OR _save_notification_guard_position EQUAL -1 OR
    NOT _save_world_guard_position EQUAL -1)
    message(FATAL_ERROR "The save notification must protect persistence without arming a world-load transition")
endif()

string(FIND
    "${_upscaling}"
    [[void Upscaling::BeginVRMenuFinalCompositeFrame(]]
    _menu_frame_begin
)
string(FIND
    "${_upscaling}"
    [[void Upscaling::PoisonVRMenuFrameTransaction(]]
    _menu_frame_end
)
if(_menu_frame_begin EQUAL -1 OR _menu_frame_end EQUAL -1 OR
    _menu_frame_end LESS_EQUAL _menu_frame_begin)
    message(FATAL_ERROR "Unable to isolate the combined menu guard")
endif()
math(EXPR _menu_frame_length "${_menu_frame_end} - ${_menu_frame_begin}")
string(SUBSTRING
    "${_upscaling}"
    ${_menu_frame_begin}
    ${_menu_frame_length}
    _menu_frame_section
)
string(REGEX REPLACE
    "[\r\n\t ]+"
    " "
    _menu_frame_section_normalized
    "${_menu_frame_section}"
)
foreach(_combined_menu_guard_contract IN ITEMS
    [[vrMenuCommittedLayerValid && (!menuPresentationContextActive || communityShadersMenuOpen)]]
    [[communityShadersMenuOpen ? "community-shaders-menu-open" :]]
    [[if (communityShadersMenuOpen && vrMenuFrameTransaction.frame == a_frame)]]
    [[PoisonVRMenuFrameTransaction("community-shaders-menu-open-during-transaction")]]
)
    string(FIND
        "${_menu_frame_section_normalized}"
        "${_combined_menu_guard_contract}"
        _combined_menu_guard_contract_position
    )
    if(_combined_menu_guard_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Combined game-menu and CS-overlay guard is missing: ${_combined_menu_guard_contract}"
        )
    endif()
endforeach()

foreach(_neural_menu_dispatch_term IN ITEMS
    [[sceneFeatureMenuPauseContext]]
    [[neuralSubmitBaseEligible]]
    [[menuContinuityAllowed]]
    [[peerInputFreshnessProven]]
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
    [[neuralMenuContinuityDispatch]]
    [[!presentationOnly]]
    [[!vendorLifecycleMutationDeferred]]
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
    [[bool lateMenuCompositeReady =]]
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
    [[bool IsNeuralRenderingHardMenuBlocked(]]
    [[neuralTemporalAdmissionLatch.compare_exchange_weak(]]
    [[phase == NeuralCenterPhase::Resolve ?]]
    [[(neuralEvaluation.successfulEyeMask & eyeBit) != 0]]
    [[(neuralEvaluation.bypassedEyeMask & eyeBit) != 0]]
    [[args.frameId =]]
    [[ApplySequentialStereo(]]
    [[evaluationAttemptedFeatureSlotMask]]
    [[IsSequentialFrame(]]
    [[activeFeatureSlot_]]
    [[ActiveFeatureSlotOrLocked(]]
    [[static std::once_flag installed;]]
    [[std::call_once(installed,]]
    [[CaptureAuthoredCategories(]]
    [[CS_GPU_PASS_CAPTURE("Upscaling::DLSS5CharacterCategoryCapture", evidence ? evidence->captureTiming : Util::PassTimingHandle{})]]
    [[static_cast<std::int32_t>(a_frame - observationFrame_) <= 0]]
    [[character category capture arrived after a newer observation frame]]
    [[capturedFrame_ != sourceWorldFrame]]
    [[const bool logicalEmptyCapture = state_->capturedCategoriesEmpty_;]]
    [[if (cpuProvenEmpty || logicalEmptyCapture) {]]
    [[state_->ClearMask(]]
    [[slot.requiresEvaluation =]]
    [[const bool cpuProvenEmpty =]]
    [[UsesAuthoredMask(a_args.settings.maskTestMode)]]
    [[const bool samplePending = std::ranges::any_of(]]
    [[sourceEyeWidth = state_->capturedEyeWidth_;]]
    [[state_->capturedEyeWidth_ != a_args.viewportCrop.fullInput.width]]
    [[state_->capturedHeight_ != a_args.viewportCrop.fullInput.height]]
    [[state_->RecordPreparedFrame(]]
    [[a_args.viewportCrop.input.right > sourceEyeWidth]]
    [[if (!characterEvaluationRequired)]]
    [[DXGI_FORMAT_R16G16_UNORM]]
    [[DXGI_FORMAT_R8_UNORM]]
    [[inline constexpr std::uint32_t kCoverageSampleIntervalFrames = 30;]]
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
    [[state_->capturedEnabledCategoryMask_ = a_enabledCategoryMask;]]
    [[(GetEnabledCharacterCategoryMask(a_args.settings) & ~state_->capturedEnabledCategoryMask_) != 0]]
    [[state_->unboundedCategoryMask_ |=]]
    [[(state_->unboundedCategoryMask_ & a_enabledCategoryMask) != 0 ||]]
    [[(unboundedCategoryMask_ &]]
    [[plan.fullEyeEligibilityFallback = true;]]
    [[const bool forcedEmpty =]]
    [[(logicalEmptyCapture || plan.regions.empty()));]]
    [[plan.projectionUncertain) {]]
    [[CharacterCategoryAuthoring::Update(pass);]]
    [[ClassifyCharacterMaterial(]]
    [[ResolveCharacterCompositeInputs(]]
    [[const bool directCommit = params.front().neuralDirectCommit;]]
    [[params.back().neuralDirectCommit != directCommit]]
    [[params.neuralDirectCommit = neuralDirectCommit;]]
    [[EvaluateTemporalAdmission(]]
    [[BuildNeuralTemporalAdmission(]]
    [[ObserveNeuralTemporalAdmission(]]
    [[neuralTemporalAdmission.admitted &&]]
    [[timingFenceValue > lastCompletedTimingFenceValue_]]
    [[RefreshProjectedActors(a_args);]]
    [[ResolveCharacterProjectionPair(corners, rasterCorners,]]
    [[globals::game::frameBufferCached.GetCameraPosAdjust(a_eye)]]
    [[globals::game::frameBufferCached.GetCameraViewProj(eye).Transpose()]]
    [[.currentDepthIdentity = currentDepthIdentity,]]
    [[.captureJitterX = state_->capturedJitterX_,]]
    [[.captureJitterY = state_->capturedJitterY_,]]
    [[.outputIsJittered = a_args.outputIsJittered,]]
    [[ResolveCharacterMaskSamplingJitter(a_args.outputIsJittered, capturedJitterX_, capturedJitterY_)]]
    [[constants.jitter[0] = samplingJitter[0];]]
    [[constants.jitter[1] = samplingJitter[1];]]
    [[capturedDepthSrv_.Get(),]]
    [[std::array<ID3D11ShaderResourceView*, 3> srvs{]]
    [[a_authoredDepthSource,]]
    [[a_args.depthGuide,]]
    [[(a_args.featureSlot & 1u) != a_args.eyeIndex]]
    [[slot.requiresEvaluation = true;]]
    [[summary.bypassedEyeMask = BuildNeuralCenterEyeMask(]]
    [[for (uint32_t stereoEye = 0; stereoEye < neuralResults.size(); ++stereoEye)]]
)
    string(FIND "${_source_contract_text}" "${_source_contract}" _source_position)
    if(_source_position EQUAL -1)
        message(FATAL_ERROR
            "Neural Rendering source contract is missing: ${_source_contract}"
        )
    endif()
endforeach()

foreach(_source_world_frame_threading_contract IN ITEMS
    [[result.sourceWorldFrame = result.retainedWorldFrame ?]]
    [[GetTemporalSourceFrame(]]
    [[admission.sourceWorldFrame == admission.currentFrame]]
    [[params.neuralSourceFrame = neuralSourceFrame;]]
    [[args.sourceWorldFrame = neuralSourceFrame;]]
    [[args.sourceWorldFrame = a_neuralSourceFrame;]]
    [[.sourceWorldFrame = a_sourceWorldFrame,]]
    [[const bool retainedSource =]]
    [[sourceArgs.frameId = sourceWorldFrame;]]
    [[slot.prepareKey.sourceWorldFrame != a_sourceWorldFrame]]
    [[submitStageNeuralStereoState.temporalAdmission.sourceWorldFrame]]
	[[a_eyeState.compositorCycle == a_compositorCycleToken]]
	[[a_eyeState.sourceWorldFrame ==]]
	[[neuralTemporalAdmission.sourceWorldFrame]]
	[[a_eyeState.temporalAdmissionAdmitted ==]]
	[[a_eyeState.retainedWorldFrame ==]]
	[[a_eyeState.neuralSettingsKey == neuralSettingsKey]]
	[[eyeState.sourceWorldFrame =]]
	[[submitStageVendorEyeState[eyeIndex].sourceWorldFrame =]]
	[[vendorEyeStateMatchesCurrentContract(targetEyeState)]]
	[[params.neuralGeneration = std::max<uint64_t>(neuralGeneration, 1u);]]
)
    string(FIND
        "${_source_contract_text}"
        "${_source_world_frame_threading_contract}"
        _source_world_frame_threading_contract_position
    )
    if(_source_world_frame_threading_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Retained source-world-frame threading contract is missing: ${_source_world_frame_threading_contract}"
        )
    endif()
endforeach()

foreach(_character_content_correlation_contract IN ITEMS
    [[std::array<std::uint64_t, 4> contentSerials{};]]
    [[std::uint64_t contentSerial = 0;]]
    [[std::uint64_t maskCoverageContentSerial = 0;]]
    [[AllocatePreparedContentSerial()]]
    [[slot.contentSerial = state_->AllocatePreparedContentSerial();]]
    [[preparedFrame->contentSerials[slotIndex] != slot.contentSerial]]
    [[preparedFrame->contentSerials[a_featureSlot] ==]]
    [[a_prepared.frame == eye.frame]]
    [[coveragePreparation->sourceWorldFrames[eye.maskCoverageFeatureSlot] ==]]
    [[coveragePreparation->contentSerials[eye.maskCoverageFeatureSlot] ==]]
    [[readback.featureSlot == slotIndex && readback.contentSerial != 0]]
    [[slot.maskCoverageContentSerial = readback.contentSerial;]]
    [[slot.maskCoverageContentSerial == slot.contentSerial]]
)
    string(FIND
        "${_source_contract_text}\n${_bridge}"
        "${_character_content_correlation_contract}"
        _character_content_correlation_contract_position
    )
    if(_character_content_correlation_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Character mask content-correlation contract is missing: ${_character_content_correlation_contract}"
        )
    endif()
endforeach()

# A repeated frame can contain additional draws and updated depth. Replacing
# those contents must expire mask preparation without discarding ROI history.
foreach(_fresh_capture_contract IN ITEMS
    [[state_->InvalidatePreparedMasks(true);]]
    [[CharacterDepthExtentPolicy::ExactCapture]]
    [[CharacterDepthExtentPolicy::ContainsActiveInput]]
    [[state_->InvalidateProjectionCache();]]
)
    string(FIND "${_character_source}" "${_fresh_capture_contract}"
        _fresh_capture_position)
    if(_fresh_capture_position EQUAL -1)
        message(FATAL_ERROR "Character capture/projection freshness contract is missing: ${_fresh_capture_contract}")
    endif()
endforeach()
foreach(_stale_capture_contract IN ITEMS
    [[Increment(state_->snapshot_.categoryCaptureReuses);]]
    [[classificationCache.try_emplace(]]
)
    string(FIND "${_character_source}\n${_character_authoring}" "${_stale_capture_contract}"
        _stale_capture_position)
    if(NOT _stale_capture_position EQUAL -1)
        message(FATAL_ERROR "Repeated-frame character draws must not reuse cached material/capture contents: ${_stale_capture_contract}")
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
    [[a_args.settings.debugView != CharacterDebugView::Off && !samplePending]]
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

foreach(_diagnostic_sampling_telemetry_contract IN ITEMS
    [[{ "gpuCoverageSamplingRequiresDebugView", true }]]
    [[{ "gpuCoverageSamplingActive", debugView != NeuralRendering::CharacterDebugView::Off }]]
)
    string(FIND "${_bridge}" "${_diagnostic_sampling_telemetry_contract}"
        _diagnostic_sampling_telemetry_position)
    if(_diagnostic_sampling_telemetry_position EQUAL -1)
        message(FATAL_ERROR
            "Character diagnostic-only GPU sampling telemetry is missing: ${_diagnostic_sampling_telemetry_contract}"
        )
    endif()
endforeach()
foreach(_strict_zero_contract IN ITEMS
    [[const bool cpuProvenEmpty =]]
    [[slot.requiresEvaluation = !cpuProvenEmpty;]]
    [[!slot.requiresEvaluation && slot.zeroCoverageCpuProven]]
    [[slot.zeroCoverageBypassed = false;]]
    [[Increment(state_->snapshot_.provenEmptyFeatureBypassRequests);]]
)
    string(FIND "${_character_source}" "${_strict_zero_contract}"
        _strict_zero_position)
    if(_strict_zero_position EQUAL -1)
        message(FATAL_ERROR
            "Strict current-frame zero-mask bypass contract is missing: ${_strict_zero_contract}"
        )
    endif()
endforeach()
foreach(_geometry_roi_contract IN ITEMS
    [[ResolveCharacterMultiRoi(]]
    [[plan.actorRegions, plan.regions, a_args.outputWidth, a_args.outputHeight,]]
    [[sourceWorldFrame, slot.stableMultiRoi, slot.multiRoiReason,]]
    [[a_args.settings.multiRoiSavingsGate, slot.computeSubrect);]]
    [[slot.multiRoiDiagnostics = slot.stableMultiRoi.diagnostics;]]
    [[a_eye.multiRoiDiagnostics = a_slot.multiRoiDiagnostics;]]
    [[slot.prepareKey.sourceWorldFrame != args.sourceWorldFrame]]
    [[slot.prepareKey.generation != args.generation]]
    [[slot.prepareKey.settings != BuildSettingsKey(args.settings)]]
    [[GetIdentityToken(depth.Get()) != slot.prepareKey.currentDepthIdentity]]
    [[a_eye.maskRoiPlanningCpuMs = a_slot.maskRoiPlanningCpuMs;]]
)
    string(FIND "${_character_source}" "${_geometry_roi_contract}" _geometry_roi_position)
    if(_geometry_roi_position EQUAL -1)
        message(FATAL_ERROR "Current geometry ROI identity contract is missing: ${_geometry_roi_contract}")
    endif()
endforeach()
foreach(_geometry_bridge_contract IN ITEMS
    [[{ "maskRoiReadbackFenceValue", eye.maskRoiReadbackFenceValue }]]
    [["maskRoiReadbackFenceValue":{"type":"integer","minimum":0]]
    [[{ "planningRequiresGpuReadback", false }]]
    [[No result is admitted through a CPU wait]]
)
    string(FIND "${_bridge}" "${_geometry_bridge_contract}" _geometry_bridge_position)
    if(_geometry_bridge_position EQUAL -1)
        message(FATAL_ERROR "Geometry planner telemetry contract is missing: ${_geometry_bridge_contract}")
    endif()
endforeach()
foreach(_stale_zero_token IN ITEMS
    [[HasFreshZeroAuthoredCoverageSample]]
    [[kZeroCoverageReuseFrames]]
    [[measuredZeroCoverageBypass]]
)
    string(FIND "${_source_contract_text}" "${_stale_zero_token}"
        _stale_zero_position)
    if(NOT _stale_zero_position EQUAL -1)
        message(FATAL_ERROR
            "Stale GPU coverage must not suppress current-frame Feature 18: ${_stale_zero_token}"
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
    "void PollReadbacks(" _diagnostic_key_end)
if(_diagnostic_key_begin EQUAL -1 OR _diagnostic_key_end EQUAL -1 OR
    NOT _diagnostic_key_begin LESS _diagnostic_key_end)
    message(FATAL_ERROR "Character diagnostic compatibility key could not be isolated")
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
            "Character diagnostic compatibility key is missing: ${_diagnostic_key_contract}"
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
            "Character diagnostic compatibility key contains unstable geometry: ${_dynamic_diagnostic_token}"
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
    [[const uint32_t evaluationEyeMask =]]
    [[summary.successfulEyeMask = evaluationEyeMask;]]
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

foreach(_forbidden_menu_resume_suppression_token IN ITEMS
    [[ConcealColdStartComposite]]
    [[kCompositeWarmupFrames]]
    [[compositeWarmupFramesRemaining]]
    [[coldStartConcealedSlotMask]]
    [[historyResetFeatureSlotMask]]
)
    string(FIND
        "${_source_contract_text}"
        "${_forbidden_menu_resume_suppression_token}"
        _forbidden_menu_resume_suppression_position
    )
    if(NOT _forbidden_menu_resume_suppression_position EQUAL -1)
        message(FATAL_ERROR
            "Feature 18 history resets must not suppress or ramp the exact character mask: ${_forbidden_menu_resume_suppression_token}"
        )
    endif()
endforeach()

math(EXPR _prepare_mask_length "${_prepare_mask_end} - ${_prepare_mask_begin}")
string(SUBSTRING "${_character_source}" ${_prepare_mask_begin}
    ${_prepare_mask_length} _prepare_mask)
string(FIND "${_prepare_mask}"
    [[Increment(state_->snapshot_.provenEmptyFeatureBypasses);]]
    _premature_proven_bypass)
if(NOT _premature_proven_bypass EQUAL -1)
    message(FATAL_ERROR
        "Mask preparation must report zero requests, not completed Feature 18 skips"
    )
endif()
string(FIND "${_prepare_mask}" "state_->Dispatch(" _prepare_mask_dispatch)
string(FIND "${_prepare_mask}" "slot.requiresEvaluation = !cpuProvenEmpty;"
    _prepare_mask_requires_evaluation)
if(_prepare_mask_dispatch EQUAL -1 OR
    _prepare_mask_requires_evaluation EQUAL -1)
    message(FATAL_ERROR
        "Current-frame mask generation and CPU-proven empty policy are required"
    )
endif()

foreach(_tuple_contract IN ITEMS
    [[static const uint Excluded = 4u;]]
    [[if (category == Excluded)]]
    [[return 1.0 / 255.0;]]
    [[float4 Encode(float inverseVertexAo, uint category, float opacity)]]
    [[saturate(inverseVertexAo),]]
    [[EncodeCategory(category),]]
    [[0.0,]]
    [[saturate(opacity))]]
    [[const uint code = uint(round(saturate(encodedValue.y) * 255.0));]]
    [[if (code == 1u)]]
    [[return Excluded;]]
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
    [[groupshared uint GroupCounters[9];]]
    [[uint4 AuthoredRegion;]]
    [[any(eyePixel < int2(AuthoredRegion.xy))]]
    [[any(eyePixel >= int2(AuthoredRegion.xy + AuthoredRegion.zw))]]
    [[const uint2 inputSize = uint2(SourceCrop.w, Options.x);]]
    [[measureCoverage && insideDispatch && all(outputPixelId < inputSize)]]
    [[ReadAuthoredCategory(int2(outputPixelId))]]
    [[AuthoredFacePixels);]]
    [[CountCategory(centerCategory, VisibleFacePixels);]]
    [[const bool visibilityRejectionEnabled = VisibilityOptions.y >= 0.5;]]
    [[return currentDepth + tolerance >= authoredDepth;]]
    [[CharacterCategoryMask::DecodeCategory(]]
    [[IsAuthoredSurfaceVisible(]]
    [[sourcePixel, centerDepth, centerAuthoredRawDepth)]]
    [[const float centerDistanceWeight =]]
    [[GetDistanceWeight(sourcePixel, centerAuthoredRawDepth);]]
    [[const bool centerWithinDistance = centerDistanceWeight > 0.0;]]
    [[GetCategoryStrength(centerCategory) * centerDistanceWeight]]
    [[neighborDistanceWeight <= 0.0]]
    [[neighborDistanceWeight);]]
    [[float ReconstructCoverage(]]
    [[const int2 basePixel = int2(floor(sourcePosition));]]
    [[for (int y = -radius; y <= radius + 1; ++y)]]
    [[for (int x = -radius; x <= radius + 1; ++x)]]
    [[length(float2(samplePixel) - sourcePosition) / float(radius + 1)]]
    [[const float featherCeiling = centerCategory != 0u ?]]
    [[if (centerEligible && mask < featherCeiling && Options.w != 0 && Options.z != 0 &&]]
    [[if (neighborStrength <= 0.0 ||]]
    [[mask = min(mask, GetCategoryStrength(centerCategory) * centerDistanceWeight);]]
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
foreach(_distance_fade_upload_contract IN ITEMS
    [[constants.visibilityOptions[3] =]]
    [[CharacterRegionPolicy::ResolveDistanceFadeWidth(]]
    [[a_args.settings.maximumDistanceMeters) /]]
)
    string(FIND "${_character_source}" "${_distance_fade_upload_contract}"
        _distance_fade_upload_position)
    if(_distance_fade_upload_position EQUAL -1)
        message(FATAL_ERROR
            "Character distance-fade upload contract is missing: ${_distance_fade_upload_contract}"
        )
    endif()
endforeach()
foreach(_coverage_dispatch_contract IN ITEMS
    [[const auto dispatchWidth = fullSurfaceDispatch ?]]
    [[std::max(a_args.outputWidth, a_args.viewportCrop.input.Width()) :]]
    [[const auto dispatchHeight = fullSurfaceDispatch ?]]
    [[std::max(a_args.outputHeight, a_args.viewportCrop.input.Height()) :]]
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
    [[if (!visibilityRejectionEnabled && !depthFeatherEnabled && !distanceCullEnabled)]]
    [[if (visibilityRejectionEnabled || depthFeatherEnabled)]]
    [[currentDepth = LinearizeDepth(ReadCurrentDepth(localSourcePixel));]]
    [[if (visibilityRejectionEnabled || distanceCullEnabled)]]
    [[authoredRawDepth = ReadAuthoredDepth(localSourcePixel);]]
    [[const float authoredDepth = LinearizeDepth(authoredRawDepth);]]
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
    "const float authoredDepth = LinearizeDepth(authoredRawDepth);"
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
    [[const auto activeWidth =]]
    [[static_cast<std::uint64_t>(a_sourceEyeWidth) * eyeCount;]]
    [[categoryCaptureDesc.Width = static_cast<UINT>(activeWidth);]]
    [[categoryCaptureDesc.Height = a_sourceHeight;]]
    [[depthCaptureDesc.Width = static_cast<UINT>(activeWidth);]]
    [[depthCaptureDesc.Height = a_sourceHeight;]]
    [[state_->EnsureCategoryCapture(a_device, categoryCaptureDesc)]]
    [[state_->EnsureDepthCapture(]]
    [[state_->EnsureCaptureShader(a_device, a_categorySource)]]
    [[std::array<ComputeSubrect, 2> sourceRects{};]]
    [[sourceRects[eye] = BuildFullComputeSubrect(a_sourceEyeWidth, a_sourceHeight);]]
    [[sourceRects[eye] = ExpandCharacterWorkRect(sourceRects[eye],]]
    [[OutputMergerStateGuard outputMerger(a_context);]]
    [[ComputeStateGuard computeState(a_context);]]
    [[state_->captureSourceCategoriesSrv_.Get(), a_depthSource]]
    [[state_->capturedCategoriesUav_.Get(), state_->capturedDepthUav_.Get()]]
    [[eye * a_sourceEyeWidth + rect.baseX, rect.baseY, rect.width, rect.height]]
    [[a_context->Dispatch((rect.width + 7u) / 8u, (rect.height + 7u) / 8u, 1);]]
    [[state_->capturedSourceRects_ = sourceRects;]]
    [[ComPtr<ID3D11Texture2D> capturedDepth_;]]
    [[ComPtr<ID3D11ShaderResourceView> capturedDepthSrv_;]]
    [[ComPtr<ID3D11UnorderedAccessView> capturedDepthUav_;]]
    [[ComPtr<ID3D11UnorderedAccessView> capturedCategoriesUav_;]]
    [[ComPtr<ID3D11ComputeShader> captureShader_;]]
    [[std::array<ComputeSubrect, 2> capturedSourceRects_{};]]
    [[.authoredDepthIdentity = authoredDepthIdentity,]]
    [[.currentDepthIdentity = currentDepthIdentity,]]
    [[state_->capturedDepthSrv_.Get(),]]
    [[state_->capturedEyeWidth_ = a_sourceEyeWidth;]]
    [[state_->capturedHeight_ = a_sourceHeight;]]
    [[static_cast<std::uint64_t>(sourceEyeWidth) * state_->capturedEyeCount_ != sourceDesc.Width]]
    [[state_->capturedHeight_ != sourceDesc.Height]]
    [[constants.sourceCrop[0] = a_args.eyeIndex * a_sourceEyeWidth;]]
    [[constants.authoredRegion[0] = capturedRegion.baseX;]]
    [[constants.authoredRegion[2] = capturedRegion.width;]]
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

foreach(_capture_shader_contract IN ITEMS
    [[uint4 CaptureRegion;]]
    [[Texture2D<unorm float2> SourceCategories : register(t0);]]
    [[Texture2D<float> SourceDepth : register(t1);]]
    [[RWTexture2D<unorm float2> FrozenCategories : register(u0);]]
    [[RWTexture2D<float> FrozenDepth : register(u1);]]
    [[if (any(id.xy >= CaptureRegion.zw))]]
    [[const uint2 pixel = CaptureRegion.xy + id.xy;]]
    [[FrozenCategories[pixel] = SourceCategories.Load(int3(pixel, 0));]]
    [[FrozenDepth[pixel] = SourceDepth.Load(int3(pixel, 0));]]
)
    string(FIND "${_character_capture_shader}" "${_capture_shader_contract}"
        _capture_shader_contract_position)
    if(_capture_shader_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Partial character-capture shader contract is missing: ${_capture_shader_contract}"
        )
    endif()
endforeach()

foreach(_forbidden_full_capture IN ITEMS
    [[a_context->CopyResource(
					state_->capturedCategories_.Get(), a_categorySource);]]
    [[a_context->CopyResource(
					state_->capturedDepth_.Get(), depthTexture.Get());]]
    [[sourceDesc.Width / 2u]]
    [[CopySubresourceRegion(]]
)
    string(FIND
        "${_character_source}"
        "${_forbidden_full_capture}"
        _forbidden_full_capture_position
    )
    if(NOT _forbidden_full_capture_position EQUAL -1)
        message(FATAL_ERROR
            "Character capture must use its exact logical mono or packed stereo extent: ${_forbidden_full_capture}"
        )
    endif()
endforeach()

string(FIND "${_deferred}"
    [[SetupRenderTarget(MASKS2, texDesc, srvDesc, rtvDesc, uavDesc, NeuralRendering::kCharacterCategoryFormat,]]
    _shared_tuple_allocation)
if(_shared_tuple_allocation EQUAL -1)
    message(FATAL_ERROR "Every runtime must allocate the category tuple consumed by character capture")
endif()

string(FIND
    "${_deferred}"
    [[NeuralRendering::kCharacterCategoryFormat]]
    _vr_tuple_format_position
)
string(FIND "${_character_format}" [[DXGI_FORMAT_R16G16_UNORM]]
    _vr_tuple_precision_position)
string(FIND "${_character_source}" [[kCharacterCategoryFormat]]
    _vr_capture_tuple_format_position)
string(FIND
    "${_character_source}"
    [[textureDesc.Format = DXGI_FORMAT_R8_UNORM;]]
    _selection_mask_format_position
)
if(_vr_tuple_format_position EQUAL -1 OR _vr_tuple_precision_position EQUAL -1 OR
   _vr_capture_tuple_format_position EQUAL -1 OR _selection_mask_format_position EQUAL -1)
    message(FATAL_ERROR
        "SE/AE/VR category provenance must preserve R16 AO using shared RG16 UNORM and the selection mask R8 UNORM"
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

function(_require_deferred_producer
    _variable_name
    _label
    _opacity_contract
    _minimum_writes
)
    set(_shader_text "${${_variable_name}}")
    string(FIND "${_shader_text}" [=[#include "Common/CharacterCategoryMask.hlsli"]=]
        _shared_category_include)
    if(_shared_category_include EQUAL -1)
        message(FATAL_ERROR "${_label} category encoding must be available to flat and VR permutations")
    endif()
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
            "${_label} must write the deferred category tuple to target 7 and preserve output opacity"
        )
    endif()
endfunction()

_require_deferred_producer(
    _lighting_shader
    "Lighting"
    [[characterCategory, psout.Diffuse.w);]]
    1
)
_require_deferred_producer(
    _grass_shader
    "RunGrass"
    [[0, psout.Diffuse.w);]]
    2
)
_require_deferred_producer(
    _effect_shader
    "Effect"
    [[0u, psout.Diffuse.w);]]
    1
)
_require_deferred_producer(
    _distant_tree_shader
    "DistantTree"
    [[0u, psout.Diffuse.w);]]
    1
)
_require_deferred_producer(
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
    [[if (actor->IsPlayerRef())]]
    [[if (!actor)]]
    [[!std::isfinite(lightingProperty->alpha)]]
    [[lightingProperty->alpha < 1.0f]]
    [[state->permutationData.ExtraShaderDescriptor &= ~categoryFlags;]]
    [[CharacterCategory::Skin]]
    [[CharacterCategory::Hair]]
)
    string(FIND
        "${_character_authoring}"
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
string(FIND "${_character_authoring}"
    "void CharacterCategoryAuthoring::Update(" _authoring_begin)
if(_observation_acceptance_api EQUAL -1 OR _authoring_begin EQUAL -1)
    message(FATAL_ERROR
        "Character semantic-ID observation acceptance contract is missing"
    )
endif()
string(SUBSTRING "${_character_authoring}" ${_authoring_begin} -1 _authoring_update)
string(REGEX REPLACE "[\r\n\t ]+" " " _authoring_normalized "${_authoring_update}")
string(FIND "${_authoring_normalized}"
    "if (characterRendering.ShouldAuthorActor(state->frameCount, actor->GetFormID(), admission) && characterRendering.ObserveGeometry("
    _accepted_observation)
string(FIND "${_authoring_normalized}"
    "static_cast<uint32_t>(classification.category)" _semantic_descriptor_write)
if(_accepted_observation EQUAL -1 OR _semantic_descriptor_write EQUAL -1 OR
    NOT _accepted_observation LESS _semantic_descriptor_write)
    message(FATAL_ERROR
        "Character category IDs must be emitted only after observation acceptance"
    )
endif()

string(FIND "${_character_authoring}" "Flag::kSkinned" _generic_skinned_position)
if(NOT _generic_skinned_position EQUAL -1)
    message(FATAL_ERROR "Generic skinned geometry must not be classified as character skin")
endif()

string(FIND "${_authoring_normalized}"
    "static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterExcluded);"
    _excluded_descriptor_write)
string(FIND "${_authoring_normalized}" "if (actor->IsPlayerRef())" _player_rejection)
if(_excluded_descriptor_write EQUAL -1 OR _player_rejection EQUAL -1 OR
   NOT _excluded_descriptor_write LESS _player_rejection OR
   NOT _player_rejection LESS _accepted_observation)
    message(FATAL_ERROR "Opaque player and unsupported actor materials must default to exact exclusion before admission")
endif()
string(FIND "${_hooks}" "CharacterCategoryAuthoring::Update(pass);" _core_authoring_call)
string(FIND "${_subsurface_source}" "CharacterCategory" _sss_category_dependency)
if(_core_authoring_call EQUAL -1 OR NOT _sss_category_dependency EQUAL -1)
    message(FATAL_ERROR "Character category authoring must run from the core lighting hook independently of SSS")
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
    [[actor.nearestSelectedDistanceUnits]]
    [[RefreshProjectedActors(a_args);]]
    [[CharacterRegionPolicy::IsWithinMaximumDistance(]]
    [[bool CharacterRendering::ShouldAuthorActor(]]
    [[ResolveCharacterActorAdmission(]]
    [[if (a_projectionUncertain)]]
    [[CharacterRegionPolicy::ResolveFaceSizeExitThreshold(]]
    [[CharacterRegionPolicy::IsDetailRelevantWithHysteresis(]]
    [[a_state.lastSizeEligibleFrame = a_frame;]]
    [[static_cast<std::uint32_t>(a_frame - a_state.lastSizeEligibleFrame) > a_holdFrames]]
    [[state_->actorAdmissions_[a_actorFormId]]
    [[admission.frame == a_args.frameId && admission.history.sizeEligible]]
    [[CharacterRegionPolicy::SelectAdaptive(]]
    [[CharacterRegionPolicy::CompactToCapacity(]]
    [[CharacterRegionPolicy::CoveredArea(plan.regions)]]
    [[globals::game::frameBufferCached.GetCameraProjInverse(]]
    [[characterRendering.ShouldAuthorActor(]]
    [[.outputWidthPerEye = projectionWidth,]]
    [[ExtraShaderDescriptors::CharacterExcluded]]
)
    string(FIND
        "${_character_source}\n${_character_actor_policy}\n${_character_authoring}"
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
    [[REX::W32::AsReal(categorySource.texture),]]
    [[REX::W32::AsReal(depthSource.depthSRV),]]
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
    "neuralPairApplied = ApplyFinalLdrNeuralStereo("
    _submit_neural_apply
)
if(_submit_neural_apply EQUAL -1)
    message(FATAL_ERROR "Submit final-LDR Neural Rendering evaluation is missing")
endif()
string(SUBSTRING "${_upscaling}" 0 ${_submit_neural_apply} _before_submit_neural_apply)
string(SUBSTRING "${_upscaling}" ${_submit_neural_apply} -1 _after_submit_neural_apply)
string(FIND
    "${_before_submit_neural_apply}"
    "if (!finalizeSubmitStageEyeOutput("
    _submit_scene_prepare REVERSE
)
string(FIND
    "${_after_submit_neural_apply}"
    "if (!finalizeSubmitStagePresentationEyeOutput("
    _submit_presentation_finalize
)
if(_submit_scene_prepare EQUAL -1 OR _submit_neural_apply EQUAL -1 OR
    _submit_presentation_finalize EQUAL -1 OR
    NOT _submit_scene_prepare LESS _submit_neural_apply OR
    _submit_presentation_finalize EQUAL 0)
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

foreach(_renderer_source_frame_contract IN ITEMS
    [[std::uint32_t sourceWorldFrame =]]
    [[left.sourceWorldFrame == right.sourceWorldFrame]]
    [[a_args.sourceWorldFrame == invalidFrame]]
    [[a_args.sourceWorldFrame > a_args.frameId]]
    [[IsSourceWorldFrameContinuous(]]
    [[static_assert(!IsSourceWorldFrameContinuous(41u, 41u));]]
    [[static_assert(IsSourceWorldFrameContinuous(41u, 42u));]]
    [[slot.lastSuccessfulSourceWorldFrame]]
    [[slots[index]->lastSuccessfulSourceWorldFrame =]]
    [[a_args[index].sourceWorldFrame]]
    [[snapshot_.sourceWorldFrame = a_args.sourceWorldFrame;]]
)
    string(FIND
        "${_renderer_header}\n${_renderer_source}"
        "${_renderer_source_frame_contract}"
        _renderer_source_frame_contract_position
    )
    if(_renderer_source_frame_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Renderer source-world-frame contract is missing: ${_renderer_source_frame_contract}"
        )
    endif()
endforeach()

string(FIND
    "${_renderer_source}"
    [[.frameId = a_args.front().frameId,]]
    _renderer_timing_evaluation_frame_position
)
if(_renderer_timing_evaluation_frame_position EQUAL -1)
    message(FATAL_ERROR
        "Feature 18 timing must remain keyed to the current evaluation frame"
    )
endif()
string(FIND
    "${_renderer_source}"
    [[.frameId = a_args.front().sourceWorldFrame,]]
    _renderer_timing_source_frame_position
)
if(NOT _renderer_timing_source_frame_position EQUAL -1)
    message(FATAL_ERROR
        "Feature 18 timing must not be rewritten to the retained source frame"
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
    [[route.observedEyeMask]]
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
    [[neural-rendering stereo implementation cycling requires Skyrim VR]]
    [[nr_cycle_modes preserves the VR-only four-lane stereo implementation cycle]]
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
    [=["propertyNames":{"enum":["action","expectedBuildId","control","valueIndex"]}]=]
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
    fovOnlyCenterScale
    peripheryTaaCenterScale
    peripheryTaaOuterScale
    centerHorizontalScale
    leftEyeOffsetX
    leftEyeOffsetY
    rightEyeOffsetX
    rightEyeOffsetY
    peripheryTaaBlendFeather
    neuralFinalLdrBlendFeather
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
    fov_only_center_scale
    periphery_taa_center_scale
    periphery_taa_outer_scale
    center_horizontal_scale
    left_eye_offset_x
    left_eye_offset_y
    right_eye_offset_x
    right_eye_offset_y
    periphery_taa_blend_feather
    neural_final_ldr_blend_feather
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
    [[std::make_shared<CSX::Api::MainThreadDispatchClaim>()]]
    [[if (!claim->TryClaim())]]
    [[claim->Complete();]]
    [[future.wait_for(a_timeout)]]
    [[if (!claim->TryCancel())]]
    [[{ "status", "in_progress" }]]
    [[{ "errorCode", "main_thread_in_progress" }]]
    [[{ "errorCode", "main_thread_timeout" }]]
)
    string(FIND "${_bridge}" "${_main_thread_claim_contract}" _claim_position)
    if(_claim_position EQUAL -1)
        message(FATAL_ERROR
            "Main-thread mutation claim contract is missing: ${_main_thread_claim_contract}"
        )
    endif()
endforeach()

file(READ "${PROJECT_ROOT}/src/Api/MainThreadDispatchPolicy.h" _main_thread_policy)
foreach(_main_thread_policy_contract IN ITEMS
    [[enum class MainThreadDispatchPhase : std::uint8_t]]
    [[MainThreadDispatchPhase::Queued]]
    [[MainThreadDispatchPhase::Running]]
    [[MainThreadDispatchPhase::Cancelled]]
    [[MainThreadDispatchPhase::Completed]]
    [[state.compare_exchange_strong(]]
)
    string(FIND "${_main_thread_policy}" "${_main_thread_policy_contract}" _claim_position)
    if(_claim_position EQUAL -1)
        message(FATAL_ERROR "Shared main-thread claim policy is missing: ${_main_thread_policy_contract}")
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
    [[NeuralRendering::RequiresBackendRetirement(]]
    [[enableStateChanged, multiRoiChanged, insertionPointChanged,]]
    [["insertionPoint":{"type":"string","enum":["upscaled_center","final_ldr_pre_ui"],"description":]]
    [[TryValidateNeuralRenderingPlacement(request,]]
    [["nr_insertion_point_conflict"]]
    [["requiredValue", NeuralRendering::GetInsertionPointName(required)]]
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
    characterAdaptiveRoiSelection
    characterMinimumFacePixelSize
    characterRoiMargin
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

foreach(_adaptive_setting_contract IN ITEMS
    [[visit("neuralCharacterAdaptiveRoiSelectionEnabled", settings.neuralCharacterAdaptiveRoiSelectionEnabled, policy.adaptiveRoiSelection);]]
    [[NeuralRendering::ReadUpscalingCharacterSettingsJson(a_json, parsed);]]
    [[NeuralRendering::WriteUpscalingCharacterSettingsJson(a_json, a_settings);]]
    [[settings.neuralCharacterAdaptiveRoiSelectionEnabled =]]
    [[o_json.erase("neuralCharacterAdaptiveRoiSelectionEnabled");]]
    [[add(a_settings.neuralCharacterAdaptiveRoiSelectionEnabled);]]
)
    string(FIND "${_upscaling}\n${_character_settings_json}" "${_adaptive_setting_contract}"
        _adaptive_setting_position)
    if(_adaptive_setting_position EQUAL -1)
        message(FATAL_ERROR
            "Adaptive character ROI setting is not fully persisted or hashed: ${_adaptive_setting_contract}"
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
    [[{ "characterStateReset", resetSucceeded }]]
    [=["characterMaximumDistanceMeters":{"type":"number","minimum":0.0,"maximum":30.0}]=]
    [=["characterAdaptiveRoiSelection":{"type":"boolean"}]=]
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
    [[{ "privateSingleSubrectEnabled", dynamicCharacterRoiEnabled || privateSingleSubrectEnabled }]]
    [[{ "providerRoiListSupported", false }]]
    [[{ "providerRoiListEvidenceScope", "observed_feature18_parameter_abi" }]]
    [[{ "multiEvaluationExecutionImplemented", true }]]
    [[{ "multiEvaluationProductionQualified", false }]]
    [[{ "multiEvaluationExperimentalEnabled", settings.neuralCharacterMultiRoiEnabled }]]
    [[{ "multiEvaluationMaximumRegionsPerEye", 2 }]]
    [[{ "multiEvaluationMechanism", "separate_persistent_feature18_instances" }]]
    [[{ "privateSingleSubrectValidation", "ghidra_dataflow_and_gpu_timing_validated" }]]
    [[Feature 18 bypasses CPU-proven empty eyes; delayed GPU coverage samples are diagnostic and never suppress current-frame evaluation.]]
    [[unions the current per-eye projected face, skin, and hair eligibility bounds into one private Feature 18 compute subrect]]
    [[Feature 18 color, depth-guide, motion-vector, provider-output, and late-overlay work are restricted to that rectangle.]]
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

string(FIND "${_character_source}" [[const Slot* FindPreparedSlot(]] _prepared_lookup_begin)
string(FIND "${_character_source}" [[void ClearMask(]] _prepared_lookup_end)
if(_prepared_lookup_begin EQUAL -1 OR _prepared_lookup_end LESS_EQUAL _prepared_lookup_begin)
    message(FATAL_ERROR "Unable to isolate shared prepared-resource validation")
endif()
math(EXPR _prepared_lookup_length "${_prepared_lookup_end} - ${_prepared_lookup_begin}")
string(SUBSTRING "${_character_source}" ${_prepared_lookup_begin} ${_prepared_lookup_length} _prepared_lookup)
foreach(_prepared_lookup_contract IN ITEMS
    [[a_featureSlot >= slots_.size()]]
    [[slot.prepared && preparedFrame != snapshot_.preparedFrames.end()]]
    [[prepared.frame == a_frameId]]
    [[(preparedFrame->preparedSlotMask & slotBit) != 0]]
    [[preparedFrame->sourceWorldFrames[a_featureSlot] == a_sourceWorldFrame]]
    [[preparedFrame->generations[a_featureSlot] == a_generation]]
    [[preparedFrame->contentSerials[a_featureSlot] != 0]]
    [[preparedFrame->contentSerials[a_featureSlot] == slot.contentSerial]]
    [[preparedFrame->widths[a_featureSlot] == a_width]]
    [[preparedFrame->heights[a_featureSlot] == a_height]]
    [[slot.prepareKey.sourceWorldFrame == a_sourceWorldFrame]]
    [[slot.prepareKey.generation == a_generation]]
    [[slot.width == a_width && slot.height == a_height]]
)
    string(FIND "${_prepared_lookup}" "${_prepared_lookup_contract}" _prepared_lookup_position)
    if(_prepared_lookup_position EQUAL -1)
        message(FATAL_ERROR "Shared prepared-resource freshness contract is missing: ${_prepared_lookup_contract}")
    endif()
endforeach()
string(REGEX MATCHALL [[state_->FindPreparedSlot\(]] _prepared_lookup_calls "${_character_source}")
list(LENGTH _prepared_lookup_calls _prepared_lookup_count)
if(NOT _prepared_lookup_count EQUAL 4)
    message(FATAL_ERROR "Mask, single rectangle, region-plan accessors and queued finalization must all use shared prepared-resource validation")
endif()

string(FIND "${_renderer_source}" [[bool Renderer::State::ApplyBatchLocked(]] _region_batch_begin)
string(FIND "${_renderer_source}" [[bool Renderer::State::ResetLocked(]] _region_batch_end)
if(_region_batch_begin EQUAL -1 OR _region_batch_end LESS_EQUAL _region_batch_begin)
    message(FATAL_ERROR "Unable to isolate independent-region batch")
endif()
math(EXPR _region_batch_length "${_region_batch_end} - ${_region_batch_begin}")
string(SUBSTRING "${_renderer_source}" ${_region_batch_begin} ${_region_batch_length} _region_batch)
foreach(_region_contract IN ITEMS
    [[GetStereoPairContractViolation(stereoArgs)]]
    [[GetCharacterRegionSubmissionViolation(]]
    [[logical.outputWidth, logical.outputHeight, logical.characterVisualIsolation)]]
    [[physical.featureSlot = PhysicalRegionFeatureSlot(logical.featureSlot, region);]]
    [[physical.computeRegions = {};]]
    [[resources[index].historyKey.regionIdentity = regionIdentities[index];]]
    [[clusterIdentities[expandedCount] = plan.clusterIdentities[region];]]
    [[IsMatchingRegionStereoPair(a_args[left].featureSlot, clusterIdentities[left],]]
    [[std::array<Slot*, kMaximumRegionEvaluations> slots{};]]
    [[IsMatchingRegionStereoPair(]]
    [[.logicalEyeCount = logicalEyeCount,]]
    [[physical.evaluationAttemptedFeatureSlotMask, required, false]]
    [[physical.evaluationSucceededFeatureSlotMask, required, true]]
)
    string(FIND "${_region_batch}" "${_region_contract}" _region_contract_position)
    if(_region_contract_position EQUAL -1)
        message(FATAL_ERROR "Independent region safety contract is missing: ${_region_contract}")
    endif()
endforeach()
set(_previous_region_stage -1)
foreach(_region_stage IN ITEMS
    [[GetStereoPairContractViolation(stereoArgs)]]
    [[physical.featureSlot = PhysicalRegionFeatureSlot(logical.featureSlot, region);]]
    [[EnsureBackendLocked(a_args.front(), execution)]]
    [[activeStage_ = RendererStage::ColorInputCopy;]]
    [[activeStage_ = RendererStage::ControlMaskCopy;]]
    [[Runtime::Instance().Execute(]]
    [[if (!interop_.EndD3D12())]]
    [[activeStage_ = RendererStage::OutputCommit;]]
)
    string(FIND "${_region_batch}" "${_region_stage}" _region_stage_position)
    if(_region_stage_position LESS_EQUAL _previous_region_stage)
        message(FATAL_ERROR "Region inputs must precede evaluations and all output commits: ${_region_stage}")
    endif()
    set(_previous_region_stage ${_region_stage_position})
endforeach()
foreach(_timing_contract IN ITEMS
    [[a_timing.evaluationCount > 4u]]
    [[a_timing.logicalEyeCount == 0u || a_timing.logicalEyeCount > 2u]]
    [[commandContext.timing.logicalEyeCount == 2u]]
    [[telemetry_.lastFeatureLogicalEyeCount =]]
)
    string(FIND "${_d3d12_interop_source}" "${_timing_contract}" _timing_contract_position)
    if(_timing_contract_position EQUAL -1)
        message(FATAL_ERROR "Region timing must distinguish actual evaluations from stereo eyes: ${_timing_contract}")
    endif()
endforeach()

message(STATUS "Neural Rendering DevBench contract passed")

string(JSON _fov_taa_disabled_type GET "${_descriptor_json}"
    outputSchema properties fovTaaDisabled type)
if(NOT _fov_taa_disabled_type STREQUAL "boolean")
    message(FATAL_ERROR "NR FOV normalization result must be exposed as a boolean")
endif()

foreach(_field IN ITEMS required available locked)
    string(JSON _type GET "${_descriptor_json}"
        outputSchema properties neuralRendering properties renderScalePrerequisite properties ${_field} type)
    if(NOT _type STREQUAL "boolean")
        message(FATAL_ERROR "Render Scale prerequisite ${_field} must be exposed as a boolean")
    endif()
endforeach()

# A rejected native target must not queue work or change saved settings.
string(FIND "${_upscaling}" "Upscaling::UpscalingTransitionApplyResult Upscaling::ApplyCSMenuUpscalingTransition(" _transition_start)
string(FIND "${_upscaling}" "void Upscaling::SetVRUpscalingTransitionProfile(" _transition_end)
math(EXPR _transition_length "${_transition_end} - ${_transition_start}")
string(SUBSTRING "${_upscaling}" ${_transition_start} ${_transition_length} _transition)
string(FIND "${_transition}" "UpscalingTransitionApplyRejection::NeuralRenderScaleRequired" _dependency_guard)
foreach(_mutation IN ITEMS "QueueVR" "settings.renderScaleMode =")
    string(FIND "${_transition}" "${_mutation}" _mutation_position)
    if(_dependency_guard LESS 0 OR _mutation_position LESS_EQUAL _dependency_guard)
        message(FATAL_ERROR "NR dependency rejection must precede transition publication: ${_mutation}")
    endif()
endforeach()
