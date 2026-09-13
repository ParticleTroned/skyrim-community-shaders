if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _source)
string(
    FIND "${_source}"
    "bool Upscaling::IsFoveatedMaskVisualizationEnabled("
    _start
)
if(_start EQUAL -1)
    message(FATAL_ERROR "FOV preview admission is missing")
endif()
string(SUBSTRING "${_source}" ${_start} -1 _tail)
string(FIND "${_tail}" "bool Upscaling::DispatchFoveatedPeripheryPass(" _length)
if(_length LESS_EQUAL 0)
    message(FATAL_ERROR "FOV preview dispatch boundary is missing")
endif()
string(SUBSTRING "${_tail}" 0 ${_length} _section)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(
    WRITE "${OUTPUT_DIRECTORY}/foveated_mask_visualization_under_test.h"
    "${_section}"
)
