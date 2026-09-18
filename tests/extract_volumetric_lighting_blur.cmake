if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_FILE are required")
endif()
file(READ "${PROJECT_ROOT}/src/Features/VolumetricLighting.cpp" _source)
set(_implementation "")
foreach(
    _function
    IN
    ITEMS UpdateBlurDimensions SetGroupCountsHCS SetGroupCountsVCS
)
    string(FIND "${_source}" "void VolumetricLighting::${_function}(" _begin)
    if(_begin LESS 0)
        message(FATAL_ERROR "Missing production blur function: ${_function}")
    endif()
    string(SUBSTRING "${_source}" ${_begin} -1 _remaining)
    string(FIND "${_remaining}" "\n}\n" _end)
    if(_end LESS 0)
        message(
            FATAL_ERROR
            "Missing end of production blur function: ${_function}"
        )
    endif()
    math(EXPR _length "${_end} + 3")
    string(SUBSTRING "${_remaining}" 0 ${_length} _body)
    string(APPEND _implementation "${_body}\n")
endforeach()
get_filename_component(_output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_directory}")
file(WRITE "${OUTPUT_FILE}" "${_implementation}")
