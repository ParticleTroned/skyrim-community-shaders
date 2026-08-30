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
)
    string(FIND "${_streamline}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Streamline hardening is missing contract behavior: ${_required}")
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
