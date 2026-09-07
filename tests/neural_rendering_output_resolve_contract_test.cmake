cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(
    _renderer_header_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.h"
)
set(
    _renderer_source_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.cpp"
)
set(
    _resolve_shader_path
    "${PROJECT_ROOT}/features/Upscaling/Shaders/Upscaling/NeuralRendering/ResolveOutputCS.hlsl"
)
set(
    _resolve_policy_path
    "${PROJECT_ROOT}/features/Upscaling/Shaders/Upscaling/NeuralRendering/OutputResolve.hlsli"
)

foreach(_required_path IN ITEMS
    "${_renderer_header_path}"
    "${_renderer_source_path}"
    "${_resolve_shader_path}"
    "${_resolve_policy_path}"
)
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR
            "Required Neural Rendering output-resolve input is missing: ${_required_path}"
        )
    endif()
endforeach()

file(READ "${_renderer_header_path}" _renderer_header)
file(READ "${_renderer_source_path}" _renderer_source)
file(READ "${_resolve_shader_path}" _resolve_shader)
file(READ "${_resolve_policy_path}" _resolve_policy)
string(REGEX REPLACE "[\r\n\t ]+" " " _renderer_normalized "${_renderer_source}")
string(REGEX REPLACE "[\r\n\t ]+" " " _resolve_shader_normalized "${_resolve_shader}")
string(REGEX REPLACE "[\r\n\t ]+" " " _resolve_policy_normalized "${_resolve_policy}")

foreach(_renderer_contract IN ITEMS
    [[RendererStage::OutputResolve]]
    [[std::array<ID3D11ShaderResourceView*, 2> shaderResources_{}]]
    [[context_->CSGetConstantBuffers(0, 1, &constantBuffer_)]]
    [[context_->CSGetShaderResources( 0, static_cast<UINT>(shaderResources_.size()), shaderResources_.data())]]
    [[context_->CSSetConstantBuffers(0, 1, &constantBuffer_)]]
    [[context_->CSSetShaderResources( 0, static_cast<UINT>(shaderResources_.size()), shaderResources_.data())]]
    [[ComPtr<ID3D11Texture2D> resolvedOutput]]
    [[ComPtr<ID3D11UnorderedAccessView> resolvedOutputUAV]]
    [[ComPtr<ID3D11Buffer> resolveOutputCB_]]
    [[D3D11_FORMAT_SUPPORT_SHADER_LOAD]]
    [[D3D11_FORMAT_SUPPORT2_SHAREABLE | D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE]]
    [[a_resources.resourceKey.outputFormat, true)]]
    [[L"Data/Shaders/Upscaling/NeuralRendering/ResolveOutputCS.hlsl"]]
    [["NeuralRendering::ResolveOutputCB"]]
    [[resolveOutputCS_.Reset()]]
    [[resolveOutputCB_.Reset()]]
    [[resolveOutputCompileFailed_ = false]]
    [[TeardownBackendLocked(true, false, true)]]
)
    string(FIND
        "${_renderer_normalized}"
        "${_renderer_contract}"
        _renderer_contract_position
    )
    if(_renderer_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Neural output-resolve renderer contract is missing: ${_renderer_contract}"
        )
    endif()
endforeach()

string(FIND
    "${_renderer_source}"
    [[bool Renderer::State::ResolveOutputBatchLocked(]]
    _resolve_batch_begin
)
string(FIND
    "${_renderer_source}"
    [[bool Renderer::State::ApplyLocked(]]
    _resolve_batch_end
)
if(_resolve_batch_begin EQUAL -1 OR _resolve_batch_end EQUAL -1 OR
    _resolve_batch_end LESS_EQUAL _resolve_batch_begin)
    message(FATAL_ERROR "Unable to isolate Renderer::ResolveOutputBatchLocked")
endif()
math(EXPR _resolve_batch_length "${_resolve_batch_end} - ${_resolve_batch_begin}")
string(SUBSTRING
    "${_renderer_source}"
    ${_resolve_batch_begin}
    ${_resolve_batch_length}
    _resolve_batch
)
string(REGEX REPLACE "[\r\n\t ]+" " " _resolve_batch_normalized "${_resolve_batch}")

foreach(_roi_contract IN ITEMS
    [[std::span<const ValidatedResources> a_resources]]
    [[const auto& roi = a_resources[index].outputSubrect]]
    [[.offsetX = roi.baseX]]
    [[.offsetY = roi.baseY]]
    [[.width = roi.width]]
    [[.height = roi.height]]
    [[resolveOutputCB_.Get(), 0, nullptr, &constants, 0, 0]]
    [[(roi.width + 7u) / 8u]]
    [[(roi.height + 7u) / 8u]]
)
    string(FIND
        "${_resolve_batch_normalized}"
        "${_roi_contract}"
        _roi_contract_position
    )
    if(_roi_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Neural output-resolve ROI contract is missing: ${_roi_contract}"
        )
    endif()
endforeach()

string(FIND
    "${_renderer_source}"
    [[bool Renderer::State::ApplySequentialStereoLocked(]]
    _sequential_begin
)
string(FIND
    "${_renderer_source}"
    [[bool Renderer::State::ApplyBatchLocked(]]
    _sequential_end
)
if(_sequential_begin EQUAL -1 OR _sequential_end EQUAL -1 OR
    _sequential_end LESS_EQUAL _sequential_begin)
    message(FATAL_ERROR "Unable to isolate Renderer::ApplySequentialStereoLocked")
endif()
math(EXPR _sequential_length "${_sequential_end} - ${_sequential_begin}")
string(SUBSTRING
    "${_renderer_source}"
    ${_sequential_begin}
    ${_sequential_length}
    _sequential
)
string(REGEX REPLACE "[\r\n\t ]+" " " _sequential_normalized "${_sequential}")

foreach(_sequential_contract IN ITEMS
    [[std::span(&synchronizedArgs[0], 1), leftOutcome, true]]
    [[std::span(&synchronizedArgs[1], 1), rightOutcome, true]]
    [[if (!rightSucceeded) return false]]
    [[slots_[synchronizedArgs[index].featureSlot].resolvedOutput.Get()]]
    [[resources[index].outputSubrect]]
    [[device removal followed the sequential stereo output commit]]
)
    string(FIND
        "${_sequential_normalized}"
        "${_sequential_contract}"
        _sequential_contract_position
    )
    if(_sequential_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Sequential stereo output-resolve contract is missing: ${_sequential_contract}"
        )
    endif()
endforeach()

string(FIND "${_sequential_normalized}"
    [[std::span(&synchronizedArgs[0], 1), leftOutcome, true]]
    _sequential_left_position)
string(FIND "${_sequential_normalized}"
    [[std::span(&synchronizedArgs[1], 1), rightOutcome, true]]
    _sequential_right_position)
string(FIND "${_sequential_normalized}"
    [[slots_[synchronizedArgs[index].featureSlot].resolvedOutput.Get()]]
    _sequential_commit_position)
if(NOT _sequential_left_position LESS _sequential_right_position OR
    NOT _sequential_right_position LESS _sequential_commit_position)
    message(FATAL_ERROR
        "Sequential stereo must finish both deferred evaluations before either output commit"
    )
endif()

string(FIND
    "${_renderer_source}"
    [[bool Renderer::State::ApplyBatchLocked(]]
    _apply_batch_begin
)
string(FIND
    "${_renderer_source}"
    [[bool Renderer::State::ResetLocked(]]
    _apply_batch_end
)
if(_apply_batch_begin EQUAL -1 OR _apply_batch_end EQUAL -1 OR
    _apply_batch_end LESS_EQUAL _apply_batch_begin)
    message(FATAL_ERROR "Unable to isolate Renderer::ApplyBatchLocked")
endif()
math(EXPR _apply_batch_length "${_apply_batch_end} - ${_apply_batch_begin}")
string(SUBSTRING
    "${_renderer_source}"
    ${_apply_batch_begin}
    ${_apply_batch_length}
    _apply_batch
)
string(REGEX REPLACE "[\r\n\t ]+" " " _apply_batch_normalized "${_apply_batch}")

foreach(_batch_contract IN ITEMS
    [[EnsureOutputResolveShaderLocked()]]
    [[ResolveOutputBatchLocked( a_args, std::span(slots.data(), a_args.size()), std::span(resources.data(), a_args.size()))]]
    [[device removal followed the private output-resolve dispatch]]
    [[slots[index]->resolvedOutput.Get(), resources[index].outputSubrect)]]
)
    string(FIND
        "${_apply_batch_normalized}"
        "${_batch_contract}"
        _batch_contract_position
    )
    if(_batch_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Neural output-resolve batch contract is missing: ${_batch_contract}"
        )
    endif()
endforeach()

string(FIND "${_apply_batch_normalized}"
    [[EnsureOutputResolveShaderLocked()]] _ensure_resolve_position)
string(FIND "${_apply_batch_normalized}"
    [[Runtime::Instance().Execute(]] _execute_position)
if(NOT _ensure_resolve_position LESS _execute_position)
    message(FATAL_ERROR
        "Resolve shader and constants must be ready before Feature 18 evaluation"
    )
endif()

string(FIND "${_apply_batch_normalized}"
    [[interop_.EndD3D12()]] _end_d3d12_position)
string(FIND "${_apply_batch_normalized}"
    [[ResolveOutputBatchLocked(]] _resolve_position)
string(FIND "${_apply_batch_normalized}"
    [[slots[index]->resolvedOutput.Get(), resources[index].outputSubrect)]]
    _commit_position)
if(NOT _end_d3d12_position LESS _resolve_position)
    message(FATAL_ERROR
        "Private output resolve must follow D3D12 Feature 18 submission"
    )
endif()
if(NOT _resolve_position LESS _commit_position)
    message(FATAL_ERROR
        "Both private output resolves must finish before caller output commit"
    )
endif()
string(FIND "${_apply_batch_normalized}"
    [[if (a_deferOutputCommit) return true]] _deferred_return_position)
if(_deferred_return_position EQUAL -1 OR
    NOT _resolve_position LESS _deferred_return_position OR
    NOT _deferred_return_position LESS _commit_position)
    message(FATAL_ERROR
        "Deferred sequential evaluation must stop after private resolve and before output commit"
    )
endif()
string(FIND "${_apply_batch_normalized}"
    [[slots[index]->output.resource11.Get(), resources[index].outputSubrect)]]
    _raw_commit_position)
if(NOT _raw_commit_position EQUAL -1)
    message(FATAL_ERROR "Raw Feature 18 output must never be committed directly")
endif()

string(FIND "${_renderer_source}"
    [[bool Renderer::State::TeardownBackendLocked(]] _teardown_begin)
string(FIND "${_renderer_source}"
    [[void Renderer::State::AbandonSlotsLocked(]] _teardown_end)
if(_teardown_begin EQUAL -1 OR _teardown_end EQUAL -1 OR
    _teardown_end LESS_EQUAL _teardown_begin)
    message(FATAL_ERROR "Unable to isolate Renderer::TeardownBackendLocked")
endif()
math(EXPR _teardown_length "${_teardown_end} - ${_teardown_begin}")
string(SUBSTRING "${_renderer_source}" ${_teardown_begin}
    ${_teardown_length} _teardown)
string(REGEX REPLACE "[\r\n\t ]+" " " _teardown_normalized "${_teardown}")
string(FIND "${_teardown_normalized}"
    [[if (a_resetShader)]] _reset_shader_condition_position)
string(FIND "${_teardown_normalized}"
    [[device_.Reset()]] _device_reset_position)
foreach(_device_owned_reset IN ITEMS
    [[copyDepthGuideCS_.Reset()]]
    [[copyDepthGuideCB_.Reset()]]
    [[resolveOutputCS_.Reset()]]
    [[resolveOutputCB_.Reset()]]
)
    string(FIND "${_teardown_normalized}"
        "${_device_owned_reset}" _device_owned_reset_position)
    if(_device_owned_reset_position EQUAL -1 OR
        NOT _device_owned_reset_position LESS _device_reset_position OR
        NOT _device_owned_reset_position LESS _reset_shader_condition_position)
        message(FATAL_ERROR
            "Every backend teardown must release ${_device_owned_reset} before clearing the device, independent of shader-cache policy"
        )
    endif()
endforeach()

foreach(_shader_contract IN ITEMS
    [[Texture2D<float4> OriginalColor : register(t0)]]
    [[Texture2D<float4> RawModelOutput : register(t1)]]
    [[RWTexture2D<float4> ResolvedOutput : register(u0)]]
    [[cbuffer OutputResolveCB : register(b0)]]
    [[uint2 RoiOffset]]
    [[uint2 RoiExtent]]
    [[any(dispatchThreadId.xy >= RoiExtent)]]
    [[const uint2 pixel = dispatchThreadId.xy + RoiOffset]]
    [[ResolvedOutput[pixel] = ResolveNeuralOutput( OriginalColor.Load(position), RawModelOutput.Load(position))]]
)
    string(FIND
        "${_resolve_shader_normalized}"
        "${_shader_contract}"
        _shader_contract_position
    )
    if(_shader_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Neural output-resolve shader contract is missing: ${_shader_contract}"
        )
    endif()
endforeach()

# A concrete nonzero-offset example guards against regressing to dispatch-local
# texture addressing while retaining an ROI-sized dispatch.
set(_test_roi_offset_x 19)
set(_test_roi_offset_y 37)
set(_test_dispatch_x 5)
set(_test_dispatch_y 7)
math(EXPR _test_pixel_x "${_test_roi_offset_x} + ${_test_dispatch_x}")
math(EXPR _test_pixel_y "${_test_roi_offset_y} + ${_test_dispatch_y}")
if(NOT _test_pixel_x EQUAL 24 OR NOT _test_pixel_y EQUAL 44)
    message(FATAL_ERROR "Nonzero output-resolve ROI addressing contract failed")
endif()

foreach(_policy_contract IN ITEMS
    [[kNeuralOutputLuminanceFloor = 1.0f / 512.0f]]
    [[kNeuralOutputMaximumLuminanceRatio = 2.0f]]
    [[all(isfinite(originalSample.rgb))]]
    [[all(isfinite(modelSample.rgb))]]
    [[modelLuminance <= kNeuralOutputMinimumModelLuminance]]
    [[boundedLuminanceRatio * (originalLuminance + kNeuralOutputLuminanceFloor) - kNeuralOutputLuminanceFloor]]
    [[const float correctionScale = boundedLuminance / modelLuminance]]
    [[const float3 resolvedColor = modelSample.rgb * correctionScale]]
    [[return float4(resolvedColor, originalAlpha)]]
)
    string(FIND
        "${_resolve_policy_normalized}"
        "${_policy_contract}"
        _policy_contract_position
    )
    if(_policy_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Neural output-resolve policy contract is missing: ${_policy_contract}"
        )
    endif()
endforeach()

message(STATUS "Neural Rendering output-resolve contract passed")
