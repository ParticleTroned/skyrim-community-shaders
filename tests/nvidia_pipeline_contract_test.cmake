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

string(FIND "${_upscaling}"
    "bool Upscaling::IsVendorRuntimeReadyForActiveContract"
    _dlss_lifecycle_start)
string(FIND "${_upscaling}"
    "void Upscaling::MarkVendorRuntimeResourcesDirty"
    _dlss_lifecycle_end)
if(_dlss_lifecycle_start EQUAL -1 OR
   _dlss_lifecycle_end LESS_EQUAL _dlss_lifecycle_start)
    message(FATAL_ERROR "DLSS lifecycle readiness function could not be isolated")
endif()
math(EXPR _dlss_lifecycle_length
    "${_dlss_lifecycle_end} - ${_dlss_lifecycle_start}")
string(SUBSTRING "${_upscaling}" ${_dlss_lifecycle_start}
    ${_dlss_lifecycle_length} _dlss_lifecycle_body)
string(FIND "${_dlss_lifecycle_body}"
    "VRVendorRelatchPolicy::IsDLSSLifecycleReady" _lifecycle_policy)
string(FIND "${_dlss_lifecycle_body}"
    "HasCompleteVRDLSSViewportResources" _lifecycle_viewport_gate)
if(_lifecycle_policy EQUAL -1 OR NOT _lifecycle_viewport_gate EQUAL -1)
    message(FATAL_ERROR
        "DLSS lifecycle readiness must not depend on first-evaluation viewport resources")
endif()

string(FIND "${_upscaling}"
    "bool Upscaling::CanDispatchExistingVRVendorEvaluation(\n\tUpscaleMethod"
    _existing_dispatch_start)
string(FIND "${_upscaling}"
    "bool Upscaling::HasTruthfulStableVRVendorResources"
    _existing_dispatch_end)
if(_existing_dispatch_start EQUAL -1 OR
   _existing_dispatch_end LESS_EQUAL _existing_dispatch_start)
    message(FATAL_ERROR "Existing DLSS dispatch function could not be isolated")
endif()
math(EXPR _existing_dispatch_length
    "${_existing_dispatch_end} - ${_existing_dispatch_start}")
string(SUBSTRING "${_upscaling}" ${_existing_dispatch_start}
    ${_existing_dispatch_length} _existing_dispatch_body)
foreach(_required IN ITEMS
    "VRVendorRelatchPolicy::IsExactExistingDLSSDispatchReady"
    "streamline.TryResolveExistingVRDLSSViewport"
)
    string(FIND "${_existing_dispatch_body}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Existing DLSS dispatch lost exact viewport proof: ${_required}")
    endif()
endforeach()

foreach(_inactive_dlss_retention_contract IN ITEMS
    "VRVendorRelatchPolicy::CanRetainInactiveDLSSForActivation"
    "streamline.CanPrepareVRDLSSViewportWithoutRecycle"
    "retainInactiveDLSSResourcesForActivation"
    "exactInactiveFullEyeDLSSAllocationReady"
    "exactInactiveFoveatedCenterDLSSAllocationReady"
    "Streamline::DLSSViewportRole::FoveatedCenter"
    "foveatedCenterColorIn[0]->resource.get()"
    "foveatedCenterColorIn[1]->resource.get()"
)
    string(FIND "${_upscaling}" "${_inactive_dlss_retention_contract}"
        _inactive_dlss_retention_position)
    if(_inactive_dlss_retention_position EQUAL -1)
        message(FATAL_ERROR
            "Inactive DLSS activation retention is incomplete: ${_inactive_dlss_retention_contract}"
        )
    endif()
endforeach()

string(FIND "${_upscaling}"
    "const bool retainedInactiveDLSSForRelatch ="
    _retention_owner_start)
string(FIND "${_upscaling}"
    "const bool pressureMemoryRelief ="
    _retention_owner_end)
if(_retention_owner_start EQUAL -1 OR
   _retention_owner_end LESS_EQUAL _retention_owner_start)
    message(FATAL_ERROR "Inactive DLSS retention owner proof could not be isolated")
endif()
math(EXPR _retention_owner_length
    "${_retention_owner_end} - ${_retention_owner_start}")
string(SUBSTRING "${_upscaling}" ${_retention_owner_start}
    ${_retention_owner_length} _retention_owner_body)
foreach(_required IN ITEMS
    "relatchPlan.valid"
    "relatchPlan.transitionEpoch == relatchEpoch"
    "relatchPlan.contractGeneration =="
    "relatchContractGeneration"
    "relatchPlan.origin == relatchOrigin"
    "relatchPlan.retainInactiveDLSSResources"
)
    string(FIND "${_retention_owner_body}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Inactive DLSS retry lost exact owner proof: ${_required}")
    endif()
endforeach()

foreach(_per_eye_allocation_proof IN ITEMS
    "const std::array<uint32_t, 2>& a_outputWidths"
    "const std::array<uint32_t, 2>& a_outputHeights"
    "a_outputWidths[eye]"
    "a_outputHeights[eye]"
    "a_colorInputs[eye]"
)
    string(FIND "${_streamline}" "${_per_eye_allocation_proof}"
        _per_eye_allocation_position)
    if(_per_eye_allocation_position EQUAL -1)
        message(FATAL_ERROR
            "Stereo DLSS allocation proof lost per-eye evidence: ${_per_eye_allocation_proof}"
        )
    endif()
endforeach()

string(FIND "${_upscaling}"
    "const bool inactiveDLSSAllocationContractReady ="
    _inactive_dlss_proof_start)
string(FIND "${_upscaling}"
    "const bool targetFoveatedDLSSSlotRequired ="
    _inactive_dlss_proof_end)
if(_inactive_dlss_proof_start EQUAL -1 OR
   _inactive_dlss_proof_end LESS_EQUAL _inactive_dlss_proof_start)
    message(FATAL_ERROR "Inactive DLSS allocation proof could not be isolated")
endif()
math(EXPR _inactive_dlss_proof_length
    "${_inactive_dlss_proof_end} - ${_inactive_dlss_proof_start}")
string(SUBSTRING "${_upscaling}" ${_inactive_dlss_proof_start}
    ${_inactive_dlss_proof_length} _inactive_dlss_proof_body)
foreach(_required IN ITEMS
    "Streamline::DLSSViewportRole::FullEye"
    "Streamline::DLSSViewportRole::FoveatedCenter"
    "cache.plan.IsValid()"
    "std::array<uint32_t, 2>"
    "foveatedCenterColorIn[0]->resource.get()"
    "foveatedCenterColorIn[1]->resource.get()"
)
    string(FIND "${_inactive_dlss_proof_body}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Inactive DLSS allocation proof lost exact topology: ${_required}")
    endif()
endforeach()
string(FIND "${_inactive_dlss_proof_body}"
    "HasCompleteVRDLSSViewportResources()" _generic_viewport_proof)
if(NOT _generic_viewport_proof EQUAL -1)
    message(FATAL_ERROR
        "Inactive DLSS retention must not use the generic viewport proof")
endif()

string(FIND "${_upscaling}"
    "const bool memoryReliefAlreadyActive = IsVRRenderScaleMemoryReliefActive();"
    _preexisting_relief_sample)
string(FIND "${_upscaling}"
    "MaybeArmVRRenderScaleMemoryRelief(relatchSignature, relatchOrigin, state->frameCount);"
    _relief_arm)
string(FIND "${_upscaling}"
    ".memoryReliefActive = memoryReliefAlreadyActive"
    _retention_relief_gate)
if(_preexisting_relief_sample EQUAL -1 OR _relief_arm EQUAL -1 OR
   _retention_relief_gate EQUAL -1 OR
   _preexisting_relief_sample GREATER _relief_arm OR
   _retention_relief_gate LESS _relief_arm)
    message(FATAL_ERROR
        "Inactive DLSS retention lost its pre-existing memory-relief gate")
endif()
foreach(_required IN ITEMS
    "retainedInactiveDLSSForRelatch"
    ".retainedAllocationPreviouslyAdmitted ="
    "relatchPlan.retainInactiveDLSSResources ="
)
    string(FIND "${_upscaling}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Inactive DLSS retention lost retry ownership: ${_required}")
    endif()
endforeach()

string(FIND "${_streamline}"
    "bool Streamline::CanPrepareVRDLSSViewportWithoutRecycle"
    _slot_preparation_start)
string(FIND "${_streamline}"
    "bool Streamline::FreeDLSSViewportResources"
    _slot_preparation_end)
if(_slot_preparation_start EQUAL -1 OR
   _slot_preparation_end LESS_EQUAL _slot_preparation_start)
    message(FATAL_ERROR "DLSS slot preparation admission could not be isolated")
endif()
math(EXPR _slot_preparation_length
    "${_slot_preparation_end} - ${_slot_preparation_start}")
string(SUBSTRING "${_streamline}" ${_slot_preparation_start}
    ${_slot_preparation_length} _slot_preparation_body)
foreach(_required IN ITEMS
    "FindVRDLSSViewportSlot"
    "VRVendorRelatchPolicy::CanUseDLSSSlotDuringRecycle"
)
    string(FIND "${_slot_preparation_body}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "DLSS preparation lost victim-specific slot admission: ${_required}")
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
    "lifecycle.BeginRetirement()"
    "lifecycle.CompleteRetirement(false,"
    "retaining every dependent D3D resource"
    "DXGI_PRESENT_TEST"
    "DXGI_ERROR_WAS_STILL_DRAWING"
    "return owner.GetDesc(pDesc)"
    "return 0;"
    "ResetFrameGenerationContexts()"
    "ResetFrameGenerationRenderContext()"
    "SetupFrameGeneration()"
    "publicFormat != publicSwapChainDesc.BufferDesc.Format"
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
