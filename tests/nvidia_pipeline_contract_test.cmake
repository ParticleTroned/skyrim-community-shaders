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
)
    string(FIND "${_swapchain_header}${_swapchain}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "DXGI frame-generation proxy is missing contract behavior: ${_required}")
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
