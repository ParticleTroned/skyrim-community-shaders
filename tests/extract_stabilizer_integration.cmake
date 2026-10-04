file(READ "${PROJECT_ROOT}/src/Features/VR/StabilizerIntegration.cpp" _source)
string(FIND "${_source}" "namespace VRFpsStabilizer" _start)
if(_start EQUAL -1)
    message(FATAL_ERROR "Cannot extract Stabilizer integration implementation")
endif()
string(SUBSTRING "${_source}" ${_start} -1 _implementation)
file(WRITE "${OUTPUT_FILE}" "${_implementation}")

file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _source)
string(
    FIND "${_source}"
    "bool HasPendingVRFpsStabilizerRenderScaleIntent("
    _start
)
string(FIND "${_source}" "\n\tbool HasVRStartupRenderScaleIntent(" _end)
if(_start EQUAL -1 OR _end LESS_EQUAL _start)
    message(FATAL_ERROR "Cannot extract Stabilizer profile intent cache")
endif()
math(EXPR _length "${_end} - ${_start}")
string(SUBSTRING "${_source}" ${_start} ${_length} _implementation)
get_filename_component(_directory "${OUTPUT_FILE}" DIRECTORY)
file(WRITE "${_directory}/stabilizer_intent_under_test.h" "${_implementation}")

string(
    FIND "${_source}"
    "bool Upscaling::IsVRFpsStabilizerSyncActive() const"
    _start
)
string(FIND "${_source}" "bool Upscaling::SaveVRFpsStabilizerConfig(" _end)
if(_start EQUAL -1 OR _end LESS_EQUAL _start)
    message(FATAL_ERROR "Cannot extract Stabilizer sync availability")
endif()
math(EXPR _length "${_end} - ${_start}")
string(SUBSTRING "${_source}" ${_start} ${_length} _implementation)
file(
    APPEND "${_directory}/stabilizer_intent_under_test.h"
    "\n${_implementation}"
)
