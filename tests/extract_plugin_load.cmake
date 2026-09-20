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
string(FIND "${_plugin}" "extern \"C\" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(" _start)
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

# Compile the pinned library's allocator, including its missing-pool fallback.
file(READ "${PROJECT_ROOT}/extern/CommonLibSSE-NG/src/SKSE/API.cpp" _api)
string(FIND "${_api}" "void AllocTrampoline(" _alloc_start)
string(FIND "${_api}" "\n}" _alloc_end REVERSE)
if(_alloc_start EQUAL -1 OR _alloc_end LESS_EQUAL _alloc_start)
    message(FATAL_ERROR "Plugin load test cannot find the CommonLib allocator")
endif()
math(EXPR _alloc_length "${_alloc_end} - ${_alloc_start}")
string(SUBSTRING "${_api}" ${_alloc_start} ${_alloc_length} _allocator)
file(WRITE "${OUTPUT_DIRECTORY}/commonlib_trampoline_under_test.h" "${_allocator}\n")
