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
        "ServiceFoundation value({ ServiceName, 1, 1, 17 })"
        "{ \"minor\", 1 }, { \"schemaRevision\", 17 }"
    "PresetCompatibility::ToJson(PresetCompatibility::GetPublished())"
)
    string(FIND "${_bridge}" "${_bridge_contract}" _bridge_position)
    if(_bridge_position EQUAL -1)
        message(FATAL_ERROR "Feature DevBench compatibility contract is missing: ${_bridge_contract}")
    endif()
endforeach()

string(
    JSON _snapshot
    GET "${_descriptor}"
    outputSchema
    properties
    result
    properties
    snapshot
)
string(JSON _snapshot_type GET "${_snapshot}" type)
string(JSON _snapshot_extra GET "${_snapshot}" additionalProperties)
string(JSON _snapshot_required_count LENGTH "${_snapshot}" required)
string(JSON _snapshot_property_count LENGTH "${_snapshot}" properties)
if(
    NOT _snapshot_type STREQUAL "object"
    OR _snapshot_extra
    OR NOT _snapshot_required_count EQUAL 5
    OR NOT _snapshot_property_count EQUAL 5
)
    message(
        FATAL_ERROR
        "Streaming snapshot identity must expose exactly five required fields"
    )
endif()
set(_snapshot_fields
    capturedAtMs
    configurationEpoch
    inventoryGeneration
    deviceGeneration
    lastServiceAttemptFrame
)
set(_snapshot_required_fields)
foreach(_index RANGE 0 4)
    string(JSON _snapshot_required GET "${_snapshot}" required ${_index})
    list(APPEND _snapshot_required_fields "${_snapshot_required}")
endforeach()
list(SORT _snapshot_fields)
list(SORT _snapshot_required_fields)
if(NOT "${_snapshot_required_fields}" STREQUAL "${_snapshot_fields}")
    message(
        FATAL_ERROR
        "Streaming snapshot identity is missing required fields"
    )
endif()
foreach(
    _field
    IN
    ITEMS capturedAtMs configurationEpoch inventoryGeneration deviceGeneration
)
    string(JSON _field_type GET "${_snapshot}" properties ${_field} type)
    string(JSON _field_minimum GET "${_snapshot}" properties ${_field} minimum)
    if(NOT _field_type STREQUAL "integer" OR NOT _field_minimum EQUAL 0)
        message(
            FATAL_ERROR
            "Streaming snapshot identity must be a nonnegative integer: ${_field}"
        )
    endif()
endforeach()
string(
    JSON _frame_type_count
    LENGTH "${_snapshot}"
    properties
    lastServiceAttemptFrame
    type
)
string(
    JSON _frame_first_type
    GET "${_snapshot}"
    properties
    lastServiceAttemptFrame
    type
    0
)
string(
    JSON _frame_second_type
    GET "${_snapshot}"
    properties
    lastServiceAttemptFrame
    type
    1
)
set(_frame_types "${_frame_first_type}" "${_frame_second_type}")
list(SORT _frame_types)
string(
    JSON _frame_minimum
    GET "${_snapshot}"
    properties
    lastServiceAttemptFrame
    minimum
)
string(
    JSON _frame_maximum
    GET "${_snapshot}"
    properties
    lastServiceAttemptFrame
    maximum
)
if(
    NOT _frame_type_count EQUAL 2
    OR NOT "${_frame_types}" STREQUAL "integer;null"
    OR NOT _frame_minimum EQUAL 0
    OR NOT _frame_maximum EQUAL 4294967295
)
    message(
        FATAL_ERROR
        "Streaming service attempt frame must preserve its nullable uint32 identity"
    )
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
