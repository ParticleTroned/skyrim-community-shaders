if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

file(READ "${PROJECT_ROOT}/src/XSEPlugin.cpp" _plugin)
string(
    REGEX MATCH "constexpr std::size_t kTrampolineCapacity[^;]*;"
    _capacity "${_plugin}"
)
if(NOT _capacity)
    message(FATAL_ERROR "Plugin load test cannot find trampoline capacity")
endif()
string(FIND "${_plugin}" "SKSE_PLUGIN_LOAD(" _start)
string(FIND "${_plugin}" "extern \"C\" DLLEXPORT constinit auto SKSEPlugin_Version" _end)
if(_start EQUAL -1 OR _end LESS_EQUAL _start)
    message(FATAL_ERROR "Plugin load test cannot find entrypoint boundaries")
endif()
math(EXPR _length "${_end} - ${_start}")
string(SUBSTRING "${_plugin}" ${_start} ${_length} _load)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(
    WRITE "${OUTPUT_DIRECTORY}/plugin_load_under_test.h"
    "${_capacity}\n${_load}"
)
