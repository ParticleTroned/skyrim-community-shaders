if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _source)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")

function(extract_section _begin _end _output)
    string(FIND "${_source}" "${_begin}" _start)
    if(_start EQUAL -1)
        message(
            FATAL_ERROR
            "Ordinary-save presentation test cannot find ${_begin}"
        )
    endif()
    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${_end}" _length)
    if(_length LESS_EQUAL 0)
        message(
            FATAL_ERROR
            "Ordinary-save presentation test cannot find ${_end}"
        )
    endif()
    string(SUBSTRING "${_tail}" 0 ${_length} _section)
    file(WRITE "${OUTPUT_DIRECTORY}/${_output}" "${_section}")
endfunction()

extract_section(
    "bool Upscaling::ShouldReuseOrdinarySaveResources() const"
    "bool Upscaling::ShouldDeferVRVendorLifecycleMutation() const"
    ordinary_save_presentation_under_test.h
)
extract_section(
    "bool ordinarySaveOutputReady = false;"
    "\ta_presentationObservation = {};"
    ordinary_save_submit_finish_under_test.h
)
extract_section(
    "if (!VRSubmitColorContract::IsVendorSupported(sourceColorContract))"
    "\tif (vrRenderScaleRelatchDrainEpoch.load(std::memory_order_acquire) != 0)"
    ordinary_save_submit_admission_under_test.h
)
extract_section(
    "ordinarySaveOutputReady = !foveatedMaskVisualizationPreview;"
    "\t\treturn true;"
    ordinary_save_preview_output_under_test.h
)
