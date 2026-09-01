if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

file(READ "${PROJECT_ROOT}/src/Api/FeatureDevBenchBridge.cpp" _bridge)
file(READ "${PROJECT_ROOT}/src/State.cpp" _state)

string(REGEX MATCH
    "R\"\\((\\{\"description\":\"Versioned CSX feature.*\\})\\)\""
    _descriptor_match
    "${_bridge}"
)
if(NOT _descriptor_match)
    message(FATAL_ERROR "Feature DevBench descriptor was not found")
endif()
set(_descriptor "${CMAKE_MATCH_1}")
string(JSON _descriptor_type ERROR_VARIABLE _descriptor_error TYPE "${_descriptor}")
if(_descriptor_error OR NOT _descriptor_type STREQUAL "OBJECT")
    message(FATAL_ERROR "Invalid feature DevBench descriptor JSON: ${_descriptor_error}")
endif()

string(JSON _action_count LENGTH
    "${_descriptor}" inputSchema properties action enum
)
set(_compatibility_found FALSE)
math(EXPR _action_last "${_action_count} - 1")
foreach(_index RANGE 0 ${_action_last})
    string(JSON _action GET
        "${_descriptor}" inputSchema properties action enum ${_index}
    )
    if(_action STREQUAL "preset_compatibility")
        set(_compatibility_found TRUE)
    endif()
endforeach()
if(NOT _compatibility_found)
    message(FATAL_ERROR "Feature DevBench schema is missing preset_compatibility")
endif()

foreach(_bridge_contract IN ITEMS
    "ServiceFoundation value({ ServiceName, 1, 1, 2 })"
    "{ \"minor\", 1 }, { \"schemaRevision\", 2 }"
    "PresetCompatibility::ToJson(PresetCompatibility::GetPublished())"
)
    string(FIND "${_bridge}" "${_bridge_contract}" _bridge_position)
    if(_bridge_position EQUAL -1)
        message(FATAL_ERROR "Feature DevBench compatibility contract is missing: ${_bridge_contract}")
    endif()
endforeach()

string(FIND "${_state}" "PresetCompatibility::Evaluate(userSettings" _evaluation_position)
string(FIND "${_state}" "canonicalizeConfig(configPath, userSettings)" _canonicalize_position)
string(FIND "${_state}" "if (!presetCompatibility.ShouldApply())" _reject_position)
string(FIND "${_state}" "Refusing to overwrite rejected user settings" _save_guard_position)
if(_evaluation_position EQUAL -1 OR
   _canonicalize_position LESS _evaluation_position OR
   _reject_position LESS _evaluation_position OR
   _save_guard_position EQUAL -1)
    message(FATAL_ERROR "Preset compatibility must gate canonicalization, merge, and save")
endif()

message(STATUS "Feature preset compatibility and DevBench contract is coherent")
