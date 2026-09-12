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
extract_between("${_fidelity_source}" "bool FidelityFX::HasFSRResources()" "bool FidelityFX::IsRuntimeUpscalerDispatchProofUsable(" _resources)
extract_between("${_fidelity_source}" "bool FidelityFX::AreFSRResourcesCompatible(" "bool FidelityFX::IsHostFSR3Supported()" _compatibility)
extract_between("${_fidelity_source}" "bool FidelityFX::CanDispatchHostFallbackForRegions(" "FidelityFX::LifecycleResult FidelityFX::DispatchRuntimeUpscalerBatch(" _fallback)
extract_between("${_fidelity_source}" "FidelityFX::UpscaleResult FidelityFX::UpscaleRegion(" "FidelityFX::StereoUpscaleResult FidelityFX::UpscaleStereoRegions(" _region)
extract_between("${_fidelity_source}" "FidelityFX::StereoUpscaleResult FidelityFX::UpscaleStereoRegions(" "FidelityFX::UpscaleResult FidelityFX::Upscale(" _stereo)
extract_between("${_upscaling_source}" "FidelityFX::UpscaleResult Upscaling::DispatchVendorEyeRegion(" "FidelityFX::UpscaleResult Upscaling::DispatchSingleFoveatedVendorEye(" _vendor)
file(
    WRITE "${OUTPUT_DIRECTORY}/fsr_eye_dispatch_under_test.h"
    "${_submit_contract}\n${_arm}\n${_resources}\n${_compatibility}\n${_fallback}\n${_region}\n${_stereo}\n${_vendor}"
)

extract_between("${_fidelity_source}" "const bool runtimeDeferredByGate =" "static bool loggedRuntimeDeferredForShaderCompilation =" _gate)
file(WRITE "${OUTPUT_DIRECTORY}/fsr_runtime_gate_under_test.h" "${_gate}")

extract_between("${_upscaling_source}" "auto presentDeferredVendorOutput =" "auto finalizeSubmitStageEyeOutput =" _presentation)
file(WRITE "${OUTPUT_DIRECTORY}/fsr_deferred_presentation_under_test.h" "${_presentation}")
