if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(READ "${PROJECT_ROOT}/src/XSEPlugin.cpp" _source)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
function(extract_discovery_region source start end output)
    string(FIND "${source}" "${start}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "API discovery test cannot find ${start}")
    endif()
    string(SUBSTRING "${source}" ${_start} -1 _remaining)
    string(FIND "${_remaining}" "${end}" _end)
    if(_end EQUAL -1)
        message(FATAL_ERROR "API discovery test cannot find ${end}")
    endif()
    string(SUBSTRING "${_remaining}" 0 ${_end} _extracted)
    set(${output} "${_extracted}" PARENT_SCOPE)
endfunction()
extract_discovery_region("${_source}" "bool RegisterCommunityShadersAPIMessageListener()" "void ResetRuntimeStateAfterGameLoad()" _registration)
extract_discovery_region("${_source}" "bool Load()" "globals::OnInit();" _load)
string(FIND "${_load}" "CSX::Api::InitializeServiceRegistryProvider();" _init)
string(SUBSTRING "${_load}" ${_init} -1 _load_registration)
extract_discovery_region("${_source}" "case SKSE::MessagingInterface::kPostLoad:" "// Establish the API owner" _postload)
string(
    FIND "${_postload}"
    "if (!RegisterCommunityShadersAPIMessageListener())"
    _refresh
)
string(SUBSTRING "${_postload}" ${_refresh} -1 _refresh_registration)
file(
    WRITE "${OUTPUT_DIRECTORY}/api_discovery_under_test.h"
    "${_registration}\nbool RegisterDuringLoad() { auto messaging = SKSE::GetMessagingInterface();\n${_load_registration}\nreturn true; }\nvoid RefreshDuringPostLoad() { do {\n${_refresh_registration}\n} while (false); }\n"
)
