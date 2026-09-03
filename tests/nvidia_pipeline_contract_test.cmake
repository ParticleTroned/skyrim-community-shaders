if(NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _upscaling)
foreach(_required IN ITEMS
    "DXGI_SWAP_CHAIN_DESC proxyDesc = *pSwapChainDesc"
    "proxyDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD"
    "ptrD3D11CreateDeviceAndSwapChainUpscaling(pAdapter"
    "pFeatureLevels"
    "FeatureLevels"
    "if (FAILED(ret) || !ppDevice || !*ppDevice)"
    "ResolveCreatedAdapter(*ppDevice, pAdapter)"
    "ResetProxyCreationState()"
    "NvidiaComIdentity::IsSame"
    "a_requireSameDeviceIdentity"
    "IsFrameGenerationProxyContractSupported"
    "TryBeginProxyCreation()"
    ".usage = a_desc.BufferUsage"
)
    string(FIND "${_upscaling}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "NVIDIA D3D hook is missing contract behavior: ${_required}")
    endif()
endforeach()

string(FIND "${_upscaling}" "pSwapChainDesc->SwapEffect =" _caller_swap_effect)
string(FIND "${_upscaling}" "pSwapChainDesc->BufferCount =" _caller_buffer_count)
string(FIND "${_upscaling}" "pSwapChainDesc->BufferDesc.Format =" _caller_format)
if(NOT _caller_swap_effect EQUAL -1 OR
   NOT _caller_buffer_count EQUAL -1 OR
   NOT _caller_format EQUAL -1)
    message(FATAL_ERROR "Optional backends must not mutate the caller's swap-chain descriptor")
endif()

file(READ "${PROJECT_ROOT}/src/Features/Upscaling/Streamline.cpp" _streamline)
foreach(_required IN ITEMS
    "ValidateStreamlineRuntime"
    "sl.common.dll"
    "sl.dlss.dll"
    "sl.interposer.dll"
    "sl.pcl.dll"
    "sl.reflex.dll"
    "ComputeConstantsIdentity"
    "a_constants.minRelativeLinearDepthObjectSeparation"
    "frameGenerationQuarantinedByReflex.store(true"
    "EnsureReflexDisabledForFrameGeneration"
    "IsDLSSRuntimeReady"
    "runtimeHasDLSS"
    "runtimeHasReflex"
    "runtimeHasPCL"
    "ShutdownQuarantined"
    "LoggingCallback(sl::LogType type, const char* msg) noexcept"
    "BoundedCopyResult::Truncated"
)
    string(FIND "${_streamline}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Streamline hardening is missing contract behavior: ${_required}")
    endif()
endforeach()

foreach(_required IN ITEMS
    "streamline.IsDLSSRuntimeReady()"
    "fidelityFX.IsRuntimeUpscalerDispatchProofUsable(dispatchPath)"
)
    string(FIND "${_upscaling}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Vendor dispatch proof is missing hard invalidation: ${_required}")
    endif()
endforeach()

file(READ "${PROJECT_ROOT}/src/Features/Upscaling/DX12SwapChain.h" _swapchain_header)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/DX12SwapChain.cpp" _swapchain)
foreach(_required IN ITEMS
    "DXGISwapChainProxy : IDXGISwapChain4"
    "std::atomic_ulong referenceCount"
    "if (!ppvObj)"
    "AddRef();"
    "return d3d11Device ? d3d11Device->QueryInterface"
    "PresentInternal"
    "runtimeQuarantined = true"
    "EnsureReflexDisabledForFrameGeneration"
    "const std::array beforeCopy"
    "producerFenceValue = fenceSequence.Next()"
    "ResolveBackendBufferCount"
    "ProxyLifecycleGate lifecycle"
    "lifecycle.BeginRetirement(upscaling.d3d12SwapChainActive)"
    "lifecycle.CompleteRetirement(false,"
    "retained the published generation through process exit"
    "DXGI_PRESENT_TEST"
    "DXGI_ERROR_WAS_STILL_DRAWING"
    "return owner.GetDesc(pDesc)"
    "return 0;"
    "ResetFrameGenerationContexts()"
    "ResetFrameGenerationRenderContext()"
    "SetupFrameGeneration()"
    "publicFormat != publicSwapChainDesc.BufferDesc.Format"
    "WaitForAllocatorSlot"
    "allocatorRetirements.ClassifyReuse"
    "allocatorRetirements.MarkSubmitted"
    "ClassifyResizeBuffers1Admission"
    "NvidiaComIdentity::IsSame(presentQueue[0], commandQueue.get())"
    "return owner.GetFullscreenState(pFullscreen, ppTarget)"
    "desc->RefreshRate = publicSwapChainDesc.BufferDesc.RefreshRate"
)
    string(FIND "${_swapchain_header}${_swapchain}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "DXGI frame-generation proxy is missing contract behavior: ${_required}")
    endif()
endforeach()

string(FIND "${_swapchain}" "HRESULT DX12SwapChain::PresentInternal" _present_start)
string(FIND "${_swapchain}" "HRESULT DX12SwapChain::GetDevice" _present_end)
math(EXPR _present_length "${_present_end} - ${_present_start}")
string(SUBSTRING "${_swapchain}" ${_present_start} ${_present_length} _present_body)
string(FIND "${_present_body}" "WaitForAllocatorSlot(" _allocator_wait)
string(FIND "${_present_body}" "commandAllocators[frameIndex]->Reset()" _allocator_reset)
string(FIND "${_present_body}" "commandQueue->Signal(d3d12Fence.get(), *consumerFenceValue)" _retirement_signal)
string(FIND "${_present_body}" "allocatorRetirements.MarkSubmitted" _retirement_mark)
if(_allocator_wait EQUAL -1 OR _allocator_reset EQUAL -1 OR
   _retirement_signal EQUAL -1 OR _retirement_mark EQUAL -1 OR
   _allocator_wait GREATER _allocator_reset OR
   _retirement_mark LESS _retirement_signal)
    message(FATAL_ERROR
        "Present must prove allocator retirement before reset and track each submission"
    )
endif()

string(FIND "${_swapchain}" "void DX12SwapChain::OnProxyDestroyed" _retirement_start)
string(FIND "${_swapchain}" "DX12SwapChain::GetLifecycleState" _retirement_end)
math(EXPR _retirement_length "${_retirement_end} - ${_retirement_start}")
string(SUBSTRING "${_swapchain}" ${_retirement_start} ${_retirement_length} _retirement_body)
foreach(_forbidden_retirement_action IN ITEMS
    "ResetResources()"
    "ResetFrameGenerationContexts()"
)
    string(FIND
        "${_retirement_body}"
        "${_forbidden_retirement_action}"
        _forbidden_retirement_position
    )
    if(NOT _forbidden_retirement_position EQUAL -1)
        message(FATAL_ERROR
            "Published final release must retain reader-owned state: ${_forbidden_retirement_action}"
        )
    endif()
endforeach()

string(FIND "${_swapchain}" "HRESULT DX12SwapChain::ResizeBuffers1(" _resize1_start)
string(FIND "${_swapchain}" "HRESULT DX12SwapChain::RefreshAfterResize" _resize1_end)
math(EXPR _resize1_length "${_resize1_end} - ${_resize1_start}")
string(SUBSTRING "${_swapchain}" ${_resize1_start} ${_resize1_length} _resize1_body)
string(FIND "${_resize1_body}" "ClassifyResizeBuffers1Admission(" _resize1_admission)
string(FIND "${_resize1_body}" "ResetFrameGenerationRenderContext()" _resize1_teardown)
if(_resize1_admission EQUAL -1 OR _resize1_teardown EQUAL -1 OR
   _resize1_admission GREATER _resize1_teardown)
    message(FATAL_ERROR "ResizeBuffers1 must reject unsupported arrays before mutation")
endif()

foreach(_forbidden_mode_reset IN ITEMS
    "publicSwapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED"
    "publicSwapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED"
)
    string(FIND "${_swapchain}" "${_forbidden_mode_reset}" _mode_reset_position)
    if(NOT _mode_reset_position EQUAL -1)
        message(FATAL_ERROR "Buffer resize must preserve public target-mode state")
    endif()
endforeach()

file(READ "${PROJECT_ROOT}/src/Features/Upscaling/FidelityFX.cpp" _fidelityfx)
foreach(_required IN ITEMS
    "completeLoaderInterface"
    "frameGenerationSessionQuarantined.exchange(true"
    "ConfigureFrameGenerationProtected"
    "DispatchFrameGenerationProtected"
    "frameGenContextIndeterminate"
    "swapChainContextIndeterminate"
    "IsRuntimeUpscalerDispatchProofUsable"
    "ConfirmFrameGenerationDisabled"
    "frameGenerationDisableConfirmed"
    "ResetFrameGenerationRenderContext"
)
    string(FIND "${_fidelityfx}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "FidelityFX frame-generation boundary is missing contract behavior: ${_required}")
    endif()
endforeach()

file(READ "${PROJECT_ROOT}/src/Hooks.cpp" _hooks)
string(FIND "${_hooks}" "streamline.UpdateReflex()" _reflex_caller)
if(_reflex_caller EQUAL -1)
    message(FATAL_ERROR "The early-frame Reflex update caller is missing")
endif()

file(READ "${PROJECT_ROOT}/src/Api/UpscalingService.cpp" _upscaling_service)
foreach(_required IN ITEMS
    "pendingOperationReservations"
    "releaseOperationReservation"
    "--pendingOperationReservations"
    "rollbackUnreadyCommand"
    "DiscardUnreadyAdmission"
)
    string(FIND "${_upscaling_service}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Upscaling service admission is missing bounded reservation behavior: ${_required}")
    endif()
endforeach()

file(READ "${PROJECT_ROOT}/src/Features/VR/InSceneOverlay.cpp" _compositor)
foreach(_required IN ITEMS
    "VRCompositor"
    "Submit"
    "IVRCompositor_Submit"
)
    string(FIND "${_compositor}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "OpenVR submission caller is missing from the review contract: ${_required}")
    endif()
endforeach()

message(STATUS "NVIDIA runtime, D3D fallback, and OpenVR submission contracts are present")
