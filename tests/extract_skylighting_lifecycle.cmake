if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

file(READ "${PROJECT_ROOT}/src/Features/Skylighting.cpp" _source)
file(READ "${PROJECT_ROOT}/src/Features/Skylighting.h" _header)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")

function(extract_between source start end output)
    string(FIND "${source}" "${start}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Skylighting lifecycle test cannot find ${start}")
    endif()
    string(SUBSTRING "${source}" ${_start} -1 _remaining)
    string(FIND "${_remaining}" "${end}" _end)
    if(_end EQUAL -1)
        message(FATAL_ERROR "Skylighting lifecycle test cannot find ${end}")
    endif()
    string(SUBSTRING "${_remaining}" 0 ${_end} _extracted)
    set(${output} "${_extracted}" PARENT_SCOPE)
endfunction()

extract_between("${_header}" "struct Settings" "static_assert(sizeof(SkylightingCB)" _buffer)
extract_between("${_header}" "// cached variables" "/** @brief Queues a render-thread" _state)
file(
    WRITE "${OUTPUT_DIRECTORY}/skylighting_buffer_under_test.h"
    "${_buffer}\n${_state}"
)

extract_between("${_source}" "uint ClampStableSliceCount(" "void ApplyOcclusionCornerFrustum(" _probe_policy)
extract_between("${_source}" "bool ShouldRunPeriodicUpdate(" "void ApplyPlatformDefaults(" _cadence)
extract_between("${_source}" "void Skylighting::QueueResetSkylighting()" "void Skylighting::SetPerformanceCostMeasurementEnabled(" _lifecycle)
extract_between("${_source}" "bool Skylighting::HasProbeUpdateResources()" "void Skylighting::PostPostLoad()" _probe_pass)
# Exercise the volume retention policy before the unrelated capture allocation.
extract_between("${_source}" "void Skylighting::SetupRenderTargetResources()" "\n\tdelete texOcclusion;" _target_policy)
file(
    WRITE "${OUTPUT_DIRECTORY}/skylighting_lifecycle_under_test.h"
    "${_probe_policy}\n${_cadence}\n${_lifecycle}\n${_probe_pass}\n${_target_policy}\n(void)renderer;\n}\n"
)
