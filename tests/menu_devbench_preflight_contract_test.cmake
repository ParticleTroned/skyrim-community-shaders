if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(_bridge_path "${PROJECT_ROOT}/src/MenuDevBenchBridge.cpp")
file(READ "${_bridge_path}" _bridge)

string(REGEX MATCH
    "R\"\\((\\{\"description\":\"Inspect and control the CSX VR menu[^\r\n]*\\})\\)\""
    _descriptor_match
    "${_bridge}"
)
if(NOT _descriptor_match)
    message(FATAL_ERROR "Menu DevBench descriptor was not found")
endif()
set(_descriptor "${CMAKE_MATCH_1}")
string(JSON _descriptor_type ERROR_VARIABLE _descriptor_error TYPE "${_descriptor}")
if(_descriptor_error OR NOT _descriptor_type STREQUAL "OBJECT")
    message(FATAL_ERROR "Invalid menu DevBench descriptor JSON: ${_descriptor_error}")
endif()

string(JSON _action_count LENGTH
    "${_descriptor}" inputSchema properties action enum
)
set(_prepare_coc_found FALSE)
set(_set_layout_unlocked_found FALSE)
set(_foliage_lighting_enabled_found FALSE)
set(_truepbr_verbose_found FALSE)
set(_dynamic_cubemap_resolution_found FALSE)
math(EXPR _action_last "${_action_count} - 1")
foreach(_index RANGE 0 ${_action_last})
    string(JSON _action GET
        "${_descriptor}" inputSchema properties action enum ${_index}
    )
    if(_action STREQUAL "prepare_coc")
        set(_prepare_coc_found TRUE)
    elseif(_action STREQUAL "set_foliage_lighting_enabled")
        set(_foliage_lighting_enabled_found TRUE)
    elseif(_action STREQUAL "set_truepbr_verbose_json_logging")
        set(_truepbr_verbose_found TRUE)
    elseif(_action STREQUAL "set_dynamic_cubemap_resolution")
        set(_dynamic_cubemap_resolution_found TRUE)
    endif()
    if(_action STREQUAL "set_layout_unlocked")
        set(_set_layout_unlocked_found TRUE)
    endif()
endforeach()
if(NOT _prepare_coc_found)
    message(FATAL_ERROR "Menu DevBench schema is missing prepare_coc")
endif()
if(NOT _set_layout_unlocked_found)
    message(FATAL_ERROR "Menu DevBench schema is missing set_layout_unlocked")
endif()
if(NOT _foliage_lighting_enabled_found)
    message(FATAL_ERROR
        "Menu DevBench schema is missing set_foliage_lighting_enabled"
    )
endif()
if(NOT _truepbr_verbose_found)
    message(FATAL_ERROR
        "Menu DevBench schema is missing set_truepbr_verbose_json_logging"
    )
endif()
if(NOT _dynamic_cubemap_resolution_found)
    message(FATAL_ERROR
        "Menu DevBench schema is missing set_dynamic_cubemap_resolution"
    )
endif()

string(JSON _resolution_count LENGTH
    "${_descriptor}" inputSchema properties resolution enum
)
if(NOT _resolution_count EQUAL 2)
    message(FATAL_ERROR "Dynamic cubemap resolution schema must have two values")
endif()
string(JSON _performance_resolution GET
    "${_descriptor}" inputSchema properties resolution enum 0
)
string(JSON _quality_resolution GET
    "${_descriptor}" inputSchema properties resolution enum 1
)
if(NOT _performance_resolution EQUAL 128 OR NOT _quality_resolution EQUAL 256)
    message(FATAL_ERROR "Dynamic cubemap resolution schema must expose 128 and 256")
endif()

foreach(_required_behavior IN ITEMS
    "if (action == \"prepare_coc\")"
    "CaptureCocPreflightSnapshot"
    "GetVRFpsStabilizerSessionConfig()"
    "IsVRFpsStabilizerSyncActive()"
    "CanApplyRuntimeSettings(before.state)"
    "SetLogLevel(spdlog::level::debug)"
    "settings.foveatedVendorDispatch = true"
    "settings.periphery_taa_enable = true"
    "kFoveatedCenterArea"
    "kPeripheryTAACenterArea"
    "kPeripheryTAAOuterScale"
    "{ \"foliageLightingEnabled\", globals::features::foliageLighting.IsEnabled() }"
    "{ \"foliageLightingActive\", globals::features::foliageLighting.IsRuntimeEnabled() }"
    "globals::features::foliageLighting.SetEnabled(enabled)"
    "{ \"truePbrVerboseJsonLogging\", globals::features::truePBR.enableVerboseJsonLogging }"
    "globals::features::truePBR.enableVerboseJsonLogging = enabled"
    "{ \"configuredResolution\", dynamicCubemaps.settings.CubemapResolution }"
    "{ \"activeResolution\", dynamicCubemaps.GetActiveCubemapResolution() }"
    "{ \"restartRequired\", dynamicCubemaps.IsCubemapResolutionRestartRequired() }"
    "globals::features::dynamicCubemaps.SetCubemapResolution(resolution)"
    "menu->RequestSettingsDirtyCheck()"
    "{ \"persisted\", false }"
    "{ \"promptRequired\", true }"
    "{ \"menuLayoutUnlocked\", vr.settings.UnlockMenuPositionAndSize }"
    "{ \"savedUnlockedFixedWorldPositionInitialized\", vr.savedUnlockedFixedWorldOverlayPosition.initialized }"
    "{ \"menuScale\", vr.GetEffectiveMenuScale() }"
    "{ \"savedMenuScale\", vr.settings.VRMenuScale }"
    "globals::features::vr.SetMenuLayoutUnlocked(enabled)"
)
    string(FIND "${_bridge}" "${_required_behavior}" _behavior_position)
    if(_behavior_position EQUAL -1)
        message(FATAL_ERROR
            "Menu COC preflight behavior is missing: ${_required_behavior}"
        )
    endif()
endforeach()

string(FIND
    "${_bridge}"
    "CanApplyRuntimeSettings(before.state)"
    _mutation_guard_position
)
string(FIND
    "${_bridge}"
    "SetLogLevel(spdlog::level::debug)"
    _first_mutation_position
)
if(_mutation_guard_position GREATER _first_mutation_position)
    message(FATAL_ERROR
        "COC preflight mutates runtime settings before checking prerequisites"
    )
endif()

foreach(_forbidden_behavior IN ITEMS
    "SaveVRFpsStabilizerConfig"
    "SaveSettings"
)
    string(FIND "${_bridge}" "${_forbidden_behavior}" _forbidden_position)
    if(NOT _forbidden_position EQUAL -1)
        message(FATAL_ERROR
            "COC preflight bridge contains a persistence path: ${_forbidden_behavior}"
        )
    endif()
endforeach()

message(STATUS "Menu DevBench COC preflight contract is coherent")
