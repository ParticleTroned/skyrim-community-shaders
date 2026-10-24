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
set(_terrain_seams_found FALSE)
math(EXPR _action_last "${_action_count} - 1")
foreach(_index RANGE 0 ${_action_last})
    string(JSON _action GET
        "${_descriptor}" inputSchema properties action enum ${_index}
    )
    if(_action STREQUAL "preset_compatibility")
        set(_compatibility_found TRUE)
    elseif(_action STREQUAL "terrain_seams")
        set(_terrain_seams_found TRUE)
    endif()
endforeach()
if(NOT _compatibility_found)
    message(FATAL_ERROR "Feature DevBench schema is missing preset_compatibility")
endif()
if(NOT _terrain_seams_found)
    message(FATAL_ERROR "Feature DevBench schema is missing terrain_seams")
endif()

foreach(_bridge_contract IN ITEMS
    "ServiceFoundation value({ ServiceName, 1, 1, 23 })"
    "{ \"minor\", 1 }, { \"schemaRevision\", 23 }"
    "PresetCompatibility::ToJson(PresetCompatibility::GetPublished())"
)
    string(FIND "${_bridge}" "${_bridge_contract}" _bridge_position)
    if(_bridge_position EQUAL -1)
        message(FATAL_ERROR "Feature DevBench compatibility contract is missing: ${_bridge_contract}")
    endif()
endforeach()

string(JSON _seams GET "${_descriptor}" inputSchema properties terrainSeams)
string(JSON _seams_type GET "${_seams}" type)
string(JSON _seams_extra GET "${_seams}" additionalProperties)
string(JSON _seams_required_count LENGTH "${_seams}" required)
string(JSON _seams_required GET "${_seams}" required 0)
string(JSON _seams_enabled_type GET "${_seams}" properties enabled type)
if(NOT _seams_type STREQUAL "object" OR _seams_extra OR
   NOT _seams_required_count EQUAL 1 OR NOT _seams_required STREQUAL "enabled" OR
   NOT _seams_enabled_type STREQUAL "boolean")
    message(FATAL_ERROR "Terrain seam requests must require one boolean enabled field")
endif()

foreach(_coverage IN ITEMS terrain parallax)
    string(JSON _coverage_default GET
        "${_descriptor}" inputSchema properties textureStreaming properties
        categories properties ${_coverage} default
    )
    if(NOT _coverage_default)
        message(FATAL_ERROR "Streaming schema must advertise default-on ${_coverage}")
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

file(READ "${PROJECT_ROOT}/docs/development/unified-preset-policy.json" policy)
file(READ "${PROJECT_ROOT}/src/PresetCompatibility.h" compatibility_header)
string(JSON policy_revision GET "${policy}" runtimeSettingsContract revision)
string(REGEX MATCH "kSettingsContractRevision = ([0-9]+)" revision_match "${compatibility_header}")
if(NOT "${CMAKE_MATCH_1}" STREQUAL "${policy_revision}")
    message(FATAL_ERROR "Preset generator and runtime settings revisions disagree")
endif()

string(JSON _shadow_type GET "${_descriptor}" inputSchema properties shadowCulling type)
string(JSON _shadow_extra GET "${_descriptor}" inputSchema properties shadowCulling additionalProperties)
if(NOT _shadow_type STREQUAL "object" OR _shadow_extra)
    message(FATAL_ERROR "Shadow-culling requests must use a strict partial object")
endif()
foreach(_field IN ITEMS sun torch actors interiorTorches)
    string(JSON _field_type GET "${_descriptor}" inputSchema properties shadowCulling properties ${_field} type)
    if(NOT _field_type STREQUAL "boolean")
        message(FATAL_ERROR "Shadow-culling switch is not boolean: ${_field}")
    endif()
endforeach()
foreach(_field IN ITEMS available spsConflict active previewActive)
    string(JSON _field_type GET "${_descriptor}" outputSchema properties result properties ${_field} type)
    if(NOT _field_type STREQUAL "boolean")
        message(FATAL_ERROR "Shadow/preview status is not exposed: ${_field}")
    endif()
endforeach()
string(JSON _payload GET "${_descriptor}" outputSchema properties result properties maximumPayloadMiB const)
if(NOT _payload EQUAL 128)
    message(FATAL_ERROR "The bounded 8K payload limit is missing from DevBench")
endif()
