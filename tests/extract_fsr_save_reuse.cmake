if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

file(READ "${PROJECT_ROOT}/src/Features/Upscaling/FidelityFX.cpp" _source)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")

function(extract_between start end output)
    string(FIND "${_source}" "${start}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "FSR save reuse test cannot find ${start}")
    endif()
    string(SUBSTRING "${_source}" ${_start} -1 _remaining)
    string(FIND "${_remaining}" "${end}" _end)
    if(_end EQUAL -1)
        message(FATAL_ERROR "FSR save reuse test cannot find ${end}")
    endif()
    string(SUBSTRING "${_remaining}" 0 ${_end} _extracted)
    set(${output} "${_extracted}" PARENT_SCOPE)
endfunction()

extract_between("D3D11_TEXTURE2D_DESC MakeSharedTextureDesc(" "template <class T, size_t N>" _descriptors)
extract_between("FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeCommandContexts()" "FidelityFX::LifecycleResult FidelityFX::AcquireRuntimeCommandContext(" _commands)
extract_between("FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeUpscalerInterop()" "FidelityFX::LifecycleResult FidelityFX::RecordRuntimeProviderResult(" _interop)
extract_between("bool FidelityFX::IsRuntimeUpscalerInteropReady()" "FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeUpscalerContexts(" _interop_ready)
extract_between("bool FidelityFX::HasCompleteRuntimeUpscalerSharedResources(" "bool FidelityFX::AreRuntimeUpscalerContextsCompatible(" _complete)
extract_between("FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeUpscalerSharedResources(" "FidelityFX::LifecycleResult FidelityFX::ExecuteRuntimeUpscalerBatch(" _shared)
extract_between("WrappedResource* FidelityFX::ResolveRuntimeSharedGuide(" "bool FidelityFX::CanDispatchHostFallbackForRegions(" _guide)
# Stop at the first recreation operation; the harness counts admission to vendor mutation.
extract_between("FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeUpscalerContexts(" "const auto idleResult = PollRuntimeUpscalerTeardownReady(\"runtime upscaler context recreation\");" _contexts)
file(
    WRITE "${OUTPUT_DIRECTORY}/fsr_save_reuse_under_test.h"
    "${_descriptors}\n${_commands}\n${_interop}\n${_interop_ready}\n${_complete}\n${_shared}\n${_guide}\n${_contexts}\n++contextRecreationAdmissions; return LifecycleResult::Ready;\n}\n"
)
