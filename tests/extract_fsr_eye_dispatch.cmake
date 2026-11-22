if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

file(READ "${PROJECT_ROOT}/src/Features/Upscaling/FidelityFX.cpp" _fidelity_source)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/FidelityFX.h" _fidelity_header)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _upscaling_source)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.h" _upscaling_header)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")

function(extract_between source start end output)
    string(FIND "${source}" "${start}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "FSR eye dispatch test cannot find ${start}")
    endif()
    string(SUBSTRING "${source}" ${_start} -1 _remaining)
    string(FIND "${_remaining}" "${end}" _end)
    if(_end EQUAL -1)
        message(FATAL_ERROR "FSR eye dispatch test cannot find ${end}")
    endif()
    string(SUBSTRING "${_remaining}" 0 ${_end} _extracted)
    set(${output} "${_extracted}" PARENT_SCOPE)
endfunction()

extract_between("${_fidelity_header}" "enum class LifecycleResult" "#ifdef DEVBENCH_BRIDGE_ENABLED" _types)
extract_between("${_fidelity_header}" "struct RuntimeDispatchPlan" "bool TryGetCurrentAdapterDesc(" _plan)
file(WRITE "${OUTPUT_DIRECTORY}/fsr_eye_dispatch_types_under_test.h" "${_types}\n${_plan}")

extract_between("${_upscaling_header}" "struct VendorEyeDispatchParams" "\n\t};" _params)
file(WRITE "${OUTPUT_DIRECTORY}/fsr_eye_dispatch_params_under_test.h" "${_params}\n};\n")

extract_between("${_fidelity_source}" "bool HasSupportedSubmitColorContract()" "bool ShouldEmitFidelityFXDiagLogs()" _submit_contract)
extract_between("${_fidelity_source}" "void FidelityFX::ArmRuntimeHostFallback(" "FidelityFX::RuntimeDispatchPlan FidelityFX::ResolveRuntimeDispatchPlan(" _arm)
extract_between("${_fidelity_source}" "FidelityFX::RuntimeDispatchPlan FidelityFX::ResolveRuntimeDispatchPlan()" "FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeUpscalerInterop()" _resolve_plan)
string(
    REPLACE "FidelityFX::ResolveRuntimeDispatchPlan()"
    "FidelityFX::ResolveRuntimeDispatchPlanUnderTest()"
    _resolve_plan
    "${_resolve_plan}"
)
extract_between("${_fidelity_source}" "bool FidelityFX::HasFSRResources()" "bool FidelityFX::IsRuntimeUpscalerDispatchProofUsable(" _resources)
extract_between("${_fidelity_source}" "bool FidelityFX::AreFSRResourcesCompatible(" "bool FidelityFX::IsHostFSR3Supported()" _compatibility)
extract_between("${_fidelity_source}" "bool FidelityFX::IsRuntimeSharedGuideQuarantined(" "WrappedResource* FidelityFX::ResolveRuntimeSharedInput(" _quarantine)
extract_between("${_upscaling_source}" "bool Upscaling::IsVRColorInputQuarantined(" "bool Upscaling::AreActiveVRIntermediateTexturesCompatible(" _producer_quarantine)
extract_between("${_upscaling_source}" "bool Upscaling::EnsureVRPresentationTextures(" "logger::debug(\"[VRRenderScale] (Re)creating presentation textures:" _presentation_admission)
extract_between("${_upscaling_source}" "bool Upscaling::StretchSubmitStageEyeOutput(" "if (DispatchSubmitStageColorRegion(" _stretch_admission)
extract_between("${_fidelity_source}" "bool FidelityFX::CanDispatchHostFallbackForRegions(" "FidelityFX::LifecycleResult FidelityFX::DispatchRuntimeUpscalerBatch(" _fallback)
extract_between("${_fidelity_source}" "FidelityFX::UpscaleResult FidelityFX::UpscaleRegion(" "FidelityFX::StereoUpscaleResult FidelityFX::UpscaleStereoRegions(" _region)
extract_between("${_fidelity_source}" "FidelityFX::StereoUpscaleResult FidelityFX::UpscaleStereoRegions(" "bool FidelityFX::IsImmediateOutputCurrent(" _stereo)
extract_between("${_upscaling_source}" "FidelityFX::UpscaleResult Upscaling::DispatchVendorEyeRegion(" "\n}\n" _vendor)
file(
    WRITE "${OUTPUT_DIRECTORY}/fsr_eye_dispatch_under_test.h"
    "${_submit_contract}\n${_arm}\n${_resolve_plan}\n${_resources}\n${_compatibility}\n${_quarantine}\n${_producer_quarantine}\n${_presentation_admission}\n++presentationReplacementAdmissions; return true; }\n${_stretch_admission}\n++stretchAdmissions; return true; }\n${_fallback}\n${_region}\n${_stereo}\n${_vendor}\n}\n"
)

extract_between("${_fidelity_source}" "const bool runtimeDeferredByGate =" "static bool loggedRuntimeDeferredForShaderCompilation =" _gate)
file(WRITE "${OUTPUT_DIRECTORY}/fsr_runtime_gate_under_test.h" "${_gate}")

extract_between("${_upscaling_source}" "auto presentDeferredVendorOutput =" "auto finalizeSubmitStageEyeOutput =" _presentation)
file(WRITE "${OUTPUT_DIRECTORY}/fsr_deferred_presentation_under_test.h" "${_presentation}")

extract_between("${_fidelity_source}" "\t\tif (dispatchOk) {\n\t\t\tstd::array<D3D12_RESOURCE_BARRIER, 12> endBarriers"
    "\n}\n\nFidelityFX::UpscaleResult FidelityFX::UpscaleRegion(" _publication)
file(WRITE "${OUTPUT_DIRECTORY}/fsr_output_publication_under_test.h" "try {\n${_publication}")
extract_between("${_fidelity_source}" "bool FidelityFX::IsImmediateOutputCurrent(" "FidelityFX::UpscaleResult FidelityFX::Upscale(" _current_output)
string(REPLACE "FidelityFX::IsImmediateOutputCurrent" "OutputPublication::IsImmediateOutputCurrent" _current_output "${_current_output}")
file(WRITE "${OUTPUT_DIRECTORY}/fsr_output_current_under_test.h" "${_current_output}")
