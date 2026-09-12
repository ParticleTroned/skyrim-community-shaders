if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

function(extract_restore_section source_path start end output_name)
    file(READ "${PROJECT_ROOT}/${source_path}" _source)
    string(FIND "${_source}" "${start}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Upscaling restore test cannot find ${start}")
    endif()
    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${end}" _length)
    if(_length LESS_EQUAL 0)
        message(FATAL_ERROR "Upscaling restore test cannot find ${end}")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_length} _section)
    file(WRITE "${OUTPUT_DIRECTORY}/${output_name}" "${_section}")
endfunction()

file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_restore_section(
    src/Features/Upscaling.h
    "enum class UpscalingTransitionApplyDisposition"
    "struct VRRenderScaleRelatchSignature"
    upscaling_measurement_restore_types.h
)
extract_restore_section(
    src/Features/Upscaling.cpp
    "void Upscaling::RestorePerformanceCostMeasurementState("
    "Upscaling::DLSSSharpenerMode Upscaling::GetDLSSSharpenerMode()"
    upscaling_measurement_restore_under_test.h
)
